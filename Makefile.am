##    Makefile.am -- Process this file with automake to produce Makefile.in

##    This file is part of linux-call-router
##    Copyright (C) 2007 Joerg Habenicht (j.habenicht@gmx.de)
##    Copyright (C) 2008 Peter Schlaile (peter -at- schlaile.de)
##    Copyright (C) 2008 Andreas Eversberg (andreas@eversberg.eu)

##    This program is free software; you can redistribute it and/or
##    modify it under the terms of the GNU General Public License as
##    published by the Free Software Foundation; either
##    version 2 of the License, or (at your option) any later version.

##    This program is distributed in the hope that it will be useful,
##    but WITHOUT ANY WARRANTY; without even the implied warranty of
##    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
##    GNU General Public License for more details.

##    You should have received a copy of the GNU General Public License
##    along with this library; see the file COPYING.  If not, write to
##    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
##    Boston, MA 02110-1301, USA.

INSTALLdir = $(DESTDIR)/usr/local/lcr

pkglogdir=$(localstatedir)/log/@PACKAGE@
pkgsysconfdir=$(sysconfdir)/@PACKAGE@

#CONFIGdir=$(DESTDIR)$(pkgsysconfdir)
#SHAREdir=$(DESTDIR)$(pkgdatadir)
#LOGdir=$(DESTDIR)$(pkglogdir)
#EXTENSIONdir=$(DESTDIR)$(localstatedir)/lib/lcr

CONFIGdir=$(INSTALLdir)
SHAREdir=$(INSTALLdir)
LOGdir=$(INSTALLdir)
EXTENSIONdir=$(INSTALLdir)/extensions

astmoddir = $(DESTDIR)/usr/lib/asterisk/modules

INSTALLATION_DEFINES = \
 -DCONFIG_DATA="\"$(CONFIGdir)\"" \
 -DSHARE_DATA="\"$(SHAREdir)\"" \
 -DLOG_DIR="\"$(LOGdir)\"" \
 -DEXTENSION_DATA="\"$(EXTENSIONdir)\""

GSM_INCLUDE =
GSM_SOURCE =
GSM_LIB =

if ENABLE_GSM

GSM_INCLUDE +=

GSM_SOURCE += gsm_audio.c gsm.cpp gsm_conf.c
  
GSM_LIB += /usr/lib/libgsm.a

#gsm_audio.po: gsm_audio.c gsm_audio.h
#	$(CC) -D_GNU_SOURCE -fPIC -c gsm_audio.c -o gsm_audio.po

endif

if ENABLE_GSM_BS

GSM_INCLUDE += -DWITH_GSM_BS -I./openbsc/include -I./libosmocore/include -I./openbsc

GSM_SOURCE += gsm_bs.cpp openbsc/src/bsc_init.c openbsc/src/bsc_vty.c openbsc/src/vty_interface_layer3.c openbsc/src/bsc_api.c openbsc/src/bsc_version.c

GSM_LIB +=  ./openbsc/src/libbsc.a ./openbsc/src/libmsc.a ./openbsc/src/libvty.a -losmovty -losmocore -ldbi -lcrypt

endif

if ENABLE_GSM_MS

GSM_INCLUDE += -DPACKAGE_VERSION=0 -DWITH_GSM_MS -I./layer23/include -I./libosmocore/include

GSM_SOURCE += gsm_ms.cpp layer23/src/mobile/app_mobile.c

GSM_LIB += ./layer23/src/mobile/libmobile.a \
	./layer23/src/common/liblayer23.a \
	./libosmocore/build-host/src/.libs/libosmocore.a \
	./libosmocore/build-host/src/vty/.libs/libosmovty.a \
	./libosmocore/build-host/src/codec/.libs/libosmocodec.a

endif

if ENABLE_SS5

SS5_INCLUDE = -DWITH_SS5

SS5_SOURCE = ss5.cpp ss5_encode.c ss5_decode.c

endif

bin_PROGRAMS = lcradmin gentones genwave

sbin_PROGRAMS = lcr genrc genextension

if ENABLE_ASTERISK_CHANNEL_DRIVER
noinst_PROGRAMS = chan_lcr.so
chan_lcr_so_SOURCES =
chan_lcr_so_LDFLAGS = -shared
chan_lcr_so_LDADD = chan_lcr.po bchannel.po options.po callerid.po select.po

chan_lcr.po: chan_lcr.c chan_lcr.h
	$(CC) $(INCLUDES) $(AST_CFLAGS) $(CPPFLAGS) $(CFLAGS) -D_GNU_SOURCE -fPIC -c chan_lcr.c -o chan_lcr.po

bchannel.po: bchannel.c bchannel.h
	$(CC) $(INCLUDES) -D_GNU_SOURCE $(CPPFLAGS) $(CFLAGS) -fPIC -c bchannel.c -o bchannel.po

callerid.po: callerid.c callerid.h
	$(CC) $(INCLUDES) -D_GNU_SOURCE $(CPPFLAGS) $(CFLAGS) -fPIC -c callerid.c -o callerid.po

options.po: options.c options.h
	$(CC) $(INCLUDES) -D_GNU_SOURCE $(CPPFLAGS) $(CFLAGS) -fPIC -c options.c -o options.po

select.po: select.c select.h
	$(CC) $(INCLUDES) -D_GNU_SOURCE $(CPPFLAGS) $(CFLAGS) -fPIC -c select.c -o select.po

install-exec-hook:
	mkdir -p $(astmoddir)
	$(INSTALL) -d $(astmoddir)
	$(INSTALL) chan_lcr.so $(astmoddir)
endif

INCLUDES = $(all_includes) $(GSM_INCLUDE) $(SS5_INCLUDE) -Wall $(INSTALLATION_DEFINES)

lcr_SOURCES = $(GSM_SOURCE) $(SS5_SOURCE) select.c action.cpp       mISDN.cpp        tones.c \
	action_efi.cpp   crypt.cpp        mail.c           trace.c \
	action_vbox.cpp  dss1.cpp         main.c           \
	vbox.cpp alawulaw.c       endpoint.cpp     interface.c     message.c \
	apppbx.cpp       endpointapp.cpp  join.cpp        options.c       \
	extension.c      joinpbx.cpp     port.cpp \
	callerid.c       joinremote.cpp  route.c \
	cause.c          socket_server.c

lcr_LDADD = $(LIBCRYPTO) -lmisdn -lpthread $(GSM_LIB)


lcradmin_SOURCES = lcradmin.c cause.c options.c
genextension_SOURCES = genext.c options.c extension.c

# Special install function to preserve existing configs.
# Optimization with equivalen results are welcome

install-data-hook:
	@sh -c 'grep -n strcpy *.c* --exclude chan_lcr.c --exclude bchannel.c --exclude callerid.c ; if test $$''? = 0 ; then echo "dont use strcpy, use makro instead." ; exit -1 ; fi'
	@sh -c 'grep -n strncpy *.c* --exclude chan_lcr.c --exclude bchannel.c --exclude callerid.c ; if test $$''? = 0 ; then echo "dont use strncpy, use makro instead." ; exit -1 ; fi'
	@sh -c 'grep -n strcat *.c* --exclude chan_lcr.c --exclude bchannel.c --exclude callerid.c ; if test $$''? = 0 ; then echo "dont use strcat, use makro instead." ; exit -1 ; fi'
	@sh -c 'grep -n strncat *.c* --exclude chan_lcr.c --exclude bchannel.c --exclude callerid.c ; if test $$''? = 0 ; then echo "dont use strncat, use makro instead." ; exit -1 ; fi'
	@sh -c 'grep -n sprintf *.c* --exclude chan_lcr.c --exclude bchannel.c --exclude callerid.c ; if test $$''? = 0 ; then echo "dont use sprintf, use makro instead." ; exit -1 ; fi'
	@sh -c 'grep -n snprintf *.c* --exclude chan_lcr.c --exclude bchannel.c --exclude callerid.c ; if test $$''? = 0 ; then echo "dont use snprintf, use makro instead." ; exit -1 ; fi'
	mkdir -p $(CONFIGdir)
	mkdir -p $(SHAREdir)
	mkdir -p $(LOGdir)
	mkdir -p $(EXTENSIONdir)
	@if test -a $(CONFIGdir)/options.conf ; then \
		echo "NOTE: options.conf already exists, not changed." ; else \
		cp -v default/options.conf $(CONFIGdir) ; fi
	@if test -a $(CONFIGdir)/interface.conf ; then \
		echo "NOTE: interface.conf already exists, not changed." ; else \
		cp -v default/interface.conf $(CONFIGdir) ; fi
	@if test -a $(CONFIGdir)/routing.conf ; then \
		echo "NOTE: routing.conf already exists, not changed." ; else \
		cp -v default/routing.conf $(CONFIGdir) ; fi
if ENABLE_GSM
	@if test -a $(CONFIGdir)/gsm.conf ; then \
		echo "NOTE: gsm.conf already exists, not changed." ; else \
		cp -v default/gsm.conf $(CONFIGdir) ; fi
	@if test -a $(CONFIGdir)/openbsc.cfg ; then \
		echo "NOTE: openbsc.cfg already exists, not changed." ; else \
		cp -v default/openbsc.cfg $(CONFIGdir) ; fi
endif
	@if test -a $(CONFIGdir)/numbering_int.conf ; then \
		echo "NOTE: numbering_int.conf is obsolete, please use routing." ; fi
	@if test -a $(CONFIGdir)/numbering_ext.conf ; then \
		echo "NOTE: numbering_ext.conf is obsolete, please use routing." ; fi
	@if test -a $(CONFIGdir)/directory.list ; then \
		echo "NOTE: directory.list already exists, not changed." ; else \
		cp -v default/directory.list $(CONFIGdir) ; fi
	@if test -a $(SHAREdir)/tones_american ; then \
		echo "NOTE: american tones already exists, not overwritten." ; else \
		cp -a tones_american $(SHAREdir) ; fi
	@if test -a $(SHAREdir)/tones_german ; then \
		echo "NOTE: german tones already exists, not overwritten." ; else \
		cp -a tones_german $(SHAREdir) ; fi
	@if test -a $(SHAREdir)/vbox_german ; then \
		echo "NOTE: german vbox tones already exists, not overwritten." ; else \
		cp -a vbox_german $(SHAREdir) ; fi
	@if test -a $(SHAREdir)/vbox_english ; then \
		echo "NOTE: english vbox tones already exists, not overwritten." ; else \
		cp -a vbox_english $(SHAREdir) ; fi
	@if test -a $(SHAREdir)/tones_efi ; then \
		echo "NOTE: special efi tones already exists, not overwritten." ; else \
		cp -a tones_efi $(SHAREdir) ; fi

clean-generic:
	rm -f *.po

# test rule, nothing important
#echo:
#	-echo $(all_libraries) >&2

# CAUTION: CRUDE CRUDE HACK !!
# This transforms all compiling and linking calls from gcc into g++
# This should better be removed and all .c files renamed to .cpp
COMPILE=$(CXXCOMPILE)
LINK=$(CXXLINK)


