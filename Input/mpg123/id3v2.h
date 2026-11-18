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
 * Reading and writing the ID3v2 tag at the start of a file.
 *
 * Versions 2.2, 2.3 and 2.4 are read. A tag is written back in the
 * version it was read as, except that 2.2 becomes 2.4, as does a new
 * tag. Frames the editor does not touch are kept as they were.
 */

#ifndef ID3V2_H
#define ID3V2_H

#include <glib.h>

typedef struct
{
	char id[5];
	guchar flags[2];	/* as the tag's version has them */
	guchar *data;
	gsize size;
} ID3v2Frame;

typedef struct
{
	gint version;		/* 3 or 4: the version it is written as */
	glong region;		/* bytes it takes at the start of the file; 0 if none */
	ID3v2Frame *frames;
	gint nframes;
} ID3v2Tag;

typedef enum
{
	ID3V2_OK,
	ID3V2_OPEN_FAILED,	/* errno says why */
	ID3V2_WRITE_FAILED,	/* errno says why */
	ID3V2_UNSUPPORTED,	/* a tag of a version or kind this cannot edit */
	ID3V2_TRUNCATED,	/* a tag the file ends in the middle of */
	ID3V2_CHANGED		/* the file changed after the tag was read */
} ID3v2Error;

typedef enum
{
	ID3V2_TITLE,
	ID3V2_ARTIST,
	ID3V2_ALBUM,
	ID3V2_YEAR,
	ID3V2_TRACK,
	ID3V2_GENRE,
	ID3V2_COMMENT,
	ID3V2_FIELDS
} ID3v2Field;

/*
 * The tag of a file, empty if it has none. NULL if the file cannot
 * be read or holds a tag this cannot handle; *error then says why.
 */
ID3v2Tag *id3v2_read(const char *filename, ID3v2Error *error);
void id3v2_free(ID3v2Tag *tag);

/* A field's text in UTF-8, or NULL. Genres are given by name. */
gchar *id3v2_get(const ID3v2Tag *tag, ID3v2Field field);
/* Set a field from UTF-8 text; NULL or "" removes it. */
void id3v2_set(ID3v2Tag *tag, ID3v2Field field, const char *text);
/* Remove every frame, so that writing removes the tag. */
void id3v2_clear(ID3v2Tag *tag);

/*
 * Put the tag in the file it was read from. A tag with no frames is
 * removed from the file.
 */
ID3v2Error id3v2_write(const ID3v2Tag *tag, const char *filename);

#endif
