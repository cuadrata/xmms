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

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <zlib.h>

#include "id3v2.h"
#include "mpg123-plugin.h"

#define HEADER 10
/* The most a tag can hold: its size is 28 bits. */
#define MAX_TAG (256 * 1024 * 1024)
#define MAX_SIZE 0x0fffffff	/* the largest a 28-bit size can say */
#define PADDING 1024		/* room left for later edits in place */

enum { ENC_LATIN1, ENC_UTF16, ENC_UTF16BE, ENC_UTF8 };

/*
 * A growing byte buffer
 */

typedef struct
{
	guchar *p;
	gsize len, cap;
} Buf;

static void buf_add(Buf *b, const void *data, gsize n)
{
	if (!n)
		return;
	if (b->len + n > b->cap)
	{
		b->cap = MAX(b->cap * 2, b->len + n + 64);
		b->p = g_realloc(b->p, b->cap);
	}
	memcpy(b->p + b->len, data, n);
	b->len += n;
}

static void buf_byte(Buf *b, guchar c)
{
	buf_add(b, &c, 1);
}

/* The buffer as a string; the buffer is used up. */
static gchar *buf_string(Buf *b)
{
	buf_byte(b, 0);
	return (gchar *) b->p;
}

/*
 * Text
 */

static void put_utf8(Buf *b, guint32 c)
{
	if (c < 0x80)
		buf_byte(b, c);
	else if (c < 0x800)
	{
		buf_byte(b, 0xc0 | c >> 6);
		buf_byte(b, 0x80 | (c & 0x3f));
	}
	else if (c < 0x10000)
	{
		buf_byte(b, 0xe0 | c >> 12);
		buf_byte(b, 0x80 | (c >> 6 & 0x3f));
		buf_byte(b, 0x80 | (c & 0x3f));
	}
	else
	{
		buf_byte(b, 0xf0 | c >> 18);
		buf_byte(b, 0x80 | (c >> 12 & 0x3f));
		buf_byte(b, 0x80 | (c >> 6 & 0x3f));
		buf_byte(b, 0x80 | (c & 0x3f));
	}
}

/*
 * The code point at *p, moving *p past it. A byte that does not
 * start a well-formed UTF-8 sequence is taken as Latin-1.
 */
static guint32 utf8_next(const guchar **p, const guchar *end)
{
	const guchar *s = *p;
	guint32 c = s[0], min;
	int n, i;

	if (c < 0x80)
		n = 0, min = 0;
	else if (c >= 0xc2 && c < 0xe0)
		n = 1, min = 0x80, c &= 0x1f;
	else if (c >= 0xe0 && c < 0xf0)
		n = 2, min = 0x800, c &= 0x0f;
	else if (c >= 0xf0 && c < 0xf5)
		n = 3, min = 0x10000, c &= 0x07;
	else
		n = -1, min = 0;

	if (n < 0 || end - s <= n)
		goto bad;
	for (i = 1; i <= n; i++)
	{
		if ((s[i] & 0xc0) != 0x80)
			goto bad;
		c = c << 6 | (s[i] & 0x3f);
	}
	if (c < min || c > 0x10ffff || (c >= 0xd800 && c < 0xe000))
		goto bad;
	*p = s + n + 1;
	return c;
bad:
	*p = s + 1;
	return s[0];
}

static gboolean utf8_valid(const guchar *p, gsize n)
{
	const guchar *end = p + n;

	while (p < end)
	{
		const guchar *s = p;
		guint32 c = utf8_next(&p, end);

		if (p == s + 1 && c >= 0x80)
			return FALSE;
	}
	return TRUE;
}

/* Text n bytes long at p, in a frame's encoding, added as UTF-8. */
static void decode(Buf *out, const guchar *p, gsize n, int enc)
{
	gsize i;

	if (enc == ENC_UTF16 || enc == ENC_UTF16BE)
	{
		/* Without a byte order mark, UTF-16 is taken as little-endian. */
		gboolean big = enc == ENC_UTF16BE;

		if (n >= 2 && p[0] == 0xff && p[1] == 0xfe)
			big = FALSE, p += 2, n -= 2;
		else if (n >= 2 && p[0] == 0xfe && p[1] == 0xff)
			big = TRUE, p += 2, n -= 2;
		for (i = 0; i + 1 < n; i += 2)
		{
			guint32 c = big ? p[i] << 8 | p[i + 1] : p[i + 1] << 8 | p[i];

			if (c >= 0xd800 && c < 0xdc00 && i + 3 < n)
			{
				guint32 d = big ? p[i + 2] << 8 | p[i + 3] :
					p[i + 3] << 8 | p[i + 2];

				if (d >= 0xdc00 && d < 0xe000)
				{
					c = 0x10000 + ((c - 0xd800) << 10) + (d - 0xdc00);
					i += 2;
				}
			}
			if (c >= 0xd800 && c < 0xe000)
				c = 0xfffd;
			put_utf8(out, c);
		}
	}
	else if (enc == ENC_UTF8 && utf8_valid(p, n))
		buf_add(out, p, n);
	else
		/* Latin-1, and UTF-8 that turns out not to be. */
		for (i = 0; i < n; i++)
			put_utf8(out, p[i]);
}

/*
 * The length of the string at p, up to its terminator or n, and in
 * *used the bytes it takes with its terminator.
 */
static gsize string_length(const guchar *p, gsize n, int enc, gsize *used)
{
	gsize i;

	if (enc == ENC_UTF16 || enc == ENC_UTF16BE)
	{
		for (i = 0; i + 1 < n; i += 2)
			if (!p[i] && !p[i + 1])
			{
				*used = i + 2;
				return i;
			}
		*used = n;
		return n & ~(gsize) 1;
	}
	for (i = 0; i < n && p[i]; i++)
		;
	*used = i < n ? i + 1 : n;
	return i;
}

/* The encoding that holds text best for the version written. */
static int pick_encoding(const char *text, int version)
{
	const guchar *p = (const guchar *) text, *end = p + strlen(text);
	guint32 max = 0;

	while (p < end)
	{
		guint32 c = utf8_next(&p, end);

		max = MAX(max, c);
	}
	if (max < 0x80 || (version == 3 && max < 0x100))
		return ENC_LATIN1;
	/* 2.3 has no UTF-8, but has UTF-16 with a byte order mark. */
	return version == 3 ? ENC_UTF16 : ENC_UTF8;
}

/* Add UTF-8 text in the encoding enc, with its terminator if asked. */
static void encode(Buf *b, const char *text, int enc, gboolean terminate)
{
	const guchar *p = (const guchar *) text, *end = p + strlen(text);

	if (enc == ENC_UTF16)
	{
		buf_byte(b, 0xff);
		buf_byte(b, 0xfe);
	}
	while (p < end)
	{
		guint32 c = utf8_next(&p, end);

		if (enc == ENC_LATIN1)
			buf_byte(b, c < 0x100 ? c : '?');
		else if (enc == ENC_UTF8)
			put_utf8(b, c);
		else
		{
			guint32 u[2];
			int i, n = 1;

			if (c >= 0x10000)
			{
				u[0] = 0xd800 + ((c - 0x10000) >> 10);
				u[1] = 0xdc00 + ((c - 0x10000) & 0x3ff);
				n = 2;
			}
			else
				u[0] = c;
			for (i = 0; i < n; i++)
			{
				buf_byte(b, u[i] & 0xff);
				buf_byte(b, u[i] >> 8);
			}
		}
	}
	if (terminate)
	{
		buf_byte(b, 0);
		if (enc == ENC_UTF16)
			buf_byte(b, 0);
	}
}

/*
 * Frames
 */

/* Whether a frame's data can be read: not encrypted, nor packed in a way reading could not undo. */
static gboolean frame_plain(const ID3v2Frame *f, int version)
{
	return !(f->flags[1] & (version == 3 ? 0xe0 : 0x4c));
}

static gint find_frame(const ID3v2Tag *tag, const char *id, gint from)
{
	gint i;

	for (i = from; i < tag->nframes; i++)
		if (!strcmp(tag->frames[i].id, id))
			return i;
	return -1;
}

static void remove_frame(ID3v2Tag *tag, gint i)
{
	g_free(tag->frames[i].data);
	tag->nframes--;
	memmove(tag->frames + i, tag->frames + i + 1,
		(tag->nframes - i) * sizeof (ID3v2Frame));
}

/* Put data in frame id: in the first one there is, else in a new one. */
static void replace_frame(ID3v2Tag *tag, const char *id, gint at, Buf *data)
{
	ID3v2Frame *f;

	if (at < 0)
	{
		tag->frames = g_renew(ID3v2Frame, tag->frames, tag->nframes + 1);
		at = tag->nframes++;
		tag->frames[at].data = NULL;
	}
	f = &tag->frames[at];
	g_free(f->data);
	strcpy(f->id, id);
	f->flags[0] = f->flags[1] = 0;
	f->data = data->p;
	f->size = data->len;
}

/* The strings of a text frame; 2.4 can hold several. */
static gchar **text_values(const ID3v2Frame *f)
{
	const guchar *p;
	gsize n, len, used;
	gchar **values = g_new0(gchar *, 1);
	gint count = 0;
	int enc;

	if (f->size < 1)
		return values;
	enc = f->data[0];
	p = f->data + 1;
	n = f->size - 1;
	while (n > 0)
	{
		Buf b = { NULL, 0, 0 };

		len = string_length(p, n, enc, &used);
		decode(&b, p, len, enc);
		values = g_renew(gchar *, values, count + 2);
		values[count++] = buf_string(&b);
		values[count] = NULL;
		p += used;
		n -= used;
	}
	/* Drop what trailing terminators leave behind. */
	while (count > 0 && !values[count - 1][0])
	{
		g_free(values[--count]);
		values[count] = NULL;
	}
	return values;
}

static gchar *join_values(gchar **values)
{
	gchar *text = NULL;

	if (values[0] && values[0][0])
		text = g_strjoinv(" / ", values);
	g_strfreev(values);
	return text;
}

static gchar *get_text(const ID3v2Tag *tag, const char *id)
{
	gint i = find_frame(tag, id, 0);

	while (i >= 0 && !frame_plain(&tag->frames[i], tag->version))
		i = find_frame(tag, id, i + 1);
	return i >= 0 ? join_values(text_values(&tag->frames[i])) : NULL;
}

static void set_text(ID3v2Tag *tag, const char *id, const char *text)
{
	gint i = find_frame(tag, id, 0), j;
	Buf b = { NULL, 0, 0 };
	int enc;

	if (i >= 0)
		while ((j = find_frame(tag, id, i + 1)) >= 0)
			remove_frame(tag, j);
	if (!text || !*text)
	{
		if (i >= 0)
			remove_frame(tag, i);
		return;
	}
	enc = pick_encoding(text, tag->version);
	buf_byte(&b, enc);
	encode(&b, text, enc, FALSE);
	replace_frame(tag, id, i, &b);
}

/*
 * The comment the editor shows is the one with no description:
 * the others are other programs' notes.
 */
static gint find_comment(const ID3v2Tag *tag)
{
	gint i;

	for (i = find_frame(tag, "COMM", 0); i >= 0; i = find_frame(tag, "COMM", i + 1))
	{
		const ID3v2Frame *f = &tag->frames[i];
		gsize used, len;

		if (!frame_plain(f, tag->version) || f->size < 4)
			continue;
		len = string_length(f->data + 4, f->size - 4, f->data[0], &used);
		if (len == 0 || (len == 2 && (f->data[0] == ENC_UTF16 || f->data[0] == ENC_UTF16BE) &&
				 ((f->data[4] == 0xff && f->data[5] == 0xfe) ||
				  (f->data[4] == 0xfe && f->data[5] == 0xff))))
			return i;
	}
	return -1;
}

static gchar *get_comment(const ID3v2Tag *tag)
{
	gint i = find_comment(tag);
	const ID3v2Frame *f;
	Buf b = { NULL, 0, 0 };
	gsize skip, used, len;
	gchar *text;

	if (i < 0)
		return NULL;
	/* Encoding, language and description come before the text. */
	f = &tag->frames[i];
	string_length(f->data + 4, f->size - 4, f->data[0], &skip);
	len = string_length(f->data + 4 + skip, f->size - 4 - skip, f->data[0], &used);
	decode(&b, f->data + 4 + skip, len, f->data[0]);
	text = buf_string(&b);
	if (!*text)
	{
		g_free(text);
		return NULL;
	}
	return text;
}

static void set_comment(ID3v2Tag *tag, const char *text)
{
	gint i = find_comment(tag);
	Buf b = { NULL, 0, 0 };
	int enc;

	if (!text || !*text)
	{
		if (i >= 0)
			remove_frame(tag, i);
		return;
	}
	enc = pick_encoding(text, tag->version);
	buf_byte(&b, enc);
	/* The language the comment had, else English, as most programs write. */
	buf_add(&b, i >= 0 ? (const char *) tag->frames[i].data + 1 : "eng", 3);
	encode(&b, "", enc, TRUE);
	encode(&b, text, enc, FALSE);
	replace_frame(tag, "COMM", i, &b);
}

/*
 * One TCON value as a name: "(17)", "17" and "(17)Rock" all say Rock.
 * Text after the references refines them, and wins.
 */
static gchar *genre_name(const char *v)
{
	gchar **names = g_new0(gchar *, 1), *text;
	gint count = 0;

	while (*v == '(' && v[1] != '(')
	{
		const char *end = strchr(v, ')');
		const char *name = NULL;
		char *num_end;
		long n;

		if (!end)
			break;
		n = strtol(v + 1, &num_end, 10);
		if (num_end == end && num_end > v + 1 && n >= 0 && n < GENRE_MAX)
			name = mpg123_id3_genres[n];
		else if (end == v + 3 && !strncmp(v + 1, "RX", 2))
			name = "Remix";
		else if (end == v + 3 && !strncmp(v + 1, "CR", 2))
			name = "Cover";
		else
			break;
		names = g_renew(gchar *, names, count + 2);
		names[count++] = g_strdup(name);
		names[count] = NULL;
		v = end + 1;
	}
	if (*v == '(' && v[1] == '(')
		v++;
	if (*v)
	{
		char *num_end;
		long n = strtol(v, &num_end, 10);

		g_strfreev(names);
		if (!*num_end && n >= 0 && n < GENRE_MAX && *v >= '0' && *v <= '9')
			return g_strdup(mpg123_id3_genres[n]);
		if (!strcmp(v, "RX"))
			return g_strdup("Remix");
		if (!strcmp(v, "CR"))
			return g_strdup("Cover");
		return g_strdup(v);
	}
	text = names[0] ? g_strjoinv(" / ", names) : g_strdup("");
	g_strfreev(names);
	return text;
}

static gchar *get_genre(const ID3v2Tag *tag)
{
	gint i = find_frame(tag, "TCON", 0), j;
	gchar **values;

	while (i >= 0 && !frame_plain(&tag->frames[i], tag->version))
		i = find_frame(tag, "TCON", i + 1);
	if (i < 0)
		return NULL;
	values = text_values(&tag->frames[i]);
	for (j = 0; values[j]; j++)
	{
		gchar *name = genre_name(values[j]);

		g_free(values[j]);
		values[j] = name;
	}
	return join_values(values);
}

gchar *id3v2_get(const ID3v2Tag *tag, ID3v2Field field)
{
	gchar *year;

	switch (field)
	{
	case ID3V2_TITLE:
		return get_text(tag, "TIT2");
	case ID3V2_ARTIST:
		return get_text(tag, "TPE1");
	case ID3V2_ALBUM:
		return get_text(tag, "TALB");
	case ID3V2_YEAR:
		/* Either may be there, whatever the version says. */
		year = get_text(tag, tag->version == 3 ? "TYER" : "TDRC");
		return year ? year : get_text(tag, tag->version == 3 ? "TDRC" : "TYER");
	case ID3V2_TRACK:
		return get_text(tag, "TRCK");
	case ID3V2_GENRE:
		return get_genre(tag);
	case ID3V2_COMMENT:
		return get_comment(tag);
	default:
		return NULL;
	}
}

void id3v2_set(ID3v2Tag *tag, ID3v2Field field, const char *text)
{
	switch (field)
	{
	case ID3V2_TITLE:
		set_text(tag, "TIT2", text);
		break;
	case ID3V2_ARTIST:
		set_text(tag, "TPE1", text);
		break;
	case ID3V2_ALBUM:
		set_text(tag, "TALB", text);
		break;
	case ID3V2_YEAR:
		/* 2.4 replaced TYER with TDRC. */
		set_text(tag, tag->version == 3 ? "TDRC" : "TYER", NULL);
		set_text(tag, tag->version == 3 ? "TYER" : "TDRC", text);
		break;
	case ID3V2_TRACK:
		set_text(tag, "TRCK", text);
		break;
	case ID3V2_GENRE:
		set_text(tag, "TCON", text);
		break;
	case ID3V2_COMMENT:
		set_comment(tag, text);
		break;
	default:
		break;
	}
}

void id3v2_clear(ID3v2Tag *tag)
{
	while (tag->nframes > 0)
		remove_frame(tag, tag->nframes - 1);
}

void id3v2_free(ID3v2Tag *tag)
{
	if (!tag)
		return;
	id3v2_clear(tag);
	g_free(tag->frames);
	g_free(tag);
}

/*
 * Reading
 */

static guint32 syncsafe(const guchar *p)
{
	return (guint32) (p[0] & 0x7f) << 21 | (p[1] & 0x7f) << 14 |
		(p[2] & 0x7f) << 7 | (p[3] & 0x7f);
}

static guint32 be32(const guchar *p)
{
	return (guint32) p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

/* Undo unsynchronisation, which puts a 0 after every 0xff. */
static gsize resync(guchar *p, gsize n)
{
	gsize i, j;

	for (i = j = 0; i < n; i++)
	{
		p[j++] = p[i];
		if (p[i] == 0xff && i + 1 < n && p[i + 1] == 0)
			i++;
	}
	return j;
}

/*
 * The bytes a tag takes from the 10 bytes h a file starts with, its
 * footer included: 0 if there is no tag, -1 for one of a version
 * this does not know or a header that makes no sense.
 */
static glong tag_region(const guchar *h)
{
	if (memcmp(h, "ID3", 3))
		return 0;
	if (h[3] < 2 || h[3] > 4 || h[4] == 0xff || (h[6] | h[7] | h[8] | h[9]) & 0x80)
		return -1;
	return HEADER + syncsafe(h + 6) + (h[3] == 4 && h[5] & 0x10 ? HEADER : 0);
}

/* The 2.2 frames that 2.4 has a frame of the same layout for. */
static const char v22_ids[][8] =
{
	"TT1TIT1", "TT2TIT2", "TT3TIT3", "TP1TPE1", "TP2TPE2", "TP3TPE3",
	"TP4TPE4", "TALTALB", "TRKTRCK", "TPATPOS", "TYETDRC", "TORTDOR",
	"TCOTCON", "TCMTCOM", "TXTTEXT", "TLATLAN", "TCRTCOP", "TPBTPUB",
	"TENTENC", "TSSTSSE", "TBPTBPM", "TKETKEY", "TLETLEN", "TMTTMED",
	"TOATOPE", "TOFTOFN", "TOLTOLY", "TOTTOAL", "TRCTSRC", "TFTTFLT",
	"TXXTXXX", "COMCOMM", "ULTUSLT", "UFIUFID", "WXXWXXX", "CNTPCNT",
	"POPPOPM", "WAFWOAF", "WARWOAR", "WASWOAS", "WCMWCOM", "WCPWCOP",
	"WPBWPUB"
};

/* A 2.2 frame as the 2.4 one; FALSE for those 2.4 has nothing like. */
static gboolean convert_v22(ID3v2Frame *f, const guchar *h, const guchar *data, gsize size)
{
	gsize i;

	if (!memcmp(h, "PIC", 3) && size >= 5)
	{
		/* PIC names its image format; APIC gives a MIME type. */
		Buf b = { NULL, 0, 0 };
		char fmt[4];

		for (i = 0; i < 3; i++)
			fmt[i] = g_ascii_tolower(data[i + 1]);
		fmt[3] = '\0';
		buf_byte(&b, data[0]);
		buf_add(&b, "image/", 6);
		if (!strcmp(fmt, "jpg"))
			buf_add(&b, "jpeg", 5);
		else
			buf_add(&b, fmt, strlen(fmt) + 1);
		buf_add(&b, data + 4, size - 4);
		strcpy(f->id, "APIC");
		f->data = b.p;
		f->size = b.len;
		return TRUE;
	}
	for (i = 0; i < sizeof (v22_ids) / sizeof (v22_ids[0]); i++)
		if (!memcmp(h, v22_ids[i], 3))
		{
			strcpy(f->id, v22_ids[i] + 3);
			f->data = g_memdup(data, size ? size : 1);
			f->size = size;
			return TRUE;
		}
	return FALSE;
}

/* What compressed data inflates to, up to the most a tag can hold. */
static guchar *inflate_data(const guchar *in, gsize n, gsize *out_len)
{
	z_stream z;
	guchar *out;
	gsize cap = n * 4 + 64;
	int ret;

	memset(&z, 0, sizeof (z));
	if (inflateInit(&z) != Z_OK)
		return NULL;
	out = g_malloc(cap);
	z.next_in = (Bytef *) in;
	z.avail_in = n;
	do
	{
		if (z.total_out == cap)
		{
			if (cap >= MAX_TAG)
				break;
			cap = MIN(cap * 2, MAX_TAG);
			out = g_realloc(out, cap);
		}
		z.next_out = out + z.total_out;
		z.avail_out = cap - z.total_out;
		ret = inflate(&z, Z_NO_FLUSH);
	}
	while (ret == Z_OK);
	*out_len = z.total_out;
	inflateEnd(&z);
	if (ret != Z_STREAM_END)
	{
		g_free(out);
		return NULL;
	}
	return out;
}

/*
 * Take off what a frame carries ahead of its data, and inflate it if it
 * is compressed, so that it reads and is written back as a plain frame.
 * A frame this cannot undo, such as an encrypted one, is kept as it is.
 */
static void unpack_frame(ID3v2Frame *f, int version)
{
	const guchar *p = f->data;
	gsize n = f->size, len;
	guchar *data;
	gboolean compressed, grouped, length;

	if (version == 3)
	{
		if (f->flags[1] & 0x40)
			return;
		compressed = f->flags[1] & 0x80;
		grouped = f->flags[1] & 0x20;
		length = compressed;
	}
	else
	{
		if (f->flags[1] & 0x04)
			return;
		compressed = f->flags[1] & 0x08;
		grouped = f->flags[1] & 0x40;
		length = f->flags[1] & 0x01;
	}
	if (!compressed && !grouped && !length)
		return;

	/* 2.3 gives the length before the group, 2.4 after it. */
	if (version == 3 && length)
	{
		if (n < 4)
			return;
		p += 4;
		n -= 4;
	}
	if (grouped)
	{
		if (n < 1)
			return;
		p++;
		n--;
	}
	if (version == 4 && length)
	{
		if (n < 4)
			return;
		p += 4;
		n -= 4;
	}

	/* The length given is a guide, which writers do not all get right. */
	if (compressed)
	{
		if (!(data = inflate_data(p, n, &len)))
			return;
	}
	else
	{
		data = g_memdup(p, n ? n : 1);
		len = n;
	}
	g_free(f->data);
	f->data = data;
	f->size = len;
	f->flags[1] &= version == 3 ? ~0xa0 : ~0x49;
}

static void add_frame(ID3v2Tag *tag, int version, const guchar *h,
		      const guchar *data, gsize size, gboolean unsynced)
{
	ID3v2Frame f;

	memset(&f, 0, sizeof (f));
	if (version == 2)
	{
		if (!convert_v22(&f, h, data, size))
			return;
	}
	else
	{
		memcpy(f.id, h, 4);
		f.flags[0] = h[8];
		f.flags[1] = h[9];
		f.data = g_memdup(data, size ? size : 1);
		f.size = size;
	}
	if (version == 4)
	{
		/* Undo a frame's unsynchronisation. */
		if (f.flags[1] & 0x02 || unsynced)
			f.size = resync(f.data, f.size);
		f.flags[1] &= ~0x02;
	}
	if (version > 2)
		unpack_frame(&f, version);
	tag->frames = g_renew(ID3v2Frame, tag->frames, tag->nframes + 1);
	tag->frames[tag->nframes++] = f;
}

/*
 * Go through the frames in buf, adding them to tag if it is given.
 * TRUE if the frames run cleanly to the padding or the end. Sizes
 * are syncsafe in 2.4, except where a writer got that wrong.
 */
static gboolean walk_frames(ID3v2Tag *tag, const guchar *buf, gsize len,
			    int version, gboolean syncsafe_sizes, gboolean unsynced)
{
	gsize pos = 0, hlen = version == 2 ? 6 : 10, idlen = version == 2 ? 3 : 4;

	while (pos + hlen <= len)
	{
		const guchar *h = buf + pos;
		gsize size, i;

		if (!h[0])
			return TRUE;	/* padding */
		for (i = 0; i < idlen; i++)
			if (!((h[i] >= 'A' && h[i] <= 'Z') || (h[i] >= '0' && h[i] <= '9')))
				return FALSE;
		if (version == 2)
			size = h[3] << 16 | h[4] << 8 | h[5];
		else if (version == 4 && syncsafe_sizes)
		{
			if ((h[4] | h[5] | h[6] | h[7]) & 0x80)
				return FALSE;
			size = syncsafe(h + 4);
		}
		else
			size = be32(h + 4);
		if (size > len - pos - hlen)
			return FALSE;
		if (tag)
			add_frame(tag, version, h, h + hlen, size, unsynced);
		pos += hlen + size;
	}
	return TRUE;
}

ID3v2Tag *id3v2_read(const char *filename, ID3v2Error *error)
{
	ID3v2Tag *tag = g_new0(ID3v2Tag, 1);
	FILE *fp;
	guchar h[HEADER], *buf = NULL;
	const guchar *p;
	gsize size, len;
	glong region;
	int version;
	gboolean syncsafe_sizes = TRUE;
	int saved;

	*error = ID3V2_OK;
	tag->version = 4;
	if (!(fp = fopen(filename, "rb")))
	{
		*error = ID3V2_OPEN_FAILED;
		goto fail;
	}
	region = fread(h, 1, HEADER, fp) == HEADER ? tag_region(h) : 0;
	if (region == 0)
	{
		fclose(fp);
		return tag;
	}
	if (region < 0 || (h[3] == 2 && h[5] & 0x40))
	{
		/* 2.2's compression was never defined. */
		*error = ID3V2_UNSUPPORTED;
		goto fail;
	}

	version = h[3];
	size = syncsafe(h + 6);
	buf = g_malloc(size ? size : 1);
	if (fread(buf, 1, size, fp) != size)
	{
		*error = ID3V2_TRUNCATED;
		goto fail;
	}
	fclose(fp);
	fp = NULL;

	len = size;
	if (version < 4 && h[5] & 0x80)
		len = resync(buf, len);
	p = buf;
	if (version > 2 && h[5] & 0x40 && len >= 4)
	{
		/* An extended header, which is not kept. */
		gsize ext = version == 3 ? be32(p) + 4 : syncsafe(p);

		ext = MIN(ext, len);
		p += ext;
		len -= ext;
	}
	if (version == 4 && !walk_frames(NULL, p, len, 4, TRUE, FALSE) &&
	    walk_frames(NULL, p, len, 4, FALSE, FALSE))
		syncsafe_sizes = FALSE;
	/* What follows a broken frame is lost; the frames before it are kept. */
	walk_frames(tag, p, len, version, syncsafe_sizes, version == 4 && h[5] & 0x80);
	g_free(buf);

	tag->version = version == 3 ? 3 : 4;
	tag->region = region;
	return tag;

fail:
	saved = errno;
	if (fp)
		fclose(fp);
	g_free(buf);
	id3v2_free(tag);
	errno = saved;
	return NULL;
}

/*
 * Writing
 */

static void put_size(guchar *p, guint32 size, gboolean syncsafe_size)
{
	if (syncsafe_size)
	{
		p[0] = size >> 21 & 0x7f;
		p[1] = size >> 14 & 0x7f;
		p[2] = size >> 7 & 0x7f;
		p[3] = size & 0x7f;
	}
	else
	{
		p[0] = size >> 24;
		p[1] = size >> 16;
		p[2] = size >> 8;
		p[3] = size;
	}
}

static void put_header(guchar *h, int version, guint32 size)
{
	memcpy(h, "ID3", 3);
	h[3] = version;
	h[4] = 0;
	h[5] = 0;
	put_size(h + 6, size, TRUE);
}

/* The frames as the tag's version writes them. */
static void build_frames(const ID3v2Tag *tag, Buf *b)
{
	gint i;

	for (i = 0; i < tag->nframes; i++)
	{
		const ID3v2Frame *f = &tag->frames[i];
		guchar h[HEADER];

		/* Frames that ask to go when the tag changes go. */
		if (f->flags[0] & (tag->version == 3 ? 0x80 : 0x40))
			continue;
		memcpy(h, f->id, 4);
		put_size(h + 4, f->size, tag->version == 4);
		h[8] = f->flags[0];
		h[9] = f->flags[1];
		buf_add(b, h, HEADER);
		buf_add(b, f->data, f->size);
	}
}

static gboolean write_all(int fd, const guchar *p, gsize n)
{
	while (n > 0)
	{
		ssize_t done = write(fd, p, n);

		if (done < 0 && errno == EINTR)
			continue;
		if (done <= 0)
			return FALSE;
		p += done;
		n -= done;
	}
	return TRUE;
}

/* A tag the size of the region it goes in, padded out. */
static void build_tag(const ID3v2Tag *tag, const Buf *frames, gsize region, Buf *out)
{
	guchar h[HEADER];
	gsize pad = region - HEADER - frames->len;

	put_header(h, tag->version, region - HEADER);
	buf_add(out, h, HEADER);
	buf_add(out, frames->p, frames->len);
	while (pad-- > 0)
		buf_byte(out, 0);
}

/*
 * Write the tag over the old one when it fits, which leaves the audio
 * where it is. Otherwise write the file anew beside it and put that in
 * its place, so that a failure part way leaves the file as it was.
 */
ID3v2Error id3v2_write(const ID3v2Tag *tag, const char *filename)
{
	Buf frames = { NULL, 0, 0 }, out = { NULL, 0, 0 };
	char *path, *tmp = NULL;
	int fd = -1, tfd = -1, saved;
	guchar h[HEADER];
	glong region = 0;
	struct stat st;
	ID3v2Error error = ID3V2_OK;

	/* Write through a link to the file, not over the link. */
	if (!(path = realpath(filename, NULL)))
		return ID3V2_OPEN_FAILED;
	if ((fd = open(path, O_RDWR)) < 0 || fstat(fd, &st) < 0)
	{
		error = ID3V2_OPEN_FAILED;
		goto done;
	}
	if (read(fd, h, HEADER) == HEADER)
		region = tag_region(h);
	if (region != tag->region)
	{
		error = ID3V2_CHANGED;
		goto done;
	}

	build_frames(tag, &frames);
	if (frames.len > MAX_SIZE - PADDING)
	{
		errno = EFBIG;
		goto write_failed;
	}

	if (frames.len > 0 && (glong) (frames.len + HEADER) <= region)
	{
		build_tag(tag, &frames, region, &out);
		if (lseek(fd, 0, SEEK_SET) != 0 || !write_all(fd, out.p, out.len) ||
		    fsync(fd) < 0)
			goto write_failed;
		goto done;
	}
	if (frames.len == 0 && region == 0)
		goto done;

	tmp = g_strconcat(path, ".XXXXXX", NULL);
	if ((tfd = mkstemp(tmp)) < 0)
	{
		g_free(tmp);
		tmp = NULL;
		goto write_failed;
	}
	if (frames.len > 0)
		build_tag(tag, &frames, HEADER + frames.len + PADDING, &out);
	if (!write_all(tfd, out.p, out.len) || lseek(fd, region, SEEK_SET) != region)
		goto write_failed;
	for (;;)
	{
		guchar copy[65536];
		ssize_t n = read(fd, copy, sizeof (copy));

		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0 || (n > 0 && !write_all(tfd, copy, n)))
			goto write_failed;
		if (n == 0)
			break;
	}
	if (fchmod(tfd, st.st_mode & 07777) < 0 || fsync(tfd) < 0)
		goto write_failed;
	/* Keep the owner when allowed to; a file of one's own is fine too. */
	if (fchown(tfd, st.st_uid, st.st_gid) < 0)
		errno = 0;
	if (close(tfd) < 0)
	{
		tfd = -1;
		goto write_failed;
	}
	tfd = -1;
	if (rename(tmp, path) < 0)
		goto write_failed;
	g_free(tmp);
	tmp = NULL;
	goto done;

write_failed:
	error = ID3V2_WRITE_FAILED;
done:
	/* Say why it failed, whatever cleaning up does to errno. */
	saved = errno;
	if (tfd >= 0)
		close(tfd);
	if (tmp)
	{
		unlink(tmp);
		g_free(tmp);
	}
	if (fd >= 0)
		close(fd);
	free(path);
	g_free(frames.p);
	g_free(out.p);
	errno = saved;
	return error;
}
