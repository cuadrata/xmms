if HAVE_MPG123

lib_LTLIBRARIES = in_mpg123.la

endif

libdir = $(plugindir)/$(INPUT_PLUGIN_DIR)

AM_CPPFLAGS = @GTK_CFLAGS@ @MPG123_CFLAGS@ -I$(top_srcdir) -I$(top_srcdir)/Input/common
in_mpg123_la_LDFLAGS = -module -avoid-version @PLUGIN_LDFLAGS@
in_mpg123_la_LIBADD = $(top_builddir)/Input/common/libhttpstream.la \
	@GTK_LIBS@ @PTHREAD_LIBS@ @MPG123_LIBS@ -lm \
	$(top_builddir)/libxmms/libxmms.la

in_mpg123_la_SOURCES = \
	mpg123.c mpg123-plugin.h \
	configure.c fileinfo.c genres.c id3v2.c id3v2.h

include $(top_srcdir)/plugin.mk
