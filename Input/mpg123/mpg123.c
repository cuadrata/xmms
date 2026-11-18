/*  XMMS - Cross-platform multimedia player
 *  Copyright (C) 1998-2000  Peter Alm, Mikael Alm, Olle Hallnas, Thomas Nilsson and 4Front Technologies
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

#include "config.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <gtk/gtk.h>
#include <mpg123.h>

#include "libxmms/charset.h"
#include "libxmms/configfile.h"
#include "libxmms/titlestring.h"
#include "libxmms/util.h"
#include "xmms/i18n.h"

#include "httpstream.h"
#include "id3v2.h"
#include "mpg123-plugin.h"

MPG123Config mpg123_cfg;

/*
 * Playback state. The decode thread owns the handle; the rest is
 * shared with XMMS's calls under lock.
 */
static pthread_t decode_thread;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static gboolean going, eof, audio_error, thread_open;	/* eof and audio_error through xmms_shared_get() and _set() */
static gint seek_to = -1;
static gchar *playing_file;
static gint playing_version, playing_layer, playing_bitrate, playing_mode;
static glong playing_rate;
gchar *mpg123_playing_url;

/* The equaliser, as XMMS last set it, and whether it has changed. */

static void stream_title_changed(const gchar *title);
static const HttpStreamClient mpg123_stream =
	{ &mpg123_ip, ".mp3", stream_title_changed };

static gboolean is_url(const char *filename)
{
	return http_stream_is_url(filename);
}

/*
 * Configuration
 */

static void init(void)
{
	ConfigFile *cfg;

	mpg123_init();

	mpg123_cfg.resolution = 16;
	mpg123_cfg.channels = 2;
	mpg123_cfg.downsample = 0;
	mpg123_cfg.title_override = FALSE;
	mpg123_cfg.disable_id3v2 = FALSE;
	mpg123_cfg.detect_by = DETECT_EXTENSION;

	cfg = xmms_cfg_open_default_file();
	xmms_cfg_read_int(cfg, "MPG123", "resolution", &mpg123_cfg.resolution);
	xmms_cfg_read_int(cfg, "MPG123", "channels", &mpg123_cfg.channels);
	xmms_cfg_read_int(cfg, "MPG123", "downsample", &mpg123_cfg.downsample);
	xmms_cfg_read_boolean(cfg, "MPG123", "title_override", &mpg123_cfg.title_override);
	xmms_cfg_read_boolean(cfg, "MPG123", "disable_id3v2", &mpg123_cfg.disable_id3v2);
	if (!xmms_cfg_read_string(cfg, "MPG123", "id3_format", &mpg123_cfg.id3_format))
		mpg123_cfg.id3_format = g_strdup("%p - %t");
	xmms_cfg_read_int(cfg, "MPG123", "detect_by", &mpg123_cfg.detect_by);
	xmms_cfg_free(cfg);

	if (mpg123_cfg.resolution != 16 && mpg123_cfg.resolution != 8)
		mpg123_cfg.resolution = 16;
	if (mpg123_cfg.channels != 1)
		mpg123_cfg.channels = 2;
	mpg123_cfg.downsample = CLAMP(mpg123_cfg.downsample, 0, 2);
	mpg123_cfg.detect_by = CLAMP(mpg123_cfg.detect_by, DETECT_EXTENSION, DETECT_BOTH);
}

/*
 * A handle set up for the output XMMS is configured for: 16 or 8 bit,
 * stereo or mono, and at 1:1, 1:2 or 1:4 of the stream's rate.
 */
static mpg123_handle *new_handle(gboolean quiet_scan)
{
	mpg123_handle *mh;
	const long *rates;
	size_t nrates, i;
	long flags = MPG123_QUIET;
	int encoding, channels;

	if (!(mh = mpg123_new(NULL, NULL)))
		return NULL;

	if (quiet_scan)
		/* Only look for a stream at the start, not throughout the file. */
		mpg123_param(mh, MPG123_RESYNC_LIMIT, 4096, 0);
	if (mpg123_cfg.channels == 1)
		flags |= MPG123_MONO_MIX;
	mpg123_param(mh, MPG123_FLAGS, flags | MPG123_GAPLESS, 0);
	mpg123_param(mh, MPG123_DOWN_SAMPLE, mpg123_cfg.downsample, 0);

	encoding = mpg123_cfg.resolution == 8 ?
		MPG123_ENC_UNSIGNED_8 : MPG123_ENC_SIGNED_16;
	channels = mpg123_cfg.channels == 1 ?
		MPG123_MONO : MPG123_MONO | MPG123_STEREO;
	mpg123_format_none(mh);
	mpg123_rates(&rates, &nrates);
	for (i = 0; i < nrates; i++)
		mpg123_format(mh, rates[i], channels, encoding);
	return mh;
}

/* Milliseconds, or -1 if the handle cannot tell. */
static gint length_msec(mpg123_handle *mh)
{
	long rate;
	int channels, encoding;
	off_t samples = mpg123_length(mh);

	if (samples <= 0 ||
	    mpg123_getformat(mh, &rate, &channels, &encoding) != MPG123_OK ||
	    rate <= 0)
		return -1;
	return (gint) MIN((gdouble) samples * 1000.0 / rate, (gdouble) G_MAXINT);
}

/*
 * Titles
 */

/* A tag's text as XMMS shows it: in the locale's character set. */
static gchar *id3v2_text(gchar *utf8)
{
	gchar *text = utf8 ? xmms_charset_from_utf8(utf8) : NULL;

	g_free(utf8);
	return text;
}

static gchar *id3v1_text(const char *field, gsize size)
{
	gchar *latin1 = g_strndup(field, size), *text;

	g_strstrip(latin1);
	if (!*latin1)
	{
		g_free(latin1);
		return NULL;
	}
	text = xmms_charset_convert(latin1, strlen(latin1), "ISO-8859-1", NULL);
	g_free(latin1);
	return text;
}

static gchar *extname(const char *filename)
{
	gchar *ext = strrchr(filename, '.');

	return ext ? ext + 1 : NULL;
}

/*
 * The title for filename: from its ID3v2 tag, else the ID3v1 tag mh has
 * read, if any. ID3v2 is read with our own reader, which reads all the
 * tag the editor does, compressed frames included.
 */
static gchar *format_title(mpg123_handle *mh, const char *filename)
{
	mpg123_id3v1 *v1 = NULL;
	ID3v2Tag *tag = NULL;
	ID3v2Error error;
	TitleInput *input;
	gchar *title, *path, *slash, *year = NULL, *track = NULL;
	gchar *owned[6] = { NULL };

	XMMS_NEW_TITLEINPUT(input);

	if (!mpg123_cfg.disable_id3v2)
		tag = id3v2_read(filename, &error);
	if (tag)
	{
		input->track_name = owned[0] = id3v2_text(id3v2_get(tag, ID3V2_TITLE));
		input->performer = owned[1] = id3v2_text(id3v2_get(tag, ID3V2_ARTIST));
		if (input->track_name || input->performer)
		{
			input->album_name = owned[2] = id3v2_text(id3v2_get(tag, ID3V2_ALBUM));
			input->genre = owned[3] = id3v2_text(id3v2_get(tag, ID3V2_GENRE));
			input->comment = owned[4] = id3v2_text(id3v2_get(tag, ID3V2_COMMENT));
			year = id3v2_text(id3v2_get(tag, ID3V2_YEAR));
			track = id3v2_text(id3v2_get(tag, ID3V2_TRACK));
		}
		id3v2_free(tag);
	}
	if (!input->track_name && !input->performer &&
	    mh && mpg123_id3(mh, &v1, NULL) == MPG123_OK && v1)
	{
		input->track_name = owned[0] = id3v1_text(v1->title, 30);
		input->performer = owned[1] = id3v1_text(v1->artist, 30);
		input->album_name = owned[2] = id3v1_text(v1->album, 30);
		if (v1->genre < GENRE_MAX)
			input->genre = gettext(mpg123_id3_genres[v1->genre]);
		/* ID3v1.1 keeps a track number in the comment's last byte. */
		if (v1->comment[28] == 0 && v1->comment[29] != 0)
		{
			input->comment = owned[4] = id3v1_text(v1->comment, 28);
			track = g_strdup_printf("%d", (guchar) v1->comment[29]);
		}
		else
			input->comment = owned[4] = id3v1_text(v1->comment, 30);
		year = id3v1_text(v1->year, 4);
	}
	if (year)
		input->year = atoi(year);
	if (track)
		input->track_number = atoi(track);

	path = g_strdup(filename);
	if ((slash = strrchr(path, '/')) != NULL)
		*slash = '\0';
	input->file_name = g_basename(filename);
	input->file_path = owned[5] = g_strdup_printf("%s/", path);
	input->file_ext = extname(filename);
	title = xmms_get_titlestring(mpg123_cfg.title_override ?
				     mpg123_cfg.id3_format :
				     xmms_get_gentitle_format(), input);
	if (!title)
	{
		gchar *ext;

		title = g_strdup(g_basename(filename));
		if ((ext = extname(title)) != NULL)
			ext[-1] = '\0';
	}

	g_free(input);
	g_free(path);
	g_free(year);
	g_free(track);
	for (gsize i = 0; i < sizeof (owned) / sizeof (owned[0]); i++)
		g_free(owned[i]);
	return title;
}

/*
 * Recognising MPEG audio
 */

static int read_n_bytes(FILE * file, guint8 * buf, int n)
{

	if (fread(buf, 1, n, file) != (size_t) n)
	{
		return FALSE;
	}
	return TRUE;
}

static guint32 convert_to_long(guint8 * buf)
{

	return (buf[3] << 24) + (buf[2] << 16) + (buf[1] << 8) + buf[0];
}

static guint16 read_wav_id(char *filename)
{
	FILE *file;
	guint8 buf[4], head[4];
	long seek = 0;

	if (!(file = fopen(filename, "rb")))
		return 0;

	if (!(read_n_bytes(file, buf, 4)))
		goto out;
	if (memcmp(buf, "RIFF", 4))
		goto out;
	if (fseek(file, 4, SEEK_CUR) != 0)
		goto out;
	if (!(read_n_bytes(file, buf, 4)))
		goto out;
	if (memcmp(buf, "WAVE", 4))
		goto out;
	do
	{
		/*
		 * We'll be looking for the fmt-chunk which comes
		 * before the data-chunk
		 */

		/*
		 * A chunk consists of an header identifier (4 bytes),
		 * the length of the chunk (4 bytes), and the
		 * chunkdata itself, padded to be an even number of
		 * bytes. We'll skip all chunks until we find the
		 * "data"-one which could contain mpeg-data
		 */
		if (seek != 0)
			if (fseek(file, seek, SEEK_CUR) != 0)
				goto out;
		if (!(read_n_bytes(file, head, 4)))
			goto out;
		if (!(read_n_bytes(file, buf, 4)))
			goto out;
		seek = convert_to_long(buf);
		/* Has to be even (padding) */
		seek += (seek % 2);
		if (seek >= 2 && !memcmp(head, "fmt ", 4))
		{
			guint16 wavid;
			if (!(read_n_bytes(file, buf, 2)))
				goto out;
			wavid = buf[0] | buf[1] << 8;
			seek -= 2;
			/*
			 * we could go on looking for other things,
			 * but all we wanted was the wavid
			 */
			fclose(file);
			return wavid;
		}
	} while (memcmp(head, "data", 4));
 out:
	fclose(file);
	return 0;
}


static gboolean detect_by_content(char *filename)
{
	mpg123_handle *mh = new_handle(TRUE);
	long rate;
	int channels, encoding;
	gboolean found = FALSE;

	if (!mh)
		return FALSE;
	if (mpg123_open(mh, filename) == MPG123_OK)
	{
		found = mpg123_getformat(mh, &rate, &channels, &encoding) == MPG123_OK;
		mpg123_close(mh);
	}
	mpg123_delete(mh);
	return found;
}

static int is_our_file(char *filename)
{
	char *ext;

	if (is_url(filename))
	{
		/* Streams of other kinds are left to their plugins. */
		ext = strrchr(filename, '.');
		if (ext && (!g_ascii_strcasecmp(ext, ".ogg") ||
			    !g_ascii_strcasecmp(ext, ".opus") ||
			    !g_ascii_strcasecmp(ext, ".aac") ||
			    !g_ascii_strcasecmp(ext, ".aacp") ||
			    !g_ascii_strcasecmp(ext, ".flac") ||
			    !g_ascii_strcasecmp(ext, ".fla") ||
			    !g_ascii_strncasecmp(ext, ".ra", 3) ||
			    !g_ascii_strcasecmp(ext, ".rm") ||
			    !g_ascii_strcasecmp(ext, ".rpm")))
			return FALSE;
		return TRUE;
	}

	if (mpg123_cfg.detect_by == DETECT_CONTENT)
		return detect_by_content(filename);

	ext = strrchr(filename, '.');
	if (ext)
	{
		if (!g_ascii_strcasecmp(ext, ".mp1") ||
		    !g_ascii_strcasecmp(ext, ".mp2") ||
		    !g_ascii_strcasecmp(ext, ".mp3"))
			return TRUE;
		if (!g_ascii_strcasecmp(ext, ".wav"))
		{
			/* MPEG in a RIFF WAVE: Microsoft says 80, files say 85. */
			guint16 wavid = read_wav_id(filename);

			if (wavid == 85 || wavid == 80)
				return TRUE;
		}
	}

	if (mpg123_cfg.detect_by == DETECT_BOTH)
		return detect_by_content(filename);
	return FALSE;
}

/*
 * The playlist's title and length
 */

static void get_song_info(char *filename, char **title, int *length)
{
	mpg123_handle *mh;
	long rate;
	int channels, encoding;

	*length = -1;
	if (is_url(filename))
	{
		*title = http_stream_get_title(filename);
		return;
	}

	mh = new_handle(FALSE);
	if (mh && mpg123_open(mh, filename) == MPG123_OK)
	{
		/* Reading the format reads the tags ahead of the first frame. */
		if (mpg123_getformat(mh, &rate, &channels, &encoding) == MPG123_OK)
			*length = length_msec(mh);
		*title = format_title(mh, filename);
		mpg123_close(mh);
	}
	else
		*title = format_title(NULL, filename);
	if (mh)
		mpg123_delete(mh);
}

/*
 * Playback
 */

static gboolean still_going(void)
{
	gboolean g;

	pthread_mutex_lock(&lock);
	g = going;
	pthread_mutex_unlock(&lock);
	return g;
}

static void stream_title_changed(const gchar *title)
{
	gboolean g;
	gint bitrate, mode;
	glong rate;

	pthread_mutex_lock(&lock);
	g = going;
	bitrate = playing_bitrate;
	rate = playing_rate;
	mode = playing_mode;
	pthread_mutex_unlock(&lock);
	if (g)
		mpg123_ip.set_info((gchar *) title, -1, bitrate * 1000, rate,
				   mode == MPG123_M_MONO ? 1 : 2);
}

gboolean mpg123_playing_info(gint *version, gint *layer, gint *bitrate,
			     glong *rate, gint *mode)
{
	gboolean known;

	pthread_mutex_lock(&lock);
	known = playing_bitrate > 0;
	*version = playing_version;
	*layer = playing_layer;
	*bitrate = playing_bitrate;
	*rate = playing_rate;
	*mode = playing_mode;
	pthread_mutex_unlock(&lock);
	return known;
}

static AFormat xmms_format(int encoding)
{
	switch (encoding)
	{
	case MPG123_ENC_UNSIGNED_8:
		return FMT_U8;
	case MPG123_ENC_SIGNED_8:
		return FMT_S8;
	default:
		return FMT_S16_NE;
	}
}

static void *decode_loop(void *arg)
{
	gchar *filename = arg, *title;
	gboolean stream = is_url(filename), opened = FALSE;
	mpg123_handle *mh = new_handle(FALSE);
	unsigned char buf[8192];
	long rate = 0;
	int channels = 0, encoding = 0, frames = 0;
	AFormat fmt = FMT_S16_NE;
	gint length = -1;

	if (!mh ||
	    (stream ? (!http_stream_open(&mpg123_stream, filename, 0) ||
		       mpg123_open_feed(mh) != MPG123_OK)
		    : mpg123_open(mh, filename) != MPG123_OK))
		goto done;

	title = NULL;

	while (still_going())
	{
		size_t done = 0;
		int ret;
		gint seek;

		pthread_mutex_lock(&lock);
		seek = seek_to;
		pthread_mutex_unlock(&lock);
		if (seek >= 0)
		{
			if (!stream && opened &&
			    mpg123_seek(mh, (off_t) seek * rate, SEEK_SET) >= 0)
			{
				mpg123_ip.output->flush(seek * 1000);
				xmms_shared_set(&eof, FALSE);
			}
			pthread_mutex_lock(&lock);
			seek_to = -1;
			pthread_mutex_unlock(&lock);
		}
		if (eof)
		{
			xmms_usleep(10000);
			continue;
		}
		ret = mpg123_read(mh, buf, sizeof (buf), &done);
		if (ret == MPG123_NEED_MORE && stream)
		{
			unsigned char in[16384];
			gint n = http_stream_read(in, sizeof (in));

			if (n <= 0)
				xmms_shared_set(&eof, TRUE);
			else
				mpg123_feed(mh, in, n);
		}
		else if (ret == MPG123_NEW_FORMAT)
		{
			struct mpg123_frameinfo fi;

			mpg123_getformat(mh, &rate, &channels, &encoding);
			if (opened)
				mpg123_ip.output->close_audio();
			fmt = xmms_format(encoding);
			if (!mpg123_ip.output->open_audio(fmt, rate, channels))
			{
				xmms_shared_set(&audio_error, TRUE);
				break;
			}
			opened = TRUE;
			if (stream)
			{
				/* The station's name, or the title it last sent. */
				g_free(title);
				title = http_stream_get_title(filename);
			}
			else if (!title)
			{
				length = length_msec(mh);
				title = format_title(mh, filename);
			}
			if (mpg123_info(mh, &fi) == MPG123_OK)
			{
				pthread_mutex_lock(&lock);
				playing_version = fi.version;
				playing_layer = fi.layer;
				playing_bitrate = fi.vbr == MPG123_CBR ? fi.bitrate : fi.abr_rate ? fi.abr_rate : fi.bitrate;
				playing_rate = fi.rate;
				playing_mode = fi.mode;
				pthread_mutex_unlock(&lock);
			}
			mpg123_ip.set_info(title, length, playing_bitrate * 1000,
					   rate, channels);
		}
		else if (ret == MPG123_DONE || (ret != MPG123_OK && ret != MPG123_NEED_MORE))
			xmms_shared_set(&eof, TRUE);

		if (done > 0 && opened)
		{
			mpg123_ip.pass_audio(fmt, channels, done, buf);

			/* Show a VBR stream's bitrate as it goes. */
			if (++frames % 64 == 0)
			{
				struct mpg123_frameinfo fi;

				if (mpg123_info(mh, &fi) == MPG123_OK &&
				    fi.vbr != MPG123_CBR && fi.bitrate != playing_bitrate)
				{
					pthread_mutex_lock(&lock);
					playing_bitrate = fi.bitrate;
					pthread_mutex_unlock(&lock);
					if (stream)
					{
						/* Keep the title the server last sent. */
						g_free(title);
						title = http_stream_get_title(filename);
					}
					mpg123_ip.set_info(title, length, fi.bitrate * 1000,
							   rate, channels);
				}
			}
		}
	}

	g_free(title);
	if (opened)
		mpg123_ip.output->close_audio();
done:
	if (stream)
		http_stream_close();
	if (mh)
		mpg123_delete(mh);
	if (!opened && still_going())
		/* Nothing played: let XMMS move on. */
		xmms_shared_set(&eof, TRUE);
	g_free(filename);
	return NULL;
}

static void play_file(char *filename)
{
	pthread_mutex_lock(&lock);
	going = TRUE;
	eof = FALSE;
	audio_error = FALSE;
	seek_to = -1;
	playing_bitrate = 0;
	g_free(playing_file);
	playing_file = g_strdup(filename);
	g_free(mpg123_playing_url);
	mpg123_playing_url = is_url(filename) ? g_strdup(filename) : NULL;
	pthread_mutex_unlock(&lock);

	thread_open = !pthread_create(&decode_thread, NULL, decode_loop,
				      g_strdup(filename));
	if (!thread_open)
		xmms_shared_set(&eof, TRUE);
}

static void stop(void)
{
	pthread_mutex_lock(&lock);
	going = FALSE;
	pthread_mutex_unlock(&lock);
	if (thread_open)
	{
		pthread_join(decode_thread, NULL);
		thread_open = FALSE;
	}
	pthread_mutex_lock(&lock);
	g_free(mpg123_playing_url);
	mpg123_playing_url = NULL;
	playing_bitrate = 0;
	pthread_mutex_unlock(&lock);
}

static void seek(int time)
{
	if (is_url(playing_file ? playing_file : ""))
		return;
	pthread_mutex_lock(&lock);
	seek_to = time;
	pthread_mutex_unlock(&lock);
	/* The decode thread takes the request; wait for it while it runs. */
	while (still_going() && thread_open)
	{
		gint pending;

		pthread_mutex_lock(&lock);
		pending = seek_to;
		pthread_mutex_unlock(&lock);
		if (pending < 0)
			break;
		xmms_usleep(10000);
	}
}

static void do_pause(short p)
{
	mpg123_ip.output->pause(p);
}

static int get_time(void)
{
	if (xmms_shared_get(&audio_error))
		return -2;
	if (!still_going() || (xmms_shared_get(&eof) && !mpg123_ip.output->buffer_playing()))
		return -1;
	return mpg123_ip.output->output_time();
}

static void cleanup(void)
{
	g_free(playing_file);
	playing_file = NULL;
}

static void aboutbox(void)
{
	static GtkWidget *aboutbox;

	if (aboutbox != NULL)
		return;

	aboutbox = xmms_show_message(
		_("About MPEG Layer 1/2/3 plugin"),
		_("Decoding by libmpg123 <https://mpg123.org/>\n"
		  "Plugin by The XMMS team"),
		_("OK"), FALSE, NULL, NULL);
	gtk_signal_connect(GTK_OBJECT(aboutbox), "destroy",
			   GTK_SIGNAL_FUNC(gtk_widget_destroyed), &aboutbox);
}

InputPlugin mpg123_ip =
{
	NULL,
	NULL,
	NULL,			/* description */
	init,
	aboutbox,
	mpg123_configure,
	is_our_file,
	NULL,
	play_file,
	stop,
	do_pause,
	seek,
	NULL,			/* set_eq: the equalizer is XMMS's */
	get_time,
	NULL, NULL,
	cleanup,
	NULL, NULL, NULL, NULL,
	get_song_info,
	mpg123_file_info_box,
	NULL,
	NULL			/* pass_audio: filled in by xmms */
};

InputPlugin *get_iplugin_info(void)
{
	mpg123_ip.description =
		g_strdup_printf(_("MPEG Layer 1/2/3 Player %s"), VERSION);
	return &mpg123_ip;
}
