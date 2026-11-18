dnl Process this file with autoreconf -vif to produce configure.

AC_PREREQ([2.71])
AC_INIT([XMMS], [1.3.0], [], [xmms])
AC_CONFIG_SRCDIR([xmms/main.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AC_CANONICAL_HOST

AC_CONFIG_HEADERS([config.h xmms/i18n.h])

AM_INIT_AUTOMAKE([1.16 foreign subdir-objects])

dnl libxmms is versioned on its own, for libtool.
LIBXMMS_MAJOR_VERSION=4
LIBXMMS_MINOR_VERSION=1
LIBXMMS_MICRO_VERSION=3
AC_SUBST([LIBXMMS_MAJOR_VERSION])
AC_SUBST([LIBXMMS_MINOR_VERSION])
AC_SUBST([LIBXMMS_MICRO_VERSION])

AC_ARG_ENABLE([one-plugin-dir],
[  --enable-one-plugin-dir  use a single plugin dir [default=no]],,
		enable_one_plugin_dir="no")

AC_ARG_ENABLE([user-plugin-dir],
[  --disable-user-plugin-dir disable per-user plugin dir],,
		enable_user_plugin_dir="yes")

AC_ARG_WITH([dev-dsp], 
  [  --with-dev-dsp=path     Path to OSS DSP data pipe, default is /dev/dsp.],
	dev_dsp="$withval", dev_dsp="/dev/dsp")
AC_DEFINE_UNQUOTED([DEV_DSP],["$dev_dsp"], [Path to OSS DSP, really just a data pipe, default is /dev/dsp.])



dnl ***
dnl *** Various compiler setup.
dnl ***
dnl Remember any -std= the user chose; AC_PROG_CC may append its own.
xmms_user_std=`echo " $CC $CFLAGS " | sed -n 's/.* \(-std=[[^ ]]*\) .*/\1/p'`
AC_PROG_CC
dnl XMMS is written in ISO C99. Default to that rather than the
dnl compiler's own dialect.
CC=`echo "$CC" | sed 's/ -std=[[^ ]]*//g'`
test -n "$xmms_user_std" || xmms_user_std=-std=c99
case " $CFLAGS " in
*" -std="*) ;;
*) CC="$CC $xmms_user_std" ;;
esac
dnl POSIX interfaces are requested the standard way. XMMS needs
dnl POSIX.1-2008 (mkdtemp), so _XOPEN_SOURCE=700.
case " $CC $CPPFLAGS $CFLAGS " in
*" -D_XOPEN_SOURCE"* | *" -D_POSIX_C_SOURCE"*) ;;
*) CPPFLAGS="$CPPFLAGS -D_XOPEN_SOURCE=700" ;;
esac
AC_C_BIGENDIAN


dnl ***
dnl *** Checks for programs.
dnl ***
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
dnl Before any PKG_CHECK_MODULES, as some of those are conditional.
PKG_PROG_PKG_CONFIG

LT_INIT([disable-static])

dnl libtool's install runs only "ldconfig -n" on the library directory.
dnl That makes the soname links but leaves glibc's cache alone, and glibc
dnl finds libraries in a directory such as /usr/local/lib only through
dnl that cache. Find ldconfig so that "make install" can refresh it for
dnl libxmms (see Makefile.am). musl keeps no cache, and a cross build's
dnl cache is not this machine's.
if test "x$cross_compiling" != xyes; then
  case "$host_os" in
    linux-gnu*)
      AC_PATH_PROG([LDCONFIG], [ldconfig], [], [$PATH:/sbin:/usr/sbin])
      ;;
  esac
fi
AC_SUBST(LDCONFIG)

AC_CHECK_HEADER([pthread.h],,[AC_MSG_ERROR([*** POSIX threads support not installed - please install first ***])])

dnl CGTK, from its pkg-config files: GLib for the parts that use nothing
dnl else, and GTK+ with GThread for the rest.
PKG_CHECK_MODULES([GLIB], [glib-1.4 >= 1.4.0],,
	[AC_MSG_ERROR([*** CGTK >= 1.4.0 not installed - please install first ***])])
PKG_CHECK_MODULES([GTK], [gtk+-1.4 >= 1.4.0 gthread-1.4],,
	[AC_MSG_ERROR([*** CGTK >= 1.4.0 not installed - please install first ***])])

dnl A window's icon in colour is the toolkit's to find and to give.
xmms_save_LIBS="$LIBS"
LIBS="$GTK_LIBS $LIBS"
AC_CHECK_FUNC([gtk_window_set_default_icon_name], [],
	[AC_MSG_ERROR([*** this CGTK is too old: it has no gtk_window_set_default_icon_name() ***])])
LIBS="$xmms_save_LIBS"

dnl The output converts songs at other rates to the device's.
PKG_CHECK_MODULES([SAMPLERATE], [samplerate >= 0.1.9],,
	[AC_MSG_ERROR([*** libsamplerate >= 0.1.9 not installed - please install first ***])])


dnl iconv is XSI; the libxmms charset conversion is built on it.
AM_ICONV
if test "x$am_cv_func_iconv" != xyes; then
  AC_MSG_ERROR([*** iconv not found - please install libiconv or a C library with iconv ***])
fi


dnl ***
dnl *** POSIX threads: in the C library, or in -lpthread where it keeps
dnl *** them apart.
dnl ***
xmms_save_LIBS="$LIBS"
LIBS=
AC_SEARCH_LIBS([pthread_create], [pthread], [],
	[AC_MSG_ERROR([*** Unable to locate working posix thread library ***])])
PTHREAD_LIBS="$LIBS"
LIBS="$xmms_save_LIBS"
AC_SUBST([PTHREAD_LIBS])


dnl ***
dnl *** OpenGL. The spectrum analyzer draws in a GtkGLArea, which CGTK
dnl *** has when it was built with EGL, using the library and headers of
dnl *** OpenGL the system has. Built when those are found, unless
dnl *** --disable-opengl is given.
dnl ***
AC_ARG_ENABLE([opengl],
[  --disable-opengl        Do not build the OpenGL plugin(s) [default=build them if GtkGLArea is found]],,
	[enable_opengl="yes"])

have_opengl=no
if test "x$enable_opengl" = "xyes"; then
	dnl GtkGLArea, and the library OpenGL is in: OpenGL where the
	dnl system has it, which is GL and nothing of a window system,
	dnl else GL.
	PKG_CHECK_MODULES([GTKGL], [gtk+-1.4 gthread-1.4 gtkgl-1.4],
		[have_opengl=yes], [have_opengl=no])
	if test "x$have_opengl" = "xyes"; then
		PKG_CHECK_MODULES([OPENGL], [opengl],,
			[PKG_CHECK_MODULES([OPENGL], [gl],,
				[OPENGL_CFLAGS=""; OPENGL_LIBS="-lGL"])])
		GTKGL_CFLAGS="$GTKGL_CFLAGS $OPENGL_CFLAGS"
		GTKGL_LIBS="$GTKGL_LIBS $OPENGL_LIBS"
		CPPFLAGS_save=$CPPFLAGS
		LIBS_save=$LIBS
		CPPFLAGS="$GTKGL_CFLAGS $CPPFLAGS"
		LIBS="$GTKGL_LIBS $LIBS"
		AC_CHECK_HEADERS([GL/gl.h],,[have_opengl=no])
		if test "x$have_opengl" = "xyes"; then
			AC_MSG_CHECKING([whether a program using OpenGL links])
			AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <GL/gl.h>]],
				[[glBegin(GL_TRIANGLES); glEnd();]])],
				[AC_MSG_RESULT([yes])],
				[AC_MSG_RESULT([no]); have_opengl=no])
		fi
		CPPFLAGS=$CPPFLAGS_save
		LIBS=$LIBS_save
	fi
fi
AM_CONDITIONAL([HAVE_OPENGL], [test "x$have_opengl" = "xyes"])


dnl ***
dnl *** https:// streams, through OpenSSL or LibreSSL
dnl ***
AC_ARG_ENABLE([tls],
[AS_HELP_STRING([--disable-tls], [Disable https:// streams [default=enabled]])],,
	[enable_tls="yes"])

have_tls=no
if test "x$enable_tls" = "xyes"; then
	PKG_CHECK_MODULES([SSL], [libssl >= 1.1.1 libcrypto], [have_tls=yes], [have_tls=no])
fi
if test "x$have_tls" = "xyes"; then
	save_CFLAGS=$CFLAGS
	save_LIBS=$LIBS
	CFLAGS="$CFLAGS $SSL_CFLAGS"
	LIBS="$SSL_LIBS $LIBS"
	AC_MSG_CHECKING([whether libssl has what https streams need])
	AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <openssl/ssl.h>
#include <openssl/x509v3.h>]], [[SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
SSL *ssl = SSL_new(ctx);
SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
SSL_set_tlsext_host_name(ssl, "example.org");
SSL_set1_host(ssl, "example.org");
X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl), "127.0.0.1");]])],
		[AC_MSG_RESULT([yes])],
		[AC_MSG_RESULT([no]); have_tls=no])
	CFLAGS=$save_CFLAGS
	LIBS=$save_LIBS
fi
if test "x$have_tls" = "xyes"; then
	AC_DEFINE([HAVE_TLS], [1], [Define to play https:// streams through libssl.])
else
	SSL_CFLAGS=
	SSL_LIBS=
	if test "x$enable_tls" = "xyes"; then
		AC_MSG_WARN([*** No OpenSSL 1.1.1 or later, or LibreSSL: https:// streams will not play ***])
	fi
fi


dnl ***
dnl *** MPEG audio, decoded by libmpg123; zlib inflates compressed ID3v2
dnl *** frames
dnl ***
AC_ARG_ENABLE([mpg123],
[AS_HELP_STRING([--disable-mpg123], [Disable the MPEG audio input plugin [default=enabled]])],,
	[enable_mpg123="yes"])

if test "x$enable_mpg123" = "xyes"; then
	PKG_CHECK_MODULES([MPG123], [libmpg123 >= 1.25 zlib],
		[have_mpg123=yes], [have_mpg123=no])
else
	AC_MSG_RESULT([*** MPEG audio plugin disabled per user request ***])
	have_mpg123=no
fi
AM_CONDITIONAL([HAVE_MPG123], [test "x$have_mpg123" = "xyes"])


dnl ***
dnl *** mikmod
dnl ***
AC_ARG_ENABLE([mikmod],
[  --disable-mikmod        Disable mikmod input plugin [default=enabled]],,
	[enable_mikmod="yes"])

if test "x$enable_mikmod" = "xyes"; then
	PKG_CHECK_MODULES([LIBMIKMOD], [libmikmod >= 3.1.5],
		[have_mikmod=yes], [have_mikmod=no])
else
	AC_MSG_RESULT([*** mikmod plugin disabled per user request ***])
	have_mikmod=no
fi
AM_CONDITIONAL([HAVE_MIKMOD], [test "x$have_mikmod" = "xyes"])


dnl ***
dnl *** Ogg Vorbis
dnl ***
AC_ARG_ENABLE([vorbis],
[  --disable-vorbis        Disable Ogg Vorbis input plugin [default=enabled]],,
	[enable_vorbis="yes"])

if test "x$enable_vorbis" = "xyes"; then
	PKG_CHECK_MODULES([OGG], [ogg], [have_ogg=yes], [have_ogg=no])
	PKG_CHECK_MODULES([VORBIS], [vorbis vorbisfile], [have_vorbis=yes], [have_vorbis=no])
	if test "x$have_vorbis" = "xyes"; then
	  AC_MSG_CHECKING([if Vorbis is sufficiently new])
	  CFLAGS_save=$CFLAGS
	  LIBS_save=$LIBS
	  CFLAGS="$CFLAGS $VORBIS_CFLAGS $OGG_CFLAGS"
	  LIBS="$LIBS $VORBIS_LIBS $OGG_LIBS"
	  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <vorbis/vorbisfile.h>]], [[OggVorbis_File vf;
		 float **pcm;
		 ov_read_float(&vf, &pcm, 1024, 0);]])],[AC_MSG_RESULT([yes])],[AC_MSG_RESULT([no (need atleast 1.0)])
		 have_vorbis=no])
	  CFLAGS=$CFLAGS_save
	  LIBS=$LIBS_save
	fi
	have_oggvorbis=no
	if test "x$have_ogg" = "xyes"; then
		if test "x$have_vorbis" = "xyes"; then
			have_oggvorbis=yes
		fi
	fi
else
	AC_MSG_RESULT([*** Ogg Vorbis plugin disabled per user request ***])
	have_oggvorbis=no
fi
dnl vorbisfile comes in VORBIS_LIBS.
VORBISFILE_LIBS=
AC_SUBST([VORBISFILE_LIBS])
AM_CONDITIONAL([HAVE_OGGVORBIS], [test "x$have_oggvorbis" = "xyes"])


dnl ***
dnl *** Opus
dnl ***
AC_ARG_ENABLE([opus],
[  --disable-opus          Disable Opus input plugin [default=enabled]],,
	[enable_opus="yes"])

if test "x$enable_opus" = "xyes"; then
	PKG_CHECK_MODULES([OPUSFILE], [opusfile >= 0.12], [have_opus=yes], [have_opus=no])
else
	AC_MSG_RESULT([*** Opus plugin disabled per user request ***])
	have_opus=no
fi
AM_CONDITIONAL([HAVE_OPUS], [test "x$have_opus" = "xyes"])


dnl ***
dnl *** MPEG-4 audio: AAC through faad2, ALAC built in
dnl ***
AC_ARG_ENABLE([mp4],
[  --disable-mp4           Disable MPEG-4 audio (AAC, ALAC) input plugin [default=enabled]],,
	[enable_mp4="yes"])

if test "x$enable_mp4" = "xyes"; then
	PKG_CHECK_MODULES([FAAD], [faad2 >= 2.10], [have_faad=yes], [have_faad=no])
else
	AC_MSG_RESULT([*** MPEG-4 audio plugin disabled per user request ***])
	have_faad=no
fi
AM_CONDITIONAL([HAVE_FAAD], [test "x$have_faad" = "xyes"])


dnl ***
dnl *** FLAC
dnl ***
AC_ARG_ENABLE([flac],
[  --disable-flac          Disable FLAC input plugin [default=enabled]],,
	[enable_flac="yes"])

if test "x$enable_flac" = "xyes"; then
	PKG_CHECK_MODULES([FLAC], [flac >= 1.2.0], [have_flac=yes], [have_flac=no])
else
	AC_MSG_RESULT([*** FLAC plugin disabled per user request ***])
	have_flac=no
fi
AM_CONDITIONAL([HAVE_FLAC], [test "x$have_flac" = "xyes"])

dnl The FLAC plugin's HTTP code uses this.
AC_CHECK_TYPES([socklen_t], [], [], [[#include <sys/types.h>
#include <sys/socket.h>]])


dnl The gettext macros come from autopoint, which autoreconf -i runs.
dnl Newer gettext no longer installs them where aclocal looks, and newer
dnl autoreconf skips autopoint silently when it is missing, so say so
dnl here rather than fail on an undefined macro.
m4_ifndef([AM_GNU_GETTEXT_NEED],
  [m4_fatal([the gettext macros are missing: install autopoint (a package of its own on Debian) and run autoreconf -vif])])
AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION([0.19.6])
AM_GNU_GETTEXT_REQUIRE_VERSION([0.19.6])

localedir=${datadir}/locale
AC_SUBST([localedir])


dnl ***
dnl *** If {type}_PLUGINS is defined, it should be a comma or space separated
dnl *** list of plugins that should be built, e.g.,
dnl *** INPUT_PLUGINS="mpg123,vorbis". If variable is not defined, all
dnl *** buildable plugins are compiled. This stuff makes life easier for
dnl *** automated builds that want to disable certain plugins.
dnl ***
for class in EFFECT GENERAL INPUT OUTPUT VISUALIZATION; do
    value=`eval echo "\\$${class}_PLUGINS"`
    if test "x${value}" = "x"; then
       eval ${class}_PLUGINS="\$\(ALL_PLUGINS\)"
    else
       value=`echo ${value} | sed -e 's/,/ /g'`
       eval ${class}_PLUGINS="\${value}"
    fi
done

AC_SUBST([EFFECT_PLUGINS])
AC_SUBST([GENERAL_PLUGINS])
AC_SUBST([INPUT_PLUGINS])
AC_SUBST([OUTPUT_PLUGINS])
AC_SUBST([VISUALIZATION_PLUGINS])


dnl ***
dnl *** Check for OSS
dnl ***
AC_ARG_ENABLE([oss],
	[  --disable-oss           disable the OSS output plugin],
	[have_oss=$enableval], [have_oss=yes])

if test "x$have_oss" = "xauto"; then
    AC_MSG_CHECKING([for OSS include dir])
    OSS_CFLAGS=""
    if test -f "/etc/oss.conf"
    then
        for i in `cat /etc/oss.conf`; 
        do
            t=`echo $i | sed -e 's/OSSLIBDIR=//'`
            if test "x$i" != "x$t"
            then
                if test -f "$t/include/sys/soundcard.h"
                then
                    OSS_CFLAGS="-I$t/include"
                fi
            fi
        done
    fi
    if test -n "$OSS_CFLAGS"
    then
        AC_MSG_RESULT([$OSS_CFLAGS])
    else
        AC_MSG_RESULT([not found])
    fi
    CPPFLAGS_save=$CPPFLAGS
    CPPFLAGS="$CPPFLAGS $OSS_CFLAGS"
    AC_CHECK_HEADERS([sys/soundcard.h])
    AC_CHECK_HEADERS([machine/soundcard.h])
    CPPFLAGS=$CPPFLAGS_save

    if test "x${ac_cv_header_sys_soundcard_h}" = "xyes" || test "x${ac_cv_header_machine_soundcard_h}" = "xyes"; then
        have_oss=yes
    fi
fi

if test "x$have_oss" = "xyes"; then
    AC_DEFINE([HAVE_OSS], [1], [Define if the OSS output plugin should be built])
else
    have_oss=no
fi
AM_CONDITIONAL([HAVE_OSS], [test "x$have_oss" = "xyes"])


dnl ***
dnl *** Check for ALSA (Advanced Linux Sound Architecture)
dnl ***
LIBS_save=$LIBS
CFLAGS_save=$CFLAGS
LDFLAGS_save=$LDFLAGS
PKG_CHECK_MODULES([ALSA], [alsa >= 0.9.0], [have_alsa=yes], [have_alsa=no])
if test "x${have_alsa}" = "xyes"; then
    AC_DEFINE([HAVE_ALSA], [1], [Define if the ALSA output plugin is built])
fi
AM_CONDITIONAL([HAVE_ALSA], [test "x${have_alsa}" = "xyes"])
LIBS=$LIBS_save
CFLAGS=$CFLAGS_save
LDFLAGS=$LDFLAGS_save


dnl ***
dnl *** Check for sndio
dnl ***
dnl *** No pkg-config: OpenBSD has sndio in its base system without one.
dnl ***
AC_ARG_ENABLE([sndio],
	[  --disable-sndio         disable the sndio output plugin],
	[have_sndio=$enableval], [have_sndio=yes])

SNDIO_LIBS=""
if test "x$have_sndio" = "xyes"; then
    AC_CHECK_HEADER([sndio.h], [], [have_sndio=no])
fi
if test "x$have_sndio" = "xyes"; then
    AC_CHECK_LIB([sndio], [sio_open], [SNDIO_LIBS="-lsndio"], [have_sndio=no])
fi
if test "x$have_sndio" = "xyes"; then
    LIBS_save=$LIBS
    LIBS="$LIBS $SNDIO_LIBS"
    AC_CHECK_FUNCS([sio_flush])
    LIBS=$LIBS_save
fi
AC_SUBST([SNDIO_LIBS])
AM_CONDITIONAL([HAVE_SNDIO], [test "x${have_sndio}" = "xyes"])


AC_CHECK_HEADERS([linux/joystick.h])

AM_CONDITIONAL([HAVE_LINUX_JOYSTICK],[test "x${ac_cv_header_linux_joystick_h}" = "xyes"])

if test -n "$GCC"
then
  CFLAGS="$CFLAGS -Wall -Wpointer-arith"
fi

plugindir=$libdir/xmms
AC_SUBST([plugindir])

if test "x$enable_one_plugin_dir" = "xyes"; then
  pluginsubs="\\\"Plugins\\\""
  INPUT_PLUGIN_DIR=Plugins
  OUTPUT_PLUGIN_DIR=Plugins
  EFFECT_PLUGIN_DIR=Plugins
  GENERAL_PLUGIN_DIR=Plugins
  VISUALIZATION_PLUGIN_DIR=Plugins
else
  pluginsubs="\\\"Output\\\",\\\"Input\\\",\\\"Effect\\\",\\\"General\\\",\\\"Visualization\\\""
  INPUT_PLUGIN_DIR=Input
  OUTPUT_PLUGIN_DIR=Output
  EFFECT_PLUGIN_DIR=Effect
  GENERAL_PLUGIN_DIR=General
  VISUALIZATION_PLUGIN_DIR=Visualization
fi

AC_SUBST([INPUT_PLUGIN_DIR])
AC_SUBST([OUTPUT_PLUGIN_DIR])
AC_SUBST([EFFECT_PLUGIN_DIR])
AC_SUBST([GENERAL_PLUGIN_DIR])
AC_SUBST([VISUALIZATION_PLUGIN_DIR])
AC_SUBST([pluginsubs])

XMMS_DEFINES="-DDATA_DIR=\\\"$datadir/xmms\\\" -DPLUGIN_DIR=\\\"$plugindir\\\" -DPLUGINSUBS=$pluginsubs -DLOCALEDIR=\\\"$localedir\\\""

if test "x$enable_user_plugin_dir" = "xno"; then
	AC_DEFINE(DISABLE_USER_PLUGIN_DIR,, [Define to disable per user plugin directory])
fi

AC_SUBST([XMMS_DEFINES])

dnl Plugins export only their entry point: get_?plugin_info, or an output
dnl driver's get_output_driver_info. -module and -avoid-version
dnl are in each plugin's Makefile.am, where automake must see -module to
dnl accept a library name without "lib".
PLUGIN_LDFLAGS='-export-symbols-regex "get_.plugin_info|get_output_driver_info"'
AC_SUBST([PLUGIN_LDFLAGS])


AC_SUBST([xmmsdir])
AC_SUBST([plugindir])

AC_CONFIG_FILES([
Makefile
xmms-config
xmms.pc
libxmms/Makefile
xmms/Makefile
xmms/defskin/Makefile
Skins/Makefile
Output/Makefile
Output/disk_writer/Makefile
Output/OSS/Makefile
Output/alsa/Makefile
Output/sndio/Makefile
Input/Makefile
Input/common/Makefile
Input/wav/Makefile
Input/mpg123/Makefile
Input/mikmod/Makefile
Input/tonegen/Makefile
Input/vorbis/Makefile
Input/opus/Makefile
Input/mp4/Makefile
Input/flac/Makefile
Effect/Makefile
Effect/voice/Makefile
Effect/echo_plugin/Makefile
Effect/stereo_plugin/Makefile
General/Makefile
General/joystick/Makefile
General/song_change/Makefile
Visualization/Makefile
Visualization/blur_scope/Makefile
Visualization/sanalyzer/Makefile
Visualization/opengl_spectrum/Makefile
po/Makefile.in
])
AC_CONFIG_COMMANDS([xmms-config-executable], [chmod +x xmms-config])
AC_OUTPUT


echo ""
echo "Configuration:"
echo "  Install path:               ${prefix}"

echo "  Build OSS plugin:           $have_oss"
echo "  Build ALSA plugin:          $have_alsa"
echo "  Build sndio plugin:         $have_sndio"
echo "  Build MPEG audio plugin:    $have_mpg123"
echo "  Build mikmod plugin:        $have_mikmod"
echo "  Build Ogg Vorbis plugin:    $have_oggvorbis"
echo "  Build Opus plugin:          $have_opus"
echo "  Build MPEG-4 audio plugin:  $have_faad"
echo "  https:// streams:           $have_tls"
echo "  Build FLAC plugin:          $have_flac"
echo "  Build OpenGL plugins:       $have_opengl"
echo "  Pthread flag:               $PTHREAD_LIBS"
echo "  Use one plugin dir:         $enable_one_plugin_dir"
echo "  Allow user plugin dir:      $enable_user_plugin_dir"
echo ""

echo ""
