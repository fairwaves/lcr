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

#INSTALLATION_DEFINES = \
# -DCONFIG_DATA="\"$(pkgsysconfdir)\"" \
# -DSHARE_DATA="\"$(pkgdatadir)\"" \
# -DLOG_DIR="\"$(pkglogdir)\"" \
# -DEXTENSION_DATA="\"$(localstatedir)/lib/lcr\""

INSTALLdir = /usr/local/lcr

astmoddir = /usr/lib/asterisk/modules

INSTALLATION_DEFINES = \
 -DCONFIG_DATA="\"$(INSTALLdir)\"" \
 -DSHARE_DATA="\"$(INSTALLdir)\"" \
 -DLOG_DIR="\"$(INSTALLdir)\"" \
 -DEXTENSION_DATA="\"$(INSTALLdir)/extensions\""

INCLUDES = $(all_includes) -I/usr/include/mISDNuser $(INSTALLATION_DEFINES) 


bin_PROGRAMS = lcradmin gentones genwave

if ENABLE_ASTERISK_CHANNEL_DRIVER
sbin_PROGRAMS = lcr genrc genextension chan_lcr.so
chan_lcr_so_SOURCES = 
chan_lcr_so_LDFLAGS = -shared
chan_lcr_so_LDADD = chan_lcr.o bchannel.o options.o

chan_lcr.o: chan_lcr.c chan_lcr.h
	$(CC) $(INCLUDES) -D_GNU_SOURCE -c chan_lcr.c

bchannel.o: bchannel.c bchannel.h
	$(CC) $(INCLUDES) -D_GNU_SOURCE -c bchannel.c

else
sbin_PROGRAMS = lcr genrc genextension 
endif

lcr_SOURCES = action.cpp       mISDN.cpp        tones.c \
	action_efi.cpp   crypt.cpp        mail.c           trace.c \
	action_vbox.cpp  dss1.cpp         main.c           \
	vbox.cpp alawulaw.c       endpoint.cpp     interface.c     message.c \
	apppbx.cpp       endpointapp.cpp  join.cpp        options.c       \
	extension.c      joinpbx.cpp     port.cpp \
	callerid.c       joinremote.cpp  route.c \
	cause.c          socket_server.c

lcr_LDADD = $(LIBCRYPTO) -lmisdn -lpthread

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
	mkdir -p $(INSTALLdir)
	mkdir -p $(INSTALLdir)/extensions
	@if test -a $(INSTALLdir)/options.conf ; then \
		echo "NOTE: options.conf already exists, not changed." ; else \
		cp -v default/options.conf $(INSTALLdir) ; fi
	@if test -a $(INSTALLdir)/interface.conf ; then \
		echo "NOTE: interface.conf already exists, not changed." ; else \
		cp -v default/interface.conf $(INSTALLdir) ; fi
	@if test -a $(INSTALLdir)/routing.conf ; then \
		echo "NOTE: routing.conf already exists, not changed." ; else \
		cp -v default/routing.conf $(INSTALLdir) ; fi
	@if test -a $(INSTALLdir)/numbering_int.conf ; then \
		echo "NOTE: numbering_int.conf is obsolete, please use routing." ; fi
	@if test -a $(INSTALLdir)/numbering_ext.conf ; then \
		echo "NOTE: numbering_ext.conf is obsolete, please use routing." ; fi
	@if test -a $(INSTALLdir)/directory.list ; then \
		echo "NOTE: directory.list already exists, not changed." ; else \
		cp -v default/directory.list $(INSTALLdir) ; fi
	@if test -a $(INSTALLdir)/tones_american ; then \
		echo "NOTE: american tones already exists, not overwritten." ; else \
		cp -a tones_american $(INSTALLdir) ; fi
	@if test -a $(INSTALLdir)/tones_german ; then \
		echo "NOTE: german tones already exists, not overwritten." ; else \
		cp -a tones_german $(INSTALLdir) ; fi
	@if test -a $(INSTALLdir)/vbox_german ; then \
		echo "NOTE: german vbox tones already exists, not overwritten." ; else \
		cp -a vbox_german $(INSTALLdir) ; fi
	@if test -a $(INSTALLdir)/vbox_english ; then \
		echo "NOTE: english vbox tones already exists, not overwritten." ; else \
		cp -a vbox_english $(INSTALLdir) ; fi
	@if test -a $(INSTALLdir)/tones_efi ; then \
		echo "NOTE: special efi tones already exists, not overwritten." ; else \
		cp -a tones_efi $(INSTALLdir) ; fi
if ENABLE_ASTERISK_CHANNEL_DRIVER
	@echo "Moving chan_lcr.so (if it fails, do it manually)"
	mv $(sbindir)/chan_lcr.so $(astmoddir)
endif

# test rule, nothing important
#echo:
#	-echo $(all_libraries) >&2

# CAUTION: CRUDE CRUDE HACK !!
# This transforms all compiling and linking calls from gcc into g++
# This should better be removed and all .c files renamed to .cpp
COMPILE=$(CXXCOMPILE)
LINK=$(CXXLINK)

pkglogdir=$(localstatedir)/log/@PACKAGE@
pkgsysconfdir=$(sysconfdir)/@PACKAGE@


