lib_LTLIBRARIES = gen_song_change.la

libdir = $(plugindir)/$(GENERAL_PLUGIN_DIR)

gen_song_change_la_LDFLAGS = -module -avoid-version @PLUGIN_LDFLAGS@
gen_song_change_la_LIBADD = @GTK_LIBS@ @PTHREAD_LIBS@

gen_song_change_la_SOURCES = song_change.c

AM_CPPFLAGS = @GTK_CFLAGS@ -I$(top_srcdir)

include $(top_srcdir)/plugin.mk
