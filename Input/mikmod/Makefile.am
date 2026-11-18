if HAVE_MIKMOD

lib_LTLIBRARIES = in_mikmod.la

endif

libdir = $(plugindir)/$(INPUT_PLUGIN_DIR)

AM_CPPFLAGS = @GTK_CFLAGS@ @LIBMIKMOD_CFLAGS@ -I$(top_srcdir)
in_mikmod_la_LDFLAGS = -module -avoid-version @PLUGIN_LDFLAGS@
in_mikmod_la_LIBADD = @GTK_LIBS@ @PTHREAD_LIBS@ \
@LIBMIKMOD_LIBS@ 

in_mikmod_la_SOURCES = drv_xmms.c mikmod-plugin.h plugin.c mikmod.xpm


include $(top_srcdir)/plugin.mk
