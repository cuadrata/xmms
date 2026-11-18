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
 * The output's sample rate converter, on libsamplerate: 16-bit native
 * audio, one or two channels, from a song's rate to the device's.
 *
 * It holds back a few milliseconds of what it is given, as any good
 * converter must, and goes on from one block to the next, and from one
 * song into the next while the rate stays the same: nothing is lost or
 * added at a join. When the rate, channels or quality change, what it
 * holds for the old ones is given out first, so the join is still exact.
 */

#ifndef RESAMPLE_H
#define RESAMPLE_H

#include <glib.h>

typedef struct Resampler Resampler;

Resampler *resampler_new(void);
void resampler_free(Resampler *r);

/* Forget what it holds: a seek or a stop. */
void resampler_reset(Resampler *r);

/*
 * A block of 16-bit native audio at in_rate to out_rate. The result is
 * what was held for other settings, if any, then this block converted,
 * or as it was when the rates are the same. Returns its length in bytes,
 * with *data pointing at it; it stays valid until the next call.
 */
gint resampler_run(Resampler *r, void **data, gint length, gint in_rate,
		   gint out_rate, gint nch, gint quality);

/* What it holds, given out: the stream ends here. 0 if it holds none. */
gint resampler_drain(Resampler *r, void **data);

/* Whether it holds anything, and how long that is at the device's rate. */
gboolean resampler_holding(const Resampler *r);
gint resampler_channels(const Resampler *r);
gint resampler_held_ms(const Resampler *r);

#endif
