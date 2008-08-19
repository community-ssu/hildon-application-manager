# configure.ac for hildon-application-manager

AC_INIT(hildon-application-manager, 2.2.0)
AC_CONFIG_SRCDIR([src/main.cc])
AM_INIT_AUTOMAKE

AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_LIBTOOL

AC_LANG_CPLUSPLUS

AC_CHECK_HEADER(apt-pkg/configuration.h)

if test x$ac_cv_header_apt_pkg_configuration_h != xyes; then
  AC_ERROR([libapt-pkg not found.])
fi

AC_MSG_CHECKING(apt_set_index_trust_level_for_package_hook)
AC_EGREP_HEADER(apt_set_index_trust_level_for_package_hook,
                apt-pkg/acquire-item.h,
                AC_MSG_RESULT(yes)
                AC_DEFINE(HAVE_APT_TRUST_HOOK),
                AC_MSG_RESULT(no))

PKG_CHECK_MODULES(HAM_DEPS, glib-2.0 gtk+-2.0 hildon-1 hildon-fm-2 libosso
                            conic gconf-2.0 gnome-vfs-2.0 hildon-help mce
                            libhildonwm libhildondesktop)
AC_SUBST(HAM_DEPS_CFLAGS)
AC_SUBST(HAM_DEPS_LIBS)

saved_CXXFLAGS="$CXXFLAGS"
saved_LDFLAGS="$LDFLAGS"
CXXFLAGS="$CXXFLAGS $HAM_DEPS_CFLAGS"
LDFLAGS="$LDFLAGS $HAM_DEPS_LIBS"

AC_CHECK_FUNCS(g_string_append_vprintf)

CXXFLAGS="$saved_CXXFLAGS"
LDFLAGS="$saved_LDFLAGS"

PKG_CHECK_MODULES(AW_DEPS, glib-2.0)
AC_SUBST(AW_DEPS_CFLAGS)
AC_SUBST(AW_DEPS_LIBS)

PKG_CHECK_MODULES(NOTIFIER_DEPS, gtk+-2.0 glib-2.0 libhildondesktop gconf-2.0
                                 libhildonwm dbus-1 libalarm libcurl)
AC_SUBST(NOTIFIER_DEPS_LIBS)
AC_SUBST(NOTIFIER_DEPS_CFLAGS)

hildonstatusbarplugindir=`pkg-config osso-af-settings --variable=hildondesktoplibdir`
hildonstatusbardesktopdir=`pkg-config osso-af-settings --variable=statusbardesktopentrydir`

AC_SUBST(hildonstatusbarplugindir)
AC_SUBST(hildonstatusbardesktopdir)

# Debian architecture
#
DEB_HOST_ARCH=`dpkg-architecture -qDEB_HOST_ARCH`
AC_DEFINE_UNQUOTED(DEB_HOST_ARCH, "$DEB_HOST_ARCH", [Debian architecture])

# Default distribution
#
AC_DEFINE(DEFAULT_DIST, "diablo", [Default distribution name])

AC_CONFIG_FILES([Makefile
                 src/Makefile
		 utils/Makefile])

AC_OUTPUT
