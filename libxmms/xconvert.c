/*
 *  Copyright (C) 2001-2003  Haavard Kvaalen <havardk@xmms.org>
 *
 *  Licensed under GNU LGPL version 2.
 */

#include "config.h"
#include <stdlib.h>
#include <xmms/plugin.h>
#include "xconvert.h"

#ifdef WORDS_BIGENDIAN
# define IS_BIG_ENDIAN TRUE
#else
# define IS_BIG_ENDIAN FALSE
#endif

struct buffer {
	void *buffer;
	int size;
};

struct xmms_convert_buffers {
	struct buffer format_buffer, stereo_buffer;
};

struct xmms_convert_buffers* xmms_convert_buffers_new(void)
{
	return g_malloc0(sizeof(struct xmms_convert_buffers));
}
	
static void* convert_get_buffer(struct buffer *buffer, size_t size)
{
	if (size > 0 && size <= (size_t) buffer->size)
		return buffer->buffer;

	buffer->size = size;
	buffer->buffer = g_realloc(buffer->buffer, size);
	return buffer->buffer;
}

void xmms_convert_buffers_free(struct xmms_convert_buffers* buf)
{
	convert_get_buffer(&buf->format_buffer, 0);
	convert_get_buffer(&buf->stereo_buffer, 0);
}

void xmms_convert_buffers_destroy(struct xmms_convert_buffers* buf)
{
	if (!buf)
		return;
	xmms_convert_buffers_free(buf);
	g_free(buf);
}

static int convert_swap_endian(struct xmms_convert_buffers* buf, void **data, int length)
{
	guint16 *ptr = *data;
	int i;
	for (i = 0; i < length; i += 2, ptr++)
		*ptr = GUINT16_SWAP_LE_BE(*ptr);

	return i;
}

static int convert_swap_sign_and_endian_to_native(struct xmms_convert_buffers* buf, void **data, int length)
{
	guint16 *ptr = *data;
	int i;
	for (i = 0; i < length; i += 2, ptr++)
		*ptr = GUINT16_SWAP_LE_BE(*ptr) ^ 1 << 15;

	return i;
}

static int convert_swap_sign_and_endian_to_alien(struct xmms_convert_buffers* buf, void **data, int length)
{
	guint16 *ptr = *data;
	int i;
	for (i = 0; i < length; i += 2, ptr++)
		*ptr = GUINT16_SWAP_LE_BE(*ptr ^ 1 << 15);

	return i;
}

static int convert_swap_sign16(struct xmms_convert_buffers* buf, void **data, int length)
{
	gint16 *ptr = *data;
	int i;
	for (i = 0; i < length; i += 2, ptr++)
		*ptr ^= 1 << 15;

	return i;
}

static int convert_swap_sign8(struct xmms_convert_buffers* buf, void **data, int length)
{
	gint8 *ptr = *data;
	int i;
	for (i = 0; i < length; i++)
		*ptr++ ^= 1 << 7;

	return i;
}

static int convert_to_8_native_endian(struct xmms_convert_buffers* buf, void **data, int length)
{
	gint8 *output = *data;
	gint16 *input = *data;
	int i;
	for (i = 0; i < length / 2; i++)
		*output++ = *input++ >> 8;

	return i;
}

static int convert_to_8_native_endian_swap_sign(struct xmms_convert_buffers* buf, void **data, int length)
{
	gint8 *output = *data;
	gint16 *input = *data;
	int i;
	for (i = 0; i < length / 2; i++)
		*output++ = (*input++ >> 8) ^ (1 << 7);

	return i;
}


static int convert_to_8_alien_endian(struct xmms_convert_buffers* buf, void **data, int length)
{
	gint8 *output = *data;
	gint16 *input = *data;
	int i;
	for (i = 0; i < length / 2; i++)
		*output++ = *input++ & 0xff;

	return i;
}

static int convert_to_8_alien_endian_swap_sign(struct xmms_convert_buffers* buf, void **data, int length)
{
	gint8 *output = *data;
	gint16 *input = *data;
	int i;
	for (i = 0; i < length / 2; i++)
		*output++ = (*input++ & 0xff) ^ (1 << 7);

	return i;
}

static int convert_to_16_native_endian(struct xmms_convert_buffers* buf, void **data, int length)
{
	guint8 *input = *data;
	guint16 *output;
	int i;
	*data = convert_get_buffer(&buf->format_buffer, length * 2);
	output = *data;
	for (i = 0; i < length; i++)
		*output++ = *input++ << 8;

	return i * 2;
}

static int convert_to_16_native_endian_swap_sign(struct xmms_convert_buffers* buf, void **data, int length)
{
	guint8 *input = *data;
	guint16 *output;
	int i;
	*data = convert_get_buffer(&buf->format_buffer, length * 2);
	output = *data;
	for (i = 0; i < length; i++)
		*output++ = (*input++ << 8) ^ (1 << 15);

	return i * 2;
}


static int convert_to_16_alien_endian(struct xmms_convert_buffers* buf, void **data, int length)
{
	guint8 *input = *data;
	guint16 *output;
	int i;
	*data = convert_get_buffer(&buf->format_buffer, length * 2);
	output = *data;
	for (i = 0; i < length; i++)
		*output++ = *input++;

	return i * 2;
}

static int convert_to_16_alien_endian_swap_sign(struct xmms_convert_buffers* buf, void **data, int length)
{
	guint8 *input = *data;
	guint16 *output;
	int i;
	*data = convert_get_buffer(&buf->format_buffer, length * 2);
	output = *data;
	for (i = 0; i < length; i++)
		*output++ = *input++ ^ (1 << 7);

	return i * 2;
}

static AFormat unnativize(AFormat fmt)
{
	if (fmt == FMT_S16_NE)
	{
		if (IS_BIG_ENDIAN)
			return FMT_S16_BE;
		else
			return FMT_S16_LE;
	}
	if (fmt == FMT_U16_NE)
	{
		if (IS_BIG_ENDIAN)
			return FMT_U16_BE;
		else
			return FMT_U16_LE;
	}
	return fmt;
}

convert_func_t xmms_convert_get_func(AFormat output, AFormat input)
{
	output = unnativize(output);
	input = unnativize(input);

	if (output == input)
		return NULL;

	if ((output == FMT_U16_BE && input == FMT_U16_LE) ||
	    (output == FMT_U16_LE && input == FMT_U16_BE) ||
	    (output == FMT_S16_BE && input == FMT_S16_LE) ||
	    (output == FMT_S16_LE && input == FMT_S16_BE))
		return convert_swap_endian;

	if ((output == FMT_U16_BE && input == FMT_S16_BE) ||
	    (output == FMT_U16_LE && input == FMT_S16_LE) ||
	    (output == FMT_S16_BE && input == FMT_U16_BE) ||
	    (output == FMT_S16_LE && input == FMT_U16_LE))
		return convert_swap_sign16;

	if ((IS_BIG_ENDIAN &&
	     ((output == FMT_U16_BE && input == FMT_S16_LE) ||
	      (output == FMT_S16_BE && input == FMT_U16_LE))) ||
	    (!IS_BIG_ENDIAN &&
	     ((output == FMT_U16_LE && input == FMT_S16_BE) ||
	      (output == FMT_S16_LE && input == FMT_U16_BE))))
		return convert_swap_sign_and_endian_to_native;
		
	if ((!IS_BIG_ENDIAN &&
	     ((output == FMT_U16_BE && input == FMT_S16_LE) ||
	      (output == FMT_S16_BE && input == FMT_U16_LE))) ||
	    (IS_BIG_ENDIAN &&
	     ((output == FMT_U16_LE && input == FMT_S16_BE) ||
	      (output == FMT_S16_LE && input == FMT_U16_BE))))
		return convert_swap_sign_and_endian_to_alien;

	if ((IS_BIG_ENDIAN &&
	     ((output == FMT_U8 && input == FMT_U16_BE) ||
	      (output == FMT_S8 && input == FMT_S16_BE))) ||
	    (!IS_BIG_ENDIAN &&
	     ((output == FMT_U8 && input == FMT_U16_LE) ||
	      (output == FMT_S8 && input == FMT_S16_LE))))
		return convert_to_8_native_endian;

	if ((IS_BIG_ENDIAN &&
	     ((output == FMT_U8 && input == FMT_S16_BE) ||
	      (output == FMT_S8 && input == FMT_U16_BE))) ||
	    (!IS_BIG_ENDIAN &&
	     ((output == FMT_U8 && input == FMT_S16_LE) ||
	      (output == FMT_S8 && input == FMT_U16_LE))))
		return convert_to_8_native_endian_swap_sign;

	if ((!IS_BIG_ENDIAN &&
	     ((output == FMT_U8 && input == FMT_U16_BE) ||
	      (output == FMT_S8 && input == FMT_S16_BE))) ||
	    (IS_BIG_ENDIAN &&
	     ((output == FMT_U8 && input == FMT_U16_LE) ||
	      (output == FMT_S8 && input == FMT_S16_LE))))
		return convert_to_8_alien_endian;

	if ((!IS_BIG_ENDIAN &&
	     ((output == FMT_U8 && input == FMT_S16_BE) ||
	      (output == FMT_S8 && input == FMT_U16_BE))) ||
	    (IS_BIG_ENDIAN &&
	     ((output == FMT_U8 && input == FMT_S16_LE) ||
	      (output == FMT_S8 && input == FMT_U16_LE))))
		return convert_to_8_alien_endian_swap_sign;

	if ((output == FMT_U8 && input == FMT_S8) ||
	    (output == FMT_S8 && input == FMT_U8))
		return convert_swap_sign8;

	if ((IS_BIG_ENDIAN &&
	     ((output == FMT_U16_BE && input == FMT_U8) ||
	      (output == FMT_S16_BE && input == FMT_S8))) ||
	    (!IS_BIG_ENDIAN &&
	     ((output == FMT_U16_LE && input == FMT_U8) ||
	      (output == FMT_S16_LE && input == FMT_S8))))
		return convert_to_16_native_endian;

	if ((IS_BIG_ENDIAN &&
	     ((output == FMT_U16_BE && input == FMT_S8) ||
	      (output == FMT_S16_BE && input == FMT_U8))) ||
	    (!IS_BIG_ENDIAN &&
	     ((output == FMT_U16_LE && input == FMT_S8) ||
	      (output == FMT_S16_LE && input == FMT_U8))))
		return convert_to_16_native_endian_swap_sign;

	if ((!IS_BIG_ENDIAN &&
	     ((output == FMT_U16_BE && input == FMT_U8) ||
	      (output == FMT_S16_BE && input == FMT_S8))) ||
	    (IS_BIG_ENDIAN &&
	     ((output == FMT_U16_LE && input == FMT_U8) ||
	      (output == FMT_S16_LE && input == FMT_S8))))
		return convert_to_16_alien_endian;

	if ((!IS_BIG_ENDIAN &&
	     ((output == FMT_U16_BE && input == FMT_S8) ||
	      (output == FMT_S16_BE && input == FMT_U8))) ||
	    (IS_BIG_ENDIAN &&
	     ((output == FMT_U16_LE && input == FMT_S8) ||
	      (output == FMT_S16_LE && input == FMT_U8))))
		return convert_to_16_alien_endian_swap_sign;

	g_warning("Translation needed, but not available.\n"
		  "Input: %d; Output %d.", input, output);
	return NULL;
}

static int convert_mono_to_stereo(struct xmms_convert_buffers* buf, void **data, int length, int b16)
{
	int i;
	void *outbuf = convert_get_buffer(&buf->stereo_buffer, length * 2);

	if (b16)
	{
		guint16 *output = outbuf, *input = *data;
		for (i = 0; i < length / 2; i++)
		{
			*output++ = *input;
			*output++ = *input;
			input++;
		}
	}
	else 
	{
		guint8 *output = outbuf, *input = *data;
		for (i = 0; i < length; i++)
		{
			*output++ = *input;
			*output++ = *input;
			input++;
		}
	}
	*data = outbuf;

	return length * 2;
}

static int convert_mono_to_stereo_8(struct xmms_convert_buffers* buf, void **data, int length)
{
	return convert_mono_to_stereo(buf, data, length, FALSE);
}

static int convert_mono_to_stereo_16(struct xmms_convert_buffers* buf, void **data, int length)
{
	return convert_mono_to_stereo(buf, data, length, TRUE);
}

static int convert_stereo_to_mono_u8(struct xmms_convert_buffers* buf, void **data, int length)
{
	guint8 *output = *data, *input = *data;
	int i;
	for (i = 0; i < length / 2; i++)
	{
		guint16 tmp;
		tmp = *input++;
		tmp += *input++;
		*output++ = tmp / 2;
	}
	return length / 2;
}
static int convert_stereo_to_mono_s8(struct xmms_convert_buffers* buf, void **data, int length)
{
	gint8 *output = *data, *input = *data;
	int i;
	for (i = 0; i < length / 2; i++)
	{
		gint16 tmp;
		tmp = *input++;
		tmp += *input++;
		*output++ = tmp / 2;
	}
	return length / 2;
}
static int convert_stereo_to_mono_u16le(struct xmms_convert_buffers* buf, void **data, int length)
{
	guint16 *output = *data, *input = *data;
	int i;
	for (i = 0; i < length / 4; i++)
	{
		guint32 tmp;
		guint16 stmp;
		tmp = GUINT16_FROM_LE(*input);
		input++;
		tmp += GUINT16_FROM_LE(*input);
		input++;
		stmp = tmp / 2;
		*output++ = GUINT16_TO_LE(stmp);
	}
	return length / 2;
}

static int convert_stereo_to_mono_u16be(struct xmms_convert_buffers* buf, void **data, int length)
{
	guint16 *output = *data, *input = *data;
	int i;
	for (i = 0; i < length / 4; i++)
	{
		guint32 tmp;
		guint16 stmp;
		tmp = GUINT16_FROM_BE(*input);
		input++;
		tmp += GUINT16_FROM_BE(*input);
		input++;
		stmp = tmp / 2;
		*output++ = GUINT16_TO_BE(stmp);
	}
	return length / 2;
}

static int convert_stereo_to_mono_s16le(struct xmms_convert_buffers* buf, void **data, int length)
{
	gint16 *output = *data, *input = *data;
	int i;
	for (i = 0; i < length / 4; i++)
	{
		gint32 tmp;
		gint16 stmp;
		tmp = GINT16_FROM_LE(*input);
		input++;
		tmp += GINT16_FROM_LE(*input);
		input++;
		stmp = tmp / 2;
		*output++ = GINT16_TO_LE(stmp);
	}
	return length / 2;
}

static int convert_stereo_to_mono_s16be(struct xmms_convert_buffers* buf, void **data, int length)
{
	gint16 *output = *data, *input = *data;
	int i;
	for (i = 0; i < length / 4; i++)
	{
		gint32 tmp;
		gint16 stmp;
		tmp = GINT16_FROM_BE(*input);
		input++;
		tmp += GINT16_FROM_BE(*input);
		input++;
		stmp = tmp / 2;
		*output++ = GINT16_TO_BE(stmp);
	}
	return length / 2;
}

convert_channel_func_t xmms_convert_get_channel_func(AFormat fmt, int output, int input)
{
	fmt = unnativize(fmt);

	if (output == input)
		return NULL;

	if (input == 1 && output == 2)
		switch (fmt)
		{
			case FMT_U8:
			case FMT_S8:
				return convert_mono_to_stereo_8;
			case FMT_U16_LE:
			case FMT_U16_BE:
			case FMT_S16_LE:
			case FMT_S16_BE:
				return convert_mono_to_stereo_16;
			default:
				g_warning("Unknown format: %d"
					  "No conversion available.", fmt);
				return NULL;
		}
	if (input == 2 && output == 1)
		switch (fmt)
		{
			case FMT_U8:
				return convert_stereo_to_mono_u8;
			case FMT_S8:
				return convert_stereo_to_mono_s8;
			case FMT_U16_LE:
				return convert_stereo_to_mono_u16le;
			case FMT_U16_BE:
				return convert_stereo_to_mono_u16be;
			case FMT_S16_LE:
				return convert_stereo_to_mono_s16le;
			case FMT_S16_BE:
				return convert_stereo_to_mono_s16be;
			default:
				g_warning("Unknown format: %d. "
					  "No conversion available.", fmt);
				return NULL;
				
		}

	g_warning("Input has %d channels, soundcard uses %d channels\n"
		  "No conversion is available", input, output);
	return NULL;
}
