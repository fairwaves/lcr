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
#WITH-ASTERISK = 42 # comment this out, if you don't require built-in Asterisk channel driver.
#WITH-SOCKET = 42 # compile for socket based mISDN (
# note: check your location and the names of libraries.

# select location to install
INSTALL_BIN = /usr/local/bin
INSTALL_DATA = /usr/local/lcr

# give locations for the libraries
LINUX_INCLUDE = -I/usr/src/linux/include

# give location of the mISDN libraries
MISDNUSER_INCLUDE = -I../mISDNuser/include -I../mISDNuser/i4lnet
MISDNUSER_LIB = -L../mISDNuser/lib -L../mISDNuser/i4lnet
LIBS += -lisdnnet -lmISDN -lpthread

# give location of the curses or ncurses library
CURSES = -lncurses

CC = g++
LD = $(CC)
WIZZARD = ./wizzard
LCR = ./lcr
LCRADMIN = ./lcradmin
ifdef WITH-ASTERISK
CHAN_LCR = ./chan_lcr
endif
LCRWATCH = ./lcrwatch
GEN = ./gentones
GENW = ./genwave
GENRC = ./genrc
GENEXT = ./genextension
CFLAGS = -Wall -g -DINSTALL_DATA=\"$(INSTALL_DATA)\"
CFLAGS += $(LINUX_INCLUDE) $(MISDNUSER_INCLUDE)
ifdef WITH-CRYPTO
CFLAGS += -DCRYPTO
endif
ifdef WITH-SOCKET
CFLAGS += -DSOCKET_MISDN
endif
LIBDIR += $(MISDNUSER_LIB)
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

all: $(LCR) $(LCRADMIN) $(CHAN_LCR) $(GEN) $(GENW) $(GENRC) $(GENEXT)
	@sh -c 'grep -n strcpy *.c* ; if test $$''? = 0 ; then echo "dont use strcpy, use makro instead." ; exit -1 ; fi'
	@sh -c 'grep -n strncpy *.c* ; if test $$''? = 0 ; then echo "dont use strncpy, use makro instead." ; exit -1 ; fi'
	@sh -c 'grep -n strcat *.c* ; if test $$''? = 0 ; then echo "dont use strcat, use makro instead." ; exit -1 ; fi'
	@sh -c 'grep -n strncat *.c* ; if test $$''? = 0 ; then echo "dont use strncat, use makro instead." ; exit -1 ; fi'
	@sh -c 'grep -n sprintf *.c* ; if test $$''? = 0 ; then echo "dont use sprintf, use makro instead." ; exit -1 ; fi'
	@sh -c 'grep -n snprintf *.c* ; if test $$''? = 0 ; then echo "dont use snprintf, use makro instead." ; exit -1 ; fi'
	@echo "All LCR binaries done"
	@sync
	@exit

main.o: main.c *.h Makefile
	$(CC) -c $(CFLAGS) main.c -o main.o

message.o: message.c *.h Makefile
	$(CC) -c $(CFLAGS) message.c -o message.o

options.o: options.c *.h Makefile
	$(CC) -c $(CFLAGS) options.c -o options.o

interface.o: interface.c *.h Makefile
	$(CC) -c $(CFLAGS) interface.c -o interface.o

extension.o: extension.c *.h Makefile
	$(CC) -c $(CFLAGS) extension.c -o extension.o

route.o: route.c *.h Makefile
	$(CC) -c $(CFLAGS) route.c -o route.o

port.o: port.cpp *.h Makefile
	$(CC) -c $(CFLAGS) port.cpp -o port.o

mISDN.o: mISDN.cpp *.h Makefile
	$(CC) -c $(CFLAGS) mISDN.cpp -o mISDN.o

dss1.o: dss1.cpp ie.cpp *.h Makefile
	$(CC) -c $(CFLAGS) dss1.cpp -o dss1.o

#knock.o: knock.cpp *.h Makefile
#	$(CC) -c $(CFLAGS) knock.cpp -o knock.o
#
vbox.o: vbox.cpp *.h Makefile
	$(CC) -c $(CFLAGS) vbox.cpp -o vbox.o

mail.o: mail.c *.h Makefile
	$(CC) -c $(CFLAGS) mail.c -o mail.o

action.o: action.cpp *.h Makefile
	$(CC) -c $(CFLAGS) action.cpp -o action.o

action_vbox.o: action_vbox.cpp *.h Makefile
	$(CC) -c $(CFLAGS) action_vbox.cpp -o action_vbox.o

action_efi.o: action_efi.cpp *.h Makefile
	$(CC) -c $(CFLAGS) action_efi.cpp -o action_efi.o

endpoint.o: endpoint.cpp *.h Makefile
	$(CC) -c $(CFLAGS) endpoint.cpp -o endpoint.o

endpointapp.o: endpointapp.cpp *.h Makefile
	$(CC) -c $(CFLAGS) endpointapp.cpp -o endpointapp.o

apppbx.o: apppbx.cpp *.h Makefile
	$(CC) -c $(CFLAGS) apppbx.cpp -o apppbx.o

join.o: join.cpp *.h Makefile
	$(CC) -c $(CFLAGS) join.cpp -o join.o

joinpbx.o: joinpbx.cpp *.h Makefile
	$(CC) -c $(CFLAGS) joinpbx.cpp -o joinpbx.o

joinremote.o: joinremote.cpp *.h Makefile
	$(CC) -c $(CFLAGS) joinremote.cpp -o joinremote.o

cause.o: cause.c *.h Makefile
	$(CC) -c $(CFLAGS) cause.c -o cause.o

alawulaw.o: alawulaw.c *.h Makefile
	$(CC) -c $(CFLAGS) alawulaw.c -o alawulaw.o

tones.o: tones.c *.h Makefile
	$(CC) -c $(CFLAGS) tones.c -o tones.o

crypt.o: crypt.cpp *.h Makefile
	$(CC) -c $(CFLAGS) crypt.cpp -o crypt.o

genext.o: genext.c *.h Makefile
	$(CC) -c $(CFLAGS) genext.c -o genext.o

admin_server.o: admin_server.c *.h Makefile
	$(CC) -c $(CFLAGS) admin_server.c -o admin_server.o

trace.o: trace.c *.h Makefile
	$(CC) -c $(CFLAGS) trace.c -o trace.o


#$(WIZZARD): wizzard.c Makefile
#	$(CC) $(LIBDIR) $(CFLAGS) -lm wizzard.c \
#	-o $(WIZZARD) 

$(LCR): main.o \
	options.o \
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
	crypt.o \
	action.o \
	action_vbox.o \
	action_efi.o \
	mail.o \
	join.o \
	joinpbx.o \
	joinremote.o \
	admin_server.o \
	trace.o
	$(LD) $(LIBDIR) \
       	main.o \
	options.o \
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
	crypt.o \
	action.o \
	action_vbox.o \
	action_efi.o \
	mail.o \
	join.o \
	joinpbx.o \
	joinremote.o \
	admin_server.o \
	trace.o \
	$(LIBS) -o $(LCR) 

$(LCRADMIN): admin_client.c cause.c *.h Makefile
	$(CC) $(LIBDIR) $(CFLAGS) $(CURSES) -lm admin_client.c cause.c \
	-o $(LCRADMIN) 

$(CHAN_LCR): asterisk_client.c *.h Makefile
	$(CC) $(LIBDIR) $(CFLAGS) $(CURSES) -lm asterisk_client.c \
	-o $(CHAN_LCR) 

$(LCRWATCH): watch.c *.h Makefile
	$(CC) $(LIBDIR) $(CFLAGS) -lm watch.c \
	-o $(LCRWATCH) 

$(GEN):	gentones.c *.h Makefile 
	$(CC) $(LIBDIR) $(CFLAGS) -lm gentones.c \
	-o $(GEN) 

$(GENW):genwave.c *.h Makefile 
	$(CC) $(LIBDIR) $(CFLAGS) -lm genwave.c \
	-o $(GENW) 

$(GENRC): genrc.c *.h Makefile
	$(CC) $(LIBDIR) $(CFLAGS) -lm genrc.c \
	-o $(GENRC) 

$(GENEXT): options.o extension.o genext.o
	$(CC) $(CFLAGS) options.o extension.o genext.o -o $(GENEXT) 

#install:
#	@echo Remember, this is a beta release. To overwrite your current installed
#	@echo version, use \"make beta_install\".
#	@exit

install:
	-killall -9 -w -q lcr # the following error must be ignored
	cp $(LCR) $(INSTALL_BIN)
	cp $(LCRADMIN) $(INSTALL_BIN)
ifdef WITH_ASTERISK
	cp $(CHAN_LCR) $(INSTALL_BIN)
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
	cp -a tones_* $(INSTALL_DATA)
	cp -a vbox_english/ $(INSTALL_DATA)
	cp -a vbox_german/ $(INSTALL_DATA)
	cp -a tones_efi/ $(INSTALL_DATA)
	sync

clean:
	touch *
	rm -f $(LCR) $(LCRADMIN) $(CHAN_LCR) $(LCRWATCH) $(GEN) $(GENW) $(GENRC) $(GENEXT)
	rm -f *.o
	rm -f .*.c.sw* .*.cpp.sw* .*.h.sw*
	rm -f bla nohup.out
	rm -f debug*.log

tar:
	make clean
	cd .. &&  tar -cvzf lcr_`date +%Y%m%d`.tar.gz lcr

start: $(LCR)
	sync
	-killall -9 -w -q lcr # the following error must be ignored
	$(LCR) start

s: $(LCR)
	sync
	-killall -9 -w -q lcr # the following error must be ignored
	$(LCR) start

fork: $(LCR)
	sync
	-killall -9 -w -q lcr # the following error must be ignored
	$(LCR) fork



