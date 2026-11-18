/*  XMMS - Cross-platform multimedia player
 *  Copyright (C) 1998-2002  Peter Alm, Mikael Alm, Olle Hallnas,
 *                           Thomas Nilsson and 4Front Technologies
 *  Copyright (C) 1999-2002  Haavard Kvaalen		 
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
#include "xmms.h"

struct rgb_quad
{
	guchar rgbBlue;
	guchar rgbGreen;
	guchar rgbRed;
	guchar rgbReserved;
};

struct bitfieldmask {
	guint32 r, g, b;
};

#define BI_RGB        0L
#define BI_RLE8       1L
#define BI_RLE4       2L
#define BI_BITFIELDS  3L

static GdkGC *bmp_gc = NULL;

/* The file's bytes, read from the start */
typedef struct
{
	const guint8 *data;
	gsize size, pos;
} BmpReader;

static int read_le_short(BmpReader *r, guint16 * ret)
{
	if (r->size - r->pos < 2)
		return 0;
	*ret = r->data[r->pos] | (r->data[r->pos + 1] << 8);
	r->pos += 2;
	return 1;
}

static int read_le_long(BmpReader *r, guint32 * ret)
{
	if (r->size - r->pos < 4)
		return 0;
	*ret = r->data[r->pos] | (r->data[r->pos + 1] << 8) |
		(r->data[r->pos + 2] << 16) | ((guint32) r->data[r->pos + 3] << 24);
	r->pos += 4;
	return 1;
}

static int skip_bytes(BmpReader *r, gsize n)
{
	if (r->size - r->pos < n)
		return 0;
	r->pos += n;
	return 1;
}

static int find_first_set(guint32 v)
{
	int i;
	if (v == 0)
		return -1;
	for (i = 0; !(v & 1); i++)
		v >>= 1;
	return i;
}

static int find_last_set(guint32 v, int s)
{
	int i;
	v >>= s;
	for (i = 0; (v & 1) && i + s < 32; i++)
		v >>= 1;
	return i;
}

static void get_shift(guint32 v, int *r, int *l)
{
	int w;
	*r = find_first_set(v);
	*l = 0;
	w = find_last_set(v, *r);
	/* Limit to 8 bits */
	if (w > 8)
		*r += w - 8;
	else
		*l = 8 - w;
}

static void read_1b_rgb(const guint8 *input, gint input_size, guint8 *output, guint32 w, guint32 h, struct rgb_quad *palette);
static void read_4b_rgb(const guint8 *input, gint input_size, guint8 *output, guint32 w, guint32 h, struct rgb_quad *palette);
static void read_4b_rle(const guint8 *input, guint32 compr_size, guint8 *output, guint32 w, guint32 h, struct rgb_quad *palette);
static void read_8b_rgb(const guint8 *input, gint input_size, guint8 *output, guint32 w, guint32 h, struct rgb_quad *palette);
static void read_8b_rle(const guint8 *input, guint32 compr_size, guint8 *output, guint32 w, guint32 h, struct rgb_quad *palette);
static void read_16b_rgb(const guint8 *input, int input_size, guint8 *output,
			 guint32 w, guint32 h, struct bitfieldmask *mask);
static void read_24b_rgb(const guint8 *input, gint input_size, guint8 *output, guint32 w, guint32 h);
static void read_32b_rgb(const guint8 *input, int input_size, guint8 *output,
			 guint32 w, guint32 h, struct bitfieldmask *mask);


/* The image in a BMP file's SIZE bytes at DATA, or NULL. */
GdkPixmap *read_bmp_data(const guchar *file_data, gsize size)
{
	BmpReader r;
	guint32 offset, headSize, w, h, comp, imgsize;
	guint16 bitcount;
	guint8 *data;
	const guint8 *buffer;

	struct rgb_quad rgb_quads[256];
	struct bitfieldmask mask = {0};
	GdkPixmap *ret = NULL;

	/* A palette shorter than its indices reads as black */
	memset(rgb_quads, 0, sizeof (rgb_quads));

	r.data = file_data;
	r.size = size;
	r.pos = 0;

	if (size < 2 || file_data[0] != 'B' || file_data[1] != 'M')
	{
		g_warning("read_bmp(): Error in BMP file: wrong type");
		return NULL;
	}
	r.pos = 2;
	if (!skip_bytes(&r, 8) ||
	    !read_le_long(&r, &offset) ||
	    !read_le_long(&r, &headSize))
		return NULL;
	if (headSize == 12) /* BITMAPCOREINFO */
	{
		guint16 tmpw, tmph, dummy;
		
		if (!read_le_short(&r, &tmpw) ||
		    !read_le_short(&r, &tmph) ||
		    !read_le_short(&r, &dummy) ||
		    !read_le_short(&r, &bitcount))
			return NULL;
		w = tmpw;
		h = tmph;
		comp = BI_RGB;
	}
	else if (headSize == 40) /* BITMAPINFO */
	{
		guint16 tmp;
		
		if (!read_le_long(&r, &w) ||
		    !read_le_long(&r, &h) ||
		    !read_le_short(&r, &tmp) ||
		    !read_le_short(&r, &bitcount) ||
		    !read_le_long(&r, &comp) ||
		    !read_le_long(&r, &imgsize) ||
		    !skip_bytes(&r, 16))
			return NULL;
	}
	else
	{
		g_warning("read_bmp(): Error in BMP file: Invalid header size: %d",
			  headSize);
		return NULL;
	}
	/*
	 * Skins come from anywhere, so refuse what the decoders cannot take:
	 * an image with no pixels, or too many to size in an int (a top-down
	 * image's negative height reads as one), a palette the header puts
	 * before its own end, or no pixel data before the end of the file.
	 */
	if (w == 0 || h == 0 || w > 32767 || h > 32767 ||
	    (size_t) w * h > G_MAXINT / 3 ||
	    offset < headSize + 14 || offset >= size)
	{
		g_warning("read_bmp(): Error in BMP file: Invalid header");
		return NULL;
	}
	imgsize = size - offset;
	if ((bitcount == 16 || bitcount == 32) && comp == BI_BITFIELDS)
	{
		if (offset - headSize - 14 >= 12)
		{
			if (!read_le_long(&r, &mask.r) ||
			    !read_le_long(&r, &mask.g) ||
			    !read_le_long(&r, &mask.b))
				return NULL;
		}
	}
	else if (bitcount != 24 && bitcount != 16 && bitcount != 32)
	{
		gint ncols, i;

		ncols = (gint) (offset - headSize - 14);
		if (headSize == 12)
		{
			ncols = MIN(ncols / 3, 256);
			for (i = 0; i < ncols; i++)
			{
				if (r.size - r.pos < 3)
					return NULL;
				memcpy(&rgb_quads[i], r.data + r.pos, 3);
				r.pos += 3;
			}
		}
		else
		{
			ncols = MIN(ncols / 4, 256);
			if (r.size - r.pos < (gsize) ncols * 4)
				return NULL;
			memcpy(rgb_quads, r.data + r.pos, ncols * 4);
		}
	}
	buffer = file_data + offset;
	data = g_malloc0((size_t) w * 3 * h + 3);	/* +3 is just for safety */

	if (bitcount == 1)
		read_1b_rgb(buffer, imgsize, data, w, h, rgb_quads);
	else if (bitcount == 4)
	{
		if (comp == BI_RLE4)
			read_4b_rle(buffer, imgsize, data, w, h, rgb_quads);
		else if (comp == BI_RGB)
			read_4b_rgb(buffer, imgsize, data, w, h, rgb_quads);
		else
			g_warning("read_bmp(): Invalid compression (%d)", comp);
	}
	else if (bitcount == 8)
	{
		if (comp == BI_RLE8)
			read_8b_rle(buffer, imgsize, data, w, h, rgb_quads);
		else if (comp == BI_RGB)
			read_8b_rgb(buffer, imgsize, data, w, h, rgb_quads);
		else
			g_warning("read_bmp(): Invalid compression (%d)", comp);
	}
	else if (bitcount == 16)
		read_16b_rgb(buffer, imgsize, data, w, h, &mask);
	else if (bitcount == 24)
		read_24b_rgb(buffer, imgsize, data, w, h);
	else if (bitcount == 32)
		read_32b_rgb(buffer, imgsize, data, w, h, &mask);
	else
		g_warning("read_bmp(): Unsupported bitdepth: %d", bitcount);

	ret = gdk_pixmap_new(mainwin->window, w, h, gdk_rgb_get_visual()->depth);

	if (!bmp_gc)
		bmp_gc = gdk_gc_new(mainwin->window);

	gdk_draw_rgb_image(ret, bmp_gc, 0, 0, w, h, GDK_RGB_DITHER_MAX, data, w * 3);

	g_free(data);
	return ret;
}


static void read_1b_rgb(const guint8 *input, gint input_size, guint8 *output, guint32 w, guint32 h, struct rgb_quad *palette)
{
	const guint8 *input_end = input + input_size;
	guint8 *output_ptr = output + ((h - 1) * w * 3);
	gint padding = (4 - ((w + 7) / 8) % 4) & 3;
	gint x, y, i;
	
	for (y = 0; (guint32) y < h; y++)
	{
		for (x = 0; (guint32) x < w && input < input_end;)
		{
			guint8 byte = *(input++);
			for (i = 0; i < 8 && (guint32) x < w; i++, x++)
			{
				*output_ptr++ = palette[(byte >> (7 - i)) & 1].rgbRed;
				*output_ptr++ = palette[(byte >> (7 - i)) & 1].rgbGreen;
				*output_ptr++ = palette[(byte >> (7 - i)) & 1].rgbBlue;
			}
		}
		input += padding;
		output_ptr -= w * 6;        /* Back up two scanlines */
	}
}

/*
 * Where pixel (X, Y) of a bottom-up image goes; past the end of the
 * output, into its spare bytes, when a run or a delta leaves the image.
 */
static guint8 *rle_pos(guint8 *output, guint32 w, guint32 h, guint32 x, guint32 y)
{
	if (x >= w || y >= h)
		return output + (gsize) w * h * 3;
	return output + ((gsize) (h - y - 1) * w + x) * 3;
}

/* The next byte of RLE data, or the end of the bitmap if there is none */
#define RLE_NEXT(byte) \
	do { \
		if (input_ptr >= input_end) \
			return; \
		(byte) = *input_ptr++; \
	} while (0)

/*
 * An RLE bitmap. A run or a delta may point anywhere, so every byte read
 * is checked against the end of the data, and every pixel written lies
 * in the row it starts in: one that would not fit is dropped.
 */
static void read_rle(const guint8 *input, guint32 compr_size, guint8 *output,
		     guint32 w, guint32 h, struct rgb_quad *palette, gboolean four)
{
	const guint8 *input_ptr = input;
	const guint8 *input_end = input + compr_size;
	guint32 x = 0, y = 0, j;
	guint8 *output_ptr = rle_pos(output, w, h, 0, 0);
	guint8 byte, num, col;

#define RLE_PIXEL(c) \
	do { \
		if (x < w && y < h) \
		{ \
			*output_ptr++ = palette[c].rgbRed; \
			*output_ptr++ = palette[c].rgbGreen; \
			*output_ptr++ = palette[c].rgbBlue; \
		} \
		x++; \
	} while (0)

	for (;;)
	{
		RLE_NEXT(num);
		if (num)
		{
			/* "Encoded mode": NUM pixels of one byte's colours */
			RLE_NEXT(byte);
			for (j = 0; j < num; j++)
			{
				if (four)
					col = (j & 1) ? (byte & 0xF) : (byte >> 4);
				else
					col = byte;
				RLE_PIXEL(col);
			}
			continue;
		}
		RLE_NEXT(byte);
		switch (byte)
		{
			case 0:
				/* End of line */
				x = 0;
				y++;
				output_ptr = rle_pos(output, w, h, x, y);
				break;
			case 1:
				/* End of bitmap */
				return;
			case 2:
				/* Delta */
				RLE_NEXT(byte);
				x += byte;
				RLE_NEXT(byte);
				y += byte;
				output_ptr = rle_pos(output, w, h, x, y);
				break;
			default:
				/* "Absolute mode": NUM pixels as they are */
				num = byte;
				for (j = 0; j < num; j++)
				{
					if (four)
					{
						if ((j & 1) == 0)
							RLE_NEXT(byte);
						col = (j & 1) ? (byte & 0xF) : (byte >> 4);
					}
					else
					{
						RLE_NEXT(byte);
						col = byte;
					}
					RLE_PIXEL(col);
				}
				/* Runs are padded to 16 bits */
				if (four ? ((num & 3) == 1 || (num & 3) == 2) : (num & 1))
					RLE_NEXT(byte);
				break;
		}
	}
#undef RLE_PIXEL
}

static void read_4b_rle(const guint8 *input, guint32 compr_size, guint8 *output, guint32 w, guint32 h, struct rgb_quad *palette)
{
	read_rle(input, compr_size, output, w, h, palette, TRUE);
}

static void read_4b_rgb(const guint8 *input, gint input_size, guint8 *output, guint32 w, guint32 h, struct rgb_quad *palette)
{
	const guint8 *input_end = input + input_size;
	guint8 *output_ptr = output + ((h - 1) * w * 3);
	gint padding = ((((w + 7) / 8) * 8) - w) / 2;
	guint x, y;
	
	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w && input < input_end; x+=2)
		{
			guint8 byte = *(input++);

			*output_ptr++ = palette[byte >> 4].rgbRed;
			*output_ptr++ = palette[byte >> 4].rgbGreen;
			*output_ptr++ = palette[byte >> 4].rgbBlue;

			if (x + 1 == w)
				break;
			
			*output_ptr++ = palette[byte & 0xF].rgbRed;
			*output_ptr++ = palette[byte & 0xF].rgbGreen;
			*output_ptr++ = palette[byte & 0xF].rgbBlue;
		}
		input += padding;
		output_ptr -= w * 6;        /* Back up two scanlines */
	}
}


static void read_8b_rle(const guint8 *input, guint32 compr_size, guint8 *output, guint32 w, guint32 h, struct rgb_quad *palette)
{
	read_rle(input, compr_size, output, w, h, palette, FALSE);
}

static void read_8b_rgb(const guint8 *input, gint input_size, guint8 *output, guint32 w, guint32 h, struct rgb_quad *palette)
{
	const guint8 *input_end = input + input_size;
	guint8 *output_ptr = output + ((h - 1) * w * 3);
	gint padding = (((w + 3) / 4) * 4) - w;
	guint8 byte;
	guint x, y;

	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w && input < input_end; x++)
		{
			byte = *(input++);
			*output_ptr++ = palette[byte].rgbRed;
			*output_ptr++ = palette[byte].rgbGreen;
			*output_ptr++ = palette[byte].rgbBlue;
		}
		output_ptr -= w * 6;
		input += padding;
	}
}


static void read_16b_rgb(const guint8 *input, int input_size, guint8 *output, guint32 w, guint32 h, struct bitfieldmask *mask)
{
	const guint8 *input_end = input + input_size;
	guint8 *output_ptr = output + ((h - 1) * w * 3);
	int padding = (4 - ((w * 2) % 4)) & 3;
	guint16 rgb16;
	guint x, y;
	int rsr, rsl, gsr, gsl, bsr, bsl;

	if (mask->r == 0)
		mask->r = 0x7C00;
	if (mask->g == 0)
		mask->g = 0x03E0;
	if (mask->b == 0)
		mask->b = 0x001F;
	get_shift(mask->r, &rsr, &rsl);
	get_shift(mask->g, &gsr, &gsl);
	get_shift(mask->b, &bsr, &bsl);

	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w && input < (input_end - 1); x++)
		{
			rgb16 = ((*(input + 1)) << 8) | (*input);
			input += 2;
			*output_ptr++ = ((rgb16 & mask->r) >> rsr) << rsl;
			*output_ptr++ = ((rgb16 & mask->g) >> gsr) << gsl;
			*output_ptr++ = ((rgb16 & mask->b) >> bsr) << bsl;
		}
		output_ptr -= w * 6; /* Back up two scanlines */
		input += padding;
	}
}

static void read_24b_rgb(const guint8 *input, gint input_size, guint8 *output, guint32 w, guint32 h)
{
	const guint8 *input_end = input + input_size;
	guint8 *output_ptr = output + ((h - 1) * w * 3);
	gint padding = (4 - ((w * 3) % 4)) & 3;
	guint8 r, g, b;
	guint x, y;
	
	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w && input < (input_end - 2); x++)
		{
			b = *(input++);
			g = *(input++);
			r = *(input++);
			*output_ptr++ = r;
			*output_ptr++ = g;
			*output_ptr++ = b;
		}
		output_ptr -= w * 6;
		input += padding;
	}
}

static void read_32b_rgb(const guint8 *input, int input_size, guint8 *output, guint32 w, guint32 h, struct bitfieldmask *mask)
{
	const guint8 *input_end = input + input_size;
	guint8 *output_ptr = output + ((h - 1) * w * 3);
	guint x, y;
	int rsr, rsl, gsr, gsl, bsr, bsl;

	if (mask->r == 0)
		mask->r = 0x00FF0000;
	if (mask->g == 0)
		mask->g = 0x0000FF00;
	if (mask->b == 0)
		mask->b = 0x000000FF;
	get_shift(mask->r, &rsr, &rsl);
	get_shift(mask->g, &gsr, &gsl);
	get_shift(mask->b, &bsr, &bsl);

	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w && input < (input_end - 3); x++)
		{
			guint32 rgb = (guint32) *(input + 3) << 24 | *(input + 2) << 16 |
				*(input + 1) << 8 | *input;
			input += 4;
			*output_ptr++ = ((rgb & mask->r) >> rsr) << rsl;
			*output_ptr++ = ((rgb & mask->g) >> gsr) << gsl;
			*output_ptr++ = ((rgb & mask->b) >> bsr) << bsl;
		}
		output_ptr -= w * 6;
	}
}

