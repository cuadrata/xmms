if HAVE_OPENGL

oglspectrumltlibs = vis_opengl_spectrum.la
oglspectrumsources = opengl_spectrum.c

else

oglspectrumltlibs = 
oglspectrumsources = 

endif

EXTRA_DIST = opengl_spectrum.c

lib_LTLIBRARIES = $(oglspectrumltlibs)

libdir = $(plugindir)/$(VISUALIZATION_PLUGIN_DIR)

vis_opengl_spectrum_la_LDFLAGS = -module -avoid-version @PLUGIN_LDFLAGS@
vis_opengl_spectrum_la_LIBADD = @GTKGL_LIBS@ -lm

vis_opengl_spectrum_la_SOURCES = $(oglspectrumsources)
AM_CPPFLAGS = @GTK_CFLAGS@ @GTKGL_CFLAGS@ @XMMS_DEFINES@ -I$(top_srcdir)

include $(top_srcdir)/plugin.mk
