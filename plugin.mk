## Included by every plugin's Makefile.am.
##
## XMMS loads plugins with dlopen() by their file names, so the libtool
## archives libtool installs beside them are of no use; remove them after
## installing. Uninstalling then removes the modules themselves, which
## libtool, finding no archive, leaves behind.

install-exec-hook:
	@for la in $(lib_LTLIBRARIES); do \
	  echo " rm -f '$(DESTDIR)$(libdir)/'$$la"; \
	  rm -f "$(DESTDIR)$(libdir)/$$la"; \
	done

uninstall-hook:
	@for la in $(lib_LTLIBRARIES); do \
	  echo " rm -f '$(DESTDIR)$(libdir)/'$${la%.la}.so"; \
	  rm -f "$(DESTDIR)$(libdir)/$${la%.la}.so"; \
	done
