/*  XMMS - MP4 input plugin
 *  Copyright (C) 2026 The CGTK authors
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
 * AAC and ALAC in MP4 files (.m4a, .m4b, .mp4), and AAC in ADTS files and
 * streams (.aac). AAC is decoded by faad2, ALAC by alac.c.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <sys/types.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <neaacdec.h>

#include "xmms/plugin.h"
#include "xmms/i18n.h"
#include "libxmms/util.h"
#include "libxmms/configfile.h"
#include "libxmms/titlestring.h"
#include "libxmms/charset.h"

#include "mp4.h"
#include "alac.h"
#include "httpstream.h"

/* The most output one packet can make: ALAC's largest frame length. */
#define MAX_FRAMES	65536
#define ADTS_MAX	8192

static void mp4_init(void);
static void mp4_about(void);
static int mp4_is_our_file(char *filename);
static void mp4_play(char *filename);
static void mp4_stop(void);
static void mp4_pause(short p);
static void mp4_seek(int time);
static int mp4_get_time(void);
static void mp4_get_song_info(char *filename, char **title, int *length);

InputPlugin mp4_ip =
{
	NULL,
	NULL,
	NULL,			/* description */
	mp4_init,
	mp4_about,
	mp4_configure,
	mp4_is_our_file,
	NULL,			/* scan_dir */
	mp4_play,
	mp4_stop,
	mp4_pause,
	mp4_seek,
	NULL,			/* set_eq */
	mp4_get_time,
	NULL,			/* get_volume */
	NULL,			/* set_volume */
	NULL,			/* cleanup */
	NULL,			/* get_vis_type */
	NULL,			/* add_vis_pcm */
	NULL,			/* set_info */
	NULL,			/* set_info_text */
	mp4_get_song_info,
	mp4_file_info_box,
	NULL,			/* output */
	NULL			/* pass_audio: filled in by xmms */
};

mp4_config_t mp4_cfg;

static const HttpStreamClient mp4_stream = { &mp4_ip, ".aac", NULL };

/* Where each channel goes when there are more than two to fold down. */
enum { SP_L, SP_R, SP_C, SP_LS, SP_RS, SP_CS, SP_LFE };

static const int layouts[9][8] =
{
	{ 0 }, { 0 }, { 0 },
	{ SP_C, SP_L, SP_R },
	{ SP_C, SP_L, SP_R, SP_CS },
	{ SP_C, SP_L, SP_R, SP_LS, SP_RS },
	{ SP_C, SP_L, SP_R, SP_LS, SP_RS, SP_LFE },
	{ SP_C, SP_L, SP_R, SP_LS, SP_RS, SP_CS, SP_LFE },
	{ SP_C, SP_L, SP_R, SP_L, SP_R, SP_LS, SP_RS, SP_LFE },
};

static const double gain_l[] = { 1.0, 0.0, 0.7071, 0.7071, 0.0, 0.5, 0.0 };
static const double gain_r[] = { 0.0, 1.0, 0.7071, 0.0, 0.7071, 0.5, 0.0 };

typedef struct
{
	gboolean adts, stream;
	Mp4File *mp4;
	Alac *alac;
	NeAACDecHandle aac;
	FILE *fp;			/* ADTS file */
	guint64 *frames;		/* ADTS frame offsets */
	guint32 nframes, core_rate;
	guint8 *packet;
	gint packet_len;		/* ADTS: a frame read ahead, or 0 */
	gint32 *wide;
	guint32 next;			/* next MP4 sample or ADTS frame */

	guint32 rate, channels;		/* what the output gets */
	guint64 skip, left;		/* frames to drop; frames to play */
	gboolean limited, trimmed;
	guint32 seed;
} Dec;

static pthread_t decode_thread;
/* Shared with the decode thread: through xmms_shared_get() and _set(). */
static gboolean playing, eos, output_error, streaming;
static int seekneeded = -1;
/* Under pause_lock: whether the output is open yet, and a pause asked
 * for before it was. */
static pthread_mutex_t pause_lock = PTHREAD_MUTEX_INITIALIZER;
static gboolean output_opened, dopause;

InputPlugin *get_iplugin_info(void)
{
	mp4_ip.description = g_strdup_printf(_("MPEG-4 Audio Player %s"), VERSION);
	return &mp4_ip;
}

static gboolean is_http(const char *filename)
{
	return http_stream_is_url(filename);
}

gboolean mp4_adts_header(const guint8 *h, guint *rate, guint *channels,
			 guint *length)
{
	static const guint rates[] =
	{
		96000, 88200, 64000, 48000, 44100, 32000,
		24000, 22050, 16000, 12000, 11025, 8000, 7350
	};
	guint sf;

	/* Sync word, and layer 0: MPEG layer 1-3 headers have it non-zero. */
	if (h[0] != 0xff || (h[1] & 0xf6) != 0xf0)
		return FALSE;
	if ((sf = (h[2] >> 2) & 0xf) >= ARRAY_LEN(rates))
		return FALSE;
	*rate = rates[sf];
	*channels = ((h[2] & 1) << 2) | (h[3] >> 6);
	*length = ((guint) (h[3] & 3) << 11) | ((guint) h[4] << 3) | (h[5] >> 5);
	return *length >= 7;
}

/* Where the audio starts, past any ID3v2 tag. */
static long skip_id3(FILE *fp)
{
	guint8 h[10];

	if (fread(h, 1, 10, fp) != 10 || memcmp(h, "ID3", 3) != 0)
		return 0;
	return 10 + ((long) (h[6] & 0x7f) << 21 | (long) (h[7] & 0x7f) << 14 |
		     (long) (h[8] & 0x7f) << 7 | (h[9] & 0x7f)) +
		((h[5] & 0x10) ? 10 : 0);
}

/* The offsets of every ADTS frame in a file. */
static gboolean adts_scan(FILE *fp, guint64 **frames, guint32 *nframes,
			  guint32 *core_rate)
{
	guint8 h[7];
	guint rate, ch, len, size = 0;
	off_t pos;

	*frames = NULL;
	*nframes = 0;
	pos = skip_id3(fp);
	while (fseeko(fp, pos, SEEK_SET) == 0 && fread(h, 1, 7, fp) == 7 &&
	       mp4_adts_header(h, &rate, &ch, &len))
	{
		if (*nframes == size)
		{
			size = size ? size * 2 : 1024;
			*frames = g_realloc(*frames, size * sizeof **frames);
		}
		(*frames)[(*nframes)++] = pos;
		if (*nframes == 1)
			*core_rate = rate;
		pos += len;
	}
	if (*nframes < 2)
	{
		g_free(*frames);
		*frames = NULL;
		return FALSE;
	}
	return TRUE;
}

static int src_read(Dec *d, guint8 *buf, int n)
{
	int got = 0, r;

	if (!d->stream)
		return fread(buf, 1, n, d->fp);
	while (got < n && (r = http_stream_read(buf + got, n - got)) > 0)
		got += r;
	return got;
}

/* The next ADTS frame into d->packet, resynchronising if need be. */
static int adts_frame(Dec *d)
{
	guint8 *h = d->packet;
	guint rate, ch, len;
	int tries = 0;

	if (d->packet_len)
	{
		int n = d->packet_len;

		d->packet_len = 0;
		return n;
	}
	if (src_read(d, h, 7) < 7)
		return 0;
	while (!mp4_adts_header(h, &rate, &ch, &len))
	{
		if (++tries > ADTS_MAX)
			return 0;
		memmove(h, h + 1, 6);
		if (src_read(d, h + 6, 1) < 1)
			return 0;
	}
	if (len > ADTS_MAX || src_read(d, h + 7, len - 7) < (int) len - 7)
		return 0;
	d->next++;
	return len;
}

static NeAACDecHandle aac_new(void)
{
	NeAACDecHandle h = NeAACDecOpen();
	NeAACDecConfigurationPtr c;

	if (!h)
		return NULL;
	c = NeAACDecGetCurrentConfiguration(h);
	c->outputFormat = FAAD_FMT_16BIT;
	c->downMatrix = 1;
	NeAACDecSetConfiguration(h, c);
	return h;
}

static void dec_close(Dec *d)
{
	if (!d)
		return;
	if (d->aac)
		NeAACDecClose(d->aac);
	alac_free(d->alac);
	mp4_close(d->mp4);
	if (d->fp)
		fclose(d->fp);
	if (d->stream)
		http_stream_close();
	g_free(d->frames);
	g_free(d->packet);
	g_free(d->wide);
	g_free(d);
}

static Dec *dec_open(const char *filename)
{
	Dec *d = g_new0(Dec, 1);
	unsigned long rate;
	unsigned char ch;

	d->seed = 22222;
	d->wide = g_new(gint32, MAX_FRAMES * 8);

	if (is_http(filename))
	{
		d->adts = d->stream = TRUE;
		if (!http_stream_open(&mp4_stream, filename, 0))
		{
			d->stream = FALSE;
			dec_close(d);
			return NULL;
		}
	}
	else if ((d->mp4 = mp4_open(filename)) == NULL)
	{
		d->adts = TRUE;
		if ((d->fp = fopen(filename, "rb")) == NULL ||
		    !adts_scan(d->fp, &d->frames, &d->nframes, &d->core_rate) ||
		    fseeko(d->fp, (off_t) d->frames[0], SEEK_SET) != 0)
		{
			dec_close(d);
			return NULL;
		}
	}

	if (d->adts)
	{
		d->packet = g_malloc(ADTS_MAX);
		if ((d->aac = aac_new()) == NULL ||
		    (d->packet_len = adts_frame(d)) == 0 ||
		    NeAACDecInit(d->aac, d->packet, d->packet_len, &rate, &ch) < 0)
		{
			dec_close(d);
			return NULL;
		}
		if (!d->stream)
			d->next = 1;
		else
			d->next = 0;
		d->trimmed = TRUE;
		return d;
	}

	d->packet = g_malloc(d->mp4->max_size + 16);
	if (d->mp4->codec == MP4_CODEC_ALAC)
	{
		if ((d->alac = alac_new(d->mp4->config, d->mp4->config_len)) == NULL ||
		    alac_frame_length(d->alac) > MAX_FRAMES)
		{
			dec_close(d);
			return NULL;
		}
		d->rate = alac_rate(d->alac);
		d->channels = alac_channels(d->alac) == 1 ? 1 : 2;
	}
	else if ((d->aac = aac_new()) == NULL ||
		 NeAACDecInit2(d->aac, d->mp4->config, d->mp4->config_len,
			       &rate, &ch) < 0)
	{
		dec_close(d);
		return NULL;
	}
	return d;
}

/* Triangular dither of one LSB, and rounding, to 16 bits. */
static gint16 dither(Dec *d, double x)
{
	double r;

	d->seed = d->seed * 1664525u + 1013904223u;
	r = (d->seed >> 8) / 16777216.0;
	d->seed = d->seed * 1664525u + 1013904223u;
	r -= (d->seed >> 8) / 16777216.0;
	x = floor(x + r + 0.5);
	return x > 32767 ? 32767 : x < -32768 ? -32768 : (gint16) x;
}

/* frames of ch channels at bits bits into 16-bit mono or stereo. */
static void convert(Dec *d, const gint32 *in, guint32 frames, guint32 ch,
		    guint32 bits, gint16 *out)
{
	guint32 i, c;
	double scale = 1.0 / (1u << (bits - 16)), norm = 0;

	if (bits == 16 && ch == d->channels)
	{
		for (i = 0; i < frames * ch; i++)
			out[i] = in[i];
		return;
	}
	if (ch > 2)
	{
		for (c = 0; c < ch && c < 8; c++)
			norm += gain_l[layouts[ch][c]];
		scale /= norm;
	}
	for (i = 0; i < frames; i++, in += ch)
	{
		double l = 0, r = 0;

		if (ch == 1)
			l = r = in[0];
		else if (ch == 2)
		{
			l = in[0];
			r = in[1];
		}
		else
			for (c = 0; c < ch && c < 8; c++)
			{
				l += in[c] * gain_l[layouts[ch][c]];
				r += in[c] * gain_r[layouts[ch][c]];
			}
		if (d->channels == 1)
			*out++ = dither(d, (l + r) / 2 * scale);
		else
		{
			*out++ = dither(d, l * scale);
			*out++ = dither(d, r * scale);
		}
	}
}

/* One packet's output, before trimming: frames, 0 at the end, -1 none. */
static int decode_packet(Dec *d, gint16 *out)
{
	NeAACDecFrameInfo fi;
	gint16 *s;
	int n, f;
	guint32 i;

	if (d->adts)
	{
		if (!d->stream && d->next >= d->nframes && !d->packet_len)
			return 0;
		if ((n = adts_frame(d)) == 0)
			return 0;
	}
	else
	{
		if (d->next >= d->mp4->nsamples)
			return 0;
		if ((n = mp4_read_sample(d->mp4, d->next++, d->packet)) < 0)
			return 0;
	}

	if (!d->alac && !d->aac)
		return 0;
	if (d->alac)
	{
		if ((f = alac_decode(d->alac, d->packet, n, d->wide)) < 0)
			return -1;
		convert(d, d->wide, f, alac_channels(d->alac), alac_bits(d->alac), out);
		return f;
	}

	/* No output (a bad frame, or the decoder's delay) is not the end. */
	s = NeAACDecDecode(d->aac, &fi, d->packet, n);
	if (!s || fi.error || fi.channels == 0 || fi.samples == 0)
		return -1;
	f = fi.samples / fi.channels;
	if (f > MAX_FRAMES)
		return -1;
	if (!d->rate)
	{
		d->rate = fi.samplerate;
		d->channels = fi.channels == 1 ? 1 : 2;
	}
	for (i = 0; i < fi.samples; i++)
		d->wide[i] = s[i];
	convert(d, d->wide, f, fi.channels, 16, out);
	return f;
}

/*
 * The gapless trim, once the output rate is known. faad2 gives nothing
 * for the first frame it decodes, which takes that much of the priming
 * off already.
 */
static void set_trim(Dec *d, guint32 frame)
{
	Mp4File *m = d->mp4;
	guint64 skip;

	d->trimmed = TRUE;
	if (!m || !d->rate)
		return;
	skip = m->priming * d->rate / m->timescale;
	if (d->aac)
		skip = skip > frame ? skip - frame : 0;
	d->skip = skip;
	if (m->valid)
	{
		d->limited = TRUE;
		d->left = m->valid * d->rate / m->timescale;
	}
}

/* Decoded, trimmed frames: 0 at the end. */
static int dec_read(Dec *d, gint16 *out)
{
	for (;;)
	{
		int f = decode_packet(d, out);

		if (f == 0)
			return 0;
		if (f < 0)
			continue;
		if (!d->trimmed)
			set_trim(d, f);
		if (d->skip)
		{
			guint64 s = MIN(d->skip, (guint64) f);

			memmove(out, out + s * d->channels,
				(f - s) * d->channels * sizeof *out);
			f -= s;
			d->skip -= s;
			if (f == 0)
				continue;
		}
		if (d->limited)
		{
			if (d->left == 0)
				return 0;
			if ((guint64) f > d->left)
				f = d->left;
			d->left -= f;
		}
		return f;
	}
}

/* faad2 discards its first frame: an ADTS file's first is priming. */
static guint64 dec_length_ms(Dec *d)
{
	if (d->stream)
		return 0;
	if (d->adts)
		return (guint64) (d->nframes - 1) * 1024 * 1000 / d->core_rate;
	return mp4_length_ms(d->mp4);
}

/* A fresh AAC decoder, set up as at the start of the file. */
static gboolean aac_restart(Dec *d)
{
	unsigned long rate;
	unsigned char ch;

	NeAACDecClose(d->aac);
	if ((d->aac = aac_new()) == NULL)
		return FALSE;
	if (!d->adts)
		return NeAACDecInit2(d->aac, d->mp4->config, d->mp4->config_len,
				     &rate, &ch) >= 0;
	d->packet_len = 0;
	d->next = 0;
	if (fseeko(d->fp, (off_t) d->frames[0], SEEK_SET) != 0 ||
	    (d->packet_len = adts_frame(d)) == 0)
		return FALSE;
	return NeAACDecInit(d->aac, d->packet, d->packet_len, &rate, &ch) >= 0;
}

static guint32 packet_duration(Dec *d, guint32 i)
{
	return d->adts ? 1024 : d->mp4->durations[i];
}

/*
 * Decoding restarts a packet early for AAC, whose output overlaps the
 * packet before; that packet's output, and the part of the target packet
 * before the time sought, are then dropped. Times here are in the
 * stream's own units (the MP4 timescale, or ADTS core samples).
 */
static void dec_seek(Dec *d, guint64 ms)
{
	guint64 t, start, first;
	guint32 i, next, units;

	if (d->stream || !d->rate)
		return;
	d->packet_len = 0;
	if (d->adts)
	{
		units = d->core_rate;
		t = ms * units / 1000 + 1024;
		i = MIN(t / 1024, (guint64) d->nframes - 1);
		start = (guint64) i * 1024;
	}
	else
	{
		units = d->mp4->timescale;
		t = ms * units / 1000 + d->mp4->priming;
		i = mp4_find_sample(d->mp4, t, &start);
	}

	next = d->aac && i > 0 ? i - 1 : i;
	first = next < i ? start - packet_duration(d, i - 1) : start;
	if (d->aac && next == 0)
	{
		/* Frame 0 again: faad2 discards it, as it did at the start. */
		first += packet_duration(d, 0);
		if (!aac_restart(d))
		{
			d->next = d->adts ? d->nframes : d->mp4->nsamples;
			return;
		}
		if (!d->adts)
			d->next = 0;
	}
	else
	{
		if (d->aac)
			NeAACDecPostSeekReset(d->aac, next);
		d->next = next;
		if (d->adts)
			fseeko(d->fp, (off_t) d->frames[next], SEEK_SET);
	}
	d->skip = t > first ? (t - first) * d->rate / units : 0;

	if (d->limited)
	{
		guint64 done = ms * d->rate / 1000;
		guint64 total = d->mp4->valid * d->rate / d->mp4->timescale;

		d->left = total > done ? total - done : 0;
	}
	d->trimmed = TRUE;
}

gchar *mp4_generate_title(const Mp4Tags *tags, const char *filename)
{
	TitleInput *input;
	gchar *title, *path, *slash;
	const char *ext;

	XMMS_NEW_TITLEINPUT(input);
	path = g_strdup(filename);
	if ((slash = strrchr(path, '/')) != NULL)
		*slash = '\0';
	input->file_name = g_basename(filename);
	ext = strrchr(filename, '.');
	input->file_ext = ext ? (gchar *) ext + 1 : NULL;
	input->file_path = g_strdup_printf("%s/", path);

	if (tags)
	{
		const char *artist = tags->artist ? tags->artist : tags->album_artist;

		input->track_name = tags->title ? xmms_charset_from_utf8(tags->title) : NULL;
		input->performer = artist ? xmms_charset_from_utf8(artist) : NULL;
		input->album_name = tags->album ? xmms_charset_from_utf8(tags->album) : NULL;
		input->date = tags->date ? xmms_charset_from_utf8(tags->date) : NULL;
		input->genre = tags->genre ? xmms_charset_from_utf8(tags->genre) : NULL;
		input->comment = tags->comment ? xmms_charset_from_utf8(tags->comment) : NULL;
		input->track_number = tags->track;
		if (input->date)
			input->year = atoi(input->date);
	}

	title = xmms_get_titlestring(mp4_cfg.tag_override ? mp4_cfg.tag_format :
				     xmms_get_gentitle_format(), input);

	g_free(input->track_name);
	g_free(input->performer);
	g_free(input->album_name);
	g_free(input->date);
	g_free(input->genre);
	g_free(input->comment);
	g_free(input->file_path);
	g_free(input);
	g_free(path);

	if (!title)
	{
		if (is_http(filename))
			title = http_stream_get_title(filename);
		else
		{
			char *dot;

			title = g_strdup(g_basename(filename));
			if ((dot = strrchr(title, '.')) != NULL)
				*dot = '\0';
		}
	}
	return title;
}

static int mp4_is_our_file(char *filename)
{
	FILE *fp;
	guint8 h[7];
	guint rate, ch, len;
	const char *ext = strrchr(filename, '.');
	Mp4File *m;
	int ours = FALSE;

	if (is_http(filename))
		return ext && (!g_ascii_strcasecmp(ext, ".aac") ||
			       !g_ascii_strcasecmp(ext, ".aacp"));

	if ((m = mp4_open(filename)) != NULL)
	{
		mp4_close(m);
		return TRUE;
	}
	/* ADTS: two frame headers in a row. */
	if ((fp = fopen(filename, "rb")) == NULL)
		return FALSE;
	if (fseek(fp, skip_id3(fp), SEEK_SET) == 0 && fread(h, 1, 7, fp) == 7 &&
	    mp4_adts_header(h, &rate, &ch, &len) &&
	    fseek(fp, len - 7, SEEK_CUR) == 0 && fread(h, 1, 7, fp) == 7)
		ours = mp4_adts_header(h, &rate, &ch, &len);
	fclose(fp);
	return ours;
}

static void show_info(Dec *d, const char *filename, int length, gchar **title)
{
	int bitrate = 0;

	g_free(*title);
	*title = mp4_generate_title(d->mp4 ? &d->mp4->tags : NULL, filename);
	if (length > 0 && d->mp4)
		bitrate = d->mp4->data_bytes * 8 * 1000 / length;
	else if (length > 0 && d->adts && d->nframes)
		bitrate = (d->frames[d->nframes - 1] - d->frames[0]) * 8 * 1000 / length;
	mp4_ip.set_info(*title, length, bitrate, d->rate, d->channels);
}

static void *decode_loop(void *arg)
{
	char *filename = arg;
	gchar *title = NULL;
	gint16 *pcm = g_new(gint16, MAX_FRAMES * 2);
	Dec *d;
	int length, frames;

	xmms_shared_set(&streaming, is_http(filename));
	if ((d = dec_open(filename)) == NULL)
	{
		xmms_shared_set(&eos, TRUE);
		goto done;
	}
	length = streaming ? -1 : (int) dec_length_ms(d);

	/* The first audio, which tells an AAC stream's real rate. */
	frames = dec_read(d, pcm);
	if (frames <= 0 || !d->rate)
	{
		xmms_shared_set(&eos, TRUE);
		goto done;
	}
	show_info(d, filename, length, &title);

	if (!mp4_ip.output->open_audio(FMT_S16_NE, d->rate, d->channels))
	{
		xmms_shared_set(&output_error, TRUE);
		goto done;
	}
	/* A pause asked for while it opened. */
	pthread_mutex_lock(&pause_lock);
	output_opened = TRUE;
	if (dopause)
	{
		mp4_ip.output->pause(TRUE);
		dopause = FALSE;
	}
	pthread_mutex_unlock(&pause_lock);

	while (xmms_shared_get(&playing))
	{
		int bytes, seek_to;

		if ((seek_to = xmms_shared_get(&seekneeded)) != -1)
		{
			mp4_ip.output->flush(seek_to * 1000);
			dec_seek(d, (guint64) seek_to * 1000);
			xmms_shared_set(&eos, FALSE);
			xmms_shared_set(&seekneeded, -1);
			frames = 0;
		}
		if (xmms_shared_get(&eos))
		{
			xmms_usleep(20000);
			continue;
		}
		if (frames == 0 && (frames = dec_read(d, pcm)) == 0)
		{
			/* Two calls end the output plugin's prebuffering. */
			mp4_ip.output->buffer_free();
			mp4_ip.output->buffer_free();
			xmms_shared_set(&eos, TRUE);
			continue;
		}

		bytes = frames * d->channels * sizeof *pcm;
		if (!mp4_ip.pass_audio(FMT_S16_NE, d->channels, bytes, pcm))
			continue;
		frames = 0;
	}

	pthread_mutex_lock(&pause_lock);
	output_opened = FALSE;
	pthread_mutex_unlock(&pause_lock);
	mp4_ip.output->close_audio();

 done:
	dec_close(d);
	g_free(pcm);
	g_free(title);
	g_free(filename);
	xmms_shared_set(&streaming, FALSE);
	return NULL;
}

static void mp4_play(char *filename)
{
	playing = TRUE;
	eos = FALSE;
	output_error = FALSE;
	seekneeded = -1;
	pthread_mutex_lock(&pause_lock);
	output_opened = FALSE;
	dopause = FALSE;
	pthread_mutex_unlock(&pause_lock);
	pthread_create(&decode_thread, NULL, decode_loop, g_strdup(filename));
}

static void mp4_stop(void)
{
	if (playing)
	{
		xmms_shared_set(&playing, FALSE);
		pthread_join(decode_thread, NULL);
	}
}

static void mp4_pause(short p)
{
	pthread_mutex_lock(&pause_lock);
	if (output_opened)
		mp4_ip.output->pause(p);
	else
		dopause = p;
	pthread_mutex_unlock(&pause_lock);
}

static void mp4_seek(int time)
{
	if (xmms_shared_get(&streaming))
		return;
	xmms_shared_set(&seekneeded, time);
	while (xmms_shared_get(&seekneeded) != -1 && playing)
		xmms_usleep(10000);
}

static int mp4_get_time(void)
{
	if (xmms_shared_get(&output_error))
		return -2;
	if (xmms_shared_get(&eos) && !mp4_ip.output->buffer_playing())
		return -1;
	return mp4_ip.output->output_time();
}

static void mp4_get_song_info(char *filename, char **title, int *length)
{
	Mp4File *m;
	FILE *fp;
	guint64 *frames;
	guint32 nframes, rate;

	if (is_http(filename))
	{
		*length = -1;
		*title = http_stream_get_title(filename);
		return;
	}
	if ((m = mp4_open(filename)) != NULL)
	{
		*length = mp4_length_ms(m);
		*title = mp4_generate_title(&m->tags, filename);
		mp4_close(m);
		return;
	}
	if ((fp = fopen(filename, "rb")) == NULL)
		return;
	if (adts_scan(fp, &frames, &nframes, &rate))
	{
		*length = (guint64) (nframes - 1) * 1024 * 1000 / rate;
		*title = mp4_generate_title(NULL, filename);
		g_free(frames);
	}
	fclose(fp);
}

static void mp4_about(void)
{
	static GtkWidget *about;

	if (about)
	{
		gdk_window_raise(about->window);
		return;
	}
	about = xmms_show_message(_("About MPEG-4 Audio Plugin"),
				  _("MPEG-4 Audio Plugin\n\n"
				    "AAC and Apple Lossless in MP4 files,\n"
				    "and AAC in ADTS files and streams.\n\n"
				    "AAC decoding by FAAD2.\n"),
				  _("OK"), FALSE, NULL, NULL);
	gtk_signal_connect(GTK_OBJECT(about), "destroy",
			   GTK_SIGNAL_FUNC(gtk_widget_destroyed), &about);
}

static void mp4_init(void)
{
	ConfigFile *cfg;

	memset(&mp4_cfg, 0, sizeof mp4_cfg);
	cfg = xmms_cfg_open_default_file();
	xmms_cfg_read_boolean(cfg, "mp4", "tag_override", &mp4_cfg.tag_override);
	if (!xmms_cfg_read_string(cfg, "mp4", "tag_format", &mp4_cfg.tag_format))
		mp4_cfg.tag_format = g_strdup("%p - %t");
	xmms_cfg_free(cfg);
}
