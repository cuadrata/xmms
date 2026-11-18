lib_LTLIBRARIES = dsp_echo.la

libdir = $(plugindir)/$(EFFECT_PLUGIN_DIR)

dsp_echo_la_LDFLAGS = -module -avoid-version @PLUGIN_LDFLAGS@
dsp_echo_la_LIBADD = @GTK_LIBS@

dsp_echo_la_SOURCES =    \
echo.c \
echo.h \
gui.c

AM_CPPFLAGS = @GTK_CFLAGS@ -I$(top_srcdir)

include $(top_srcdir)/plugin.mk
