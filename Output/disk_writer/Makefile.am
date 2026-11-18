lib_LTLIBRARIES = out_disk_writer.la

libdir = $(plugindir)/$(OUTPUT_PLUGIN_DIR)

out_disk_writer_la_LDFLAGS = -module -avoid-version @PLUGIN_LDFLAGS@
out_disk_writer_la_LIBADD = @GTK_LIBS@
out_disk_writer_la_SOURCES= disk_writer.c

AM_CPPFLAGS = @GTK_CFLAGS@ -I$(top_srcdir)

include $(top_srcdir)/plugin.mk
