/*  XMMS - Cross-platform multimedia player
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
 * An output driver's sample rate setting: the rate to ask its device for
 * (0 for each song's own rate), and how well songs at other rates than
 * the device's are converted. A device runs at the nearest rate it can
 * to the one asked for. Kept in the driver's section of the config, and
 * chosen in a frame of its Configure dialog, which also shows the rate
 * the device runs at.
 */

#ifndef XMMS_RATECONF_H
#define XMMS_RATECONF_H

#include <gtk/gtk.h>
#include "configfile.h"

#define XMMS_RATE_DEFAULT	44100

void xmms_rate_read(ConfigFile *cfg, const gchar *section, gint *rate, gint *quality);
void xmms_rate_write(ConfigFile *cfg, const gchar *section, gint rate, gint quality);

/* device_rate: the rate the open device runs at, 0 when it isn't open. */
GtkWidget *xmms_rate_frame_new(gint rate, gint quality, gint (*device_rate) (void));
void xmms_rate_frame_get(GtkWidget *frame, gint *rate, gint *quality);

#endif
