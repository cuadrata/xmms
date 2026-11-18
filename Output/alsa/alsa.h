/*  XMMS - ALSA output plugin
 *  Copyright (C) 2001-2003 Matthieu Sozeau
 *  Copyright (C) 1998-2003  Peter Alm, Mikael Alm, Olle Hallnas,
 *                           Thomas Nilsson and 4Front Technologies
 *  Copyright (C) 1999-2004  Håvard Kvålen
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
#ifndef ALSA_H
#define ALSA_H

#include "config.h"

#include <libxmms/util.h>
#include <xmms/plugin.h>
#include <libxmms/configfile.h>
#include <xmms/i18n.h>
#include <libxmms/rateconf.h>

/* The snd_*_alloca() macros need alloca(). */
#include <alloca.h>

#define ALSA_PCM_NEW_HW_PARAMS_API
#define ALSA_PCM_NEW_SW_PARAMS_API
#include <alsa/asoundlib.h>

#include <gtk/gtk.h>

#ifdef WORDS_BIGENDIAN
# define IS_BIG_ENDIAN TRUE
#else
# define IS_BIG_ENDIAN FALSE
#endif

struct alsa_config
{
	char *pcm_device;
	int buffer_time;	/* ms, the device's */
	int period_time;
	int thread_buffer_time;	/* ms, XMMS's, ahead of the device */
	int output_rate;	/* the device's, 0 for each song's own */
	int resample_quality;	/* OUTPUT_QUALITY_* */
};

extern struct alsa_config alsa_cfg;

void alsa_init(void);
void alsa_cleanup(void);
void alsa_about(void);
void alsa_configure(void);
void alsa_save_config(void);
void alsa_get_buffering(int *buffer_ms, int *prebuffer);
void alsa_get_rate(int *rate, int *quality);

int alsa_open(AFormat *fmt, int *rate, int *nch);
int alsa_write(void *data, int length);
int alsa_delay(void);
int alsa_pause(int paused);
void alsa_flush(void);
void alsa_start(void);
void alsa_close(void);
int alsa_device_rate(void);

#endif
