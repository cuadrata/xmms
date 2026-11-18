/*  XMMS - Cross-platform multimedia player
 *  Copyright (C) 2026  The CGTK authors
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
#ifndef SKINARCHIVE_H
#define SKINARCHIVE_H

#include <glib.h>

/* The largest skin file read, packed or not. Skin images are tens of
 * kilobytes; this leaves room for odd ones and stops a crafted archive
 * from taking all memory. */
#define SKIN_FILE_MAX (8 * 1024 * 1024)

typedef struct _SkinArchive SkinArchive;

gboolean skin_archive_is_archive(const gchar *path);
SkinArchive *skin_archive_open(const gchar *path);
const guchar *skin_archive_find(SkinArchive *archive, const gchar *name, gsize *size);
void skin_archive_free(SkinArchive *archive);

guchar *skin_file_read(const gchar *filename, gsize max, gsize *size);

#endif
