lib_LTLIBRARIES = in_wav.la

libdir = $(plugindir)/$(INPUT_PLUGIN_DIR)

in_wav_la_LDFLAGS = -module -avoid-version @PLUGIN_LDFLAGS@

in_wav_la_SOURCES = 	\
wav.c \
wav.h

AM_CPPFLAGS = @GTK_CFLAGS@ -I$(top_srcdir)


include $(top_srcdir)/plugin.mk
