/*  XMMS - Cross-platform multimedia player
 *  Copyright (C) 1998-2000  Peter Alm, Mikael Alm, Olle Hallnas, Thomas Nilsson and 4Front Technologies
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
#include "OSS.h"
#include "libxmms/configfile.h"
#include "libxmms/rateconf.h"

OSSConfig oss_cfg;

void oss_init(void)
{
	ConfigFile *cfgfile;

	memset(&oss_cfg, 0, sizeof (OSSConfig));
	oss_cfg.buffer_size = 3000;
	oss_cfg.prebuffer = 25;

	cfgfile = xmms_cfg_open_default_file();
	xmms_rate_read(cfgfile, "OSS", &oss_cfg.output_rate, &oss_cfg.resample_quality);
	if (cfgfile)
	{
		xmms_cfg_read_int(cfgfile, "OSS", "audio_device", &oss_cfg.audio_device);
		xmms_cfg_read_int(cfgfile, "OSS", "buffer_size", &oss_cfg.buffer_size);
		xmms_cfg_read_int(cfgfile, "OSS", "prebuffer", &oss_cfg.prebuffer);
		xmms_cfg_read_boolean(cfgfile, "OSS", "use_alt_audio_device", &oss_cfg.use_alt_audio_device);
		xmms_cfg_read_string(cfgfile, "OSS", "alt_audio_device", &oss_cfg.alt_audio_device);
		xmms_cfg_free(cfgfile);
	}
}

void oss_get_buffering(int *buffer_ms, int *prebuffer)
{
	*buffer_ms = oss_cfg.buffer_size;
	*prebuffer = oss_cfg.prebuffer;
}

void oss_get_rate(int *rate, int *quality)
{
	*rate = oss_cfg.output_rate;
	*quality = oss_cfg.resample_quality;
}

void oss_cleanup(void)
{
	oss_close();
	g_free(oss_cfg.alt_audio_device);
	oss_cfg.alt_audio_device = NULL;
}
