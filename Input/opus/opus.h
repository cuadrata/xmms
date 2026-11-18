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

#ifndef OPUS_PLUGIN_H
#define OPUS_PLUGIN_H

#include <glib.h>
#include <opusfile.h>

#define ARRAY_LEN(a) (sizeof (a) / sizeof (a)[0])

enum
{
	OPUS_GAIN_HEADER,	/* the header gain alone */
	OPUS_GAIN_TRACK,	/* R128_TRACK_GAIN */
	OPUS_GAIN_ALBUM		/* R128_ALBUM_GAIN */
};

typedef struct
{
	gboolean tag_override;
	gchar *tag_format;
	gint gain_mode;
	/* R128 aims at -23 LUFS; +5 dB meets ReplayGain's level. */
	gboolean gain_rg_level;
} opus_config_t;

extern opus_config_t opus_cfg;

void opus_configure(void);
void opus_file_info_box(char *filename);

/* The title for a file, from its tags and the title format. */
gchar *opus_generate_title(const OpusTags *tags, const char *filename);

#endif
