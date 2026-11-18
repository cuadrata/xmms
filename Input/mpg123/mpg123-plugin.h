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

/* The MPEG audio input plugin, decoding with libmpg123. */

#ifndef MPG123_PLUGIN_H
#define MPG123_PLUGIN_H

#include <glib.h>

#include "xmms/plugin.h"

enum
{
	DETECT_EXTENSION,
	DETECT_CONTENT,
	DETECT_BOTH
};

typedef struct
{
	gint resolution;		/* 16 or 8 bits */
	gint channels;			/* 2 for stereo when available, 1 for mono */
	gint downsample;		/* 0, 1 or 2: 1:1, 1:2 or 1:4 */
	gchar *id3_format;
	gboolean title_override, disable_id3v2;
	gint detect_by;
} MPG123Config;

/* The 128 bytes at the end of a file that ID3v1 and v1.1 tags occupy. */
struct id3v1tag_t
{
	char tag[3];			/* "TAG" */
	char title[30];
	char artist[30];
	char album[30];
	char year[4];
	union
	{
		struct
		{
			char comment[30];
		} v1_0;
		struct
		{
			char comment[28];
			char __zero;
			unsigned char track_number;
		} v1_1;
	} u;
	unsigned char genre;
};

#define GENRE_MAX 0x94

extern MPG123Config mpg123_cfg;
extern InputPlugin mpg123_ip;
extern const char *mpg123_id3_genres[GENRE_MAX];

void mpg123_configure(void);
void mpg123_file_info_box(char *filename);

/*
 * What the stream playing now reports, for the file info box, which
 * shows it for streams. Valid while mpg123_playing_url is not NULL.
 */
extern gchar *mpg123_playing_url;
gboolean mpg123_playing_info(gint *version, gint *layer, gint *bitrate,
			     glong *rate, gint *mode);

#endif
