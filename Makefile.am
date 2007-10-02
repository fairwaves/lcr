## Makefile.am -- Process this file with automake to produce Makefile.in


INSTALLATION_DEFINES = -DCONFIG_DATA="\"$(pkgsysconfdir)\"" -DSHARE_DATA="\"$(pkgdatadir)\"" -DLOG_DIR="\"$(pkglogdir)\"" -DPBX -DINSTALL_DATA="\"$(pkgdatadir)\""
# FIXME: find a macro for the include path of linux
# or is /lib/modules/$(uname -r)/source/include sufficient?
# Do we take /usr/src/linux or /usr/src/linux-$(uname -r)
# if complied without modules?
# UPDATE: Not needed, compiles cleanly without.
INCLUDES = $(all_includes) $(INSTALLATION_DEFINES) 


bin_PROGRAMS = lcradmin gentones genwave #lcrwatch
sbin_PROGRAMS = lcr genrc genextension 


conditional_sources_common = \
       	main.c \
	options.c \
	interface.c \
	extension.c \
	cause.c \
	alawulaw.c \
	tones.c \
	message.c \
	route.c \
	port.cpp \
	mISDN.cpp \
	dss1.cpp \
	vbox.cpp \
	endpoint.cpp \
	endpointapp.cpp \
	apppbx.cpp \
	crypt.cpp \
	action.cpp \
	action_vbox.cpp \
	action_efi.cpp \
	mail.c \
	join.cpp \
	joinpbx.cpp \
	joinremote.cpp \
	admin_server.c \
	trace.c 

# extension.c  main.c     tones.c \
# admin_server.c  message.c \
# alawulaw.c      interface.c  options.c \
# cause.c         mail.c       route.c \
# action.cpp       crypt.cpp        port.cpp \
# action_efi.cpp   dss1.cpp         rule.cpp \
# action_vbox.cpp  endpointapp.cpp  ie.cpp \
# apppbx.cpp       endpoint.cpp     vbox.cpp \
# call.cpp         mISDN.cpp        vector.cpp \
# admin.h         dss1.h         route.h \
# admin_server.h  endpointapp.h  rule.h \
# alawulaw.h      endpoint.h     interface.h  save.h \
# apppbx.h        extension.h    main.h \
# call.h          message.h      options.h    tones.h \
# cause.h         mISDN.h        port.h       vbox.h \
# crypt.h         q931.h         vector.h




# wizzard is currently disabled.
# it can be compiled with "make wizzard.o" but nothing more
#wizzard_SOURCES = wizzard.c wizzard.h

lcr_SOURCES = $(conditional_sources_common)
lcr_LDADD = $(all_libraries) -lisdnnet -lmISDN
if WITH_CRYPTO
# TODO: maybe we could add crypt.{h,cpp} here?
#lcr_LDADD += -lcrypto -ldl
lcr_LDADD += -lcrypto
INSTALLATION_DEFINES += -DCRYPTO
endif

if ENABLE_SOCKET_MISDN
INSTALLATION_DEFINES += -DSOCKET_MISDN
endif

if ENABLE_ASTERISK_CHANNEL_DRIVER
sbin_PROGRAMS += chan_lcr
chan_lcr_SOURCES = asterisk_client.c
endif

lcradmin_SOURCES = admin_client.c cause.c
genextension_SOURCES = genext.c options.c extension.c
# genrc, gentones, genwave are made automaticaly
lcrwatch_SOURCES = watch.c
#gentones: gentones.c
#genwave:genwave.c
#genrc: genrc.c


# test rule, nothing important
#echo:
#	-echo $(all_libraries) >&2


# CAUTION: CRUDE CRUDE HACK !!
# This transforms all compiling and linking calls from gcc into g++
# This should better be removed and all .c files renamed to .cpp
COMPILE=$(CXXCOMPILE)
LINK=$(CXXLINK)
# in the end, it should look like:
# g++ -c -Wall -g -DINSTALL_DATA=\"/usr/local/lcr\" -I/usr/src/linux/include -I../mISDNuser/include -DPBX -DCRYPTO -I/usr/local/ssl/include route.cpp -o route.o



pkglogdir=$(localstatedir)/log/@PACKAGE@
# install runtime config in $prefix/etc/pbx4linux/ (e.g. /etc/pbx4linux)
# and voice data in $prefix/share/pbx4linux/ (e.g. /usr/share/pbx4linux)
pkgsysconfdir=$(sysconfdir)/@PACKAGE@
dist_pkgsysconf_DATA = $(config_default)

# install all voice data files along with their directory structure
# into $prefix/share/pbx4linux
nobase_dist_pkgdata_DATA = $(vbox_english) $(vbox_german) \
	$(tones_american) $(tones_efi) $(tones_german)


config_default = \
	default/directory.list \
	default/interface.conf \
	default/options.conf \
	default/routing.conf

tones_american = \
	tones_american/activated_loop.isdn \
	tones_american/busy_loop.isdn \
	tones_american/cause_01_loop.isdn \
	tones_american/cause_02_loop.isdn \
	tones_american/cause_03_loop.isdn \
	tones_american/cause_05_loop.isdn \
	tones_american/cause_06_loop.isdn \
	tones_american/cause_12_loop.isdn \
	tones_american/cause_13_loop.isdn \
	tones_american/cause_14_loop.isdn \
	tones_american/cause_15_loop.isdn \
	tones_american/cause_16_loop.isdn \
	tones_american/cause_1b_loop.isdn \
	tones_american/cause_1c_loop.isdn \
	tones_american/cause_1d_loop.isdn \
	tones_american/cause_1f_loop.isdn \
	tones_american/cause_22_loop.isdn \
	tones_american/cause_26_loop.isdn \
	tones_american/cause_29_loop.isdn \
	tones_american/cause_2a_loop.isdn \
	tones_american/cause_2f_loop.isdn \
	tones_american/cause_31_loop.isdn \
	tones_american/cause_32_loop.isdn \
	tones_american/cause_39_loop.isdn \
	tones_american/cause_3a_loop.isdn \
	tones_american/cause_3f_loop.isdn \
	tones_american/cause_41_loop.isdn \
	tones_american/cause_42_loop.isdn \
	tones_american/cause_45_loop.isdn \
	tones_american/cause_46_loop.isdn \
	tones_american/cause_55_loop.isdn \
	tones_american/cause_80_loop.isdn \
	tones_american/cause_81_loop.isdn \
	tones_american/cause_82_loop.isdn \
	tones_american/cause_83_loop.isdn \
	tones_american/cause_84_loop.isdn \
	tones_american/cause_85_loop.isdn \
	tones_american/cause_86_loop.isdn \
	tones_american/cause_87_loop.isdn \
	tones_american/crypt_off.isdn \
	tones_american/crypt_on.isdn \
	tones_american/deactivated_loop.isdn \
	tones_american/dialpbx.isdn \
	tones_american/dialpbx_loop.isdn \
	tones_american/error_loop.isdn \
	tones_american/hold_loop.isdn \
	tones_american/password_loop.isdn \
	tones_american/redial.isdn \
	tones_american/ringing_loop.isdn \
	tones_american/ringpbx_loop.isdn \
	tones_american/test_loop.isdn

tones_efi = \
	tones_efi/die.isdn \
	tones_efi/number_00.isdn \
	tones_efi/number_01.isdn \
	tones_efi/number_02.isdn \
	tones_efi/number_03.isdn \
	tones_efi/number_04.isdn \
	tones_efi/number_05.isdn \
	tones_efi/number_06.isdn \
	tones_efi/number_07.isdn \
	tones_efi/number_08.isdn \
	tones_efi/number_09.isdn

tones_german = \
	tones_german/activated_loop.isdn \
	tones_german/busy_loop.isdn \
	tones_german/cause_01_loop.isdn \
	tones_german/cause_02_loop.isdn \
	tones_german/cause_03_loop.isdn \
	tones_german/cause_05_loop.isdn \
	tones_german/cause_06_loop.isdn \
	tones_german/cause_12_loop.isdn \
	tones_german/cause_13_loop.isdn \
	tones_german/cause_14_loop.isdn \
	tones_german/cause_15_loop.isdn \
	tones_german/cause_16_loop.isdn \
	tones_german/cause_1b_loop.isdn \
	tones_german/cause_1c_loop.isdn \
	tones_german/cause_1d_loop.isdn \
	tones_german/cause_1f_loop.isdn \
	tones_german/cause_22_loop.isdn \
	tones_german/cause_26_loop.isdn \
	tones_german/cause_29_loop.isdn \
	tones_german/cause_2a_loop.isdn \
	tones_german/cause_2f_loop.isdn \
	tones_german/cause_31_loop.isdn \
	tones_german/cause_32_loop.isdn \
	tones_german/cause_39_loop.isdn \
	tones_german/cause_3a_loop.isdn \
	tones_german/cause_3f_loop.isdn \
	tones_german/cause_41_loop.isdn \
	tones_german/cause_42_loop.isdn \
	tones_german/cause_45_loop.isdn \
	tones_german/cause_46_loop.isdn \
	tones_german/cause_55_loop.isdn \
	tones_german/cause_80_loop.isdn \
	tones_german/cause_81_loop.isdn \
	tones_german/cause_82_loop.isdn \
	tones_german/cause_83_loop.isdn \
	tones_german/cause_84_loop.isdn \
	tones_german/cause_85_loop.isdn \
	tones_german/cause_86_loop.isdn \
	tones_german/cause_87_loop.isdn \
	tones_german/deactivated_loop.isdn \
	tones_german/dialpbx_loop.isdn \
	tones_german/dialtone_loop.isdn \
	tones_german/error_loop.isdn \
	tones_german/password_loop.isdn \
	tones_german/redial.isdn \
	tones_german/release_loop.isdn \
	tones_german/ringing.isdn \
	tones_german/ringing_loop.isdn \
	tones_german/ringpbx_loop.isdn \
	tones_german/test_loop.isdn

vbox_english = \
	vbox_english/call_anonymous.isdn \
	vbox_english/call_from.isdn \
	vbox_english/day_01.isdn \
	vbox_english/day_02.isdn \
	vbox_english/day_03.isdn \
	vbox_english/day_04.isdn \
	vbox_english/day_05.isdn \
	vbox_english/day_06.isdn \
	vbox_english/day_07.isdn \
	vbox_english/day_08.isdn \
	vbox_english/day_09.isdn \
	vbox_english/day_10.isdn \
	vbox_english/day_11.isdn \
	vbox_english/day_12.isdn \
	vbox_english/day_13.isdn \
	vbox_english/day_14.isdn \
	vbox_english/day_15.isdn \
	vbox_english/day_16.isdn \
	vbox_english/day_17.isdn \
	vbox_english/day_18.isdn \
	vbox_english/day_19.isdn \
	vbox_english/day_20.isdn \
	vbox_english/day_21.isdn \
	vbox_english/day_22.isdn \
	vbox_english/day_23.isdn \
	vbox_english/day_24.isdn \
	vbox_english/day_25.isdn \
	vbox_english/day_26.isdn \
	vbox_english/day_27.isdn \
	vbox_english/day_28.isdn \
	vbox_english/day_29.isdn \
	vbox_english/day_30.isdn \
	vbox_english/day_31.isdn \
	vbox_english/delete_ask.isdn \
	vbox_english/delete_done.isdn \
	vbox_english/intro.isdn \
	vbox_english/menu.isdn \
	vbox_english/minute.isdn \
	vbox_english/minutes.isdn \
	vbox_english/month_01.isdn \
	vbox_english/month_02.isdn \
	vbox_english/month_03.isdn \
	vbox_english/month_04.isdn \
	vbox_english/month_05.isdn \
	vbox_english/month_06.isdn \
	vbox_english/month_07.isdn \
	vbox_english/month_08.isdn \
	vbox_english/month_09.isdn \
	vbox_english/month_10.isdn \
	vbox_english/month_11.isdn \
	vbox_english/month_12.isdn \
	vbox_english/nothing.isdn \
	vbox_english/number_00.isdn \
	vbox_english/number_01.isdn \
	vbox_english/number_02.isdn \
	vbox_english/number_03.isdn \
	vbox_english/number_04.isdn \
	vbox_english/number_05.isdn \
	vbox_english/number_06.isdn \
	vbox_english/number_07.isdn \
	vbox_english/number_08.isdn \
	vbox_english/number_09.isdn \
	vbox_english/number_10.isdn \
	vbox_english/number_11.isdn \
	vbox_english/number_12.isdn \
	vbox_english/number_13.isdn \
	vbox_english/number_14.isdn \
	vbox_english/number_15.isdn \
	vbox_english/number_16.isdn \
	vbox_english/number_17.isdn \
	vbox_english/number_18.isdn \
	vbox_english/number_19.isdn \
	vbox_english/number_20.isdn \
	vbox_english/number_21.isdn \
	vbox_english/number_22.isdn \
	vbox_english/number_23.isdn \
	vbox_english/number_24.isdn \
	vbox_english/number_25.isdn \
	vbox_english/number_26.isdn \
	vbox_english/number_27.isdn \
	vbox_english/number_28.isdn \
	vbox_english/number_29.isdn \
	vbox_english/number_30.isdn \
	vbox_english/number_31.isdn \
	vbox_english/number_32.isdn \
	vbox_english/number_33.isdn \
	vbox_english/number_34.isdn \
	vbox_english/number_35.isdn \
	vbox_english/number_36.isdn \
	vbox_english/number_37.isdn \
	vbox_english/number_38.isdn \
	vbox_english/number_39.isdn \
	vbox_english/number_40.isdn \
	vbox_english/number_41.isdn \
	vbox_english/number_42.isdn \
	vbox_english/number_43.isdn \
	vbox_english/number_44.isdn \
	vbox_english/number_45.isdn \
	vbox_english/number_46.isdn \
	vbox_english/number_47.isdn \
	vbox_english/number_48.isdn \
	vbox_english/number_49.isdn \
	vbox_english/number_50.isdn \
	vbox_english/number_51.isdn \
	vbox_english/number_52.isdn \
	vbox_english/number_53.isdn \
	vbox_english/number_54.isdn \
	vbox_english/number_55.isdn \
	vbox_english/number_56.isdn \
	vbox_english/number_57.isdn \
	vbox_english/number_58.isdn \
	vbox_english/number_59.isdn \
	vbox_english/oclock_am.isdn \
	vbox_english/oclock_pm.isdn \
	vbox_english/pause.isdn \
	vbox_english/play.isdn \
	vbox_english/record_ask.isdn \
	vbox_english/record_play.isdn \
	vbox_english/record_record.isdn \
	vbox_english/store_ask.isdn \
	vbox_english/store_done.isdn

vbox_german = \
	vbox_german/call_anonymous.isdn \
	vbox_german/call_from.isdn \
	vbox_german/day_01.isdn \
	vbox_german/day_02.isdn \
	vbox_german/day_03.isdn \
	vbox_german/day_04.isdn \
	vbox_german/day_05.isdn \
	vbox_german/day_06.isdn \
	vbox_german/day_07.isdn \
	vbox_german/day_08.isdn \
	vbox_german/day_09.isdn \
	vbox_german/day_10.isdn \
	vbox_german/day_11.isdn \
	vbox_german/day_12.isdn \
	vbox_german/day_13.isdn \
	vbox_german/day_14.isdn \
	vbox_german/day_15.isdn \
	vbox_german/day_16.isdn \
	vbox_german/day_17.isdn \
	vbox_german/day_18.isdn \
	vbox_german/day_19.isdn \
	vbox_german/day_20.isdn \
	vbox_german/day_21.isdn \
	vbox_german/day_22.isdn \
	vbox_german/day_23.isdn \
	vbox_german/day_24.isdn \
	vbox_german/day_25.isdn \
	vbox_german/day_26.isdn \
	vbox_german/day_27.isdn \
	vbox_german/day_28.isdn \
	vbox_german/day_29.isdn \
	vbox_german/day_30.isdn \
	vbox_german/day_31.isdn \
	vbox_german/delete_ask.isdn \
	vbox_german/delete_done.isdn \
	vbox_german/intro.isdn \
	vbox_german/menu.isdn \
	vbox_german/minute.isdn \
	vbox_german/minutes.isdn \
	vbox_german/month_01.isdn \
	vbox_german/month_02.isdn \
	vbox_german/month_03.isdn \
	vbox_german/month_04.isdn \
	vbox_german/month_05.isdn \
	vbox_german/month_06.isdn \
	vbox_german/month_07.isdn \
	vbox_german/month_08.isdn \
	vbox_german/month_09.isdn \
	vbox_german/month_10.isdn \
	vbox_german/month_11.isdn \
	vbox_german/month_12.isdn \
	vbox_german/nothing.isdn \
	vbox_german/number_00.isdn \
	vbox_german/number_01.isdn \
	vbox_german/number_02.isdn \
	vbox_german/number_03.isdn \
	vbox_german/number_04.isdn \
	vbox_german/number_05.isdn \
	vbox_german/number_06.isdn \
	vbox_german/number_07.isdn \
	vbox_german/number_08.isdn \
	vbox_german/number_09.isdn \
	vbox_german/number_10.isdn \
	vbox_german/number_11.isdn \
	vbox_german/number_12.isdn \
	vbox_german/number_13.isdn \
	vbox_german/number_14.isdn \
	vbox_german/number_15.isdn \
	vbox_german/number_16.isdn \
	vbox_german/number_17.isdn \
	vbox_german/number_18.isdn \
	vbox_german/number_19.isdn \
	vbox_german/number_20.isdn \
	vbox_german/number_21.isdn \
	vbox_german/number_22.isdn \
	vbox_german/number_23.isdn \
	vbox_german/number_24.isdn \
	vbox_german/number_25.isdn \
	vbox_german/number_26.isdn \
	vbox_german/number_27.isdn \
	vbox_german/number_28.isdn \
	vbox_german/number_29.isdn \
	vbox_german/number_30.isdn \
	vbox_german/number_31.isdn \
	vbox_german/number_32.isdn \
	vbox_german/number_33.isdn \
	vbox_german/number_34.isdn \
	vbox_german/number_35.isdn \
	vbox_german/number_36.isdn \
	vbox_german/number_37.isdn \
	vbox_german/number_38.isdn \
	vbox_german/number_39.isdn \
	vbox_german/number_40.isdn \
	vbox_german/number_41.isdn \
	vbox_german/number_42.isdn \
	vbox_german/number_43.isdn \
	vbox_german/number_44.isdn \
	vbox_german/number_45.isdn \
	vbox_german/number_46.isdn \
	vbox_german/number_47.isdn \
	vbox_german/number_48.isdn \
	vbox_german/number_49.isdn \
	vbox_german/number_50.isdn \
	vbox_german/number_51.isdn \
	vbox_german/number_52.isdn \
	vbox_german/number_53.isdn \
	vbox_german/number_54.isdn \
	vbox_german/number_55.isdn \
	vbox_german/number_56.isdn \
	vbox_german/number_57.isdn \
	vbox_german/number_58.isdn \
	vbox_german/number_59.isdn \
	vbox_german/number_eine.isdn \
	vbox_german/number_ein.isdn \
	vbox_german/oclock.isdn \
	vbox_german/pause.isdn \
	vbox_german/play.isdn \
	vbox_german/record_ask.isdn \
	vbox_german/record_play.isdn \
	vbox_german/record_record.isdn \
	vbox_german/store_ask.isdn \
	vbox_german/store_done.isdn
