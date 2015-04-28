AC_INIT([samplicate], [1.3.6],,, [https://github.com/sleinen/samplicator])
AC_CONFIG_SRCDIR([samplicate.c])
AM_INIT_AUTOMAKE
AM_CONFIG_HEADER(config.h)
AC_PROG_CC
AC_PROG_INSTALL
AC_CHECK_LIB(nsl,gethostbyname)
AC_CHECK_LIB(socket,bind)
AC_STDC_HEADERS
AC_CHECK_HEADERS(stdlib.h unistd.h ctype.h arpa/inet.h netinet/in_systm.h sys/uio.h)
AC_CHECK_FUNCS(memcpy strchr)
AC_DEFINE([HAVE_STRUCT_IP], 1,
	  [Define if the system has `struct ip'.])
AC_DEFINE([HAVE_STRUCT_IPHDR], 1,
	  [Define if the system has `struct iphdr'.])
AC_OUTPUT(Makefile)
