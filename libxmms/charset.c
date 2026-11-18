/*
 * Copyright (C) 1999-2001  Haavard Kvaalen <havardk@xmms.org>
 *
 * Licensed under GNU LGPL version 2.
 *
 */

#include <config.h>
#include <stdlib.h>
#include <glib.h>
#include <string.h>

#include <iconv.h>

#include <errno.h>

#include <langinfo.h>

#include "charset.h"


char* xmms_charset_get_current(void)
{
	char *charset = getenv("CHARSET");

	if (!charset)
		charset = nl_langinfo(CODESET);
	/* Maybe we should default to ISO-8859-1 instead? */
	if (!charset)
		charset = "US-ASCII";

	return charset;
}


char* xmms_charset_convert(const char *string, size_t insize, char *from, char *to)
{
	size_t outleft, outsize;
	iconv_t cd;
	char *out, *outptr;
	ICONV_CONST char *input = (ICONV_CONST char *) string;

	if (!string)
		return NULL;

	if (!from)
		from = xmms_charset_get_current();
	if (!to)
		to = xmms_charset_get_current();

/* 	g_message("converting %s from %s to %s (%u)", string, from, to, insize); */
	if ((cd = iconv_open(to, from)) == (iconv_t)-1)
	{
		g_warning("convert_string(): Conversion not supported. "
			  "Charsets: %s -> %s", from, to);
		return g_strdup(string);
	}

	/* Due to a GLIBC bug, round outbuf_size up to a multiple of 4 */
	/* + 1 for nul in case len == 1 */
	outsize = ((insize + 3) & ~3) + 1;
	out = g_malloc(outsize);
	outleft = outsize - 1;
	outptr = out;

 retry:
	if (iconv(cd, &input, &insize, &outptr, &outleft) == (size_t) -1)
	{
		int used;
		switch (errno)
		{
			case E2BIG:
				used = outptr - out;
				outsize = (outsize - 1) * 2 + 1;
				out = g_realloc(out, outsize);
				outptr = out + used;
				outleft = outsize - 1 - used;
				goto retry;
			case EINVAL:
				break;
			case EILSEQ:
				/* Invalid sequence, try to get the
                                   rest of the string */
				input++;
				insize--;
				goto retry;
			default:
				g_warning("convert_string(): Conversion failed. "
					  "Inputstring: %s; Error: %s",
					  string, strerror(errno));
				break;
		}
	}
	*outptr = '\0';

	iconv_close(cd);
	return out;
}

char* xmms_charset_from_utf8(const char *string)
{
	if (!string)
		return NULL;
	return xmms_charset_convert(string, strlen(string), "UTF-8", NULL);
}


char* xmms_charset_to_utf8(const char *string)
{
	if (!string)
		return NULL;
	return xmms_charset_convert(string, strlen(string), NULL, "UTF-8");
}
