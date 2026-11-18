lib_LTLIBRARIES = dsp_stereo.la

libdir = $(plugindir)/$(EFFECT_PLUGIN_DIR)

dsp_stereo_la_LDFLAGS = -module -avoid-version @PLUGIN_LDFLAGS@
dsp_stereo_la_LIBADD = @GTK_LIBS@

dsp_stereo_la_SOURCES = 	\
stereo.c

AM_CPPFLAGS = @GTK_CFLAGS@ -I$(top_srcdir)

include $(top_srcdir)/plugin.mk
