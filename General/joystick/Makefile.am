if HAVE_LINUX_JOYSTICK

joyltlibs = gen_joystick.la
joysources = joy.c \
joy.h \
configure.c \
about.c

else

joyltlibs = 
joysources = 

endif

EXTRA_DIST = joy.c joy.h configure.c about.c

lib_LTLIBRARIES = $(joyltlibs)

libdir = $(plugindir)/$(GENERAL_PLUGIN_DIR)

gen_joystick_la_LDFLAGS = -module -avoid-version @PLUGIN_LDFLAGS@
gen_joystick_la_LIBADD = @GTK_LIBS@ @PTHREAD_LIBS@

gen_joystick_la_SOURCES = $(joysources)

AM_CPPFLAGS = @GTK_CFLAGS@ -I$(top_srcdir)

include $(top_srcdir)/plugin.mk
