##   -*- autoconf -*-

dnl    This file is part of linux-call-router
dnl    Copyright (C) 2007 Joerg Habenicht (j.habenicht@gmx.de)
dnl    Copyright (C) 2008 Peter Schlaile (peter -at- schlaile.de)

dnl    This program is free software; you can redistribute it and/or
dnl    modify it under the terms of the GNU General Public License as
dnl    published by the Free Software Foundation; either
dnl    version 2 of the License, or (at your option) any later version.

dnl    This program is distributed in the hope that it will be useful,
dnl    but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl    GNU General Public License for more details.

dnl    You should have received a copy of the GNU General Public License
dnl    along with this library; see the file COPYING.  If not, write to
dnl    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
dnl    Boston, MA 02110-1301, USA.



dnl This keeps being the first instruction.
dnl Change the 2nd argument if the version increases
dnl 1st + 2nd argument is used for distribution package name
AC_INIT(lcr, 1.4, andreas@eversberg.eu)
AC_PREREQ(2.59)
AC_CONFIG_SRCDIR([main.c])
AM_CONFIG_HEADER(config.h)

# fix warnings from autoconf + automake
AC_GNU_SOURCE
# AC_USE_SYSTEM_EXTENSIONS
AM_INIT_AUTOMAKE(lcr,1.4)



# Checks for programs.
AC_PROG_CPP
AC_PROG_CXX
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET
dnl required, if per binary CFLAGS are used
dnl AM_PROG_CC_C_O


# site specific configs
dnl enable this to have all special options on one place
dnl AC_PRESERVE_HELP_ORDER

# check for asterisk
AC_ARG_WITH([asterisk],
	[AS_HELP_STRING([--with-asterisk],
			[compile with built-in Asterisk channel driver @<:@default=check@:>@])
	],
	[],
	[with_asterisk="check"])

AS_IF([test "x$with_asterisk" != xno],
      [AC_CHECK_HEADERS([asterisk/compiler.h],
			[with_asterisk="yes"],
			[if test "x$with_asterisk" != xcheck ; then
                      AC_MSG_FAILURE(
                        [--with-asterisk was given, but test for header-file asterisk/compiler.h failed])
                     fi
		     ])
      ])

AM_CONDITIONAL(ENABLE_ASTERISK_CHANNEL_DRIVER, test "x$with_asterisk" == "xyes" )

# check for crypto
AC_ARG_WITH([ssl],
	[AS_HELP_STRING([--with-ssl],
			[compile with ssl support (libcrypto) @<:@default=check@:>@])
	],
	[],
	[with_ssl=check])
LIBCRYPTO=
AS_IF([test "x$with_ssl" != xno],
      [AC_CHECK_HEADERS([openssl/rsa.h])
       AC_CHECK_LIB([crypto], [main],
                    [AC_SUBST([LIBCRYPTO], ["-lcrypto"])
                     AC_DEFINE([HAVE_LIBCRYPTO], [1],
                               [Define if you have libcrypto])
                    ],
                    [if test "x$with_ssl" != xcheck ; then
                      AC_MSG_FAILURE(
                        [--with-ssl was given, but test for libcrypto failed])
                     fi
                    ],
                    -lcrypto
                   )
      ]
     )

# Checks for libraries.
AC_CHECK_LIB([m], [main])
AC_CHECK_LIB([ncurses], [main])
AC_CHECK_LIB([pthread], [main])

# Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([fcntl.h netinet/in.h stdlib.h string.h sys/file.h sys/ioctl.h sys/socket.h sys/time.h unistd.h ctype.h assert.h mISDNuser/mbuffer.h assert.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_C_INLINE
AC_TYPE_PID_T
AC_HEADER_TIME
AC_STRUCT_TM

# Checks for library functions.
AC_FUNC_FORK
AC_PROG_GCC_TRADITIONAL
AC_FUNC_LSTAT
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_TYPE_SIGNAL
AC_FUNC_STRNLEN
AC_FUNC_STRTOD
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([gettimeofday memmove memset mkdir socket strcasecmp strchr strerror strncasecmp strstr strtol strtoul])


AC_CONFIG_FILES([Makefile])
AC_OUTPUT
