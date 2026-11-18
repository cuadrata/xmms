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
 * Just enough of the MP4 (ISO base media) file format to play one audio
 * track: its codec configuration, where each sample is and how long it
 * lasts, the gapless trim, and the iTunes-style tags.
 */

#ifndef MP4FILE_H
#define MP4FILE_H

#include <stdio.h>
#include <stdint.h>

typedef enum
{
	MP4_CODEC_NONE,
	MP4_CODEC_AAC,
	MP4_CODEC_ALAC
} Mp4Codec;

typedef struct
{
	char *title, *artist, *album_artist, *album, *date, *genre, *comment;
	int track;
	char *encoder;
} Mp4Tags;

typedef struct
{
	FILE *fp;
	Mp4Codec codec;
	/* AudioSpecificConfig for AAC, the ALAC magic cookie for ALAC. */
	uint8_t *config;
	uint32_t config_len;
	uint32_t channels, rate, bits;	/* from the sample entry */

	uint32_t timescale;		/* of the track's media */
	uint64_t duration;		/* in timescale units */
	/* Gapless: leading samples to drop, and how many to keep (0: all). */
	uint64_t priming, valid;

	uint32_t nsamples, max_size;
	uint32_t *sizes, *durations;
	uint64_t *offsets;
	uint64_t data_bytes;

	Mp4Tags tags;
} Mp4File;

/* NULL unless path is an MP4 file with an AAC or ALAC track. */
Mp4File *mp4_open(const char *path);
void mp4_close(Mp4File *mp4);

/* Read sample i into buf, which holds max_size bytes; its size, or -1. */
int mp4_read_sample(Mp4File *mp4, uint32_t i, uint8_t *buf);

/* The sample playing at time t (timescale units), and when it starts. */
uint32_t mp4_find_sample(const Mp4File *mp4, uint64_t t, uint64_t *start);

/* Playing time in milliseconds, after the gapless trim. */
uint64_t mp4_length_ms(const Mp4File *mp4);

#endif
