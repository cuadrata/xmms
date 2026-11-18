lib_LTLIBRARIES = vis_sanalyzer.la

libdir = $(plugindir)/$(VISUALIZATION_PLUGIN_DIR)

vis_sanalyzer_la_LDFLAGS = -module -avoid-version @PLUGIN_LDFLAGS@
vis_sanalyzer_la_LIBADD = @GTK_LIBS@

vis_sanalyzer_la_SOURCES =  spectrum.c
AM_CPPFLAGS = @GTK_CFLAGS@ -I$(top_srcdir)

EXTRA_DIST = xmms_logo.xpm

include $(top_srcdir)/plugin.mk
