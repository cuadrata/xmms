osssources = OSS.c OSS.h \
about.c \
configure.c \
audio.c \
init.c

if HAVE_OSS

ossltlibs = out_oss.la
ossbuildsources = $(osssources)


else

ossltlibs =
ossbuildsources =

endif

EXTRA_DIST = $(osssources)

lib_LTLIBRARIES = $(ossltlibs)

libdir = $(plugindir)/$(OUTPUT_PLUGIN_DIR)

out_oss_la_LDFLAGS = -module -avoid-version @PLUGIN_LDFLAGS@
out_oss_la_LIBADD = @GTK_LIBS@ @PTHREAD_LIBS@
out_oss_la_SOURCES= $(ossbuildsources)
AM_CPPFLAGS = @GTK_CFLAGS@ -I$(top_srcdir)

include $(top_srcdir)/plugin.mk
