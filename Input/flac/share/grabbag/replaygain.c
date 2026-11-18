/* grabbag - Convenience lib for various routines common to several tools
 * Copyright (C) 2002-2009  Josh Coalson
 * Copyright (C) 2011-2022  Xiph.Org Foundation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/* Trimmed for XMMS to the functions the FLAC input plugin uses: reading
 * ReplayGain tags and computing the scale factor. The analysis and
 * tag-writing code, which needs FLAC's replaygain_analysis library, is
 * not included.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FLAC/assert.h"
#include "FLAC/metadata.h"
#include "share/grabbag.h"
#include "share/safe_str.h"

#ifdef local_min
#undef local_min
#endif
#define local_min(a,b) ((a)<(b)?(a):(b))

const FLAC__byte * const GRABBAG__REPLAYGAIN_TAG_REFERENCE_LOUDNESS = (const FLAC__byte * const)"REPLAYGAIN_REFERENCE_LOUDNESS";
const FLAC__byte * const GRABBAG__REPLAYGAIN_TAG_TITLE_GAIN = (const FLAC__byte * const)"REPLAYGAIN_TRACK_GAIN";
const FLAC__byte * const GRABBAG__REPLAYGAIN_TAG_TITLE_PEAK = (const FLAC__byte * const)"REPLAYGAIN_TRACK_PEAK";
const FLAC__byte * const GRABBAG__REPLAYGAIN_TAG_ALBUM_GAIN = (const FLAC__byte * const)"REPLAYGAIN_ALBUM_GAIN";
const FLAC__byte * const GRABBAG__REPLAYGAIN_TAG_ALBUM_PEAK = (const FLAC__byte * const)"REPLAYGAIN_ALBUM_PEAK";

static FLAC__bool parse_double_(const FLAC__StreamMetadata_VorbisComment_Entry *entry, double *val)
{
	char s[32], *end;
	const char *p, *q;
	double v;

	FLAC__ASSERT(0 != entry);
	FLAC__ASSERT(0 != val);

	p = (const char *)entry->entry;
	q = strchr(p, '=');
	if(0 == q)
		return false;
	q++;
	safe_strncpy(s, q, local_min(sizeof(s), (size_t) (entry->length - (q-p))));

	v = strtod(s, &end);
	if(end == s)
		return false;

	*val = v;
	return true;
}

FLAC__bool grabbag__replaygain_load_from_vorbiscomment(const FLAC__StreamMetadata *block, FLAC__bool album_mode, FLAC__bool strict, double *reference, double *gain, double *peak)
{
	int reference_offset, gain_offset, peak_offset;
	char *saved_locale;
	FLAC__bool res = true;

	FLAC__ASSERT(0 != block);
	FLAC__ASSERT(0 != reference);
	FLAC__ASSERT(0 != gain);
	FLAC__ASSERT(0 != peak);
	FLAC__ASSERT(block->type == FLAC__METADATA_TYPE_VORBIS_COMMENT);

	/* Default to current level until overridden by a detected tag; this
	 * will always be true until we change replaygain_analysis.c
	 */
	*reference = 89.0; /* ReplayGainReferenceLoudness, dB SPL (FLAC replaygain_analysis.c) */

	/*
	 * We need to save the old locale and switch to "C" because the locale
	 * influences the behaviour of strtod and we want it a certain way.
	 */
	saved_locale = strdup(setlocale(LC_ALL, 0));
	if (0 == saved_locale)
		return false;
	setlocale(LC_ALL, "C");

	if(0 <= (reference_offset = FLAC__metadata_object_vorbiscomment_find_entry_from(block, /*offset=*/0, (const char *)GRABBAG__REPLAYGAIN_TAG_REFERENCE_LOUDNESS)))
		(void)parse_double_(block->data.vorbis_comment.comments + reference_offset, reference);

	if(0 > (gain_offset = FLAC__metadata_object_vorbiscomment_find_entry_from(block, /*offset=*/0, (const char *)(album_mode? GRABBAG__REPLAYGAIN_TAG_ALBUM_GAIN : GRABBAG__REPLAYGAIN_TAG_TITLE_GAIN))))
		res = false;
	if(0 > (peak_offset = FLAC__metadata_object_vorbiscomment_find_entry_from(block, /*offset=*/0, (const char *)(album_mode? GRABBAG__REPLAYGAIN_TAG_ALBUM_PEAK : GRABBAG__REPLAYGAIN_TAG_TITLE_PEAK))))
		res = false;

	if(res && !parse_double_(block->data.vorbis_comment.comments + gain_offset, gain))
		res = false;
	if(res && !parse_double_(block->data.vorbis_comment.comments + peak_offset, peak))
		res = false;

	setlocale(LC_ALL, saved_locale);
	free(saved_locale);

	/* something failed; retry with strict */
	if (!res && !strict)
		res = grabbag__replaygain_load_from_vorbiscomment(block, !album_mode, /*strict=*/true, reference, gain, peak);

	return res;
}

double grabbag__replaygain_compute_scale_factor(double peak, double gain, double preamp, FLAC__bool prevent_clipping)
{
	double scale;
	FLAC__ASSERT(peak >= 0.0);
 	gain += preamp;
	scale = (float) pow(10.0, gain * 0.05);
	if(prevent_clipping && peak > 0.0) {
		const double max_scale = (float)(1.0 / peak);
		if(scale > max_scale)
			scale = max_scale;
	}
	return scale;
}
