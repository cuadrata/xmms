/*  XMMS - Opus input plugin
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

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <opusfile.h>

#include "xmms/plugin.h"
#include "xmms/i18n.h"
#include "libxmms/util.h"
#include "libxmms/configfile.h"
#include "libxmms/titlestring.h"
#include "libxmms/charset.h"

#include "opus.h"
#include "httpstream.h"

/* Opus always decodes at 48 kHz. */
#define OPUS_RATE 48000

static void opus_init(void);
static void opus_about(void);
static int opus_is_our_file(char *filename);
static void opus_play(char *filename);
static void opus_stop(void);
static void opus_pause(short p);
static void opus_seek(int time);
static int opus_get_time(void);
static void opus_get_song_info(char *filename, char **title, int *length);

InputPlugin opus_ip =
{
	NULL,
	NULL,
	NULL,			/* description */
	opus_init,
	opus_about,
	opus_configure,
	opus_is_our_file,
	NULL,			/* scan_dir */
	opus_play,
	opus_stop,
	opus_pause,
	opus_seek,
	NULL,			/* set_eq */
	opus_get_time,
	NULL,			/* get_volume */
	NULL,			/* set_volume */
	NULL,			/* cleanup */
	NULL,			/* get_vis_type */
	NULL,			/* add_vis_pcm */
	NULL,			/* set_info */
	NULL,			/* set_info_text */
	opus_get_song_info,
	opus_file_info_box,
	NULL,			/* output */
	NULL			/* pass_audio: filled in by xmms */
};

opus_config_t opus_cfg;

static const HttpStreamClient opus_stream = { &opus_ip, ".opus", NULL };

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
	opus_ip.description = g_strdup_printf(_("Opus Player %s"), VERSION);
	return &opus_ip;
}

static gboolean is_http(const char *filename)
{
	return http_stream_is_url(filename);
}

/* Reading an HTTP stream through the shared stream code. */
static int stream_read(void *stream, unsigned char *ptr, int nbytes)
{
	(void) stream;
	return http_stream_read(ptr, nbytes);
}

static int stream_close(void *stream)
{
	(void) stream;
	http_stream_close();
	return 0;
}

/* No seek or tell: opusfile treats the stream as unseekable. */
static const OpusFileCallbacks stream_callbacks =
{
	stream_read, NULL, NULL, stream_close
};

static int opus_is_our_file(char *filename)
{
	OggOpusFile *of;
	const char *ext;

	if (is_http(filename))
	{
		ext = strrchr(filename, '.');
		return ext && !g_ascii_strcasecmp(ext, ".opus");
	}

	if ((of = op_test_file(filename, NULL)) == NULL)
		return FALSE;
	op_free(of);
	return TRUE;
}

static gchar *tag(const OpusTags *tags, const char *name)
{
	const char *value = opus_tags_query(tags, name, 0);

	return value ? xmms_charset_from_utf8(value) : NULL;
}

gchar *opus_generate_title(const OpusTags *tags, const char *filename)
{
	TitleInput *input;
	gchar *title, *path, *slash, *value;
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
		input->track_name = tag(tags, "TITLE");
		input->performer = tag(tags, "ARTIST");
		input->album_name = tag(tags, "ALBUM");
		input->date = tag(tags, "DATE");
		input->genre = tag(tags, "GENRE");
		input->comment = tag(tags, "COMMENT");
		if (!input->comment)
			input->comment = tag(tags, "DESCRIPTION");
		if ((value = tag(tags, "TRACKNUMBER")) != NULL)
		{
			input->track_number = atoi(value);
			g_free(value);
		}
		if (input->date)
			input->year = atoi(input->date);
	}

	title = xmms_get_titlestring(opus_cfg.tag_override ?
				     opus_cfg.tag_format :
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

static void apply_gain(OggOpusFile *of)
{
	int type = OP_HEADER_GAIN, offset = 0;

	if (opus_cfg.gain_mode == OPUS_GAIN_TRACK)
		type = OP_TRACK_GAIN;
	else if (opus_cfg.gain_mode == OPUS_GAIN_ALBUM)
		type = OP_ALBUM_GAIN;
	if (type != OP_HEADER_GAIN && opus_cfg.gain_rg_level)
		offset = 5 * 256;
	op_set_gain_offset(of, type, offset);
}

/* What the main window shows for a link. */
static void show_info(OggOpusFile *of, int link, const char *filename,
		      int length, gchar **title)
{
	const OpusHead *head = op_head(of, link);
	/* No average for an unseekable stream until it has ended. */
	opus_int32 bitrate = op_bitrate(of, link);

	g_free(*title);
	*title = opus_generate_title(op_tags(of, link), filename);
	opus_ip.set_info(*title, length, bitrate > 0 ? bitrate : 0, OPUS_RATE,
			 head && head->channel_count == 1 ? 1 : 2);
}

static void do_seek(OggOpusFile *of, int seek_to)
{
	opus_ip.output->flush(seek_to * 1000);
	op_pcm_seek(of, (ogg_int64_t) seek_to * OPUS_RATE);
	xmms_shared_set(&eos, FALSE);
	xmms_shared_set(&seekneeded, -1);
}

static void *decode_loop(void *arg)
{
	char *filename = arg;
	OggOpusFile *of;
	gchar *title = NULL;
	opus_int16 pcm[2048];
	int length = -1, link, last_link, timer = 0;
	/* A link decoded but not yet heard, and where it will be. */
	int pending_link = -1, pending_time = 0;
	static int stream_token;

	if (is_http(filename))
	{
		xmms_shared_set(&streaming, TRUE);
		http_stream_open(&opus_stream, filename, 0);
		of = op_open_callbacks(&stream_token, &stream_callbacks,
				       NULL, 0, NULL);
		if (of == NULL)
			http_stream_close();
	}
	else
		of = op_open_file(filename, NULL);

	if (of == NULL)
	{
		xmms_shared_set(&eos, TRUE);
		goto done;
	}

	apply_gain(of);
	if (!streaming)
	{
		ogg_int64_t total = op_pcm_total(of, -1);

		if (total >= 0)
			length = total * 1000 / OPUS_RATE;
	}
	/* Until something is decoded, the current link is not the first. */
	last_link = 0;
	show_info(of, 0, filename, length, &title);

	if (!opus_ip.output->open_audio(FMT_S16_NE, OPUS_RATE, 2))
	{
		xmms_shared_set(&output_error, TRUE);
		goto done;
	}
	/* A pause asked for while it opened. */
	pthread_mutex_lock(&pause_lock);
	output_opened = TRUE;
	if (dopause)
	{
		opus_ip.output->pause(TRUE);
		dopause = FALSE;
	}
	pthread_mutex_unlock(&pause_lock);

	while (xmms_shared_get(&playing))
	{
		int n, bytes, seek_to;

		if ((seek_to = xmms_shared_get(&seekneeded)) != -1)
		{
			do_seek(of, seek_to);
			pending_link = -1;
			last_link = op_current_link(of);
			show_info(of, last_link, filename, length, &title);
		}
		if (pending_link != -1 &&
		    opus_ip.output->output_time() >= pending_time)
		{
			show_info(of, pending_link, filename, length, &title);
			pending_link = -1;
		}
		if (xmms_shared_get(&eos))
		{
			xmms_usleep(20000);
			continue;
		}

		n = op_read_stereo(of, pcm, ARRAY_LEN(pcm));
		if (n == OP_HOLE)
			continue;
		if (n <= 0)
		{
			/* Two calls end the output plugin's prebuffering. */
			opus_ip.output->buffer_free();
			opus_ip.output->buffer_free();
			xmms_shared_set(&eos, TRUE);
			continue;
		}
		bytes = n * 2 * sizeof pcm[0];

		/* A chained stream's next link: a new song, shown when heard. */
		if ((link = op_current_link(of)) != last_link)
		{
			last_link = link;
			pending_link = link;
			pending_time = opus_ip.output->written_time();
		}

		if (!opus_ip.pass_audio(FMT_S16_NE, 2, bytes, pcm))
			continue;

		/* The bitrate as it varies, once a second. */
		if (opus_ip.output->output_time() > timer + 1000 ||
		    opus_ip.output->output_time() < timer)
		{
			opus_int32 br = op_bitrate_instant(of);

			if (br > 0 && pending_link == -1)
				opus_ip.set_info(title, length, br, OPUS_RATE,
						 op_head(of, last_link)->channel_count == 1 ? 1 : 2);
			timer = opus_ip.output->output_time();
		}
	}

	pthread_mutex_lock(&pause_lock);
	output_opened = FALSE;
	pthread_mutex_unlock(&pause_lock);
	opus_ip.output->close_audio();

 done:
	if (of)
		op_free(of);
	g_free(title);
	g_free(filename);
	xmms_shared_set(&streaming, FALSE);
	return NULL;
}

static void opus_play(char *filename)
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

static void opus_stop(void)
{
	if (playing)
	{
		xmms_shared_set(&playing, FALSE);
		pthread_join(decode_thread, NULL);
	}
}

static void opus_pause(short p)
{
	pthread_mutex_lock(&pause_lock);
	if (output_opened)
		opus_ip.output->pause(p);
	else
		dopause = p;
	pthread_mutex_unlock(&pause_lock);
}

static void opus_seek(int time)
{
	if (xmms_shared_get(&streaming))
		return;
	xmms_shared_set(&seekneeded, time);
	while (xmms_shared_get(&seekneeded) != -1 && playing)
		xmms_usleep(10000);
}

static int opus_get_time(void)
{
	if (xmms_shared_get(&output_error))
		return -2;
	if (xmms_shared_get(&eos) && !opus_ip.output->buffer_playing())
		return -1;
	return opus_ip.output->output_time();
}

static void opus_get_song_info(char *filename, char **title, int *length)
{
	OggOpusFile *of;
	ogg_int64_t total;

	if (is_http(filename))
	{
		*length = -1;
		*title = http_stream_get_title(filename);
		return;
	}
	if ((of = op_open_file(filename, NULL)) == NULL)
		return;
	total = op_pcm_total(of, -1);
	*length = total >= 0 ? total * 1000 / OPUS_RATE : -1;
	*title = opus_generate_title(op_tags(of, 0), filename);
	op_free(of);
}

static void opus_about(void)
{
	static GtkWidget *about;

	if (about)
	{
		gdk_window_raise(about->window);
		return;
	}
	about = xmms_show_message(_("About Opus Plugin"),
				  _("Opus Plugin\n\n"
				    "Decoding by libopusfile from the Xiph.Org Foundation.\n"),
				  _("OK"), FALSE, NULL, NULL);
	gtk_signal_connect(GTK_OBJECT(about), "destroy",
			   GTK_SIGNAL_FUNC(gtk_widget_destroyed), &about);
}

static void opus_init(void)
{
	ConfigFile *cfg;

	memset(&opus_cfg, 0, sizeof opus_cfg);
	opus_cfg.gain_mode = OPUS_GAIN_HEADER;

	cfg = xmms_cfg_open_default_file();
	xmms_cfg_read_boolean(cfg, "opus", "tag_override", &opus_cfg.tag_override);
	if (!xmms_cfg_read_string(cfg, "opus", "tag_format", &opus_cfg.tag_format))
		opus_cfg.tag_format = g_strdup("%p - %t");
	xmms_cfg_read_int(cfg, "opus", "gain_mode", &opus_cfg.gain_mode);
	xmms_cfg_read_boolean(cfg, "opus", "gain_rg_level", &opus_cfg.gain_rg_level);
	xmms_cfg_free(cfg);

	if (opus_cfg.gain_mode < OPUS_GAIN_HEADER ||
	    opus_cfg.gain_mode > OPUS_GAIN_ALBUM)
		opus_cfg.gain_mode = OPUS_GAIN_HEADER;
}
