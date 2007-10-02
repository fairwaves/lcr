##   -*- autoconf -*-

dnl    This file is part of linux-call-router
dnl    Copyright (C) 2007 Joerg Habenicht (j.habenicht@gmx.de)

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
AC_INIT(lcr, 0.1.20071001, pbx4linux@koeln.ccc.de)
AC_PREREQ(2.61)
AC_CONFIG_SRCDIR([main.c])
AC_CONFIG_HEADER([config.h])

# fix warnings from autoconf + automake
AC_GNU_SOURCE
AC_USE_SYSTEM_EXTENSIONS
AM_INIT_AUTOMAKE([dist-bzip2])



# Checks for programs.
dnl AC_CHECK_COMPILERS contains AC_PROG_CC AC_PROG_CPP and AC_PROG_CXX
AC_CHECK_COMPILERS
dnl AC_PROG_CXX
dnl AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET
dnl required, if per binary CFLAGS are used
dnl AM_PROG_CC_C_O


# site specific configs
dnl enable this to have all special options on one place
dnl AC_PRESERVE_HELP_ORDER

# enable socket access to misdn kernel module on demand
dnl SOCKET_MISDN=
AC_ARG_ENABLE([socket_misdn],
              [AS_HELP_STRING([--enable-socket-misdn],
                              [use socket connection to misdn kernel code (default=disable)])
              ],
              [use_misdn_socket="yes"],
              [use_misdn_socket="no"]
             )
AM_CONDITIONAL(ENABLE_SOCKET_MISDN, test "x$use_misdn_socket" == "xyes" )

# check for asterisk
AC_ARG_WITH([asterisk],
	[AS_HELP_STRING([--with-asterisk],
			[compile with built-in Asterisk channel driver @<:@default=no@:>@])
	],
	[with_asterisk_channel_driver="yes"],
	[with_asterisk_channel_driver="no"])
AM_CONDITIONAL(ENABLE_ASTERISK_CHANNEL_DRIVER, test "x$with_asterisk_channel_driver" == "xyes" )

# check for crypto
AC_ARG_WITH([crypto],
	[AS_HELP_STRING([--with-crypto],
			[compile with lib Crypto support @<:@default=check@:>@])
	],
	[],
	[with_crypto=check])
LIBCRYPTO=
AS_IF([test "x$with_crypto" != xno],
      [AC_CHECK_HEADERS([openssl/rsa.h])
       AC_CHECK_LIB([crypto], [main],
                    [AC_SUBST([LIBCRYPTO], ["-lcrypto"])
                     AC_DEFINE([HAVE_LIBCRYPTO], [1],
                               [Define if you have libcrypto])
                    ],
                    [if test "x$with_crypto" != xcheck ; then
                      AC_MSG_FAILURE(
                        [--with-crypto was given, but test for lib Crypto failed])
                     fi
                    ],
                    -lcrypto
                   )
      ]
     )
AM_CONDITIONAL(WITH_CRYPTO, test "x$LIBCRYPTO" == "x-lcrypto" )

# check for isdn_net.h
# check for mISDNlib.h
# --with-isdn_net-include=<include>, default: env MISDNUSER= , $include/mISDNuser

# self written test for mISDNuser lib
AC_PATH_MISDNUSER

# self written test for mISDN kernel header
AC_PATH_MISDNKERNEL




# Checks for libraries.
dnl FIXME: Replace `main' with a function in `-ldl':
dnl AC_CHECK_LIB([dl], [main])
dnl FIXME: Replace `main' with a function in `-lh323_linux_x86_r':
dnl AC_CHECK_LIB([h323_linux_x86_r], [main])
dnl FIXME: Replace `main' with a function in `-lisdnnet':
dnl AC_CHECK_LIB([isdnnet], [main])
# FIXME: Replace `main' with a function in `-lm':
AC_CHECK_LIB([m], [main])
dnl FIXME: Replace `main' with a function in `-lmISDN':
dnl AC_CHECK_LIB([mISDN], [main])
# FIXME: Replace `main' with a function in `-lncurses':
AC_CHECK_LIB([ncurses], [main])
dnl FIXME: Replace `main' with a function in `-lpt_linux_x86_r':
dnl AC_CHECK_LIB([pt_linux_x86_r], [main])
# FIXME: Replace `main' with a function in `-lpthread':
AC_CHECK_LIB([pthread], [main])

# Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([fcntl.h netinet/in.h stdlib.h string.h sys/file.h sys/ioctl.h sys/socket.h sys/time.h unistd.h ctype.h assert.h])
AC_HEADER_ASSERT

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

AC_SUBST(misdnuser_libraries)
AC_SUBST(misdnuser_includes)
AC_SUBST(misdnkernel_includes)
AC_SUBST(all_includes)
AC_SUBST(all_libraries)

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
