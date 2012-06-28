##   -*- autoconf -*-

dnl    This file is part of linux-call-router
dnl    Copyright (C) 2007 Joerg Habenicht (j.habenicht@gmx.de)
dnl    Copyright (C) 2008 Peter Schlaile (peter -at- schlaile.de)
dnl    Copyright (C) 2010 Andreas Eversberg (jolly@eversberg.eu)

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
AC_INIT(lcr, 1.12, andreas@eversberg.eu)
AC_PREREQ(2.59)
AC_CONFIG_SRCDIR([main.c])
AM_CONFIG_HEADER(config.h)

# fix warnings from autoconf + automake
AC_GNU_SOURCE
# AC_USE_SYSTEM_EXTENSIONS
AM_INIT_AUTOMAKE(lcr,1.12)



# Checks for programs.
AC_PROG_GREP
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

# check for mISDN
AC_ARG_WITH([misdn],
	[AS_HELP_STRING([--with-misdn],
			[compile with mISDN driver @<:@default=check@:>@])
	],
	[],
	[with_misdn="check"])

AS_IF([test "x$with_misdn" != xno],
      [AC_CHECK_HEADERS([mISDN/mbuffer.h],
			[with_misdn="yes"],
			[if test "x$with_misdn" != xcheck ; then
                      AC_MSG_FAILURE(
                        [--with-misdn was given, but test for header-file mISDN/mbuffer.h failed])
                     fi
		     ])
      ])

AM_CONDITIONAL(ENABLE_MISDN, test "x$with_misdn" == "xyes" )

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

if test "x$with_asterisk" = "xyes"; then
	dnl detect whether asterisk/indications.h defines ast_tone_zone_sound (1.6.2
	dnl and later), ind_tone_zone_sound (1.6.0) or tone_zone_sound (1.4)
	ast_tone_zone_sound=
	apc_test_result=
	ast_1_8_or_higher=

	AC_CHECK_TYPE([struct ast_tone_zone_sound],
		[ast_tone_zone_sound=ast_tone_zone_sound], [], [
#include <asterisk.h>
#include <asterisk/linkedlists.h>
#include <asterisk/indications.h>])

	AS_IF([test "x$ast_tone_zone_sound" = "x"],
		[AC_CHECK_TYPE([struct ind_tone_zone_sound],
			[ast_tone_zone_sound=ind_tone_zone_sound], [], [
#include <asterisk.h>
#include <asterisk/linkedlists.h>
#include <asterisk/indications.h>])])
	AS_IF([test "x$ast_tone_zone_sound" = "x"],
		[AC_CHECK_TYPE([struct tone_zone_sound],
			[ast_tone_zone_sound=tone_zone_sound], [], [
#include <asterisk.h>
#include <asterisk/indications.h>])])

	if test "x$ast_tone_zone_sound" = "x"; then
		AC_MSG_FAILURE([No ast_tone_zone_sound, confused...])
	fi


	AC_CHECK_TYPE([struct ast_party_caller],
		[apc_test_result=yes], [], [
#include <asterisk.h>
#include <asterisk/channel.h>])

	if test "x$apc_test_result" = "xyes"; then
		ast_1_8_or_higher="-DAST_1_8_OR_HIGHER"
	fi

	AST_CFLAGS="-Dast_tone_zone_sound=$ast_tone_zone_sound $ast_1_8_or_higher"

	AC_SUBST([AST_CFLAGS])
fi

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

# check for gsm-bs
AC_ARG_WITH([gsm-bs],
	[AS_HELP_STRING([--with-gsm-bs],
			[compile with OpenBSC support @<:@default=no@:>@])
	],
	[],
	[with_gsm_bs="check"])

AM_CONDITIONAL(ENABLE_GSM_BS, test "x$with_gsm_bs" == "xyes" )

# check for gsm-ms
AC_ARG_WITH([gsm-ms],
	[AS_HELP_STRING([--with-gsm-ms],
			[compile with Osmocom-bb support @<:@default=no@:>@])
	],
	[],
	[with_gsm_ms="check"])

AM_CONDITIONAL(ENABLE_GSM_MS, test "x$with_gsm_ms" == "xyes" )

AM_CONDITIONAL(ENABLE_GSM, test "x$with_gsm_bs" == "xyes" -o "x$with_gsm_ms" == "xyes")

AS_IF([test "x$with_gsm_bs" == xyes -o "x$with_gsm_ms" == xyes],
	[AC_MSG_CHECKING(/usr/include/gsm/gsm.h)
	if test -e /usr/include/gsm/gsm.h; then
		AC_MSG_RESULT(yes)
	else
	AC_MSG_FAILURE([You have enabled GSM, but /usr/include/gsm/gsm.h not found! Please install the lossy GSM codec. Be sure to install it in /usr/ and not in /usr/local/. You will also find a copy on http://www.linux-call-router.de.])
	fi
	])

# check for ss5
AC_ARG_WITH([ss5],
	[AS_HELP_STRING([--with-ss5],
			[compile with CCITT No. 5 support @<:@default=no@:>@])
	],
	[],
	[with_ss5="check"])

AM_CONDITIONAL(ENABLE_SS5, test "x$with_ss5" == "xyes" )

# check for SIP
AC_ARG_WITH([sip],
	[AS_HELP_STRING([--with-sip],
			[compile with SIP support (sofia-sip is required) @<:@default=no@:>@])
	],
	[],
	[with_sip="check"])

AM_CONDITIONAL(ENABLE_SIP, test "x$with_sip" == "xyes" )

AS_IF([test "x$with_sip" == xyes -o "x$with_sip" == xyes], [
		PKG_CHECK_MODULES(SOFIA, sofia-sip-ua >= 1.12)
	])

# Checks for libraries.
AC_CHECK_LIB([m], [main])
AC_CHECK_LIB([ncurses], [main])
AC_CHECK_LIB([pthread], [main])

# Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([fcntl.h netinet/in.h stdlib.h string.h sys/file.h sys/ioctl.h sys/socket.h sys/time.h unistd.h ctype.h assert.h assert.h])

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


AS_IF([test "x$with_misdn" == xyes],[AC_MSG_NOTICE( Compiled with mISDN support )],[AC_MSG_NOTICE( Not compiled with mISDN support)])
AS_IF([test "x$with_gsm_bs" == xyes],[AC_MSG_NOTICE( Compiled with GSM network side support )],[AC_MSG_NOTICE( Not compiled with GSM network side support)])
AS_IF([test "x$with_gsm_ms" == xyes],[AC_MSG_NOTICE( Compiled with GSM mobile side support )],[AC_MSG_NOTICE( Not compiled with GSM mobile side support)])
AS_IF([test "x$with_asterisk" == xyes],[AC_MSG_NOTICE( Compiled with Asterisk channel driver support )],[AC_MSG_NOTICE( Not compiled with Asterisk channel driver support)])
AS_IF([test "x$with_ss5" == xyes],[AC_MSG_NOTICE( Compiled with CCITT No.5 support )],[AC_MSG_NOTICE( Not compiled with CCITT No.5 support)])
AS_IF([test "x$with_sip" == xyes],[AC_MSG_NOTICE( Compiled with SIP support )],[AC_MSG_NOTICE( Not compiled with SIP support)])

