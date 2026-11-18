/*  XMMS - Cross-platform multimedia player
 *  Copyright (C) 1998-2002  Peter Alm, Mikael Alm, Olle Hallnas,
 *                           Thomas Nilsson and 4Front Technologies
 *  Copyright (C) 1999-2002  Haavard Kvaalen
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

#include "config.h"
#include "urldecode.h"
#include <glib.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* URL-decode a file: URL path, return NULL if it's not what we expect */
char *xmms_urldecode_path(char *encoded_path)
{
	char *out, *cur;

	if (!encoded_path || *encoded_path == '\0' ||
	    g_ascii_strncasecmp(encoded_path, "file:", 5))
		return NULL;
	cur = encoded_path + 5;
	if (!g_ascii_strncasecmp(cur, "//localhost", 11))
		cur += 11;
	/* Decode in place: the result is never longer than the URL. */
	out = encoded_path;
	while (*cur)
	{
		/*
		 * A '%' not followed by two hex digits is taken literally.
		 * Several file managers send unencoded file: urls on drag
		 * and drop.
		 */
		if (cur[0] == '%' && isxdigit((unsigned char) cur[1]) &&
		    isxdigit((unsigned char) cur[2]))
		{
			gchar hex[3] = { cur[1], cur[2], '\0' };

			*out++ = (gchar) strtol(hex, NULL, 16);
			cur += 3;
		}
		else
			*out++ = *cur++;
	}
	*out = '\0';

	return encoded_path;
}
