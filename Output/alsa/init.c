/*  XMMS - ALSA output plugin
 *  Copyright (C) 2001-2003 Matthieu Sozeau <mattam@altern.org>
 *  Copyright (C) 2003-2005 Haavard Kvaalen
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
#include "alsa.h"
#include <dlfcn.h>

#define THREAD_BUFFER_TIME_MIN 1000
#define THREAD_BUFFER_TIME_MAX 10000

struct alsa_config alsa_cfg;

void alsa_init(void)
{
	ConfigFile *cfgfile;

	memset(&alsa_cfg, 0, sizeof (alsa_cfg));
	alsa_cfg.buffer_time = 500;
	alsa_cfg.period_time = 50;
	alsa_cfg.thread_buffer_time = 3000;

	cfgfile = xmms_cfg_open_default_file();
	if (!xmms_cfg_read_string(cfgfile, "ALSA", "pcm_device",
				  &alsa_cfg.pcm_device))
		alsa_cfg.pcm_device = g_strdup("default");
	xmms_cfg_read_int(cfgfile, "ALSA", "buffer_time", &alsa_cfg.buffer_time);
	xmms_cfg_read_int(cfgfile, "ALSA", "period_time", &alsa_cfg.period_time);
	xmms_cfg_read_int(cfgfile, "ALSA", "thread_buffer_time", &alsa_cfg.thread_buffer_time);
	xmms_rate_read(cfgfile, "ALSA", &alsa_cfg.output_rate, &alsa_cfg.resample_quality);
	xmms_cfg_free(cfgfile);
	alsa_cfg.thread_buffer_time = CLAMP(alsa_cfg.thread_buffer_time,
					    THREAD_BUFFER_TIME_MIN, THREAD_BUFFER_TIME_MAX);
	alsa_cfg.buffer_time = CLAMP(alsa_cfg.buffer_time, 20, 10000);
	alsa_cfg.period_time = CLAMP(alsa_cfg.period_time, 1, alsa_cfg.buffer_time / 2);

	/* For ALSA's own plugins, which look for its symbols. */
	if (dlopen("libasound.so.2", RTLD_NOW | RTLD_GLOBAL) == NULL)
		g_message("Cannot load the ALSA library: %s", dlerror());
}

/* XMMS's buffer for ALSA: half of it before a song starts, as ever. */
void alsa_get_buffering(int *buffer_ms, int *prebuffer)
{
	*buffer_ms = alsa_cfg.thread_buffer_time;
	*prebuffer = 50;
}

void alsa_get_rate(int *rate, int *quality)
{
	*rate = alsa_cfg.output_rate;
	*quality = alsa_cfg.resample_quality;
}

void alsa_cleanup(void)
{
	alsa_close();
	g_free(alsa_cfg.pcm_device);
	alsa_cfg.pcm_device = NULL;
}
