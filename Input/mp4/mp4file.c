/*  XMMS - MP4 input plugin
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

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "mp4file.h"

#define FOURCC(a, b, c, d) \
	((uint32_t) (a) << 24 | (uint32_t) (b) << 16 | (uint32_t) (c) << 8 | (uint32_t) (d))

/* What one trak box says, until it is known to be the track we want. */
typedef struct
{
	int sound;
	Mp4Codec codec;
	uint8_t *config;
	uint32_t config_len, channels, rate, bits;
	uint32_t timescale;
	uint64_t duration;
	int64_t edit_start;		/* -1: no edit list */
	uint64_t edit_length;		/* in the movie's timescale */
	uint32_t nsizes, fixed_size, *sizes;
	uint32_t nchunks;
	uint64_t *chunks;
	uint32_t nstsc, *stsc;		/* first chunk, samples per chunk, - */
	uint32_t nstts, *stts;		/* count, delta */
} Track;

static uint32_t be16(const uint8_t *p)
{
	return (uint32_t) p[0] << 8 | p[1];
}

static uint32_t be32(const uint8_t *p)
{
	return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 |
		(uint32_t) p[2] << 8 | p[3];
}

static uint64_t be64(const uint8_t *p)
{
	return (uint64_t) be32(p) << 32 | be32(p + 4);
}

static int rd(FILE *fp, void *buf, size_t n)
{
	return fread(buf, 1, n, fp) == n;
}

static int rd32(FILE *fp, uint32_t *v)
{
	uint8_t b[4];

	if (!rd(fp, b, 4))
		return 0;
	*v = be32(b);
	return 1;
}

static int seek(FILE *fp, uint64_t pos)
{
	return fseeko(fp, (off_t) pos, SEEK_SET) == 0;
}

/*
 * The box at pos, which must end by limit: its type, where its payload
 * starts and where it ends.
 */
static int box_at(FILE *fp, uint64_t pos, uint64_t limit, uint32_t *type,
		  uint64_t *payload, uint64_t *end)
{
	uint8_t h[8];
	uint64_t size;

	if (pos + 8 > limit || !seek(fp, pos) || !rd(fp, h, 8))
		return 0;
	size = be32(h);
	*type = be32(h + 4);
	*payload = pos + 8;
	if (size == 1)
	{
		if (!rd(fp, h, 8))
			return 0;
		size = be64(h);
		*payload = pos + 16;
	}
	else if (size == 0)
		size = limit - pos;
	*end = pos + size;
	return *end >= *payload && *end <= limit;
}

/* A table of count entries of width bytes, if the box can hold them. */
static void *read_table(FILE *fp, uint32_t count, size_t width, uint64_t room)
{
	uint8_t *buf;

	if (count == 0 || count > room / width)
		return NULL;
	if ((buf = malloc((size_t) count * width)) == NULL)
		return NULL;
	if (!rd(fp, buf, (size_t) count * width))
	{
		free(buf);
		return NULL;
	}
	return buf;
}

static uint32_t *read_u32_table(FILE *fp, uint32_t count, uint64_t room)
{
	uint8_t *raw = read_table(fp, count, 4, room);
	uint32_t *t, i;

	if (!raw)
		return NULL;
	t = (uint32_t *) raw;
	for (i = 0; i < count; i++)
		t[i] = be32(raw + i * 4);
	return t;
}

/* MPEG-4 descriptor length: up to four bytes of seven bits each. */
static int desc_len(const uint8_t **p, const uint8_t *end, uint32_t *len)
{
	int i;

	*len = 0;
	for (i = 0; i < 4 && *p < end; i++)
	{
		uint8_t b = *(*p)++;

		*len = (*len << 7) | (b & 0x7f);
		if (!(b & 0x80))
			return 1;
	}
	return 0;
}

/* The AudioSpecificConfig in an esds box. */
static void parse_esds(FILE *fp, uint64_t payload, uint64_t end, Track *t)
{
	uint8_t buf[512];
	const uint8_t *p = buf, *e;
	uint32_t len;
	size_t n = end - payload;

	if (n < 4 || n > sizeof buf || !seek(fp, payload) || !rd(fp, buf, n))
		return;
	e = buf + n;
	p += 4;				/* version and flags */

	if (p >= e || *p++ != 0x03 || !desc_len(&p, e, &len) || p + 3 > e)
		return;
	{
		uint8_t flags = p[2];

		p += 3;
		if (flags & 0x80)
			p += 2;
		if ((flags & 0x40) && p < e)
			p += 1 + *p;
		if (flags & 0x20)
			p += 2;
	}
	if (p >= e || *p++ != 0x04 || !desc_len(&p, e, &len) || p + 13 > e)
		return;
	if (p[0] != 0x40 && p[0] != 0x66 && p[0] != 0x67 && p[0] != 0x68)
		return;			/* not MPEG-4 or MPEG-2 AAC */
	p += 13;
	if (p >= e || *p++ != 0x05 || !desc_len(&p, e, &len) || len == 0 ||
	    p + len > e)
		return;

	free(t->config);
	if ((t->config = malloc(len)) == NULL)
		return;
	memcpy(t->config, p, len);
	t->config_len = len;
	t->codec = MP4_CODEC_AAC;
}

/* The ALAC magic cookie, with or without its version and flags. */
static void parse_alac_cookie(FILE *fp, uint64_t payload, uint64_t end, Track *t)
{
	uint64_t n = end - payload;

	if (n >= 28)
		payload += 4, n -= 4;
	if (n < 24 || !seek(fp, payload))
		return;
	free(t->config);
	if ((t->config = malloc(24)) == NULL || !rd(fp, t->config, 24))
		return;
	t->config_len = 24;
	t->codec = MP4_CODEC_ALAC;
}

/* Codec boxes inside a sample entry, or inside its QuickTime wave box. */
static void parse_entry_children(FILE *fp, uint64_t pos, uint64_t end,
				 uint32_t format, Track *t)
{
	uint32_t type;
	uint64_t payload, next;

	for (; box_at(fp, pos, end, &type, &payload, &next); pos = next)
	{
		if (type == FOURCC('e', 's', 'd', 's') &&
		    format == FOURCC('m', 'p', '4', 'a'))
			parse_esds(fp, payload, next, t);
		else if (type == FOURCC('a', 'l', 'a', 'c') &&
			 format == FOURCC('a', 'l', 'a', 'c'))
			parse_alac_cookie(fp, payload, next, t);
		else if (type == FOURCC('w', 'a', 'v', 'e'))
			parse_entry_children(fp, payload, next, format, t);
	}
}

static void parse_stsd(FILE *fp, uint64_t payload, uint64_t end, Track *t)
{
	uint8_t b[28];
	uint32_t type, version;
	uint64_t epay, eend, children;

	/* version, flags, entry count; then the first entry. */
	if (!box_at(fp, payload + 8, end, &type, &epay, &eend))
		return;
	if (type != FOURCC('m', 'p', '4', 'a') && type != FOURCC('a', 'l', 'a', 'c'))
		return;
	if (!seek(fp, epay) || !rd(fp, b, 28))
		return;
	version = be16(b + 8);
	t->channels = be16(b + 16);
	t->bits = be16(b + 18);
	t->rate = be32(b + 24) >> 16;
	children = epay + 28;
	if (version == 1)
		children += 16;
	else if (version == 2)
		children += 36;
	parse_entry_children(fp, children, eend, type, t);
}

static void parse_stbl(FILE *fp, uint64_t pos, uint64_t end, Track *t)
{
	uint32_t type, count, v;
	uint64_t payload, next, room;

	for (; box_at(fp, pos, end, &type, &payload, &next); pos = next)
	{
		/* All of these begin with version and flags. */
		if (!seek(fp, payload + 4))
			continue;
		room = next - payload;
		switch (type)
		{
		case FOURCC('s', 't', 's', 'd'):
			parse_stsd(fp, payload, next, t);
			break;
		case FOURCC('s', 't', 's', 'z'):
			if (!rd32(fp, &v) || !rd32(fp, &count))
				break;
			t->fixed_size = v;
			t->nsizes = count;
			if (v == 0)
				t->sizes = read_u32_table(fp, count, room);
			break;
		case FOURCC('s', 't', 'c', 'o'):
			if (rd32(fp, &count))
			{
				uint32_t *c32 = read_u32_table(fp, count, room);
				uint32_t i;

				if (c32 && (t->chunks = malloc(count * sizeof *t->chunks)))
				{
					for (i = 0; i < count; i++)
						t->chunks[i] = c32[i];
					t->nchunks = count;
				}
				free(c32);
			}
			break;
		case FOURCC('c', 'o', '6', '4'):
			if (rd32(fp, &count))
			{
				uint8_t *raw = read_table(fp, count, 8, room);
				uint32_t i;

				if (raw && (t->chunks = malloc(count * sizeof *t->chunks)))
				{
					for (i = 0; i < count; i++)
						t->chunks[i] = be64(raw + i * 8);
					t->nchunks = count;
				}
				free(raw);
			}
			break;
		case FOURCC('s', 't', 's', 'c'):
			if (rd32(fp, &count) && count <= UINT32_MAX / 3 &&
			    (t->stsc = read_u32_table(fp, count * 3, room)))
				t->nstsc = count;
			break;
		case FOURCC('s', 't', 't', 's'):
			if (rd32(fp, &count) && count <= UINT32_MAX / 2 &&
			    (t->stts = read_u32_table(fp, count * 2, room)))
				t->nstts = count;
			break;
		}
	}
}

static void parse_mdia(FILE *fp, uint64_t pos, uint64_t end, Track *t)
{
	uint8_t b[32];
	uint32_t type;
	uint64_t payload, next;

	for (; box_at(fp, pos, end, &type, &payload, &next); pos = next)
	{
		if (type == FOURCC('m', 'd', 'h', 'd'))
		{
			if (!seek(fp, payload) || !rd(fp, b, 32))
				continue;
			if (b[0] == 1)
			{
				t->timescale = be32(b + 20);
				t->duration = be64(b + 24);
			}
			else
			{
				t->timescale = be32(b + 12);
				t->duration = be32(b + 16);
			}
		}
		else if (type == FOURCC('h', 'd', 'l', 'r'))
		{
			if (seek(fp, payload) && rd(fp, b, 12))
				t->sound = be32(b + 8) == FOURCC('s', 'o', 'u', 'n');
		}
		else if (type == FOURCC('m', 'i', 'n', 'f'))
		{
			uint32_t t2;
			uint64_t p2, n2, pos2;

			for (pos2 = payload; box_at(fp, pos2, next, &t2, &p2, &n2); pos2 = n2)
				if (t2 == FOURCC('s', 't', 'b', 'l'))
					parse_stbl(fp, p2, n2, t);
		}
	}
}

/* The first edit that shows media: where it starts and how long it is. */
static void parse_edts(FILE *fp, uint64_t pos, uint64_t end, Track *t)
{
	uint8_t b[20];
	uint32_t type, count, i;
	uint64_t payload, next;

	for (; box_at(fp, pos, end, &type, &payload, &next); pos = next)
	{
		int v1;

		if (type != FOURCC('e', 'l', 's', 't') || !seek(fp, payload) ||
		    !rd(fp, b, 8))
			continue;
		v1 = b[0] == 1;
		count = be32(b + 4);
		for (i = 0; i < count && i < 16; i++)
		{
			int64_t start;
			uint64_t length;

			/* duration, media time, rate */
			if (!rd(fp, b, v1 ? 20 : 12))
				return;
			length = v1 ? be64(b) : be32(b);
			start = v1 ? (int64_t) be64(b + 8) : (int32_t) be32(b + 4);
			if (start >= 0)
			{
				t->edit_start = start;
				t->edit_length = length;
				return;
			}
		}
	}
}

static void track_free(Track *t)
{
	free(t->config);
	free(t->sizes);
	free(t->chunks);
	free(t->stsc);
	free(t->stts);
	memset(t, 0, sizeof *t);
}

/* Turn the track's sample tables into one entry per sample. */
static int build_samples(Mp4File *mp4, Track *t)
{
	uint32_t n = t->nsizes, s = 0, i, c, k;

	if (n == 0 || t->nchunks == 0 || t->nstsc == 0 || t->timescale == 0)
		return 0;
	if (!t->sizes)
	{
		if (t->fixed_size == 0 ||
		    (t->sizes = malloc((size_t) n * sizeof *t->sizes)) == NULL)
			return 0;
		for (i = 0; i < n; i++)
			t->sizes[i] = t->fixed_size;
	}
	mp4->offsets = malloc((size_t) n * sizeof *mp4->offsets);
	mp4->durations = malloc((size_t) n * sizeof *mp4->durations);
	if (!mp4->offsets || !mp4->durations)
		return 0;

	for (i = 0; i < t->nstsc && s < n; i++)
	{
		uint32_t first = t->stsc[i * 3], per = t->stsc[i * 3 + 1];
		uint32_t last = i + 1 < t->nstsc ? t->stsc[(i + 1) * 3] - 1 : t->nchunks;

		if (first == 0 || last > t->nchunks)
			last = t->nchunks;
		for (c = first; c >= 1 && c <= last && s < n; c++)
		{
			uint64_t off = t->chunks[c - 1];

			for (k = 0; k < per && s < n; k++)
			{
				mp4->offsets[s] = off;
				off += t->sizes[s];
				s++;
			}
		}
	}
	if (s == 0)
		return 0;
	mp4->nsamples = s;

	for (i = 0, k = 0; i < t->nstts && k < s; i++)
		for (c = 0; c < t->stts[i * 2] && k < s; c++)
			mp4->durations[k++] = t->stts[i * 2 + 1];
	for (; k < s; k++)
		mp4->durations[k] = k ? mp4->durations[k - 1] : 1024;

	for (i = 0; i < s; i++)
	{
		if (t->sizes[i] > mp4->max_size)
			mp4->max_size = t->sizes[i];
		mp4->data_bytes += t->sizes[i];
	}
	mp4->sizes = t->sizes;
	t->sizes = NULL;
	return 1;
}

static char *dup_string(const uint8_t *p, size_t n)
{
	char *s = malloc(n + 1);

	if (s)
	{
		memcpy(s, p, n);
		s[n] = '\0';
	}
	return s;
}

static void set_tag(char **field, const uint8_t *p, size_t n)
{
	if (*field == NULL)
		*field = dup_string(p, n);
}

/* iTunSMPB: " 00000000 00000840 000001CA 00000000000D0E1E ..." */
static void parse_smpb(Mp4File *mp4, const char *s)
{
	unsigned long long v[4];
	int i;

	for (i = 0; i < 4; i++)
	{
		char *end;

		v[i] = strtoull(s, &end, 16);
		if (end == s)
			return;
		s = end;
	}
	mp4->priming = v[1];
	mp4->valid = v[3];
}

/* One ilst item: a tag, or a freeform ---- item such as iTunSMPB. */
static void parse_item(Mp4File *mp4, FILE *fp, uint32_t item, uint64_t pos,
		       uint64_t end)
{
	uint32_t type;
	uint64_t payload, next;
	char *name = NULL;
	Mp4Tags *tags = &mp4->tags;

	for (; box_at(fp, pos, end, &type, &payload, &next); pos = next)
	{
		uint8_t *v;
		size_t n;

		if (next - payload < 4 || next - payload > 65536)
			continue;
		if (type == FOURCC('n', 'a', 'm', 'e'))
		{
			n = next - payload - 4;
			if ((v = malloc(n)) && seek(fp, payload + 4) && rd(fp, v, n))
			{
				free(name);
				name = dup_string(v, n);
			}
			free(v);
			continue;
		}
		if (type != FOURCC('d', 'a', 't', 'a') || next - payload < 8)
			continue;
		n = next - payload - 8;
		if ((v = malloc(n + 1)) == NULL)
			continue;
		if (!seek(fp, payload + 8) || !rd(fp, v, n))
		{
			free(v);
			continue;
		}
		switch (item)
		{
		case FOURCC(0xa9, 'n', 'a', 'm'): set_tag(&tags->title, v, n); break;
		case FOURCC(0xa9, 'A', 'R', 'T'): set_tag(&tags->artist, v, n); break;
		case FOURCC('a', 'A', 'R', 'T'): set_tag(&tags->album_artist, v, n); break;
		case FOURCC(0xa9, 'a', 'l', 'b'): set_tag(&tags->album, v, n); break;
		case FOURCC(0xa9, 'd', 'a', 'y'): set_tag(&tags->date, v, n); break;
		case FOURCC(0xa9, 'g', 'e', 'n'): set_tag(&tags->genre, v, n); break;
		case FOURCC(0xa9, 'c', 'm', 't'): set_tag(&tags->comment, v, n); break;
		case FOURCC(0xa9, 't', 'o', 'o'): set_tag(&tags->encoder, v, n); break;
		case FOURCC('t', 'r', 'k', 'n'):
			if (n >= 4)
				tags->track = be16(v + 2);
			break;
		case FOURCC('-', '-', '-', '-'):
			if (name && !strcmp(name, "iTunSMPB"))
			{
				v[n] = '\0';
				parse_smpb(mp4, (char *) v);
			}
			break;
		}
		free(v);
	}
	free(name);
}

static void parse_udta(Mp4File *mp4, FILE *fp, uint64_t pos, uint64_t end)
{
	uint32_t type, t2, t3;
	uint64_t payload, next, p2, n2, pos2, p3, n3, pos3;
	uint8_t b[8];

	for (; box_at(fp, pos, end, &type, &payload, &next); pos = next)
	{
		if (type != FOURCC('m', 'e', 't', 'a'))
			continue;
		/* An ISO meta box has version and flags; a QuickTime one not. */
		pos2 = payload;
		if (seek(fp, payload) && rd(fp, b, 8) &&
		    be32(b + 4) != FOURCC('h', 'd', 'l', 'r'))
			pos2 += 4;
		for (; box_at(fp, pos2, next, &t2, &p2, &n2); pos2 = n2)
		{
			if (t2 != FOURCC('i', 'l', 's', 't'))
				continue;
			for (pos3 = p2; box_at(fp, pos3, n2, &t3, &p3, &n3); pos3 = n3)
				parse_item(mp4, fp, t3, p3, n3);
		}
	}
}

static void parse_trak(FILE *fp, uint64_t pos, uint64_t end, Track *t)
{
	uint32_t type;
	uint64_t payload, next;

	t->edit_start = -1;
	for (; box_at(fp, pos, end, &type, &payload, &next); pos = next)
	{
		if (type == FOURCC('m', 'd', 'i', 'a'))
			parse_mdia(fp, payload, next, t);
		else if (type == FOURCC('e', 'd', 't', 's'))
			parse_edts(fp, payload, next, t);
	}
}

Mp4File *mp4_open(const char *path)
{
	Mp4File *mp4;
	FILE *fp;
	uint8_t b[24];
	uint32_t type, movie_timescale = 0;
	uint64_t pos, payload, next, file_end;
	int found = 0;

	if ((fp = fopen(path, "rb")) == NULL)
		return NULL;
	if (!rd(fp, b, 8) || be32(b + 4) != FOURCC('f', 't', 'y', 'p') ||
	    fseeko(fp, 0, SEEK_END) != 0)
	{
		fclose(fp);
		return NULL;
	}
	file_end = (uint64_t) ftello(fp);
	if ((mp4 = calloc(1, sizeof *mp4)) == NULL)
	{
		fclose(fp);
		return NULL;
	}
	mp4->fp = fp;

	for (pos = 0; box_at(fp, pos, file_end, &type, &payload, &next); pos = next)
	{
		uint32_t t2;
		uint64_t p2, n2, pos2;

		if (type != FOURCC('m', 'o', 'o', 'v'))
			continue;
		for (pos2 = payload; box_at(fp, pos2, next, &t2, &p2, &n2); pos2 = n2)
		{
			if (t2 == FOURCC('m', 'v', 'h', 'd'))
			{
				/* version and flags, times, timescale */
				if (seek(fp, p2) && rd(fp, b, 24))
					movie_timescale = be32(b + (b[0] == 1 ? 20 : 12));
			}
			else if (t2 == FOURCC('t', 'r', 'a', 'k') && !found)
			{
				Track t;

				memset(&t, 0, sizeof t);
				parse_trak(fp, p2, n2, &t);
				if (t.sound && t.codec != MP4_CODEC_NONE &&
				    build_samples(mp4, &t))
				{
					found = 1;
					mp4->codec = t.codec;
					mp4->config = t.config;
					mp4->config_len = t.config_len;
					t.config = NULL;
					mp4->channels = t.channels;
					mp4->rate = t.rate;
					mp4->bits = t.bits;
					mp4->timescale = t.timescale;
					mp4->duration = t.duration;
					if (t.edit_start > 0)
						mp4->priming = t.edit_start;
					if (t.edit_length && movie_timescale)
						mp4->valid = t.edit_length *
							t.timescale / movie_timescale;
				}
				else
				{
					free(mp4->offsets);
					free(mp4->durations);
					mp4->offsets = NULL;
					mp4->durations = NULL;
				}
				track_free(&t);
			}
			else if (t2 == FOURCC('u', 'd', 't', 'a'))
				parse_udta(mp4, fp, p2, n2);
		}
		break;
	}

	if (!found)
	{
		mp4_close(mp4);
		return NULL;
	}
	return mp4;
}

void mp4_close(Mp4File *mp4)
{
	if (!mp4)
		return;
	if (mp4->fp)
		fclose(mp4->fp);
	free(mp4->config);
	free(mp4->sizes);
	free(mp4->durations);
	free(mp4->offsets);
	free(mp4->tags.title);
	free(mp4->tags.artist);
	free(mp4->tags.album_artist);
	free(mp4->tags.album);
	free(mp4->tags.date);
	free(mp4->tags.genre);
	free(mp4->tags.comment);
	free(mp4->tags.encoder);
	free(mp4);
}

int mp4_read_sample(Mp4File *mp4, uint32_t i, uint8_t *buf)
{
	if (i >= mp4->nsamples || !seek(mp4->fp, mp4->offsets[i]) ||
	    !rd(mp4->fp, buf, mp4->sizes[i]))
		return -1;
	return (int) mp4->sizes[i];
}

uint32_t mp4_find_sample(const Mp4File *mp4, uint64_t t, uint64_t *start)
{
	uint64_t at = 0;
	uint32_t i;

	for (i = 0; i + 1 < mp4->nsamples && at + mp4->durations[i] <= t; i++)
		at += mp4->durations[i];
	*start = at;
	return i;
}

uint64_t mp4_length_ms(const Mp4File *mp4)
{
	uint64_t units = mp4->valid, i;

	if (units == 0)
	{
		for (i = 0; i < mp4->nsamples; i++)
			units += mp4->durations[i];
		units = units > mp4->priming ? units - mp4->priming : 0;
	}
	return units * 1000 / mp4->timescale;
}
