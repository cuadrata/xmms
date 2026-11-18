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

#include <string.h>
#include <samplerate.h>

#include "plugin.h"
#include "resample.h"

struct Resampler
{
	SRC_STATE *src;
	gint in_rate, out_rate, nch, quality;
	double ratio;
	gboolean active;		/* given audio since it was set up */
	guint64 in_frames, out_frames;	/* since then */
	float *fin, *fout;
	gsize fin_cap, fout_cap;	/* in samples */
	gint16 *res;			/* the result */
	gsize res_cap, res_len;		/* in samples */
};

Resampler *resampler_new(void)
{
	return g_new0(Resampler, 1);
}

void resampler_free(Resampler *r)
{
	if (!r)
		return;
	if (r->src)
		src_delete(r->src);
	g_free(r->fin);
	g_free(r->fout);
	g_free(r->res);
	g_free(r);
}

void resampler_reset(Resampler *r)
{
	if (r->src)
		src_reset(r->src);
	r->active = FALSE;
	r->in_frames = r->out_frames = 0;
}

static void *grow(void *p, gsize *cap, gsize want, gsize size)
{
	if (want > *cap)
	{
		*cap = MAX(want, *cap * 2);
		p = g_realloc(p, *cap * size);
	}
	return p;
}

static void res_add(Resampler *r, const gint16 *s, gsize n)
{
	r->res = grow(r->res, &r->res_cap, r->res_len + n, sizeof (gint16));
	memcpy(r->res + r->res_len, s, n * sizeof (gint16));
	r->res_len += n;
}

/* Convert frames of the current setting, or with last, give out the rest. */
static void convert(Resampler *r, const gint16 *in, glong frames, gboolean last)
{
	SRC_DATA d;
	gint err;

	r->fin = grow(r->fin, &r->fin_cap, MAX(frames, 1) * r->nch, sizeof (float));
	if (frames > 0)
		src_short_to_float_array(in, r->fin, frames * r->nch);
	r->fout = grow(r->fout, &r->fout_cap,
		       ((gsize) (frames * r->ratio) + 256) * r->nch, sizeof (float));
	memset(&d, 0, sizeof (d));
	d.data_in = r->fin;
	d.input_frames = frames;
	d.src_ratio = r->ratio;
	d.end_of_input = last;
	for (;;)
	{
		gsize at;

		d.data_out = r->fout;
		d.output_frames = r->fout_cap / r->nch;
		if ((err = src_process(r->src, &d)) != 0)
		{
			g_warning("output: resampling failed (%s)", src_strerror(err));
			resampler_reset(r);
			return;
		}
		at = r->res_len;
		r->res = grow(r->res, &r->res_cap, at + d.output_frames_gen * r->nch, sizeof (gint16));
		src_float_to_short_array(r->fout, r->res + at, d.output_frames_gen * r->nch);
		r->res_len = at + d.output_frames_gen * r->nch;
		r->in_frames += d.input_frames_used;
		r->out_frames += d.output_frames_gen;
		d.data_in += d.input_frames_used * r->nch;
		d.input_frames -= d.input_frames_used;
		/* All taken; at the end, until nothing more comes out. */
		if (d.input_frames == 0 && (!last || d.output_frames_gen == 0))
			break;
	}
}

static void give_out(Resampler *r)
{
	if (r->active)
		convert(r, NULL, 0, TRUE);
	resampler_reset(r);
}

/* What was given out for one channel count, made the other's: it goes
 * out in one block with what follows. */
static void remix(Resampler *r, gsize from, gint old_nch, gint nch)
{
	gsize frames, i;
	gint16 *s;

	if (old_nch == nch || old_nch < 1 || nch < 1)
		return;
	frames = (r->res_len - from) / old_nch;
	r->res = grow(r->res, &r->res_cap, from + frames * nch, sizeof (gint16));
	s = r->res + from;
	if (old_nch == 1)
		for (i = frames; i-- > 0;)	/* backwards: it grows in place */
			s[2 * i] = s[2 * i + 1] = s[i];
	else
		for (i = 0; i < frames; i++)
			s[i] = (gint16) (((gint32) s[2 * i] + s[2 * i + 1]) / 2);
	r->res_len = from + frames * nch;
}

static gboolean setup(Resampler *r, gint in_rate, gint out_rate, gint nch, gint quality)
{
	gint type = quality == OUTPUT_QUALITY_MEDIUM ? SRC_SINC_MEDIUM_QUALITY
		: SRC_SINC_BEST_QUALITY, err;

	if (r->src && r->nch == nch && r->quality == quality)
		src_reset(r->src);
	else
	{
		if (r->src)
			src_delete(r->src);
		if (!(r->src = src_new(type, nch, &err)))
		{
			g_warning("output: can't resample (%s)", src_strerror(err));
			r->nch = 0;
			return FALSE;
		}
	}
	r->in_rate = in_rate;
	r->out_rate = out_rate;
	r->nch = nch;
	r->quality = quality;
	r->ratio = (double) out_rate / in_rate;
	r->active = FALSE;
	r->in_frames = r->out_frames = 0;
	return TRUE;
}

gint resampler_run(Resampler *r, void **data, gint length, gint in_rate,
		   gint out_rate, gint nch, gint quality)
{
	gboolean same = r->active && r->in_rate == in_rate && r->out_rate == out_rate &&
		r->nch == nch && r->quality == quality;
	glong frames = length / (gint) (2 * nch);

	if (!r->active && in_rate == out_rate)
		return length;		/* nothing held, nothing to do */

	r->res_len = 0;
	if (!same)
	{
		gint old_nch = r->nch;

		give_out(r);
		remix(r, 0, old_nch, nch);
	}
	if (in_rate == out_rate)
		res_add(r, *data, frames * nch);
	else if (r->active || setup(r, in_rate, out_rate, nch, quality))
	{
		convert(r, *data, frames, FALSE);
		r->active = TRUE;
	}
	else
		/* No converter: as it was, at the wrong speed, but heard. */
		res_add(r, *data, frames * nch);
	*data = r->res;
	return r->res_len * sizeof (gint16);
}

gint resampler_drain(Resampler *r, void **data)
{
	if (!r->active)
		return 0;
	r->res_len = 0;
	give_out(r);
	*data = r->res;
	return r->res_len * sizeof (gint16);
}

gboolean resampler_holding(const Resampler *r)
{
	return r->active;
}

gint resampler_channels(const Resampler *r)
{
	return r->nch;
}

gint resampler_held_ms(const Resampler *r)
{
	double held;

	if (!r->active || r->out_rate <= 0)
		return 0;
	held = r->in_frames * r->ratio - r->out_frames;
	return held > 0 ? (gint) (held * 1000 / r->out_rate) : 0;
}
