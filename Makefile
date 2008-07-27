#*****************************************************************************\
#*                                                                           **
#* Linux Call Router                                                         **
#*                                                                           **
#*---------------------------------------------------------------------------**
#* Copyright: Andreas Eversberg                                              **
#*                                                                           **
#* Makefile                                                                  **
#*                                                                           **
#*****************************************************************************/ 

WITH-CRYPTO = 42 # comment this out, if no libcrypto should be used
WITH-ASTERISK = 42 # comment this out, if you don't require built-in Asterisk channel driver.
# note: check your location and the names of libraries.

# select location to install
INSTALL_BIN = /usr/local/bin
INSTALL_CHAN = /usr/lib/asterisk/modules
INSTALL_DATA = /usr/local/lcr

LIBS += -lmisdn -lpthread
CHANLIBS += -lmISDN

# give location of the curses or ncurses library
CURSES = -lncurses

CC = gcc
PP = g++
WIZZARD = ./wizzard
LCR = ./lcr
LCRADMIN = ./lcradmin
CFLAGS_LCRADMIN = -DINSTALL_DATA=\"$(INSTALL_DATA)\"
ifdef WITH-ASTERISK
CHAN_LCR = ./chan_lcr.so
endif
LCRWATCH = ./lcrwatch
GEN = ./gentones
GENW = ./genwave
GENRC = ./genrc
GENEXT = ./genextension
CFLAGS = -Wall -g -DINSTALL_DATA=\"$(INSTALL_DATA)\"
CFLAGS += -I/usr/include/mISDNuser
#CFLAGS = -Wall -g -DINSTALL_DATA=\"$(INSTALL_DATA)\"
ifdef WITH-CRYPTO
CFLAGS += -DCRYPTO
endif
ifdef WITH-CRYPTO
LIBDIR += -L/usr/local/ssl/lib
CFLAGS += -I/usr/local/ssl/include
#LIBS += -lcrypto
LIBS += /usr/local/ssl/lib/libcrypto.a
endif

#all:
#	@echo Note that this version is a beta release. It is only for testing purpose.
#	@echo Please report any bug. To compile use \"make beta\".
#	@exit

all: $(CHAN_LCR) $(LCR) $(LCRADMIN) $(GEN) $(GENW) $(GENRC) $(GENEXT)
	@sh -c 'grep -n strcpy *.c* --exclude chan_lcr.c --exclude bchannel.c --exclude callerid.c ; if test $$''? = 0 ; then echo "dont use strcpy, use makro instead." ; exit -1 ; fi'
	@sh -c 'grep -n strncpy *.c* --exclude chan_lcr.c --exclude bchannel.c --exclude callerid.c ; if test $$''? = 0 ; then echo "dont use strncpy, use makro instead." ; exit -1 ; fi'
	@sh -c 'grep -n strcat *.c* --exclude chan_lcr.c --exclude bchannel.c --exclude callerid.c ; if test $$''? = 0 ; then echo "dont use strcat, use makro instead." ; exit -1 ; fi'
	@sh -c 'grep -n strncat *.c* --exclude chan_lcr.c --exclude bchannel.c --exclude callerid.c ; if test $$''? = 0 ; then echo "dont use strncat, use makro instead." ; exit -1 ; fi'
	@sh -c 'grep -n sprintf *.c* --exclude chan_lcr.c --exclude bchannel.c --exclude callerid.c ; if test $$''? = 0 ; then echo "dont use sprintf, use makro instead." ; exit -1 ; fi'
	@sh -c 'grep -n snprintf *.c* --exclude chan_lcr.c --exclude bchannel.c --exclude callerid.c ; if test $$''? = 0 ; then echo "dont use snprintf, use makro instead." ; exit -1 ; fi'
	@echo "All LCR binaries done"
	@exit

main.o: main.c *.h Makefile
	$(PP) -c $(CFLAGS) main.c -o main.o

message.o: message.c *.h Makefile
	$(PP) -c $(CFLAGS) message.c -o message.o

options.o: options.c *.h Makefile
	$(CC) -c $(CFLAGS) options.c -o options.o
options.ooo: options.c *.h Makefile
	$(PP) -c $(CFLAGS) options.c -o options.ooo

interface.o: interface.c *.h Makefile
	$(PP) -c $(CFLAGS) interface.c -o interface.o

extension.o: extension.c *.h Makefile
	$(PP) -c $(CFLAGS) extension.c -o extension.o

route.o: route.c *.h Makefile
	$(PP) -c $(CFLAGS) route.c -o route.o

port.o: port.cpp *.h Makefile
	$(PP) -c $(CFLAGS) port.cpp -o port.o

mISDN.o: mISDN.cpp *.h Makefile
	$(PP) -c $(CFLAGS) mISDN.cpp -o mISDN.o

dss1.o: dss1.cpp ie.cpp *.h Makefile
	$(PP) -c $(CFLAGS) dss1.cpp -o dss1.o

#knock.o: knock.cpp *.h Makefile
#	$(PP) -c $(CFLAGS) knock.cpp -o knock.o
#
vbox.o: vbox.cpp *.h Makefile
	$(PP) -c $(CFLAGS) vbox.cpp -o vbox.o

mail.o: mail.c *.h Makefile
	$(PP) -c $(CFLAGS) mail.c -o mail.o

action.o: action.cpp *.h Makefile
	$(PP) -c $(CFLAGS) action.cpp -o action.o

action_vbox.o: action_vbox.cpp *.h Makefile
	$(PP) -c $(CFLAGS) action_vbox.cpp -o action_vbox.o

action_efi.o: action_efi.cpp *.h Makefile
	$(PP) -c $(CFLAGS) action_efi.cpp -o action_efi.o

endpoint.o: endpoint.cpp *.h Makefile
	$(PP) -c $(CFLAGS) endpoint.cpp -o endpoint.o

endpointapp.o: endpointapp.cpp *.h Makefile
	$(PP) -c $(CFLAGS) endpointapp.cpp -o endpointapp.o

apppbx.o: apppbx.cpp *.h Makefile
	$(PP) -c $(CFLAGS) apppbx.cpp -o apppbx.o

callerid.o: callerid.c *.h Makefile
	$(CC) -c $(CFLAGS) callerid.c -o callerid.o
callerid.ooo: callerid.c *.h Makefile
	$(PP) -c $(CFLAGS) callerid.c -o callerid.ooo

join.o: join.cpp *.h Makefile
	$(PP) -c $(CFLAGS) join.cpp -o join.o

joinpbx.o: joinpbx.cpp *.h Makefile
	$(PP) -c $(CFLAGS) joinpbx.cpp -o joinpbx.o

joinremote.o: joinremote.cpp *.h Makefile
	$(PP) -c $(CFLAGS) joinremote.cpp -o joinremote.o

cause.o: cause.c *.h Makefile
	$(PP) -c $(CFLAGS) cause.c -o cause.o

alawulaw.o: alawulaw.c *.h Makefile
	$(PP) -c $(CFLAGS) alawulaw.c -o alawulaw.o

tones.o: tones.c *.h Makefile
	$(PP) -c $(CFLAGS) tones.c -o tones.o

crypt.o: crypt.cpp *.h Makefile
	$(PP) -c $(CFLAGS) crypt.cpp -o crypt.o

genext.o: genext.c *.h Makefile
	$(PP) -c $(CFLAGS) genext.c -o genext.o

socket_server.o: socket_server.c *.h Makefile
	$(PP) -c $(CFLAGS) socket_server.c -o socket_server.o

trace.o: trace.c *.h Makefile
	$(PP) -c $(CFLAGS) trace.c -o trace.o

chan_lcr.o: chan_lcr.c *.h Makefile
	$(CC) -D_GNU_SOURCE -c $(CFLAGS) chan_lcr.c -o chan_lcr.o

bchannel.o: bchannel.c *.h Makefile
	$(CC) -D_GNU_SOURCE -c $(CFLAGS) bchannel.c -o bchannel.o


#$(WIZZARD): wizzard.c Makefile
#	$(PP) $(LIBDIR) $(CFLAGS) -lm wizzard.c \
#	-o $(WIZZARD) 

$(LCR): main.o \
	options.ooo \
	interface.o \
	extension.o \
	cause.o \
	alawulaw.o \
	tones.o \
	message.o \
	route.o \
	port.o \
	mISDN.o \
	dss1.o \
	vbox.o \
	endpoint.o \
	endpointapp.o \
	apppbx.o \
	callerid.ooo \
	crypt.o \
	action.o \
	action_vbox.o \
	action_efi.o \
	mail.o \
	join.o \
	joinpbx.o \
	joinremote.o \
	socket_server.o \
	trace.o
	$(PP) $(LIBDIR) \
	main.o \
	options.ooo \
	interface.o \
	extension.o \
	cause.o \
	alawulaw.o \
	tones.o \
	message.o \
	route.o \
	port.o \
	mISDN.o \
	dss1.o \
	vbox.o \
	endpoint.o \
	endpointapp.o \
	apppbx.o \
	callerid.ooo \
	crypt.o \
	action.o \
	action_vbox.o \
	action_efi.o \
	mail.o \
	join.o \
	joinpbx.o \
	joinremote.o \
	socket_server.o \
	trace.o \
	$(LIBS) -o $(LCR) 

$(LCRADMIN): lcradmin.c cause.c *.h Makefile
	$(PP) $(LIBDIR) $(CFLAGS_LCRADMIN) $(CURSES) -lm lcradmin.c cause.c \
	-o $(LCRADMIN) 

$(CHAN_LCR): chan_lcr.o bchannel.o callerid.o options.o *.h Makefile
	$(CC) -shared -Xlinker -x $(LDFLAGS) -o $(CHAN_LCR) chan_lcr.o bchannel.o callerid.o options.o


$(LCRWATCH): watch.c *.h Makefile
	$(PP) $(LIBDIR) $(CFLAGS) -lm watch.c \
	-o $(LCRWATCH) 

$(GEN):	gentones.c *.h Makefile 
	$(PP) $(LIBDIR) $(CFLAGS) -lm gentones.c \
	-o $(GEN) 

$(GENW):genwave.c *.h Makefile 
	$(PP) $(LIBDIR) $(CFLAGS) -lm genwave.c \
	-o $(GENW) 

$(GENRC): genrc.c *.h Makefile
	$(PP) $(LIBDIR) $(CFLAGS) -lm genrc.c \
	-o $(GENRC) 

$(GENEXT): options.ooo extension.o genext.o
	$(PP) $(CFLAGS) options.ooo extension.o genext.o -o $(GENEXT) 

#install:
#	@echo Remember, this is a beta release. To overwrite your current installed
#	@echo version, use \"make beta_install\".
#	@exit

install:
	make
	cp $(LCR) $(INSTALL_BIN)
	cp $(LCRADMIN) $(INSTALL_BIN)
ifdef WITH-ASTERISK
	cp $(CHAN_LCR) $(INSTALL_CHAN)
endif
#	cp $(LCRWATCH) $(INSTALL_BIN)
	cp $(GEN) $(INSTALL_BIN)
	cp $(GENW) $(INSTALL_BIN)
	cp $(GENRC) $(INSTALL_BIN)
	cp $(GENEXT) $(INSTALL_BIN)
	mkdir -p $(INSTALL_DATA)
	mkdir -p $(INSTALL_DATA)/extensions
	@if test -a $(INSTALL_DATA)/options.conf ; then \
		echo "NOTE: options.conf already exists, not changed." ; else \
		cp -v default/options.conf $(INSTALL_DATA) ; fi
	@if test -a $(INSTALL_DATA)/interface.conf ; then \
		echo "NOTE: interface.conf already exists, not changed." ; else \
		cp -v default/interface.conf $(INSTALL_DATA) ; fi
	@if test -a $(INSTALL_DATA)/routing.conf ; then \
		echo "NOTE: routing.conf already exists, not changed." ; else \
		cp -v default/routing.conf $(INSTALL_DATA) ; fi
	@if test -a $(INSTALL_DATA)/numbering_int.conf ; then \
		echo "NOTE: numbering_int.conf is obsolete, please use routing." ; fi
	@if test -a $(INSTALL_DATA)/numbering_ext.conf ; then \
		echo "NOTE: numbering_ext.conf is obsolete, please use routing." ; fi
	@if test -a $(INSTALL_DATA)/directory.list ; then \
		echo "NOTE: directory.list already exists, not changed." ; else \
		cp -v default/directory.list $(INSTALL_DATA) ; fi
	@if test -a $(INSTALL_DATA)/tones_american ; then \
		echo "NOTE: american tones already exists, not overwritten." ; else \
		cp -a tones_american $(INSTALL_DATA) ; fi
	@if test -a $(INSTALL_DATA)/tones_german ; then \
		echo "NOTE: german tones already exists, not overwritten." ; else \
		cp -a tones_german $(INSTALL_DATA) ; fi
	@if test -a $(INSTALL_DATA)/vbox_german ; then \
		echo "NOTE: german vbox tones already exists, not overwritten." ; else \
		cp -a vbox_german $(INSTALL_DATA) ; fi
	@if test -a $(INSTALL_DATA)/vbox_english ; then \
		echo "NOTE: english vbox tones already exists, not overwritten." ; else \
		cp -a vbox_english $(INSTALL_DATA) ; fi
	@if test -a $(INSTALL_DATA)/tones_efi ; then \
		echo "NOTE: special efi tones already exists, not overwritten." ; else \
		cp -a tones_efi $(INSTALL_DATA) ; fi

clean:
	touch *
	rm -f $(LCR) $(LCRADMIN) $(CHAN_LCR) $(LCRWATCH) $(GEN) $(GENW) $(GENRC) $(GENEXT)
	rm -f *.o *.ooo
	rm -f .*.c.sw* .*.cpp.sw* .*.h.sw*
	rm -f bla nohup.out a.out
	rm -f debug*.log

tar:
	make clean
	cd .. &&  tar --exclude=.git -cvzf lcr_`date +%Y%m%d`.tar.gz lcr

start: $(LCR)
	$(LCR) start

fork: $(LCR)
	$(LCR) fork

snapshot: clean
	DIR=lcr-$$(date +"20%y_%m_%d") ; \
	mkdir -p /tmp/$$DIR ; \
	cp -a * /tmp/$$DIR ; \
	cd /tmp/; \
	tar czf $$DIR.tar.gz $$DIR


