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

/*
 * An ALAC packet is a run of channel elements, as in AAC: single channels
 * and channel pairs, each with its own header, ended by an END element.
 * Each channel is a block of prediction residuals, coded with an adaptive
 * Golomb-Rice code, run through an adaptive linear predictor; a pair is
 * then un-mixed from mid/side-like form. At 24 and 32 bits the low bytes
 * are stored verbatim beside the coded high part.
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "alac.h"

enum { ID_SCE, ID_CPE, ID_CCE, ID_LFE, ID_DSE, ID_PCE, ID_FIL, ID_END };

/* The adaptive Golomb coder's constants. */
#define QBSHIFT		9
#define QB		(1u << QBSHIFT)
#define MMULSHIFT	2
#define MDENSHIFT	(QBSHIFT - MMULSHIFT - 1)
#define MOFF		(1u << (MDENSHIFT - 2))
#define BITOFF		24
#define MAX_PREFIX_16	9
#define MAX_PREFIX_32	9
#define MAX_DATATYPE_BITS_16 16
#define N_MAX_MEAN_CLAMP 0xffffu
#define N_MEAN_CLAMP_VAL 0xffffu

/* Room after a packet, so the bit reader may look past its end. */
#define PADDING		16

struct Alac
{
	uint32_t frame_length, bits, pb, mb, kb, channels, max_run, rate;
	int32_t *predictor, *mix_u, *mix_v;
	uint16_t *shift_buf;
	uint8_t *packet;
	uint32_t packet_size;
};

static uint32_t be32(const uint8_t *p)
{
	return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 |
		(uint32_t) p[2] << 8 | p[3];
}

/* Leading zero bits of a 32-bit value. */
static uint32_t lead(uint32_t x)
{
	uint32_t n = 0;

	if (x == 0)
		return 32;
	while (!(x & 0x80000000u))
	{
		x <<= 1;
		n++;
	}
	return n;
}

static uint32_t lg3a(uint32_t x)
{
	return 31 - lead(x + 3);
}

/* n bits (at most 32) at bit position pos. */
static uint32_t peek(const uint8_t *in, uint32_t pos, uint32_t n)
{
	const uint8_t *p = in + (pos >> 3);
	uint64_t v;
	int i;

	if (n == 0)
		return 0;
	for (v = 0, i = 0; i < 8; i++)
		v = (v << 8) | p[i];
	return (uint32_t) ((v << (pos & 7)) >> (64 - n));
}

/* The low bits of x, sign-extended from bit number bits - 1. */
static int32_t sign_extend(uint32_t x, uint32_t bits)
{
	uint32_t shift = 32 - bits;

	return (int32_t) (x << shift) >> shift;
}

static int32_t shl(int32_t x, uint32_t n)
{
	return (int32_t) ((uint32_t) x << n);
}

static int32_t sign_of(int32_t i)
{
	return (int32_t) ((uint32_t) -(int64_t) i >> 31) | (i >> 31);
}

/*
 * The predictor works in wrapping 32-bit arithmetic, as Apple's does; in
 * unsigned types, so that damaged input cannot overflow a signed one.
 */
static int32_t wadd(int32_t a, int32_t b) { return (int32_t) ((uint32_t) a + (uint32_t) b); }
static int32_t wsub(int32_t a, int32_t b) { return (int32_t) ((uint32_t) a - (uint32_t) b); }
static int32_t wmul(int32_t a, int32_t b) { return (int32_t) ((uint32_t) a * (uint32_t) b); }

/* The run-length code for runs of zeros. */
static uint32_t dyn_get(const uint8_t *in, uint32_t *bitpos, uint32_t m, uint32_t k)
{
	uint32_t pos = *bitpos, pre, v, result;

	pre = lead(~peek(in, pos, 32));
	if (pre >= MAX_PREFIX_16)
	{
		pos += MAX_PREFIX_16;
		result = peek(in, pos, MAX_DATATYPE_BITS_16);
		pos += MAX_DATATYPE_BITS_16;
	}
	else
	{
		pos += pre + 1;
		v = peek(in, pos, k);
		pos += k;
		result = pre * m + v - 1;
		if (v < 2)
		{
			result -= v - 1;
			pos -= 1;
		}
	}
	*bitpos = pos;
	return result;
}

/* The code for residuals: a unary prefix, then k bits; or an escape. */
static uint32_t dyn_get_32bit(const uint8_t *in, uint32_t *bitpos, uint32_t m,
			      uint32_t k, uint32_t maxbits)
{
	uint32_t pos = *bitpos, result, v;

	result = lead(~peek(in, pos, 32));
	if (result >= MAX_PREFIX_32)
	{
		result = peek(in, pos + MAX_PREFIX_32, maxbits);
		pos += MAX_PREFIX_32 + maxbits;
	}
	else
	{
		pos += result + 1;
		if (k != 1)
		{
			v = peek(in, pos, k);
			pos += k - 1;
			result *= m;
			if (v >= 2)
			{
				result += v - 1;
				pos += 1;
			}
		}
	}
	*bitpos = pos;
	return result;
}

/* Decode n residuals into pc. */
static int dyn_decomp(const Alac *a, const uint8_t *in, uint32_t *bitpos,
		      uint32_t maxpos, int32_t *pc, uint32_t n,
		      uint32_t maxbits, uint32_t pb)
{
	uint32_t pos = *bitpos, mb = a->mb, kb = a->kb;
	uint32_t wb = (1u << kb) - 1, zmode = 0, c = 0;

	if (maxbits > 32)
		maxbits = 32;
	while (c < n)
	{
		uint32_t m, k, v, nd;

		if (pos >= maxpos)
			return -1;
		k = lg3a(mb >> QBSHIFT);
		if (k > kb)
			k = kb;
		m = (1u << k) - 1;

		v = dyn_get_32bit(in, &pos, m, k, maxbits);

		/* The low bit is the sign. */
		nd = v + zmode;
		pc[c++] = (int32_t) (((nd + 1) >> 1) * (uint32_t) (-(int32_t) (nd & 1) | 1));

		mb = pb * (v + zmode) + mb - ((pb * mb) >> QBSHIFT);
		if (v > N_MAX_MEAN_CLAMP)
			mb = N_MEAN_CLAMP_VAL;

		zmode = 0;
		if ((mb << MMULSHIFT) < QB && c < n)
		{
			uint32_t j, kz, mz;

			zmode = 1;
			kz = lead(mb) - BITOFF + ((mb + MOFF) >> MDENSHIFT);
			mz = ((1u << kz) - 1) & wb;
			v = dyn_get(in, &pos, mz, kz);
			if (v > n - c)
				return -1;
			for (j = 0; j < v; j++)
				pc[c++] = 0;
			if (v >= 65535)
				zmode = 0;
			mb = 0;
		}
	}
	*bitpos = pos;
	return 0;
}

/* Run the adaptive predictor over the residuals in pc. */
static void unpc_block(const int32_t *pc, int32_t *out, uint32_t num,
		       int16_t *coefs, uint32_t numactive, uint32_t chanbits,
		       uint32_t denshift)
{
	int32_t denhalf = denshift ? 1 << (denshift - 1) : 0;
	uint32_t j, lim;
	int k;

	out[0] = pc[0];
	if (numactive == 0)
	{
		if (num > 1 && pc != out)
			memcpy(&out[1], &pc[1], (num - 1) * sizeof *out);
		return;
	}
	if (numactive == 31)
	{
		int32_t prev = out[0];

		for (j = 1; j < num; j++)
		{
			prev = sign_extend((uint32_t) pc[j] + (uint32_t) prev, chanbits);
			out[j] = prev;
		}
		return;
	}

	for (j = 1; j <= numactive && j < num; j++)
		out[j] = sign_extend((uint32_t) pc[j] + (uint32_t) out[j - 1], chanbits);

	lim = numactive + 1;
	for (j = lim; j < num; j++)
	{
		const int32_t *pout = out + j - 1;
		int32_t top = out[j - lim], del, del0, sg, sum1 = 0;

		for (k = 0; k < (int) numactive; k++)
			sum1 = wadd(sum1, wmul(coefs[k], wsub(pout[-k], top)));

		del = del0 = pc[j];
		sg = sign_of(del);
		del = wadd(wadd(del, top), wadd(sum1, denhalf) >> denshift);
		out[j] = sign_extend((uint32_t) del, chanbits);

		/* Adapt the coefficients towards the error. */
		if (sg > 0)
		{
			for (k = numactive - 1; k >= 0; k--)
			{
				int32_t dd = wsub(top, pout[-k]), sgn = sign_of(dd);

				coefs[k] -= sgn;
				del0 = wsub(del0, wmul(numactive - k, wmul(sgn, dd) >> denshift));
				if (del0 <= 0)
					break;
			}
		}
		else if (sg < 0)
		{
			for (k = numactive - 1; k >= 0; k--)
			{
				int32_t dd = wsub(top, pout[-k]), sgn = sign_of(dd);

				coefs[k] += sgn;
				del0 = wsub(del0, wmul(numactive - k, wmul(-sgn, dd) >> denshift));
				if (del0 >= 0)
					break;
			}
		}
	}
}

/* One channel's predictor parameters. */
typedef struct
{
	uint32_t mode, denshift, pbfactor, num;
	int16_t coefs[32];
} Predictor;

static void read_predictor(const uint8_t *in, uint32_t *pos, Predictor *p)
{
	uint32_t i, b;

	b = peek(in, *pos, 8);
	p->mode = b >> 4;
	p->denshift = b & 0xf;
	b = peek(in, *pos + 8, 8);
	p->pbfactor = b >> 5;
	p->num = b & 0x1f;
	*pos += 16;
	for (i = 0; i < p->num; i++, *pos += 16)
		p->coefs[i] = (int16_t) peek(in, *pos, 16);
}

/* Residuals, then the predictor, for one channel. */
static int decode_channel(Alac *a, const uint8_t *in, uint32_t *pos,
			  uint32_t maxpos, Predictor *p, int32_t *out,
			  uint32_t n, uint32_t chanbits)
{
	if (dyn_decomp(a, in, pos, maxpos, a->predictor, n, chanbits,
		       (a->pb * p->pbfactor) / 4) < 0)
		return -1;
	if (p->mode != 0)
		unpc_block(a->predictor, a->predictor, n, NULL, 31, chanbits, 0);
	unpc_block(a->predictor, out, n, p->coefs, p->num, chanbits, p->denshift);
	return 0;
}

/* Uncompressed samples of bits bits. */
static int32_t read_verbatim(const uint8_t *in, uint32_t *pos, uint32_t bits)
{
	int32_t v = sign_extend(peek(in, *pos, bits), bits);

	*pos += bits;
	return v;
}

/* A single channel element (or LFE) into channel ch of out. */
static int decode_sce(Alac *a, const uint8_t *in, uint32_t *pos, uint32_t maxpos,
		      int32_t *out, uint32_t ch, uint32_t *nframes)
{
	uint32_t header, partial, shifted, escape, chanbits, n = a->frame_length;
	uint32_t i, shift_pos = 0;
	Predictor p;

	*pos += 4;			/* element instance tag */
	if (peek(in, *pos, 12) != 0)
		return -1;
	*pos += 12;
	header = peek(in, *pos, 4);
	*pos += 4;
	partial = header >> 3;
	shifted = (header >> 1) & 3;
	escape = header & 1;
	if (shifted == 3)
		return -1;
	chanbits = a->bits - shifted * 8;
	if (partial)
	{
		n = peek(in, *pos, 32);
		*pos += 32;
		if (n > a->frame_length)
			return -1;
	}

	if (!escape)
	{
		*pos += 16;		/* mix bits and residue: none for one channel */
		read_predictor(in, pos, &p);
		if (shifted)
		{
			shift_pos = *pos;
			*pos += shifted * 8 * n;
		}
		if (decode_channel(a, in, pos, maxpos, &p, a->mix_u, n, chanbits) < 0)
			return -1;
		for (i = 0; i < n && shifted; i++)
			a->shift_buf[i] = (uint16_t) peek(in, shift_pos + i * shifted * 8,
							  shifted * 8);
	}
	else
	{
		chanbits = a->bits;
		shifted = 0;
		for (i = 0; i < n; i++)
			a->mix_u[i] = read_verbatim(in, pos, chanbits);
	}

	for (i = 0; i < n; i++)
	{
		int32_t v = a->mix_u[i];

		if (shifted)
			v = shl(v, shifted * 8) | a->shift_buf[i];
		out[i * a->channels + ch] = v;
	}
	*nframes = n;
	return 0;
}

/* A channel pair element into channels ch and ch + 1 of out. */
static int decode_cpe(Alac *a, const uint8_t *in, uint32_t *pos, uint32_t maxpos,
		      int32_t *out, uint32_t ch, uint32_t *nframes)
{
	uint32_t header, partial, shifted, escape, chanbits, n = a->frame_length;
	uint32_t i, shift_pos = 0, mixbits = 0;
	int32_t mixres = 0;
	Predictor pu, pv;

	if (ch + 2 > a->channels)
		return -1;
	*pos += 4;
	if (peek(in, *pos, 12) != 0)
		return -1;
	*pos += 12;
	header = peek(in, *pos, 4);
	*pos += 4;
	partial = header >> 3;
	shifted = (header >> 1) & 3;
	escape = header & 1;
	if (shifted == 3)
		return -1;
	chanbits = a->bits - shifted * 8 + 1;
	if (partial)
	{
		n = peek(in, *pos, 32);
		*pos += 32;
		if (n > a->frame_length)
			return -1;
	}

	if (!escape)
	{
		mixbits = peek(in, *pos, 8);
		mixres = (int8_t) peek(in, *pos + 8, 8);
		*pos += 16;
		if (mixbits > 31)
			return -1;
		read_predictor(in, pos, &pu);
		read_predictor(in, pos, &pv);
		if (shifted)
		{
			shift_pos = *pos;
			*pos += shifted * 8 * 2 * n;
		}
		if (decode_channel(a, in, pos, maxpos, &pu, a->mix_u, n, chanbits) < 0 ||
		    decode_channel(a, in, pos, maxpos, &pv, a->mix_v, n, chanbits) < 0)
			return -1;
		for (i = 0; i < 2 * n && shifted; i++)
			a->shift_buf[i] = (uint16_t) peek(in, shift_pos + i * shifted * 8,
							  shifted * 8);
	}
	else
	{
		chanbits = a->bits;
		shifted = 0;
		for (i = 0; i < n; i++)
		{
			a->mix_u[i] = read_verbatim(in, pos, chanbits);
			a->mix_v[i] = read_verbatim(in, pos, chanbits);
		}
	}

	for (i = 0; i < n; i++)
	{
		int32_t u = a->mix_u[i], v = a->mix_v[i], l, r;

		if (mixres != 0)
		{
			l = wsub(wadd(u, v), (int32_t) (((int64_t) mixres * v) >> mixbits));
			r = wsub(l, v);
		}
		else
		{
			l = u;
			r = v;
		}
		if (shifted)
		{
			l = shl(l, shifted * 8) | a->shift_buf[2 * i];
			r = shl(r, shifted * 8) | a->shift_buf[2 * i + 1];
		}
		out[i * a->channels + ch] = l;
		out[i * a->channels + ch + 1] = r;
	}
	*nframes = n;
	return 0;
}

Alac *alac_new(const uint8_t *cookie, uint32_t len)
{
	Alac *a;

	if (len < 24 || cookie[4] != 0)
		return NULL;
	if ((a = calloc(1, sizeof *a)) == NULL)
		return NULL;
	a->frame_length = be32(cookie);
	a->bits = cookie[5];
	a->pb = cookie[6];
	a->mb = cookie[7];
	a->kb = cookie[8];
	a->channels = cookie[9];
	a->max_run = (uint32_t) cookie[10] << 8 | cookie[11];
	a->rate = be32(cookie + 20);

	if (a->frame_length == 0 || a->frame_length > 65536 ||
	    a->channels == 0 || a->channels > 8 || a->kb > 31 ||
	    (a->bits != 16 && a->bits != 20 && a->bits != 24 && a->bits != 32))
	{
		free(a);
		return NULL;
	}
	a->predictor = malloc(a->frame_length * sizeof *a->predictor);
	a->mix_u = malloc(a->frame_length * sizeof *a->mix_u);
	a->mix_v = malloc(a->frame_length * sizeof *a->mix_v);
	a->shift_buf = malloc(a->frame_length * 2 * sizeof *a->shift_buf);
	if (!a->predictor || !a->mix_u || !a->mix_v || !a->shift_buf)
	{
		alac_free(a);
		return NULL;
	}
	return a;
}

void alac_free(Alac *a)
{
	if (!a)
		return;
	free(a->predictor);
	free(a->mix_u);
	free(a->mix_v);
	free(a->shift_buf);
	free(a->packet);
	free(a);
}

uint32_t alac_frame_length(const Alac *a) { return a->frame_length; }
uint32_t alac_channels(const Alac *a) { return a->channels; }
uint32_t alac_bits(const Alac *a) { return a->bits; }
uint32_t alac_rate(const Alac *a) { return a->rate; }

/* Skip a data stream element. */
static int skip_dse(const uint8_t *in, uint32_t *pos)
{
	uint32_t align, count;

	*pos += 4;			/* element instance tag */
	align = peek(in, *pos, 1);
	count = peek(in, *pos + 1, 8);
	*pos += 9;
	if (count == 255)
	{
		count += peek(in, *pos, 8);
		*pos += 8;
	}
	if (align)
		*pos = (*pos + 7) & ~7u;
	*pos += count * 8;
	return 0;
}

/* Skip a fill element. */
static int skip_fil(const uint8_t *in, uint32_t *pos)
{
	uint32_t count = peek(in, *pos, 4);

	*pos += 4;
	if (count == 15)
	{
		count += peek(in, *pos, 8) - 1;
		*pos += 8;
	}
	*pos += count * 8;
	return 0;
}

int alac_decode(Alac *a, const uint8_t *packet, uint32_t size, int32_t *out)
{
	uint32_t pos = 0, maxpos = size * 8, ch = 0, nframes = 0;

	if (size + PADDING > a->packet_size)
	{
		uint8_t *p = realloc(a->packet, size + PADDING);

		if (!p)
			return -1;
		a->packet = p;
		a->packet_size = size + PADDING;
	}
	memcpy(a->packet, packet, size);
	memset(a->packet + size, 0, PADDING);

	while (pos + 3 <= maxpos)
	{
		uint32_t tag = peek(a->packet, pos, 3);
		int err = 0;

		pos += 3;
		switch (tag)
		{
		case ID_SCE:
		case ID_LFE:
			if (ch >= a->channels)
				return -1;
			err = decode_sce(a, a->packet, &pos, maxpos, out, ch, &nframes);
			ch += 1;
			break;
		case ID_CPE:
			err = decode_cpe(a, a->packet, &pos, maxpos, out, ch, &nframes);
			ch += 2;
			break;
		case ID_DSE:
			err = skip_dse(a->packet, &pos);
			break;
		case ID_FIL:
			err = skip_fil(a->packet, &pos);
			break;
		case ID_END:
			return ch ? (int) nframes : -1;
		default:
			return -1;
		}
		if (err < 0 || pos > maxpos)
			return -1;
	}
	return ch ? (int) nframes : -1;
}
