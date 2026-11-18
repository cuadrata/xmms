# Sources share a basename across directories (replaygain.c).
AUTOMAKE_OPTIONS = subdir-objects

if HAVE_FLAC

lib_LTLIBRARIES = in_flac.la

endif

libdir = $(plugindir)/$(INPUT_PLUGIN_DIR)

AM_CPPFLAGS = @GTK_CFLAGS@ @FLAC_CFLAGS@ -I$(top_srcdir) -I$(srcdir) -I$(top_srcdir)/Input/common
in_flac_la_LDFLAGS = -module -avoid-version @PLUGIN_LDFLAGS@
in_flac_la_LIBADD = $(top_builddir)/Input/common/libhttpstream.la @GTK_LIBS@ @PTHREAD_LIBS@ @FLAC_LIBS@ -lm \
	$(top_builddir)/libxmms/libxmms.la

in_flac_la_SOURCES = \
configure.c configure.h \
fileinfo.c \
plugin.c plugin.h \
tag.c tag.h \
plugin_common/all.h \
plugin_common/defs.h \
plugin_common/dither.c plugin_common/dither.h \
plugin_common/replaygain.c plugin_common/replaygain.h \
plugin_common/tags.c plugin_common/tags.h \
share/alloc.h share/compat.h share/safe_str.h \
share/grabbag.h share/grabbag/replaygain.h share/grabbag/replaygain.c \
share/grabbag/alloc.c \
share/replaygain_synthesis.h share/replaygain_synthesis/replaygain_synthesis.c

EXTRA_DIST = README COPYING.GPL COPYING.Xiph

include $(top_srcdir)/plugin.mk
