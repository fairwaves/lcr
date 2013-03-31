INCLUDES = $(all_includes) -I$(top_srcdir)/include -I$(top_builddir)
AM_CFLAGS = -fPIC -Wall ${SYMBOL_VISIBILITY}

LIBVERSION=0:0:0

REFSRC_PATH=refsrc
REFSRC_SRC=refsrc/dtx.c refsrc/globdefs.c refsrc/host.c refsrc/mathhalf.c refsrc/sp_enc.c refsrc/sp_rom.c refsrc/vad.c refsrc/err_conc.c refsrc/homing.c refsrc/mathdp31.c refsrc/sp_dec.c refsrc/sp_frm.c refsrc/sp_sfrm.c

${REFSRC_PATH}/.downloaded:
	./fetch_sources.py "${REFSRC_PATH}"
	for f in "${REFSRC_PATH}"/*.{c,h}; do	\
		sed -i -e"s/round/round_l2s/" "$$f";	\
	done
	sed -i -e"s/long int/int/" -e"s/long/int/" "${REFSRC_PATH}/typedefs.h"
	touch $@

${REFSRC_PATH}/dtx.c: ${REFSRC_PATH}/.downloaded

lib_LTLIBRARIES = libgsmhr.la
libgsmhr_la_SOURCES = $(REFSRC_SRC) libgsmhr.c

clean-local:
	-rm -rf ${REFSRC_PATH}
