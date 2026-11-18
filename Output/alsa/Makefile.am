alsasources = \
alsa.c alsa.h \
about.c \
audio.c \
configure.c \
init.c

if HAVE_ALSA

alsaltlibs = out_alsa.la
alsabuildsources = $(alsasources)

else

alsaltlibs =
alsabuildsources = 

endif

EXTRA_DIST = $(alsasources)

lib_LTLIBRARIES = $(alsaltlibs)

libdir = $(plugindir)/$(OUTPUT_PLUGIN_DIR)

out_alsa_la_LDFLAGS = -module -avoid-version @PLUGIN_LDFLAGS@
out_alsa_la_LIBADD = @GTK_LIBS@ @PTHREAD_LIBS@ @ALSA_LIBS@
out_alsa_la_SOURCES= $(alsabuildsources)
AM_CPPFLAGS = @GTK_CFLAGS@ @ALSA_CFLAGS@ -I$(top_srcdir)

include $(top_srcdir)/plugin.mk
