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

/*
 * Skin archives, read into memory.
 *
 * A skin is a handful of BMP images and text files, and an archived one
 * (.wsz, .zip, .tar, .tar.gz, .tgz) comes from anywhere. Rather than
 * hand it to unzip or tar, which would write whatever it names --
 * links, devices, FIFOs, paths outside the directory -- the archive is
 * read here and its files kept in memory. Only regular files named
 * *.bmp or *.txt are kept, by their name without its directory, so
 * nothing in an archive can reach the file system. Sizes are limited,
 * so a small archive cannot unpack to fill memory.
 *
 * ZIP members may be stored or deflated; gzip is deflate. The inflater
 * follows RFC 1951 and checks every read and every back reference
 * against what it has.
 */

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "skinarchive.h"

/* The largest archive read, and the most its members may hold in all. */
#define ARCHIVE_MAX (16 * 1024 * 1024)
#define UNPACKED_MAX (32 * 1024 * 1024)
/* Far more files than any skin has. */
#define MEMBERS_MAX 1024

typedef struct
{
	gchar *name;		/* Without its directory */
	gint depth;		/* How many directories it was in */
	const guchar *data;
	gsize size;
} SkinMember;

struct _SkinArchive
{
	guchar *file;		/* The archive as read */
	guchar *unpacked;	/* A .tar.gz's tar */
	GSList *owned;		/* Inflated ZIP members */
	SkinMember *members;
	gint n_members;
	gsize total;
};

/* ------------------------------------------------------------------ */

/*
 * The contents of a regular file, at most MAX bytes, with a NUL after
 * them that SIZE does not count. Anything else -- a FIFO, a device, a
 * directory -- is refused without waiting on it.
 */
guchar *skin_file_read(const gchar *filename, gsize max, gsize *size)
{
	struct stat st;
	guchar *data;
	gsize done = 0;
	int fd;

	fd = open(filename, O_RDONLY | O_NONBLOCK | O_NOCTTY);
	if (fd < 0)
		return NULL;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
	    st.st_size < 0 || (guint64) st.st_size > max)
	{
		close(fd);
		return NULL;
	}

	data = g_malloc((gsize) st.st_size + 1);
	while (done < (gsize) st.st_size)
	{
		ssize_t n = read(fd, data + done, (gsize) st.st_size - done);

		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			break;
		done += n;
	}
	close(fd);
	if (done != (gsize) st.st_size)
	{
		g_free(data);
		return NULL;
	}
	data[done] = '\0';
	*size = done;
	return data;
}

static guint32 crc32_update(guint32 crc, const guchar *data, gsize len)
{
	gsize i;
	gint k;

	crc = ~crc;
	for (i = 0; i < len; i++)
	{
		crc ^= data[i];
		for (k = 0; k < 8; k++)
			crc = (crc >> 1) ^ (0xedb88320 & (0 - (crc & 1)));
	}
	return ~crc;
}

static guint16 get_le16(const guchar *p)
{
	return p[0] | (p[1] << 8);
}

static guint32 get_le32(const guchar *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((guint32) p[3] << 24);
}

/* ------------------------------------------------------------------ */
/* Inflate (RFC 1951) */

#define MAXBITS 15
#define MAXLCODES 286
#define MAXDCODES 30
#define FIXLCODES 288

typedef struct
{
	const guchar *in;
	gsize in_len, in_pos;
	guint32 bitbuf;
	gint bitcnt;

	guchar *out;
	gsize out_len, out_size, out_max;
} Inflate;

typedef struct
{
	gshort count[MAXBITS + 1];	/* Codes of each length */
	gshort symbol[FIXLCODES];	/* Symbols in canonical order */
} Huffman;

/* NEED bits, or -1 at the end of the input. */
static gint inflate_bits(Inflate *s, gint need)
{
	guint32 val = s->bitbuf;

	while (s->bitcnt < need)
	{
		if (s->in_pos >= s->in_len)
			return -1;
		val |= (guint32) s->in[s->in_pos++] << s->bitcnt;
		s->bitcnt += 8;
	}
	s->bitbuf = val >> need;
	s->bitcnt -= need;
	return val & ((1U << need) - 1);
}

static gboolean inflate_put(Inflate *s, guchar c)
{
	if (s->out_len == s->out_size)
	{
		gsize size = s->out_size ? s->out_size * 2 : 65536;

		if (s->out_len >= s->out_max)
			return FALSE;
		if (size > s->out_max)
			size = s->out_max;
		s->out = g_realloc(s->out, size);
		s->out_size = size;
	}
	s->out[s->out_len++] = c;
	return TRUE;
}

static gint inflate_stored(Inflate *s)
{
	guint len;

	s->bitbuf = 0;
	s->bitcnt = 0;
	if (s->in_len - s->in_pos < 4)
		return -1;
	len = get_le16(s->in + s->in_pos);
	if ((get_le16(s->in + s->in_pos + 2) ^ 0xffff) != len)
		return -1;
	s->in_pos += 4;
	if (s->in_len - s->in_pos < len)
		return -1;
	while (len--)
		if (!inflate_put(s, s->in[s->in_pos++]))
			return -1;
	return 0;
}

/* A symbol, or -1 for no input or a code the table does not have. */
static gint inflate_decode(Inflate *s, const Huffman *h)
{
	gint len, code = 0, first = 0, index = 0, bit, count;

	for (len = 1; len <= MAXBITS; len++)
	{
		if ((bit = inflate_bits(s, 1)) < 0)
			return -1;
		code |= bit;
		count = h->count[len];
		if (code - count < first)
			return h->symbol[index + (code - first)];
		index += count;
		first += count;
		first <<= 1;
		code <<= 1;
	}
	return -1;
}

/* 0 for a complete code, more for an incomplete one, less for one that
 * has too many codes of some length. */
static gint inflate_construct(Huffman *h, const gshort *length, gint n)
{
	gshort offs[MAXBITS + 1];
	gint symbol, len, left;

	for (len = 0; len <= MAXBITS; len++)
		h->count[len] = 0;
	for (symbol = 0; symbol < n; symbol++)
		h->count[length[symbol]]++;
	if (h->count[0] == n)
		return 0;

	left = 1;
	for (len = 1; len <= MAXBITS; len++)
	{
		left <<= 1;
		left -= h->count[len];
		if (left < 0)
			return left;
	}

	offs[1] = 0;
	for (len = 1; len < MAXBITS; len++)
		offs[len + 1] = offs[len] + h->count[len];
	for (symbol = 0; symbol < n; symbol++)
		if (length[symbol] != 0)
			h->symbol[offs[length[symbol]]++] = symbol;
	return left;
}

static gint inflate_codes(Inflate *s, const Huffman *lencode, const Huffman *distcode)
{
	static const gshort lbase[29] = {
		3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
		35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
	static const gshort lext[29] = {
		0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
		3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
	static const gshort dbase[30] = {
		1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
		257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
		8193, 12289, 16385, 24577};
	static const gshort dext[30] = {
		0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
		7, 7, 8, 8, 9, 9, 10, 10, 11, 11,
		12, 12, 13, 13};
	gint symbol, extra;
	gsize len, dist;

	for (;;)
	{
		symbol = inflate_decode(s, lencode);
		if (symbol < 0)
			return -1;
		if (symbol < 256)
		{
			if (!inflate_put(s, symbol))
				return -1;
		}
		else if (symbol == 256)
			return 0;
		else
		{
			symbol -= 257;
			if (symbol >= 29)
				return -1;
			if ((extra = inflate_bits(s, lext[symbol])) < 0)
				return -1;
			len = lbase[symbol] + extra;

			symbol = inflate_decode(s, distcode);
			if (symbol < 0 || symbol >= 30)
				return -1;
			if ((extra = inflate_bits(s, dext[symbol])) < 0)
				return -1;
			dist = dbase[symbol] + extra;
			if (dist > s->out_len)
				return -1;

			while (len--)
				if (!inflate_put(s, s->out[s->out_len - dist]))
					return -1;
		}
	}
}

static gint inflate_fixed(Inflate *s)
{
	Huffman lencode, distcode;
	gshort lengths[FIXLCODES];
	gint symbol;

	for (symbol = 0; symbol < 144; symbol++)
		lengths[symbol] = 8;
	for (; symbol < 256; symbol++)
		lengths[symbol] = 9;
	for (; symbol < 280; symbol++)
		lengths[symbol] = 7;
	for (; symbol < FIXLCODES; symbol++)
		lengths[symbol] = 8;
	inflate_construct(&lencode, lengths, FIXLCODES);
	for (symbol = 0; symbol < MAXDCODES; symbol++)
		lengths[symbol] = 5;
	inflate_construct(&distcode, lengths, MAXDCODES);

	return inflate_codes(s, &lencode, &distcode);
}

static gint inflate_dynamic(Inflate *s)
{
	static const gshort order[19] = {
		16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
	gshort lengths[MAXLCODES + MAXDCODES];
	Huffman lencode, distcode;
	gint nlen, ndist, ncode, index, err, symbol, len, n;

	if ((nlen = inflate_bits(s, 5)) < 0 ||
	    (ndist = inflate_bits(s, 5)) < 0 ||
	    (ncode = inflate_bits(s, 4)) < 0)
		return -1;
	nlen += 257;
	ndist += 1;
	ncode += 4;
	if (nlen > MAXLCODES || ndist > MAXDCODES)
		return -1;

	for (index = 0; index < ncode; index++)
		if ((lengths[order[index]] = inflate_bits(s, 3)) < 0)
			return -1;
	for (; index < 19; index++)
		lengths[order[index]] = 0;
	if (inflate_construct(&lencode, lengths, 19) != 0)
		return -1;

	index = 0;
	while (index < nlen + ndist)
	{
		symbol = inflate_decode(s, &lencode);
		if (symbol < 0)
			return -1;
		if (symbol < 16)
		{
			lengths[index++] = symbol;
			continue;
		}
		len = 0;
		if (symbol == 16)
		{
			if (index == 0)
				return -1;
			len = lengths[index - 1];
			n = inflate_bits(s, 2);
			symbol = 3 + n;
		}
		else if (symbol == 17)
		{
			n = inflate_bits(s, 3);
			symbol = 3 + n;
		}
		else
		{
			n = inflate_bits(s, 7);
			symbol = 11 + n;
		}
		if (n < 0 || index + symbol > nlen + ndist)
			return -1;
		while (symbol--)
			lengths[index++] = len;
	}

	/* There has to be an end-of-block code */
	if (lengths[256] == 0)
		return -1;

	err = inflate_construct(&lencode, lengths, nlen);
	if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1))
		return -1;
	err = inflate_construct(&distcode, lengths + nlen, ndist);
	if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1))
		return -1;

	return inflate_codes(s, &lencode, &distcode);
}

/*
 * Inflate the deflate stream at IN, at most OUT_MAX bytes of it. The
 * result is g_malloc()ed; *USED says how much of IN the stream took.
 */
static guchar *inflate(const guchar *in, gsize in_len, gsize out_max,
		       gsize *out_len, gsize *used)
{
	Inflate s;
	gint last, type, err;

	memset(&s, 0, sizeof (s));
	s.in = in;
	s.in_len = in_len;
	s.out_max = out_max;

	do
	{
		if ((last = inflate_bits(&s, 1)) < 0 ||
		    (type = inflate_bits(&s, 2)) < 0)
			err = -1;
		else if (type == 0)
			err = inflate_stored(&s);
		else if (type == 1)
			err = inflate_fixed(&s);
		else if (type == 2)
			err = inflate_dynamic(&s);
		else
			err = -1;
	}
	while (err == 0 && !last);

	if (err != 0)
	{
		g_free(s.out);
		return NULL;
	}
	*out_len = s.out_len;
	if (used)
		*used = s.in_pos;
	/* Something to free, and to read as empty, even with no output */
	return s.out ? s.out : g_malloc(1);
}

/* ------------------------------------------------------------------ */
/* Members */

static gboolean member_wanted(const gchar *name)
{
	gsize len = strlen(name);

	return len > 4 &&
		(!g_ascii_strcasecmp(name + len - 4, ".bmp") ||
		 !g_ascii_strcasecmp(name + len - 4, ".txt"));
}

/*
 * The name to keep a member by, if it is one of a skin's files and
 * there is room for SIZE more bytes, else NULL. PATH is its name in
 * the archive, LEN bytes and maybe not NUL-terminated. Only the part
 * after its last / or \ is kept; *DEPTH is how many directories it was
 * in, to prefer a file near the top, as for a skin directory.
 */
static gchar *member_name(SkinArchive *archive, const gchar *path, gsize len,
			  gsize size, gint *depth)
{
	const gchar *base = path;
	gchar *name;
	gsize i;

	*depth = 0;
	for (i = 0; i < len && path[i] != '\0'; i++)
		if (path[i] == '/' || path[i] == '\\')
		{
			base = path + i + 1;
			(*depth)++;
		}
	len = i - (base - path);
	if (len == 0 || archive->n_members >= MEMBERS_MAX ||
	    size > SKIN_FILE_MAX || size > UNPACKED_MAX - archive->total)
		return NULL;

	name = g_strndup(base, len);
	if (!member_wanted(name))
	{
		g_free(name);
		return NULL;
	}
	return name;
}

/* Keep a member; NAME is from member_name(), and now the archive's. */
static void archive_keep(SkinArchive *archive, gchar *name, gint depth,
			 const guchar *data, gsize size)
{
	SkinMember *m = &archive->members[archive->n_members++];

	m->name = name;
	m->depth = depth;
	m->data = data;
	m->size = size;
	archive->total += size;
}

/* Keep a member that is a regular file, if it is one of a skin's. */
static void archive_add(SkinArchive *archive, const gchar *path, gsize len,
			const guchar *data, gsize size)
{
	gchar *name;
	gint depth;

	if ((name = member_name(archive, path, len, size, &depth)) != NULL)
		archive_keep(archive, name, depth, data, size);
}

/* ------------------------------------------------------------------ */
/* tar */

/* An octal field; FALSE if it is not one, or too large for us. */
static gboolean tar_octal(const guchar *field, gint len, gsize *value)
{
	gsize v = 0;
	gint i = 0;

	while (i < len && field[i] == ' ')
		i++;
	if (i == len || field[i] < '0' || field[i] > '7')
		return FALSE;
	for (; i < len && field[i] >= '0' && field[i] <= '7'; i++)
	{
		if (v > ARCHIVE_MAX + UNPACKED_MAX)
			return FALSE;
		v = v * 8 + (field[i] - '0');
	}
	*value = v;
	return TRUE;
}

static gboolean tar_header_ok(const guchar *h)
{
	gsize sum = 0, stored;
	gint i;

	for (i = 0; i < 512; i++)
		sum += (i >= 148 && i < 156) ? ' ' : h[i];
	return tar_octal(h + 148, 8, &stored) && stored == sum;
}

/* The path in a pax extended header, if it gives one. */
static gchar *tar_pax_path(const guchar *data, gsize size)
{
	gsize pos = 0;

	while (pos < size)
	{
		gsize len = 0, i = pos, key;

		while (i < size && data[i] >= '0' && data[i] <= '9' && len < size)
			len = len * 10 + (data[i++] - '0');
		if (i >= size || data[i] != ' ' || len <= i - pos || len > size - pos)
			return NULL;
		key = i + 1;
		if (pos + len - key > 5 && !memcmp(data + key, "path=", 5) &&
		    data[pos + len - 1] == '\n')
			return g_strndup((const gchar *) data + key + 5,
					 pos + len - 1 - key - 5);
		pos += len;
	}
	return NULL;
}

static gboolean tar_read(SkinArchive *archive, const guchar *data, gsize len)
{
	gchar *long_name = NULL;
	gsize pos = 0;
	gboolean any = FALSE;

	while (len - pos >= 512)
	{
		const guchar *h = data + pos;
		gsize size, padded;
		gint i;

		for (i = 0; i < 512 && h[i] == 0; i++)
			;
		if (i == 512)
			break;
		if (!tar_header_ok(h) || !tar_octal(h + 124, 12, &size))
			break;
		pos += 512;
		if (size > len - pos)
			break;
		padded = (size + 511) & ~(gsize) 511;

		switch (h[156])
		{
			case 'L':	/* GNU long name */
			case 'x':	/* pax extended header */
				g_free(long_name);
				if (h[156] == 'L')
					long_name = g_strndup((const gchar *) data + pos,
							      MIN(size, 4096));
				else
					long_name = tar_pax_path(data + pos, size);
				break;
			case '0':
			case '\0':
			case '7':	/* Regular files */
				if (long_name)
					archive_add(archive, long_name, strlen(long_name),
						    data + pos, size);
				else
					/* ustar's prefix only adds directories */
					archive_add(archive, (const gchar *) h, 100,
						    data + pos, size);
				any = TRUE;
				/* fall through */
			default:	/* Links, devices, directories... */
				g_free(long_name);
				long_name = NULL;
				break;
		}
		if (padded > len - pos)
			break;
		pos += padded;
	}
	g_free(long_name);
	return any;
}

/* ------------------------------------------------------------------ */
/* gzip (RFC 1952) */

static gboolean gzip_read(SkinArchive *archive, const guchar *data, gsize len)
{
	gsize pos = 10, out_len, used;
	guchar flags;

	if (len < 18 || data[0] != 0x1f || data[1] != 0x8b || data[2] != 8)
		return FALSE;
	flags = data[3];
	if (flags & 0xe0)
		return FALSE;
	if (flags & 4)		/* FEXTRA */
	{
		if (len - pos < 2)
			return FALSE;
		pos += 2 + get_le16(data + pos);
	}
	if (flags & 8)		/* FNAME */
	{
		while (pos < len && data[pos])
			pos++;
		pos++;
	}
	if (flags & 16)		/* FCOMMENT */
	{
		while (pos < len && data[pos])
			pos++;
		pos++;
	}
	if (flags & 2)		/* FHCRC */
		pos += 2;
	if (pos > len)
		return FALSE;

	archive->unpacked = inflate(data + pos, len - pos, UNPACKED_MAX, &out_len, &used);
	if (!archive->unpacked)
		return FALSE;
	pos += used;
	if (len - pos < 8 ||
	    get_le32(data + pos) != crc32_update(0, archive->unpacked, out_len) ||
	    get_le32(data + pos + 4) != (guint32) out_len)
		return FALSE;

	return tar_read(archive, archive->unpacked, out_len);
}

/* ------------------------------------------------------------------ */
/* ZIP */

static gboolean zip_read(SkinArchive *archive, const guchar *data, gsize len)
{
	gsize eocd, pos, end;
	guint entries, i;
	gboolean any = FALSE;

	if (len < 22)
		return FALSE;
	/* The end record, after which there may be a comment */
	for (eocd = len - 22;; eocd--)
	{
		if (get_le32(data + eocd) == 0x06054b50 &&
		    get_le16(data + eocd + 20) == len - eocd - 22)
			break;
		if (eocd == 0 || len - eocd > 22 + 65535)
			return FALSE;
	}
	entries = get_le16(data + eocd + 10);
	pos = get_le32(data + eocd + 16);
	end = pos + get_le32(data + eocd + 12);
	if (pos > eocd || end > eocd || end < pos)
		return FALSE;

	for (i = 0; i < entries; i++)
	{
		guint16 made_by, flags, method, nlen, xlen, clen;
		guint32 crc, csize, usize, mode, local;
		const guchar *name, *member;
		guchar *out = NULL;
		gchar *kept;
		gsize start;
		gint depth;

		if (end - pos < 46 || get_le32(data + pos) != 0x02014b50)
			break;
		made_by = get_le16(data + pos + 4);
		flags = get_le16(data + pos + 8);
		method = get_le16(data + pos + 10);
		crc = get_le32(data + pos + 16);
		csize = get_le32(data + pos + 20);
		usize = get_le32(data + pos + 24);
		nlen = get_le16(data + pos + 28);
		xlen = get_le16(data + pos + 30);
		clen = get_le16(data + pos + 32);
		mode = get_le32(data + pos + 38) >> 16;
		local = get_le32(data + pos + 42);
		name = data + pos + 46;
		if (end - pos - 46 < (gsize) nlen + xlen + clen)
			break;
		pos += 46 + nlen + xlen + clen;

		/* Encrypted, or not a regular file (a Unix symlink...) */
		if ((flags & 1) || nlen == 0 || name[nlen - 1] == '/' ||
		    ((made_by >> 8) == 3 && (mode & 0170000) != 0 &&
		     (mode & 0170000) != 0100000))
			continue;
		/* A ZIP is read, whatever it holds */
		any = TRUE;
		if (method != 0 && method != 8)
			continue;
		/* Before inflating anything: is it wanted, and is there room? */
		if ((kept = member_name(archive, (const gchar *) name, nlen,
					usize, &depth)) == NULL)
			continue;

		/* The data follows the member's local header */
		if (local > eocd || eocd - local < 30 ||
		    get_le32(data + local) != 0x04034b50)
		{
			g_free(kept);
			continue;
		}
		start = local + 30 + get_le16(data + local + 26) +
			get_le16(data + local + 28);
		if (start > eocd || eocd - start < csize ||
		    (method == 0 && csize != usize))
		{
			g_free(kept);
			continue;
		}

		if (method == 0)
			member = data + start;
		else
		{
			gsize out_len;

			out = inflate(data + start, csize, usize, &out_len, NULL);
			if (out && out_len != usize)
			{
				g_free(out);
				out = NULL;
			}
			if (!out)
			{
				g_free(kept);
				continue;
			}
			member = out;
		}
		if (crc32_update(0, member, usize) != crc)
		{
			if (method != 0)
				g_free(out);
			g_free(kept);
			continue;
		}
		if (method != 0)
			archive->owned = g_slist_prepend(archive->owned, out);
		archive_keep(archive, kept, depth, member, usize);
	}
	return any;
}

/* ------------------------------------------------------------------ */

/* Archives are told apart by their content; this is how the skin
 * browser and the loader recognise one by name. */
gboolean skin_archive_is_archive(const gchar *path)
{
	static const gchar *endings[] = { ".wsz", ".zip", ".tgz", ".gz", ".tar" };
	const gchar *ending = strrchr(path, '.');
	guint i;

	if (!ending)
		return FALSE;
	for (i = 0; i < sizeof (endings) / sizeof (endings[0]); i++)
		if (!g_ascii_strcasecmp(ending, endings[i]))
			return TRUE;
	return FALSE;
}

SkinArchive *skin_archive_open(const gchar *path)
{
	SkinArchive *archive;
	gsize len;
	gboolean ok;

	archive = g_new0(SkinArchive, 1);
	archive->file = skin_file_read(path, ARCHIVE_MAX, &len);
	if (!archive->file)
	{
		g_message("Unable to read skin %s", path);
		g_free(archive);
		return NULL;
	}
	archive->members = g_new(SkinMember, MEMBERS_MAX);

	if (len >= 4 && get_le32(archive->file) == 0x04034b50)
		ok = zip_read(archive, archive->file, len);
	else if (len >= 2 && archive->file[0] == 0x1f && archive->file[1] == 0x8b)
		ok = gzip_read(archive, archive->file, len);
	else
		ok = tar_read(archive, archive->file, len);

	if (!ok)
	{
		g_message("Unable to unpack skin %s", path);
		skin_archive_free(archive);
		return NULL;
	}
	return archive;
}

/* The member called NAME (in any case), the nearest the top if there
 * are several, or NULL. */
const guchar *skin_archive_find(SkinArchive *archive, const gchar *name, gsize *size)
{
	SkinMember *found = NULL;
	gint i;

	for (i = 0; i < archive->n_members; i++)
	{
		SkinMember *m = &archive->members[i];

		if (!g_ascii_strcasecmp(m->name, name) &&
		    (!found || m->depth < found->depth))
			found = m;
	}
	if (!found)
		return NULL;
	*size = found->size;
	return found->data;
}

void skin_archive_free(SkinArchive *archive)
{
	GSList *l;
	gint i;

	for (i = 0; i < archive->n_members; i++)
		g_free(archive->members[i].name);
	for (l = archive->owned; l; l = l->next)
		g_free(l->data);
	g_slist_free(archive->owned);
	g_free(archive->members);
	g_free(archive->unpacked);
	g_free(archive->file);
	g_free(archive);
}
