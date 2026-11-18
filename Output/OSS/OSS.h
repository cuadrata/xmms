/*  XMMS - Cross-platform multimedia player
 *  Copyright (C) 1998-2003  Peter Alm, Mikael Alm, Olle Hallnas,
 *                           Thomas Nilsson and 4Front Technologies
 *  Copyright (C) 1999-2003  Haavard Kvaalen
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
#ifndef OSS_H
#define OSS_H

#include "config.h"

#include <gtk/gtk.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <fcntl.h>
#include <unistd.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef HAVE_MACHINE_SOUNDCARD_H
#include <machine/soundcard.h>
#else
#include <sys/soundcard.h>
#endif

#include "xmms/plugin.h"
#include "libxmms/configfile.h"

#ifdef WORDS_BIGENDIAN
# define IS_BIG_ENDIAN TRUE
#else
# define IS_BIG_ENDIAN FALSE
#endif

typedef struct
{
	gint audio_device;
	gint buffer_size;	/* ms, XMMS's buffer, ahead of the device */
	gint prebuffer;		/* how full it gets before playing, in % */
	gboolean use_alt_audio_device;
	gchar *alt_audio_device;
	gint output_rate;	/* the device's, 0 for each song's own */
	gint resample_quality;	/* OUTPUT_QUALITY_* */
}
OSSConfig;

extern OSSConfig oss_cfg;

void oss_init(void);
void oss_cleanup(void);
void oss_about(void);
void oss_configure(void);
void oss_get_buffering(int *buffer_ms, int *prebuffer);
void oss_get_rate(int *rate, int *quality);

int oss_open(AFormat *fmt, int *rate, int *nch);
int oss_device_rate(void);
int oss_write(void *data, int length);
int oss_delay(void);
int oss_pause(int paused);
void oss_flush(void);
void oss_start(void);
void oss_close(void);

#endif
