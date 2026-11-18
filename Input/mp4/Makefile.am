if HAVE_FAAD

lib_LTLIBRARIES = in_mp4.la

endif

libdir = $(plugindir)/$(INPUT_PLUGIN_DIR)

AM_CPPFLAGS = @GTK_CFLAGS@ @FAAD_CFLAGS@ -I$(top_srcdir) -I$(top_srcdir)/Input/common
in_mp4_la_LDFLAGS = -module -avoid-version @PLUGIN_LDFLAGS@
in_mp4_la_LIBADD = $(top_builddir)/Input/common/libhttpstream.la @GTK_LIBS@ @PTHREAD_LIBS@ @FAAD_LIBS@ -lm

in_mp4_la_SOURCES = \
mp4.c mp4.h \
mp4file.c mp4file.h \
alac.c alac.h \
configure.c \
fileinfo.c

include $(top_srcdir)/plugin.mk
