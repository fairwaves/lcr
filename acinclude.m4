


dnl ------------------------------------------------------------------------
dnl create a small program to test the settings of mISDNuser package
dnl ------------------------------------------------------------------------
# ported from the KDE project
AC_DEFUN([PBX_PRINT_MISDNUSER_PROGRAM],
[
cat > conftest.$ac_ext <<EOF
#ifdef __cplusplus
extern "C" {
#endif
#include <isdn_net.h>
#include <mISDNlib.h>
#ifdef __cplusplus
}
#endif

int main()
{
	/* TODO: try to reference some function here */
	return 0;
}
EOF
])

dnl ------------------------------------------------------------------------
dnl create a small program to test the settings of mISDN package
dnl ------------------------------------------------------------------------
# ported from the KDE project
AC_DEFUN([PBX_PRINT_MISDNKERNEL_PROGRAM],
[
cat > conftest.$ac_ext <<EOF
#ifdef __cplusplus
extern "C" {
#endif
#include <mISDNlib.h>
#include <linux/isdn_compat.h>
#include <linux/mISDNif.h>
#ifdef __cplusplus
}
#endif

#define MISDN_MAJOR_VERSION_TEST $misdn_kernel_major_version_demand
#define MISDN_MINOR_VERSION_TEST $misdn_kernel_minor_version_demand
#define MISDN_VERSION_TEST	((MISDN_MAJOR_VERSION_TEST<<16) | MISDN_MINOR_VERSION_TEST)
//#define MISDN_VERSION           ((MISDN_MAJOR_VERSION<<16) | MISDN_MINOR_VERSION)

#if (MISDN_VERSION < MISDN_VERSION_TEST)
# error mISDN version mismatch. Need at least $misdn_kernel_major_version_demand.$misdn_kernel_minor_version_demand
#endif


int main()
{
	static u16 sum1;
	/* access some constants from misdn kernel headers */
	u16 v1 = CMX_TXDATA_ON;
	u16 v2 = CMX_TXDATA_OFF;
	u16 v3 = CMX_DELAY;
	u16 v4 = CMX_TX_DATA;
	u16 v5 = CMX_JITTER;
	
	/* extend here more variables or function call
	 * to access more header information */
	 
	
	/* anti optimisation constuct */
	sum1 = v1 + v2 + v3 + v4 + v5;
	
	return 0;
}
EOF
])



dnl prints a program.
dnl $1 is the include file
dnl $2 is the prefix before include
dnl $3 is the suffix, e.g. some variable or class definitions
dnl $4 is the program body within main()
AC_DEFUN([PBX_PRINT_PROGRAM],
[AC_LANG_PROGRAM([$2
$1

$3],
[$4])
])



dnl ------------------------------------------------------------------------
dnl if --enable-debug, it disables optimisation and enables debugging symbols
dnl if --disable-debug (default) other way around
dnl ------------------------------------------------------------------------
AC_DEFUN([AC_CHECK_COMPILERS],
[# taken from KDE project

  # enable debugging options on demand
  DEBUGGING=
  AC_ARG_ENABLE([debug],
                [AS_HELP_STRING([--enable-debug],
                                [enable debugging symbols, turns off compiler optimisations (default=disable)])
                ],
                [use_debug_code="yes"],
                [use_debug_code="no"]
               )
  AM_CONDITIONAL(ENABLE_DEBUGGING, test "x$DEBUGGING" == "xyes" )
  AC_ARG_ENABLE(dummyoption,
                AC_HELP_STRING([--disable-debug],
                               [disables debugging symbols, turns on compiler optimisations]),
                [:],
                [:]
               )

  dnl this prevents stupid AC_PROG_CC to add "-g" to the default CFLAGS
  CFLAGS=" $CFLAGS"
  AC_PROG_CC
  AC_PROG_CPP
  
  if test "$GCC" = "yes" ; then
    if test "$use_debug_code" != "no"; then
      CFLAGS="-g -O0 $CFLAGS"
    else
      CFLAGS="-O2 $CFLAGS"
    fi
  fi
  
  if test -z "$LDFLAGS" && test "$use_debug_code" = "no" && test "$GCC" = "yes"; then
    LDFLAGS=""
  fi
  
  CXXFLAGS=" $CXXFLAGS"
  AC_PROG_CXX
  
  if test "$GXX" = "yes" ; then
    if test "$use_debug_code" != "no"; then
      CXXFLAGS="-g -O0 $CXXFLAGS"
    else
      CXXFLAGS="-O2 $CXXFLAGS"
    fi
  fi
  
])


dnl ------------------------------------------------------------------------
dnl Try to find the mISDN-user headers and libraries.
dnl $(MISDN_LDFLAGS) will be -Lmisdnliblocation (if needed)
dnl and $(MISDN_INCLUDES) will be -Imisdnhdrlocation (if needed)
dnl ------------------------------------------------------------------------
dnl
AC_DEFUN([AC_PATH_MISDNUSER],
[
dnl AC_REQUIRE([K_PATH_X])
dnl AC_REQUIRE([KDE_USE_MISDN])
dnl AC_REQUIRE([KDE_CHECK_LIB64])

dnl ------------------------------------------------------------------------
dnl Add configure flag to enable linking to MT version of mISDN-user library.
dnl ------------------------------------------------------------------------

dnl AC_ARG_ENABLE(
dnl   mt,
dnl   AC_HELP_STRING([--disable-mt],[link to non-threaded mISDN-user (deprecated)]),
dnl   pbx_use_misdn_mt=$enableval,
dnl   [
dnl     if test $pbx_misdnver = 3; then
dnl       pbx_use_misdn_mt=yes
dnl     else
dnl       pbx_use_misdn_mt=no
dnl     fi
dnl   ]
dnl )

dnl USING_MISDN_MT=""

dnl ------------------------------------------------------------------------
dnl If we not get --disable-misdn-mt then adjust some vars for the host.
dnl ------------------------------------------------------------------------

dnl KDE_MT_LDFLAGS=
dnl KDE_MT_LIBS=
dnl if test "x$pbx_use_misdn_mt" = "xyes"; then
dnl   KDE_CHECK_THREADING
dnl   if test "x$pbx_use_threading" = "xyes"; then
dnl     CPPFLAGS="$USE_THREADS -DMISDN_THREAD_SUPPORT $CPPFLAGS"
dnl     KDE_MT_LDFLAGS="$USE_THREADS"
dnl     KDE_MT_LIBS="$LIBPTHREAD"
dnl   else
dnl     pbx_use_misdn_mt=no
dnl   fi
dnl fi
dnl AC_SUBST(KDE_MT_LDFLAGS)
dnl AC_SUBST(KDE_MT_LIBS)

dnl pbx_misdn_was_given=yes

dnl ------------------------------------------------------------------------
dnl If we haven't been told how to link to mISDN-user, we work it out for ourselves.
dnl ------------------------------------------------------------------------
dnl if test -z "$LIBMISDN_GLOB"; then
dnl   if test "x$pbx_use_misdn_emb" = "xyes"; then
dnl     LIBMISDN_GLOB="libmisdne.*"
dnl   else
dnl     LIBMISDN_GLOB="libmisdn.*"
dnl   fi
dnl fi

dnl ------------------------------------------------------------
dnl If we got --enable-embedded then adjust the mISDN-user library name.
dnl ------------------------------------------------------------
dnl if test "x$pbx_use_misdn_emb" = "xyes"; then
dnl   misdnlib="misdne"
dnl else
   misdnlib="mISDN"
dnl fi

dnl pbx_int_misdn="-l$misdnlib"

dnl if test -z "$LIBQPE"; then
dnl ------------------------------------------------------------
dnl If we got --enable-palmtop then add -lqpe to the link line
dnl ------------------------------------------------------------
dnl   if test "x$pbx_use_misdn_emb" = "xyes"; then
dnl     if test "x$pbx_use_misdn_emb_palm" = "xyes"; then
dnl       LIB_QPE="-lqpe"
dnl     else
dnl       LIB_QPE=""
dnl     fi
dnl   else
dnl     LIB_QPE=""
dnl   fi
dnl fi

dnl ------------------------------------------------------------------------
dnl If we got --enable-misdn-mt then adjust the mISDN-user library name for the host.
dnl ------------------------------------------------------------------------

dnl if test "x$pbx_use_misdn_mt" = "xyes"; then
dnl   LIBMISDN="-l$misdnlib-mt"
dnl   pbx_int_misdn="-l$misdnlib-mt"
dnl   LIBMISDN_GLOB="lib$misdnlib-mt.*"
dnl   USING_MISDN_MT="using -mt"
dnl else
dnl   LIBMISDN="-l$misdnlib"
dnl fi

dnl if test $pbx_misdnver != 1; then

dnl   AC_REQUIRE([AC_FIND_PNG])
dnl   AC_REQUIRE([AC_FIND_JPEG])
dnl   LIBMISDN="$LIBMISDN $LIBPNG $LIBJPEG"
dnl fi

dnl if test $pbx_misdnver = 3; then
dnl   AC_REQUIRE([KDE_CHECK_LIBDL])
dnl   LIBMISDN="$LIBMISDN $LIBDL"
dnl fi

dnl probably there could be a whole installation of misdn
pbx_misdn_dirs="/usr/local/mISDNuser /usr/local/misdn /usr/lib/mISDNuser /usr/lib/misdn"


AC_MSG_CHECKING([for mISDN-user])
LIBMISDN="-l$misdnlib -lisdnnet"

dnl if test "x$pbx_use_misdn_emb" != "xyes" && test "x$pbx_use_misdn_mac" != "xyes"; then
dnl LIBMISDN="$LIBMISDN $X_PRE_LIBS -lXext -lX11 $LIBSM $LIBSOCKET"
dnl fi
ac_misdnuser_includes=NO ac_misdnuser_libraries=NO ac_misdnuser_bindir=NO
misdnuser_libraries=""
misdnuser_includes=""
AC_ARG_WITH(misdnuser-dir,
    AC_HELP_STRING([--with-misdnuser-dir=DIR],[where the root of mISDN-user is installed]),
    [  ac_misdnuser_includes="$withval"/include
       ac_misdnuser_libraries="$withval"/lib
       ac_misdnuser_bindir="$withval"/bin
    ])

AC_ARG_WITH(misdnuser-includes,
    AC_HELP_STRING([--with-misdnuser-includes=DIR],[where the mISDN-user includes are (default=$with-misdnuser-dir/include)]),
    [
       ac_misdnuser_includes="$withval"
    ])

pbx_misdn_libs_given=no

AC_ARG_WITH(misdnuser-libraries,
    AC_HELP_STRING([--with-misdnuser-libraries=DIR],[where the mISDN-user library is installed (default=$with-misdnuser-dir/lib)]),
    [  ac_misdnuser_libraries="$withval"
       pbx_misdn_libs_given=yes
    ])

AC_CACHE_VAL(ac_cv_have_misdn,
[#try to guess mISDN-user locations

misdn_incdirs=""
for dir in $pbx_misdn_dirs; do
   misdn_incdirs="$misdn_incdirs $dir/include $dir"
done
misdn_incdirs="$MISDNINC $misdn_incdirs /usr/include/mISDNuser /usr/include/misdn /usr/include"
if test ! "$ac_misdnuser_includes" = "NO"; then
   misdn_incdirs="$ac_misdnuser_includes $misdn_incdirs"
fi

dnl if test "$pbx_misdnver" != "1"; then
  pbx_misdn_header=mISDNlib.h
dnl  ->  und auch "isdn_net.h"
dnl else
dnl  pbx_misdn_header=qglobal.h
dnl fi

AC_FIND_FILE($pbx_misdn_header, $misdn_incdirs, misdn_incdir)
ac_misdnuser_includes="$misdn_incdir"

misdn_libdirs=""
for dir in $pbx_misdn_dirs; do
   misdn_libdirs="$misdn_libdirs $dir/lib $dir"
done
misdn_libdirs="$MISDNLIB $misdn_libdirs /usr/lib /usr/local/lib"
if test ! "$ac_misdnuser_libraries" = "NO"; then
  misdn_libdir=$ac_misdnuser_libraries
else
  misdn_libdirs="$ac_misdnuser_libraries $misdn_libdirs"
  # if the mISDN-user was given, the chance is too big that libmisdn.* doesn't exist
  misdn_libdir=NONE
  for dir in $misdn_libdirs; do
    try="ls -1 $dir/${LIBMISDN_GLOB}"
    if test -n "`$try 2> /dev/null`"; then misdn_libdir=$dir; break; else echo "tried $dir" >&AC_FD_CC ; fi
  done
fi
for a in $misdn_libdir/lib`echo ${pbx_int_misdn} | sed 's,^-l,,'`_incremental.*; do
  if test -e "$a"; then
    LIBMISDN="$LIBMISDN ${pbx_int_misdn}_incremental"
    break
  fi
done

ac_misdnuser_libraries="$misdn_libdir"

AC_LANG_SAVE
AC_LANG_CPLUSPLUS

ac_cxxflags_safe="$CXXFLAGS"
ac_ldflags_safe="$LDFLAGS"
ac_libs_safe="$LIBS"

CXXFLAGS="$CXXFLAGS -I$misdn_incdir $all_includes"
LDFLAGS="$LDFLAGS -L$misdn_libdir $all_libraries $USER_LDFLAGS"
LIBS="$LIBS $LIBMISDN"

PBX_PRINT_MISDNUSER_PROGRAM

if AC_TRY_EVAL(ac_link) && test -s conftest; then
  rm -f conftest*
else
  echo "configure: failed program was:" >&AC_FD_CC
  cat conftest.$ac_ext >&AC_FD_CC
  ac_misdnuser_libraries="NO"
fi
rm -f conftest*
CXXFLAGS="$ac_cxxflags_safe"
LDFLAGS="$ac_ldflags_safe"
LIBS="$ac_libs_safe"

AC_LANG_RESTORE
if test "$ac_misdnuser_includes" = NO || test "$ac_misdnuser_libraries" = NO; then
  ac_cv_have_misdn="have_misdn=no"
  ac_misdnuser_notfound=""
  missing_misdn_mt=""
  if test "$ac_misdnuser_includes" = NO; then
    if test "$ac_misdnuser_libraries" = NO; then
      ac_misdnuser_notfound="(headers and libraries)";
    else
      ac_misdnuser_notfound="(headers)";
    fi
  else
dnl     if test "x$pbx_use_misdn_mt" = "xyes"; then
dnl        missing_misdn_mt="Make sure that you have compiled mISDN-user with thread support!"
dnl        ac_misdnuser_notfound="(library $misdnlib-mt)";
dnl     else
       ac_misdnuser_notfound="(library $misdnlib)";
dnl     fi
  fi

  AC_MSG_ERROR([mISDN-user ($pbx_misdn_minversion) $ac_misdnuser_notfound not found. Please check your installation!
For more details about this problem, look at the end of config.log.$missing_misdn_mt])
else
  have_misdn="yes"
fi
])


eval "$ac_cv_have_misdn"

if test "$have_misdn" != yes; then
  AC_MSG_RESULT([$have_misdn]);
else
  ac_cv_have_misdn="have_misdn=yes \
    ac_misdnuser_includes=$ac_misdnuser_includes ac_misdnuser_libraries=$ac_misdnuser_libraries"
  AC_MSG_RESULT([libraries $ac_misdnuser_libraries, headers $ac_misdnuser_includes $USING_MISDN_MT])

  misdnuser_libraries="$ac_misdnuser_libraries"
  misdnuser_includes="$ac_misdnuser_includes"
fi

dnl if test ! "$pbx_misdn_libs_given" = "yes" && test ! "$pbx_misdnver" = 3; then
dnl      KDE_CHECK_MISDN_DIRECT(misdnuser_libraries= ,[])
dnl fi

AC_SUBST(misdnuser_libraries)
AC_SUBST(misdnuser_includes)

if test -z "$misdnuser_includes"; then
 MISDN_INCLUDES=""
else
 MISDN_INCLUDES="-I$misdnuser_includes"
 all_includes="$MISDN_INCLUDES $all_includes"
fi

if test -z "$misdnuser_libraries"; then
 MISDN_LDFLAGS=""
else
 MISDN_LDFLAGS="-L$misdnuser_libraries"
 all_libraries="$MISDN_LDFLAGS $all_libraries"
fi
dnl test -z "$KDE_MT_LDFLAGS" || all_libraries="$all_libraries $KDE_MT_LDFLAGS"

AC_SUBST(MISDN_INCLUDES)
AC_SUBST(MISDN_LDFLAGS)
dnl AC_PATH_MISDN_MOC_UIC

dnl KDE_CHECK_MISDN_JPEG

dnl if test "x$pbx_use_misdn_emb" != "xyes" && test "x$pbx_use_misdn_mac" != "xyes"; then
dnl LIB_MISDN="$pbx_int_misdn $LIBJPEG_MISDN "'$(LIBZ) $(LIBPNG) -lXext $(LIB_X11) $(LIBSM)'
dnl else
dnl LIB_MISDN="$pbx_int_misdn $LIBJPEG_MISDN "'$(LIBZ) $(LIBPNG)'
dnl fi
dnl test -z "$KDE_MT_LIBS" || LIB_MISDN="$LIB_MISDN $KDE_MT_LIBS"
dnl for a in $misdn_libdir/lib`echo ${pbx_int_misdn} | sed 's,^-l,,'`_incremental.*; do
dnl   if test -e "$a"; then
dnl      LIB_MISDN="$LIB_MISDN ${pbx_int_misdn}_incremental"
dnl      break
dnl   fi
dnl done

AC_SUBST(LIB_MISDN)
dnl AC_SUBST(LIB_QPE)

AC_SUBST(pbx_misdnver)

])



dnl ------------------------------------------------------------------------
dnl Try to find the mISDN-kernel headers.
dnl $(MISDNKERNEL_INCLUDES) will be -Imisdnkernelhdrlocation (if needed)
dnl ------------------------------------------------------------------------
dnl
AC_DEFUN([AC_PATH_MISDNKERNEL],
[

   misdnlib="mISDN"
misdn_kernel_major_version_demand=6
misdn_kernel_minor_version_demand=0
lcr_misdn_kernel_minversion=$misdn_kernel_major_version_demand.$misdn_kernel_minor_version_demand

dnl ## TODO !! convert misdnuser variables to misdn kernel header variables

dnl probably there could be a whole installation of misdn
kernel_ver=$(uname -r)
misdn_kernelheader_dirs="/lib/modules/$kernel_ver/source/include /usr/src/linux/include/"


AC_MSG_CHECKING([for mISDN-kernel])
dnl LIBMISDN="-l$misdnlib -lisdnnet"
dnl 
dnl if test "x$pbx_use_misdn_emb" != "xyes" && test "x$pbx_use_misdn_mac" != "xyes"; then
dnl LIBMISDN="$LIBMISDN $X_PRE_LIBS -lXext -lX11 $LIBSM $LIBSOCKET"
dnl fi
dnl ac_misdnkernel_includes=NO ac_misdnkernel_libraries=NO ac_misdnkernel_bindir=NO
ac_misdnkernel_includes=NO
dnl misdnkernel_libraries=""
misdnkernel_includes=""
dnl AC_ARG_WITH(misdnkernel-dir,
dnl     AC_HELP_STRING([--with-misdnkernel-dir=DIR],[where the mISDN-headers are installed ]),
dnl     [  ac_misdnkernel_includes="$withval"/include
dnl        ac_misdnkernel_libraries="$withval"/lib
dnl        ac_misdnkernel_bindir="$withval"/bin
dnl     ])

AC_ARG_WITH(misdnkernel-includes,
    AC_HELP_STRING([--with-misdnkernel-includes=DIR],[where the mISDN-kernel includes are. Used if enable-socket-misdn]),
    [
       ac_misdnkernel_includes="$withval"
    ])

dnl pbx_misdn_libs_given=no

dnl AC_ARG_WITH(misdnkernel-libraries,
dnl     AC_HELP_STRING([--with-misdnkernel-libraries=DIR],[where the mISDN-kernel library is installed.]),
dnl     [  ac_misdnkernel_libraries="$withval"
dnl        pbx_misdn_libs_given=yes
dnl     ])

AC_CACHE_VAL(ac_cv_have_misdnkernel,
[#try to guess mISDN-user locations

misdnkernel_incdirs=""
for dir in $misdn_kernelheader_dirs; do
   misdnkernel_incdirs="$misdnkernel_incdirs $dir/include $dir"
done
misdnkernel_incdirs="$MISDNKERNELINC $misdnkernel_incdirs /usr/include/mISDNuser /usr/include/misdn /usr/include"
if test ! "$ac_misdnkernel_includes" = "NO"; then
   misdnkernel_incdirs="$ac_misdnkernel_includes $misdnkernel_incdirs"
fi

  pbx_misdn_kernel_header=linux/mISDNif.h

AC_FIND_FILE($pbx_misdn_kernel_header, $misdnkernel_incdirs, misdnkernel_incdir)
ac_misdnkernel_includes="$misdnkernel_incdir"

dnl misdn_libdirs=""
dnl for dir in $misdn_kernelheader_dirs; do
dnl    misdn_libdirs="$misdn_libdirs $dir/lib $dir"
dnl done
dnl misdn_libdirs="$MISDNLIB $misdn_libdirs /usr/lib /usr/local/lib"
dnl if test ! "$ac_misdnkernel_libraries" = "NO"; then
dnl   misdn_libdir=$ac_misdnkernel_libraries
dnl else
dnl   misdn_libdirs="$ac_misdnkernel_libraries $misdn_libdirs"
dnl   # if the mISDN-user was given, the chance is too big that libmisdn.* doesn't exist
dnl   misdn_libdir=NONE
dnl   for dir in $misdn_libdirs; do
dnl     try="ls -1 $dir/${LIBMISDN_GLOB}"
dnl     if test -n "`$try 2> /dev/null`"; then misdn_libdir=$dir; break; else echo "tried $dir" >&AC_FD_CC ; fi
dnl   done
dnl fi
dnl for a in $misdn_libdir/lib`echo ${pbx_int_misdn} | sed 's,^-l,,'`_incremental.*; do
dnl   if test -e "$a"; then
dnl     LIBMISDN="$LIBMISDN ${pbx_int_misdn}_incremental"
dnl     break
dnl   fi
dnl done
dnl 
dnl ac_misdnkernel_libraries="$misdn_libdir"
dnl 
AC_LANG_SAVE
AC_LANG_CPLUSPLUS

ac_cxxflags_safe="$CXXFLAGS"
ac_ldflags_safe="$LDFLAGS"
ac_libs_safe="$LIBS"

CXXFLAGS="$CXXFLAGS -I$misdnkernel_incdir $all_includes"
LDFLAGS="$LDFLAGS -L$misdn_libdir $all_libraries $USER_LDFLAGS"
LIBS="$LIBS $LIBMISDN"

PBX_PRINT_MISDNKERNEL_PROGRAM

if AC_TRY_EVAL(ac_compile) && test -s conftest.o; then
  rm -f conftest*
else
  echo "configure: failed program was:" >&AC_FD_CC
  cat conftest.$ac_ext >&AC_FD_CC
  ac_misdnkernel_includes="NO"
dnl   ac_misdnkernel_libraries="NO"
fi
rm -f conftest*
CXXFLAGS="$ac_cxxflags_safe"
LDFLAGS="$ac_ldflags_safe"
LIBS="$ac_libs_safe"

AC_LANG_RESTORE
dnl if test "$ac_misdnkernel_includes" = NO || test "$ac_misdnkernel_libraries" = NO; then
if test "$ac_misdnkernel_includes" = NO; then
  ac_cv_have_misdnkernel="have_misdnkernel=no"
  ac_misdnkernel_notfound=""
  missing_misdn_mt=""
dnl  if test "$ac_misdnkernel_includes" = NO; then
dnl    if test "$ac_misdnkernel_libraries" = NO; then
dnl      ac_misdnkernel_notfound="(headers and libraries)";
dnl    else
      ac_misdnkernel_notfound="(headers)";
dnl    fi
dnl  else
dnl    ac_misdnkernel_notfound="(library $misdnlib)";
dnl  fi

  AC_MSG_ERROR([mISDN kernel header (version >= $lcr_misdn_kernel_minversion) not found. Please check your installation!
For more details about this problem, look at the end of config.log.$missing_misdn_mt])
else
  have_misdnkernel="yes"
fi
])

dnl check cache content
dnl TODO: maybe move this to beginning (before compilation test?)
eval "$ac_cv_have_misdnkernel"

if test "$have_misdnkernel" != yes; then
  AC_MSG_RESULT([$have_misdnkernel]);
else
  ac_cv_have_misdnkernel="have_misdnkernel=yes \
   ac_misdnkernel_includes=$ac_misdnkernel_includes"
dnl    ac_misdnkernel_libraries=$ac_misdnkernel_libraries"
  AC_MSG_RESULT([headers $ac_misdnkernel_includes])

dnl   misdnkernel_libraries="$ac_misdnkernel_libraries"
  misdnkernel_includes="$ac_misdnkernel_includes"
fi

dnl if test ! "$pbx_misdn_libs_given" = "yes" && test ! "$pbx_misdnver" = 3; then
dnl      KDE_CHECK_MISDN_DIRECT(misdnkernel_libraries= ,[])
dnl fi

dnl AC_SUBST(misdnkernel_libraries)
AC_SUBST(misdnkernel_includes)

if test -z "$misdnkernel_includes"; then
 MISDNKERNEL_INCLUDES=""
else
 MISDNKERNEL_INCLUDES="-I$misdnkernel_includes"
 all_includes="$MISDNKERNEL_INCLUDES $all_includes"
fi

dnl if test -z "$misdnkernel_libraries"; then
dnl  MISDN_LDFLAGS=""
dnl else
dnl  MISDN_LDFLAGS="-L$misdnkernel_libraries"
dnl  all_libraries="$MISDN_LDFLAGS $all_libraries"
dnl fi
dnl test -z "$KDE_MT_LDFLAGS" || all_libraries="$all_libraries $KDE_MT_LDFLAGS"

AC_SUBST(MISDNKERNEL_INCLUDES)
dnl AC_SUBST(MISDN_LDFLAGS)
dnl AC_PATH_MISDN_MOC_UIC

dnl KDE_CHECK_MISDN_JPEG

dnl if test "x$pbx_use_misdn_emb" != "xyes" && test "x$pbx_use_misdn_mac" != "xyes"; then
dnl LIB_MISDN="$pbx_int_misdn $LIBJPEG_MISDN "'$(LIBZ) $(LIBPNG) -lXext $(LIB_X11) $(LIBSM)'
dnl else
dnl LIB_MISDN="$pbx_int_misdn $LIBJPEG_MISDN "'$(LIBZ) $(LIBPNG)'
dnl fi
dnl test -z "$KDE_MT_LIBS" || LIB_MISDN="$LIB_MISDN $KDE_MT_LIBS"
dnl for a in $misdn_libdir/lib`echo ${pbx_int_misdn} | sed 's,^-l,,'`_incremental.*; do
dnl   if test -e "$a"; then
dnl      LIB_MISDN="$LIB_MISDN ${pbx_int_misdn}_incremental"
dnl      break
dnl   fi
dnl done

dnl AC_SUBST(LIB_MISDN)
dnl AC_SUBST(LIB_QPE)

AC_SUBST(pbx_misdnver)

])



dnl ------------------------------------------------------------------------
dnl Find a file (or one of more files in a list of dirs)
dnl ------------------------------------------------------------------------
AC_DEFUN([AC_FIND_FILE],
[
$3=NO
for i in $2;
do
  for j in $1;
  do
    echo "configure: __oline__: $i/$j" >&AC_FD_CC
    if test -r "$i/$j"; then
      echo "taking that" >&AC_FD_CC
      $3=$i
      break 2
    fi
  done
done
])



dnl ------------------------------------------------------------------------
dnl Taken from http://autoconf-archive.cryp.to/ac_define_dir.html
dnl Copyright © 2006 Stepan Kasal <kasal@ucw.cz>
dnl Copyright © 2006 Andreas Schwab <schwab@suse.de>
dnl Copyright © 2006 Guido U. Draheim <guidod@gmx.de>
dnl Copyright © 2006 Alexandre Oliva
dnl Copying and distribution of this file, with or without modification, are permitted in any medium without royalty provided the copyright notice and this notice are preserved.
dnl 
dnl This macro sets VARNAME to the expansion of the DIR variable, taking care
dnl  of fixing up ${prefix} and such.
dnl ------------------------------------------------------------------------
AC_DEFUN([AC_DEFINE_DIR], [
  prefix_NONE=
  exec_prefix_NONE=
  test "x$prefix" = xNONE && prefix_NONE=yes && prefix=$ac_default_prefix
  test "x$exec_prefix" = xNONE && exec_prefix_NONE=yes && exec_prefix=$prefix
dnl In Autoconf 2.60, ${datadir} refers to ${datarootdir}, which in turn
dnl refers to ${prefix}.  Thus we have to use `eval' twice.
  eval ac_define_dir="\"[$]$2\""
  eval ac_define_dir="\"$ac_define_dir\""
  AC_SUBST($1, "$ac_define_dir")
  AC_DEFINE_UNQUOTED($1, "$ac_define_dir", [$3])
  test "$prefix_NONE" && prefix=NONE
  test "$exec_prefix_NONE" && exec_prefix=NONE
])


dnl ------------------------------------------------------------------------
dnl taken from http://autoconf-archive.cryp.to/ax_ext_check_header.html
dnl Copyright © 2005 Duncan Simpson <dps@simpson.demon.co.uk>
dnl Copying and distribution of this file, with or without modification, are permitted in any medium without royalty provided the copyright notice and this notice are preserved.
dnl 
dnl Checks locations of headers in various places
dnl Extended by Jörg Habenicht
dnl ------------------------------------------------------------------------
AC_DEFUN([AX_EXT_HAVE_HEADER],
[AC_LANG_PUSH(C)
 AC_CHECK_HEADER($1, [$3 got="yes"], [$4 got="no"], [$5])
 hdr=`echo $1 | $as_tr_sh`
 for dir in $2
 do
  if test "x${got}" = "xno"; then
   ext_hashdr_cvdir=`echo $dir | $as_tr_sh`
   AC_CACHE_CHECK([for $1 header with -I$dir],
    [ext_cv${ext_hashdr_cvdir}_hashdr_${hdr}],
    [ext_have_hdr_save_cflags=${CFLAGS}
     CFLAGS="${CFLAGS} -I${dir}"
     AC_COMPILE_IFELSE(
      [AC_LANG_PROGRAM([#include <$1>])],
      [got="yes"; eval "ext_cv${ext_hashdr_cvdir}_hashdr_${hdr}"="yes"],
      [got="no"; eval "ext_cv${ext_hashdr_cvdir}_hashdr_${hdr}"="no"])
     CFLAGS=$ext_have_hdr_save_cflags])
   if eval `echo 'test x${'ext_cv${ext_hashdr_cvdir}_hashdr_${hdr}'}' = "xyes"`; then
    CFLAGS="${CFLAGS} -I${dir}"
    CPPFLAGS="${CPPFLAGS} -I${dir}"
    got="yes";
    hdr=`echo $1 | $as_tr_cpp`
    AC_DEFINE_UNQUOTED(HAVE_${hdr}, 1,
     [Define this if you have the $1 header])
   fi; 
  fi; 
 done
AC_LANG_POP])


dnl ------------------------------------------------------------------------
dnl taken from http://autoconf-archive.cryp.to/ax_ext_check_header.html
dnl Copyright © 2005 Duncan Simpson <dps@simpson.demon.co.uk>
dnl Copying and distribution of this file, with or without modification, are permitted in any medium without royalty provided the copyright notice and this notice are preserved.
dnl 
dnl Checks locations of headers in various places
dnl Extended by Jörg Habenicht
dnl ------------------------------------------------------------------------
dnl $1 include file name
dnl $2 directory list
dnl $3 action yes
dnl $4 action no
dnl $5 include prefix
dnl $6 include suffix
dnl $7 main() body content
AC_DEFUN([PBX_EXT_HAVE_CXX_HEADER],
[AC_LANG_PUSH(C++)
 AC_CHECK_HEADER($1, [$3 got="yes"], [$4 got="no"], [$5])
 hdr=`echo $1 | $as_tr_sh`
 for dir in $2 ;  do
  if test "x${got}" = "xno"; then
   ext_hashdr_cvdir=`echo $dir | $as_tr_sh`
   AC_CACHE_CHECK([for $1 header with -I$dir],
    [ext_cv${ext_hashdr_cvdir}_hashdr_${hdr}],
    [ext_have_hdr_save_cxxflags=${CXXFLAGS}
     CXXFLAGS="${CXXFLAGS} -I${dir}"
     AC_COMPILE_IFELSE(
      [PBX_PRINT_PROGRAM([#include <$1>],[$5],[$6],[$7])],
      [$3 got="yes"; eval "ext_cv${ext_hashdr_cvdir}_hashdr_${hdr}"="yes"],
      [$4 got="no"; eval "ext_cv${ext_hashdr_cvdir}_hashdr_${hdr}"="no"])
     CXXFLAGS=$ext_have_hdr_save_cxxflags])
   if eval `echo 'test x${'ext_cv${ext_hashdr_cvdir}_hashdr_${hdr}'}' = "xyes"`; then
    CXXFLAGS="${CXXFLAGS} -I${dir}"
    CPPFLAGS="${CPPFLAGS} -I${dir}"
    got="yes";
    hdr=`echo $1 | $as_tr_cpp`
    AC_DEFINE_UNQUOTED(HAVE_${hdr}, 1,
     [Define this if you have the $1 header])
   fi; 
  fi; 
 done
AC_LANG_POP])

