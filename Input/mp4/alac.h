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

/* Apple Lossless (ALAC) decoding. */

#ifndef ALAC_H
#define ALAC_H

#include <stdint.h>

typedef struct Alac Alac;

/* A decoder for the stream the 24-byte magic cookie describes. */
Alac *alac_new(const uint8_t *cookie, uint32_t len);
void alac_free(Alac *alac);

uint32_t alac_frame_length(const Alac *alac);
uint32_t alac_channels(const Alac *alac);
uint32_t alac_bits(const Alac *alac);
uint32_t alac_rate(const Alac *alac);

/*
 * Decode one packet into out, interleaved, one int32_t per sample at the
 * stream's bit depth; out holds frame_length * channels samples. Returns
 * the number of frames, or -1 if the packet is damaged.
 */
int alac_decode(Alac *alac, const uint8_t *packet, uint32_t size, int32_t *out);

#endif
