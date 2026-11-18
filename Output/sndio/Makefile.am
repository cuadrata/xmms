sndiosources = \
sndioout.h \
sndio.c \
audio.c

if HAVE_SNDIO

sndioltlibs = out_sndio.la
sndiobuildsources = $(sndiosources)

else

sndioltlibs =
sndiobuildsources =

endif

EXTRA_DIST = $(sndiosources)

lib_LTLIBRARIES = $(sndioltlibs)

libdir = $(plugindir)/$(OUTPUT_PLUGIN_DIR)

out_sndio_la_LDFLAGS = -module -avoid-version @PLUGIN_LDFLAGS@
out_sndio_la_LIBADD = @GTK_LIBS@ @PTHREAD_LIBS@ @SNDIO_LIBS@
out_sndio_la_SOURCES= $(sndiobuildsources)
AM_CPPFLAGS = @GTK_CFLAGS@ -I$(top_srcdir)

include $(top_srcdir)/plugin.mk
