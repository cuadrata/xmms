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

#ifndef MP4_PLUGIN_H
#define MP4_PLUGIN_H

#include <glib.h>

#include "mp4file.h"

#define ARRAY_LEN(a) (sizeof (a) / sizeof (a)[0])

typedef struct
{
	gboolean tag_override;
	gchar *tag_format;
} mp4_config_t;

extern mp4_config_t mp4_cfg;

void mp4_configure(void);
void mp4_file_info_box(char *filename);

/* The title for a file, from its tags (which may be NULL). */
gchar *mp4_generate_title(const Mp4Tags *tags, const char *filename);

/* ADTS: the stream's sample rate and channels from a frame header. */
gboolean mp4_adts_header(const guint8 *h, guint *rate, guint *channels,
			 guint *length);

#endif
