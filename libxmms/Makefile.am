xmmsincludedir = $(includedir)/xmms

lib_LTLIBRARIES = libxmms.la

libxmms_la_LDFLAGS = -export-dynamic -version-info $(LIBXMMS_MAJOR_VERSION):$(LIBXMMS_MINOR_VERSION):$(LIBXMMS_MICRO_VERSION)
libxmms_la_LIBADD = @GTK_LIBS@ @PTHREAD_LIBS@
AM_CPPFLAGS = @GTK_CFLAGS@ -I$(top_srcdir) -I$(top_builddir)

libxmms_la_SOURCES = \
configfile.c configfile.h \
xmmsctrl.c xmmsctrl.h \
dirbrowser.c dirbrowser.h \
util.c util.h \
formatter.c formatter.h \
titlestring.c titlestring.h \
xentry.c xentry.h \
xconvert.c xconvert.h \
rateconf.c rateconf.h \
charset.c charset.h

xmmsinclude_HEADERS = configfile.h xmmsctrl.h dirbrowser.h util.h formatter.h titlestring.h

