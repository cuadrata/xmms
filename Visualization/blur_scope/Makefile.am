lib_LTLIBRARIES = vis_blur_scope.la

libdir = $(plugindir)/$(VISUALIZATION_PLUGIN_DIR)

vis_blur_scope_la_LDFLAGS = -module -avoid-version @PLUGIN_LDFLAGS@
vis_blur_scope_la_LIBADD = @GTK_LIBS@ @PTHREAD_LIBS@

EXTRA_DIST = xmms_logo.xpm

vis_blur_scope_la_SOURCES = blur_scope.c blur_scope.h config.c

AM_CPPFLAGS = @GTK_CFLAGS@ -I$(top_srcdir)

include $(top_srcdir)/plugin.mk
