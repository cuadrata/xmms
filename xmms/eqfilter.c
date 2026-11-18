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
 * The equalizer, which the output pipeline runs over all audio.
 *
 * Each of the ten sliders is the gain, in dB, at its band's centre, and
 * between centres the gain goes smoothly from one to the next, as it did
 * in the MPEG audio plugin, which had the only equalizer before. Below
 * the first centre the gain is the first band's, above the last the
 * last's.
 *
 * That is a cascade of biquad filters: a low shelf for the first band, a
 * peaking filter at each of the eight between, and a high shelf for the
 * last. The filters overlap, so a filter's own gain is not what the
 * cascade gives at its centre; the gains are solved for, so that it is.
 * A cascade's response in dB is the sum of its filters', so the
 * response at the centres is a sum, and a few steps of Newton's method
 * on that settle it.
 *
 * What all the bands have in common, their mean, is a plain gain, with
 * the preamp's: the filters shape only how they differ. All the same,
 * the response is flat, and otherwise it ripples less between centres.
 *
 * Off, or flat, the audio is passed through untouched.
 */

#include "xmms.h"

#include <math.h>
#include <pthread.h>
#include <string.h>

#define BANDS		10
#define PI		3.14159265358979323846
#define MAX_CHANNELS	8
/* A filter's gain is kept within this, whatever the solving asks. */
#define MAX_GAIN_DB	30.0
/*
 * A peak is this many times as wide as half the space between its
 * neighbours; a shelf turns this far, on a logarithmic scale, from its
 * band towards the next, with this Q. Chosen, with the solving, to come
 * nearest the curve between centres, within about 2 dB for curves one
 * would set.
 */
#define PEAK_WIDTH	2.0
#define SHELF_TURN	0.4
#define SHELF_Q		0.9

static const gdouble centres[BANDS] =
	{ 60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000, 16000 };

/* The settings, as the interface last gave them. */
static pthread_mutex_t settings_lock = PTHREAD_MUTEX_INITIALIZER;
static gboolean set_on;
static gfloat set_preamp, set_bands[BANDS];
static guint set_serial;

/* The pipeline thread's own. */
typedef struct
{
	gdouble b0, b1, b2, a1, a2;
} Biquad;

static guint used_serial = (guint) -1;
static gint used_rate;
static gboolean active;
static gint nfilters;
static Biquad filters[BANDS];
static gdouble preamp_gain;
/* Each filter's last two inputs and outputs, for each channel. */
static gdouble in1[MAX_CHANNELS][BANDS], in2[MAX_CHANNELS][BANDS];
static gdouble out1[MAX_CHANNELS][BANDS], out2[MAX_CHANNELS][BANDS];

void eq_filter_set(gboolean on, gfloat preamp, const gfloat *bands)
{
	pthread_mutex_lock(&settings_lock);
	set_on = on;
	set_preamp = preamp;
	memcpy(set_bands, bands, sizeof set_bands);
	set_serial++;
	pthread_mutex_unlock(&settings_lock);
}

/* RBJ's cookbook filters: kind 0 a low shelf, 1 a peak, 2 a high shelf. */
static Biquad design(gint kind, gdouble f0, gdouble q, gdouble gain_db, gint rate)
{
	gdouble A = pow(10.0, gain_db / 40.0);
	gdouble w0 = 2.0 * PI * f0 / rate;
	gdouble cw = cos(w0), sw = sin(w0);
	gdouble alpha = sw / (2.0 * q);
	gdouble b0, b1, b2, a0, a1, a2;
	Biquad f;

	if (kind == 1)
	{
		b0 = 1 + alpha * A;
		b1 = -2 * cw;
		b2 = 1 - alpha * A;
		a0 = 1 + alpha / A;
		a1 = -2 * cw;
		a2 = 1 - alpha / A;
	}
	else
	{
		gdouble s = kind == 0 ? -1.0 : 1.0;	/* low or high */
		gdouble r = 2.0 * sqrt(A) * alpha;

		b0 = A * ((A + 1) + s * (A - 1) * cw + r);
		b1 = -2 * s * A * ((A - 1) + s * (A + 1) * cw);
		b2 = A * ((A + 1) + s * (A - 1) * cw - r);
		a0 = (A + 1) - s * (A - 1) * cw + r;
		a1 = 2 * s * ((A - 1) - s * (A + 1) * cw);
		a2 = (A + 1) - s * (A - 1) * cw - r;
	}
	f.b0 = b0 / a0;
	f.b1 = b1 / a0;
	f.b2 = b2 / a0;
	f.a1 = a1 / a0;
	f.a2 = a2 / a0;
	return f;
}

/* A filter's gain at a frequency, in dB. */
static gdouble response_db(const Biquad *f, gdouble freq, gint rate)
{
	gdouble w = 2.0 * PI * freq / rate;
	gdouble c1 = cos(w), s1 = sin(w), c2 = cos(2 * w), s2 = sin(2 * w);
	gdouble nr = f->b0 + f->b1 * c1 + f->b2 * c2, ni = -(f->b1 * s1 + f->b2 * s2);
	gdouble dr = 1.0 + f->a1 * c1 + f->a2 * c2, di = -(f->a1 * s1 + f->a2 * s2);

	return 10.0 * log10((nr * nr + ni * ni) / (dr * dr + di * di));
}

/* Solve a.x = b, n by n, by Gaussian elimination; FALSE if singular. */
static gboolean solve(gdouble a[BANDS][BANDS], gdouble *b, gdouble *x, gint n)
{
	gint i, j, k;

	for (i = 0; i < n; i++)
	{
		gint p = i;

		for (k = i + 1; k < n; k++)
			if (fabs(a[k][i]) > fabs(a[p][i]))
				p = k;
		if (fabs(a[p][i]) < 1e-9)
			return FALSE;
		if (p != i)
		{
			gdouble t;

			for (j = 0; j < n; j++)
			{
				t = a[i][j];
				a[i][j] = a[p][j];
				a[p][j] = t;
			}
			t = b[i];
			b[i] = b[p];
			b[p] = t;
		}
		for (k = i + 1; k < n; k++)
		{
			gdouble m = a[k][i] / a[i][i];

			for (j = i; j < n; j++)
				a[k][j] -= m * a[i][j];
			b[k] -= m * b[i];
		}
	}
	for (i = n - 1; i >= 0; i--)
	{
		gdouble s = b[i];

		for (j = i + 1; j < n; j++)
			s -= a[i][j] * x[j];
		x[i] = s / a[i][i];
	}
	return TRUE;
}

/* The filters for these settings at this rate; returns the gain, in dB,
 * that they leave to be applied as it is. */
static gdouble build(const gfloat *bands_in, gint rate)
{
	gdouble q[BANDS], f0[BANDS], gain[BANDS], bands[BANDS], mean = 0.0;
	gint kind[BANDS], n, i, j, step;

	/* The bands the rate leaves room for, all but the very top. */
	for (n = 0; n < BANDS && centres[n] < 0.45 * rate; n++)
		;
	nfilters = n;
	if (n == 0)
		return 0.0;
	for (i = 0; i < n; i++)
		mean += bands_in[i];
	mean /= n;
	for (i = 0; i < n; i++)
		bands[i] = bands_in[i] - mean;

	for (i = 0; i < n; i++)
	{
		/* As wide as the space to its neighbours, in octaves. */
		gdouble lo = i > 0 ? centres[i - 1] : centres[i] / 2.0;
		gdouble hi = i + 1 < BANDS ? centres[i + 1] : centres[i] * 2.0;
		gdouble octaves = log(hi / lo) / log(2.0) / 2.0 * PEAK_WIDTH;
		gdouble p = pow(2.0, octaves);

		q[i] = sqrt(p) / (p - 1.0);
		f0[i] = centres[i];
		kind[i] = i == 0 ? 0 : i == n - 1 ? 2 : 1;
		if (kind[i] == 0)
			f0[i] = centres[0] * pow(centres[1] / centres[0], SHELF_TURN);
		else if (kind[i] == 2)
			f0[i] = centres[i] * pow(centres[i - 1] / centres[i], SHELF_TURN);
		if (kind[i] != 1)
			q[i] = SHELF_Q;
		gain[i] = bands[i];
	}

	/* Newton's method: what the cascade gives at the centres, put right. */
	for (step = 0; step < 12; step++)
	{
		gdouble jac[BANDS][BANDS], err[BANDS], delta[BANDS], worst = 0.0;

		for (i = 0; i < n; i++)
			filters[i] = design(kind[i], f0[i], q[i], gain[i], rate);
		for (i = 0; i < n; i++)
		{
			gdouble total = 0.0;

			for (j = 0; j < n; j++)
				total += response_db(&filters[j], centres[i], rate);
			err[i] = bands[i] - total;
			worst = MAX(worst, fabs(err[i]));
		}
		if (worst < 0.01)
			break;
		/* How each centre moves with each filter's gain. */
		for (j = 0; j < n; j++)
		{
			Biquad nudged = design(kind[j], f0[j], q[j], gain[j] + 0.1, rate);

			for (i = 0; i < n; i++)
				jac[i][j] = (response_db(&nudged, centres[i], rate) -
					     response_db(&filters[j], centres[i], rate)) / 0.1;
		}
		if (!solve(jac, err, delta, n))
			break;
		for (j = 0; j < n; j++)
			gain[j] = CLAMP(gain[j] + delta[j], -MAX_GAIN_DB, MAX_GAIN_DB);
	}
	for (i = 0; i < n; i++)
		filters[i] = design(kind[i], f0[i], q[i], gain[i], rate);
	return mean;
}

/* The filters' memories cleared: a new stream, or a seek. */
void eq_filter_reset(void)
{
	memset(in1, 0, sizeof in1);
	memset(in2, 0, sizeof in2);
	memset(out1, 0, sizeof out1);
	memset(out2, 0, sizeof out2);
}

/* 16-bit native samples, interleaved, equalized in place. */
void eq_filter_apply(gint16 *samples, gint frames, gint nch, gint rate)
{
	gboolean on, flat = TRUE;
	gfloat preamp, bands[BANDS];
	guint serial;
	gint i, ch, b;

	pthread_mutex_lock(&settings_lock);
	serial = set_serial;
	on = set_on;
	preamp = set_preamp;
	memcpy(bands, set_bands, sizeof bands);
	pthread_mutex_unlock(&settings_lock);

	if (serial != used_serial || rate != used_rate)
	{
		for (b = 0; b < BANDS; b++)
			if (fabs(bands[b]) > 0.01)
				flat = FALSE;
		active = on && (!flat || fabs(preamp) > 0.01) && rate > 0;
		if (active)
		{
			if (rate != used_rate)
				eq_filter_reset();
			preamp_gain = pow(10.0, (preamp + build(bands, rate)) / 20.0);
		}
		used_serial = serial;
		used_rate = rate;
	}
	if (!active || nch < 1 || nch > MAX_CHANNELS)
		return;

	for (i = 0; i < frames; i++)
	{
		for (ch = 0; ch < nch; ch++)
		{
			gdouble v = samples[i * nch + ch] * preamp_gain;

			for (b = 0; b < nfilters; b++)
			{
				const Biquad *f = &filters[b];
				gdouble y = f->b0 * v + f->b1 * in1[ch][b] + f->b2 * in2[ch][b]
					- f->a1 * out1[ch][b] - f->a2 * out2[ch][b];

				/* Dying away in silence: zero before it is denormal and slow. */
				if (fabs(y) < 1e-15)
					y = 0.0;
				in2[ch][b] = in1[ch][b];
				in1[ch][b] = v;
				out2[ch][b] = out1[ch][b];
				out1[ch][b] = y;
				v = y;
			}
			samples[i * nch + ch] = (gint16) CLAMP(lrint(v), -32768, 32767);
		}
	}
}
