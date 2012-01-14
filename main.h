/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** Header file for defining fixed values for the current version             **
**                                                                           **
\*****************************************************************************/ 

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <dirent.h>
#include <math.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/resource.h>

#define NAME		"LCR"

#define DEFAULT_ENDPOINT_APP EndpointAppPBX

#define VERSION_STRING	VERSION

extern int memuse;
extern int mmemuse;
extern int cmemuse;
extern int ememuse;
extern int pmemuse;
extern int amemuse;
extern int rmemuse;
extern int classuse;
extern int fduse;
extern int fhuse;

//extern pthread_mutex_t mutex_lcr; // lcr process mutex

extern FILE *debug_fp;

#define PDEBUG(mask, fmt, arg...) _printdebug(__FILE__, __FUNCTION__, __LINE__, mask, fmt, ## arg)
#define PERROR(fmt, arg...) _printerror(__FILE__, __FUNCTION__, __LINE__, fmt, ## arg)
#define PDEBUG_RUNTIME(mask, fmt, arg...) _printdebug(NULL, NULL, 0, mask, fmt, ## arg)
#define PERROR_RUNTIME(fmt, arg...) _printerror(NULL, NULL, 0, fmt, ## arg)
void _printdebug(const char *file, const char *function, int line, unsigned int mask, const char *fmt, ...);
void _printerror(const char *file, const char *function, int line, const char *fmt, ...);
#define DEBUG_FUNC
void debug(const char *file, const char *function, int line, const char *prefix, char *buffer);

#define DEBUG_CONFIG	0x0001
#define DEBUG_MSG 	0x0002
#define DEBUG_STACK 	0x0004
#define DEBUG_BCHANNEL 	0x0008
#define DEBUG_PORT 	0x0100
#define DEBUG_ISDN 	0x0110
#define DEBUG_GSM 	0x0120
#define DEBUG_SS5 	0x0140
#define DEBUG_VBOX	0x0180
#define DEBUG_SIP	0x10100
#define DEBUG_EPOINT 	0x0200
#define DEBUG_JOIN 	0x0400
#define DEBUG_VERSATEL 	0x0800
#define DEBUG_CRYPT	0x1000
#define DEBUG_ROUTE	0x2000
#define DEBUG_IDLETIME	0x4000

// check any faulty malloc
#define MALLOC_CHECK_	1

/*
 * one of the bits must be enabled in order to write log files
 */
#define DEBUG_LOG	0xfffff

/*
 * load transmit buffer to avoid gaps at the beginning due to jitter
 * also the maximum load that will be kept in tx-buffer
 * also the (minimum) number of data to transmit in a frame
 */
#define ISDN_LOAD	1024 // samples
#define ISDN_MAXLOAD	2048 // samples

/* give sendmail program. if not inside $PATH, give absolute path here (e.g. "/usr/sbin/sendmail")
 */
#define SENDMAIL	"sendmail"

/* leave it above 1024, because lower values can be unsafe, higher valuse cause
 * data larger than 512 bytes of hex strings.
 */
#define RSA_BITS	1536

/* 'goto' or 'menu' actions may cause infinite loops. they will be prevented by this limit.
 * Also other recursions, like redialing the 'redial' action must be prevented.
 * increase it ONLY IF you have a deeper tree of rule sets, than the value given here.
 */
#define RULE_NESTING	10

/* to debug core bridging, rather than mISDN dsp bridging, enable.
 * this is for debugging only, bridging conferences will not work
 */
//#define DEBUG_COREBRIDGE

/* special debugging for buffer overflow bugs
 * note: whenever a buffer gets strange values, the budetect function must
 * be modified to detect the change of these values. whenever it is detected,
 * an error message is given at budetect function.
 */
//#define BUDETECT_DEF

#ifdef BUDETECT_DEF
 #define BUDETECT	budetect(__FILE__, __LINE__, __FUNCTION__);
 void budetect(const char *file, int line, const char *function);
#else
 #define BUDETECT	;
#endif

#ifdef __cplusplus
extern "C" {
#endif
#include <mISDN/mbuffer.h>
#ifdef __cplusplus
}
#endif
#include "macro.h"
#include "select.h"
#include "options.h"
#include "interface.h"
#include "extension.h"
#include "message.h"
#include "endpoint.h"
#include "endpointapp.h"
#include "apppbx.h"
#include "callerid.h"
#include "route.h"
#include "port.h"
#include "mISDN.h"
#include "dss1.h"
#include "loop.h"
#include "remote.h"
#if defined WITH_GSM_BS || defined WITH_GSM_MS
#include "gsm.h"
#endif
#ifdef WITH_GSM_BS
#include "gsm_bs.h"
#endif
#ifdef WITH_GSM_MS
#include "gsm_ms.h"
#endif
#ifdef WITH_SS5
#include "ss5_encode.h"
#include "ss5_decode.h"
#include "ss5.h"
#endif
#ifdef WITH_SIP
#include "sip.h"
#endif
#include "vbox.h"
#include "join.h"
#include "joinpbx.h"
#include "joinremote.h"
#include "cause.h"
#include "alawulaw.h"
#include "tones.h"
#include "crypt.h"
#include "socket_server.h"
#include "trace.h"

extern int quit;

#define DIRECTION_NONE	0
#define DIRECTION_OUT	1
#define DIRECTION_IN	2

#if 0
struct lcr_fdset {
	struct mISDNport *mISDNport;
	int b_index;
};
#endif

