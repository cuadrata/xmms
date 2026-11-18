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
#include "xmms/i18n.h"

static OutputDriver oss_driver =
{
	NULL,
	NULL,
	NULL,
	OUTPUT_DRIVER_VERSION,
	0,
	oss_init,
	oss_cleanup,
	oss_about,
	oss_configure,
	oss_open,
	oss_write,
	oss_delay,
	oss_start,
	oss_pause,
	oss_flush,
	oss_close,
	oss_get_buffering,
	oss_get_rate,
};

OutputDriver *get_output_driver_info(void)
{
	oss_driver.description = g_strdup_printf(_("OSS Driver %s"), VERSION);
	return &oss_driver;
}
