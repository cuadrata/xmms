SUBDIRS = defskin

xmmsincludedir = $(includedir)/xmms

bin_PROGRAMS = xmms

xmms_LDFLAGS = -export-dynamic
xmms_LDADD = @GTK_LIBS@ @SAMPLERATE_LIBS@ @PTHREAD_LIBS@ \
$(top_builddir)/libxmms/libxmms.la @LTLIBINTL@

AM_CPPFLAGS = @GTK_CFLAGS@ @SAMPLERATE_CFLAGS@ @XMMS_DEFINES@ -I$(top_srcdir)


xmms_SOURCES =  bmp.c bmp.h \
skin.c skin.h skinarchive.c skinarchive.h \
util.c util.h \
output.c output.h outputcore.c eqfilter.c resample.c resample.h \
fft.c fft.h \
input.c input.h \
effect.c effect.h \
general.c general.h \
visualization.c visualization.h \
pluginenum.c pluginenum.h \
playlist.c playlist.h \
controlsocket.c controlsocket.h \
dock.c dock.h \
layout.c layout.h \
widget.c widget.h \
sbutton.c sbutton.h \
pbutton.c pbutton.h \
tbutton.c tbutton.h \
textbox.c textbox.h \
menurow.c menurow.h \
hslider.c hslider.h \
monostereo.c monostereo.h \
vis.c vis.h \
svis.c svis.h \
number.c number.h \
playstatus.c playstatus.h \
playlist_list.c playlist_list.h \
playlist_slider.c playlist_slider.h \
playlist_popup.c playlist_popup.h \
eq_graph.c eq_graph.h \
eq_slider.c eq_slider.h \
main.c main.h \
skinwin.c skinwin.h \
prefswin.c prefswin.h \
playlistwin.c playlistwin.h \
equalizer.c equalizer.h \
hints.c hints.h \
about.c about.h \
plugin.h \
xmms.h \
urldecode.c urldecode.h \
dnd.h

EXTRA_DIST= xmms_logo.xpm xmms_mini.xpm xmms.desktop \
	icons/render-icons.sh icons/xmms-small.svg \
	icons/xmms-16.png icons/xmms-22.png icons/xmms-24.png icons/xmms-32.png \
	icons/xmms-48.png icons/xmms-64.png icons/xmms-128.png icons/xmms-256.png

desktopdir = $(datadir)/applications
desktop_DATA = xmms.desktop

pixmapdir = $(datadir)/pixmaps
pixmap_DATA = xmms.xpm

# The icon a taskbar or a dock shows, where the desktop file's Icon=xmms
# and the toolkit both find it. It is two drawings: the plate,
# icons/xmms.svg, from 32 pixels up, and the headphones,
# icons/xmms-small.svg, up to 24, where the plate's lettering is too
# small to read. The sizes are rendered beforehand by
# icons/render-icons.sh; the smallest is xmms_mini.xpm itself, which the
# headphones are drawn after.
iconsizes = 16 22 24 32 48 64 128 256
hicolordir = $(datadir)/icons/hicolor
scalabledir = $(hicolordir)/scalable/apps
dist_scalable_DATA = icons/xmms.svg

install-data-local:
	for s in $(iconsizes); do \
		$(MKDIR_P) "$(DESTDIR)$(hicolordir)/$${s}x$${s}/apps" && \
		$(INSTALL_DATA) "$(srcdir)/icons/xmms-$$s.png" \
			"$(DESTDIR)$(hicolordir)/$${s}x$${s}/apps/xmms.png" || exit 1; \
	done

uninstall-local:
	for s in $(iconsizes); do \
		rm -f "$(DESTDIR)$(hicolordir)/$${s}x$${s}/apps/xmms.png"; \
	done

xmms.xpm: xmms_mini.xpm
	sed 's/xmms_mini_xpm/xmms_xpm/' $(srcdir)/xmms_mini.xpm > $@

CLEANFILES = xmms.xpm

xmmsinclude_HEADERS = plugin.h
