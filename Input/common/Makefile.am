## HTTP streaming shared by the input plugins, linked into each of them.

noinst_LTLIBRARIES = libhttpstream.la

libhttpstream_la_SOURCES = httpstream.c httpstream.h
libhttpstream_la_LIBADD = @PTHREAD_LIBS@ @SSL_LIBS@

AM_CPPFLAGS = @GTK_CFLAGS@ @SSL_CFLAGS@ -I$(top_srcdir) -I$(top_builddir)
