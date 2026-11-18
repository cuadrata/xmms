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

/*
 * HTTP streaming for the input plugins: one stream at a time, fetched by
 * a thread of its own into a ring buffer the plugin reads from. The
 * settings are shared by every plugin, in the "stream" section of the
 * XMMS configuration.
 */

#ifndef HTTPSTREAM_H
#define HTTPSTREAM_H

#include <gtk/gtk.h>

#include "xmms/plugin.h"

typedef struct
{
	gint buffer_size;		/* KB */
	gint prebuffer;			/* percent of the buffer */
	gboolean use_proxy;
	gchar *proxy_host;
	gint proxy_port;
	gboolean proxy_use_auth;
	gchar *proxy_user, *proxy_pass;
	gboolean save_stream;
	gchar *save_path;
	gboolean title_streaming;	/* ask for ICY metadata */
} HttpStreamConfig;

typedef struct
{
	/* The plugin, for its status line. */
	InputPlugin *ip;
	/* Appended to the names of saved streams, such as ".ogg". */
	const gchar *save_suffix;
	/*
	 * Called from the stream's thread with each title the server
	 * sends in its ICY metadata; may be NULL.
	 */
	void (*title_changed)(const gchar *title);
} HttpStreamClient;

extern HttpStreamConfig http_stream_cfg;

/* Read the shared settings. */
void http_stream_config_load(void);

/*
 * The settings page for a plugin's configuration dialog, and the call
 * its OK button makes to keep what was set there.
 */
GtkWidget *http_stream_config_page(void);
void http_stream_config_page_apply(void);

/* Whether name is an http:// or https:// URL. */
gboolean http_stream_is_url(const gchar *name);

/*
 * Start fetching url from byte offset, closing any stream already open.
 * Reads block until the buffer has filled to the prebuffer level.
 */
gboolean http_stream_open(const HttpStreamClient *client, const gchar *url,
			  guint64 offset);

/* Read up to length bytes; 0 at the end of the stream or once closed. */
gint http_stream_read(gpointer data, gint length);

void http_stream_close(void);

/*
 * For the stream open, the title the server last sent, or failing that
 * the station's name; for any other url, its file name.
 */
gchar *http_stream_get_title(const gchar *url);

#endif
