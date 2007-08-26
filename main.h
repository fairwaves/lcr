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

#define NAME		"LCR"

#define DEFAULT_ENDPOINT_APP EndpointAppPBX

#define VERSION_STRING	"0.2 (August 2007)"

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

extern int global_debug;

#define PDEBUG(mask, fmt, arg...) _printdebug(__FUNCTION__, __LINE__, mask, fmt, ## arg)
#define PERROR(fmt, arg...) _printerror(__FUNCTION__, __LINE__, fmt, ## arg)
#define PDEBUG_RUNTIME(mask, fmt, arg...) _printdebug(NULL, 0, mask, fmt, ## arg)
#define PERROR_RUNTIME(fmt, arg...) _printerror(NULL, 0, fmt, ## arg)
void _printdebug(const char *function, int line, unsigned long mask, const char *fmt, ...);
void _printerror(const char *function, int line, const char *fmt, ...);
#define DEBUG_FUNC
void debug(const char *function, int line, char *prefix, char *buffer);

#define DEBUG_CONFIG	0x0001
#define DEBUG_MSG 	0x0002
#define DEBUG_STACK 	0x0004
#define DEBUG_BCHANNEL 	0x0008
#define DEBUG_PORT 	0x0100
#define DEBUG_ISDN 	0x0110
//#define DEBUG_KNOCK	0x0140
#define DEBUG_VBOX	0x0180
#define DEBUG_EPOINT 	0x0200
#define DEBUG_JOIN 	0x0400
#define DEBUG_VERSATEL 	0x0800
#define DEBUG_CRYPT	0x1000
#define DEBUG_ROUTE	0x2000
#define DEBUG_IDLETIME	0x4000
#define DEBUG_LOG	0x7fff

// check any faulty malloc
#define MALLOC_CHECK_	1

/*
 * one of the bits must be enabled in order to write log files
 */
#define DEBUG_LOG	0x7fff

/*
 * load transmit buffer to avoid gaps at the beginning due to jitter
 * also the maximum load that will be kept in tx-buffer
 * also the (minimum) number of data to transmit in a frame
 */
#define ISDN_LOAD	1024 // samples
#define ISDN_MAXLOAD	2048 // samples
#define ISDN_TRANSMIT	256 // samples

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
 void budetect(const char *file, int line, char *function);
#else
 #define BUDETECT	;
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
//#include <asm/bitops.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifndef SOCKET_MISDN
#include <isdn_net.h>
#include <../i4lnet/net_l3.h>
#endif
#ifdef __cplusplus
}
#endif
#include "macro.h"
#include "options.h"
#include "interface.h"
#include "extension.h"
#include "message.h"
#include "endpoint.h"
#include "endpointapp.h"
#include "apppbx.h"
#include "route.h"
#include "port.h"
#include "mISDN.h"
#include "dss1.h"
#include "vbox.h"
#include "join.h"
#include "joinpbx.h"
#include "joinremote.h"
#include "cause.h"
#include "alawulaw.h"
#include "tones.h"
#include "crypt.h"
#include "admin_server.h"
#include "trace.h"

extern double now_d;
extern time_t now;
extern struct tm *now_tm;
extern struct timeval now_tv;
extern struct timezone now_tz;

#define DIRECTION_NONE	0
#define DIRECTION_OUT	1
#define DIRECTION_IN	2

#if 0
struct lcr_fdset {
	struct mISDNport *mISDNport;
	int b_index;
};
#endif

