if HAVE_OGGVORBIS

lib_LTLIBRARIES = in_vorbis.la

endif

libdir = $(plugindir)/$(INPUT_PLUGIN_DIR)

AM_CPPFLAGS = @GTK_CFLAGS@ @OGG_CFLAGS@ @VORBIS_CFLAGS@ -I$(top_srcdir) -I$(top_srcdir)/Input/common
in_vorbis_la_LDFLAGS = -module -avoid-version @PLUGIN_LDFLAGS@
in_vorbis_la_LIBADD = $(top_builddir)/Input/common/libhttpstream.la @GTK_LIBS@ @PTHREAD_LIBS@ @VORBIS_LIBS@ @OGG_LIBS@ @VORBISFILE_LIBS@

in_vorbis_la_SOURCES = \
vorbis.c vorbis.h \
configure.c \
fileinfo.c \
vcedit.c vcedit.h

include $(top_srcdir)/plugin.mk
