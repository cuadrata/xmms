/*  XMMS - sndio output plugin
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

#ifndef SNDIOOUT_H
#define SNDIOOUT_H

#include "config.h"

#include <gtk/gtk.h>
#include <xmms/plugin.h>
#include <xmms/i18n.h>

struct sndio_config
{
	gchar *device;		/* a sndio device name; "default" for any */
	gint buffer_time;	/* ms, XMMS's buffer, ahead of sndio */
	gint output_rate;	/* the device's, 0 for each song's own */
	gint resample_quality;	/* OUTPUT_QUALITY_* */
};

extern struct sndio_config sndio_cfg;

void sndio_init(void);
void sndio_cleanup(void);
void sndio_about(void);
void sndio_configure(void);
void sndio_get_buffering(int *buffer_ms, int *prebuffer);
void sndio_get_rate(int *rate, int *quality);

gint sndio_open(AFormat *fmt, gint *rate, gint *nch);
gint sndio_write(gpointer data, gint length);
gint sndio_delay(void);
gint sndio_pause(gint paused);
void sndio_flush(void);
void sndio_start(void);
void sndio_close(void);
int sndio_device_rate(void);

#endif
