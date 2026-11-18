if HAVE_OPUS

lib_LTLIBRARIES = in_opus.la

endif

libdir = $(plugindir)/$(INPUT_PLUGIN_DIR)

AM_CPPFLAGS = @GTK_CFLAGS@ @OPUSFILE_CFLAGS@ -I$(top_srcdir) -I$(top_srcdir)/Input/common
in_opus_la_LDFLAGS = -module -avoid-version @PLUGIN_LDFLAGS@
in_opus_la_LIBADD = $(top_builddir)/Input/common/libhttpstream.la @GTK_LIBS@ @PTHREAD_LIBS@ @OPUSFILE_LIBS@

in_opus_la_SOURCES = \
opus.c opus.h \
configure.c \
fileinfo.c

include $(top_srcdir)/plugin.mk
