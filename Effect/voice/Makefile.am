lib_LTLIBRARIES = dsp_voice.la

libdir = $(plugindir)/$(EFFECT_PLUGIN_DIR)

dsp_voice_la_LDFLAGS = -module -avoid-version @PLUGIN_LDFLAGS@
dsp_voice_la_LIBADD = @GTK_LIBS@

dsp_voice_la_SOURCES = 	\
about.c \
voice.c \
voice.h

AM_CPPFLAGS = @GTK_CFLAGS@ -I$(top_srcdir)

include $(top_srcdir)/plugin.mk
