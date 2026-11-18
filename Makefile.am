ACLOCAL_AMFLAGS = -I m4

SUBDIRS = libxmms xmms Output Input Effect General Visualization Skins po

bin_SCRIPTS = xmms-config

EXTRA_DIST = xmms.m4 xmms.1.in history

m4datadir = $(datadir)/aclocal
m4data_DATA = xmms.m4

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = xmms.pc

man_MANS = xmms.1
CLEANFILES = xmms.1

# Refresh glibc's dynamic linker cache after a direct install or
# uninstall as root, so that xmms can load libxmms at once (see
# configure.ac). Staged installs (DESTDIR) leave this to the packager,
# and "make install LDCONFIG=" skips it.
run_ldconfig = \
	ldconfig='$(LDCONFIG)'; \
	if test -n "$$ldconfig" && test -z "$(DESTDIR)" && \
	    test "`id -u`" = 0; then \
	  echo " $$ldconfig"; \
	  $$ldconfig || echo "warning: $$ldconfig failed; run it by hand" >&2; \
	fi

install-exec-hook:
	@$(run_ldconfig)

uninstall-hook:
	@$(run_ldconfig)

# Made here rather than by configure, so the manual page names the
# data directory itself rather than the unexpanded ${datarootdir}.
xmms.1: $(srcdir)/xmms.1.in Makefile
	$(AM_V_GEN)sed -e 's|@datadir[@]|$(datadir)|g' \
	    -e 's|@VERSION[@]|$(VERSION)|g' $(srcdir)/xmms.1.in > $@
