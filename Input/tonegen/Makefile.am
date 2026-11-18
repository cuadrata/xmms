lib_LTLIBRARIES = in_tonegen.la

libdir = $(plugindir)/$(INPUT_PLUGIN_DIR)

in_tonegen_la_LDFLAGS = -module -avoid-version @PLUGIN_LDFLAGS@

in_tonegen_la_SOURCES = tonegen.c
AM_CPPFLAGS = @GTK_CFLAGS@ -I$(top_srcdir)


include $(top_srcdir)/plugin.mk
