/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** Asterisk socket client                                                    **
**                                                                           **
\*****************************************************************************/

/*

Registering to LCR:

To connect, open an LCR socket and send a MESSAGE_HELLO to socket with
the application name. This name is unique an can be used for routing calls.
Now the channel driver is linked to LCR and can receive and make calls.


Call is initiated by LCR:

If a call is received from LCR, a MESSAGE_NEWREF is received first.
The ref_was_assigned ist set to 1.
A new chan_call instance is created. The call reference (ref) is given by
the received MESSAGE_NEWREF. The state is CHAN_LCR_STATE_IN_PREPARE.
After receiving MESSAGE_SETUP from LCR, the ast_channel instance is created
using ast_channel_alloc(1).  The setup information is given to asterisk.
The new Asterisk instance pointer (ast) is stored to chan_call structure.
The state changes to CHAN_LCR_STATE_IN_SETUP.


Call is initiated by Asterisk:

If a call is requested from Asterisk, a new chan_call instance is created.
The new Asterisk instance pointer (ast) is stored to chan_call structure.
The current call ref is set to 0, the state is CHAN_LCR_STATE_OUT_PREPARE.
If the call is received (lcr_call) A MESSAGE_NEWREF is sent to LCR requesting
a new call reference (ref).
The ref_was_assigned ist set to 1.
Further dialing information is queued.
After the new callref is received by special MESSAGE_NEWREF reply, new ref
is stored in the chan_call structure.
The setup information is sent to LCR using MESSAGE_SETUP.
The state changes to CHAN_LCR_STATE_OUT_SETUP.


Call is in process:

During call process, messages are received and sent.
The state changes accordingly.
Any message is allowed to be sent to LCR at any time except MESSAGE_RELEASE.
If a MESSAGE_OVERLAP is received, further dialing is required.
Queued dialing information, if any, is sent to LCR using MESSAGE_DIALING.
In this case, the state changes to CHAN_LCR_STATE_OUT_DIALING.


Call is released by LCR:

A MESSAGE_RELEASE is received with the call reference (ref) to be released.
The current ref is set to 0, to indicate released reference.
The ref_was_assigned==1 shows that there is no other ref to be assigned.
The state changes to CHAN_LCR_STATE_RELEASE.
ast_queue_hangup() is called, if asterisk instance (ast) exists, if not,
the chan_call instance is destroyed.
After lcr_hangup() is called-back by Asterisk, the chan_call instance
is destroyed, because the current ref is set to 0 and the state equals
CHAN_LCR_STATE_RELEASE.
If the ref is 0 and the state is not CHAN_LCR_STATE_RELEASE, see the proceedure
"Call is released by Asterisk".


Call is released by Asterisk:

lcr_hangup() is called-back by Asterisk. If the call reference (ref) is set,
a MESSAGE_RELEASE is sent to LCR and the chan_call instance is destroyed.
If the ref is 0 and the state is not CHAN_LCR_STATE_RELEASE, the new state is
set to CHAN_LCR_STATE_RELEASE.
The ref_was_assigned==0 shows that a ref is still requested.
Later, if the MESSAGE_NEWREF reply is received, a MESSAGE_RELEASE is sent to
LCR and the chan_call instance is destroyed.
If the ref is 0 and the state is CHAN_LCR_STATE_RELEASE, see the proceedure
"Call is released by LCR".


Locking issues:

The deadlocking problem:

- chan_lcr locks chan_lock and waits inside ast_queue_xxxx() for ast_channel
to be unlocked.
- ast_channel thread locks ast_channel and calls a tech function and waits
there for chan_lock to be unlocked.

The solution:

Never call ast_queue_xxxx() if ast_channel is not locked and don't wait until
ast_channel can be locked. All messages to asterisk are queued inside call
instance and will be handled using a try-lock to get ast_channel lock.
If it succeeds to lock ast_channel, the ast_queue_xxxx can safely called even
if the lock is incremented and decremented there.

Exception: Calling ast_queue_frame inside ast->tech->read is safe, because
it is called from ast_channel process which has already locked ast_channel.

*/


/* Choose if you want to have chan_lcr for Asterisk 1.4.x or CallWeaver 1.2.x */
#define LCR_FOR_ASTERISK
/* #define LCR_FOR_CALLWEAVER */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h>
#include <time.h>
//#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <semaphore.h>

#define HAVE_ATTRIBUTE_always_inline 1
#define HAVE_ARPA_INET_H 1
#define HAVE_TIMERSUB 1
#define HAVE_STRTOQ 1
#define HAVE_INET_ATON 1

#include <asterisk/compiler.h>
#ifdef LCR_FOR_ASTERISK
#include <asterisk/buildopts.h>
#endif

/*
 * Fwd declare struct ast_channel to get rid of gcc warning about
 * incompatible pointer type passed to ast_register_application2.
 */
struct ast_channel;

#include <asterisk/module.h>
#include <asterisk/channel.h>
#include <asterisk/config.h>
#include <asterisk/logger.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <asterisk/io.h>
#include <asterisk/frame.h>
#include <asterisk/translate.h>
#include <asterisk/cli.h>
#include <asterisk/musiconhold.h>
#include <asterisk/dsp.h>
#include <asterisk/translate.h>
#include <asterisk/file.h>
#ifdef LCR_FOR_ASTERISK
#include <asterisk/callerid.h>
#endif
#ifdef LCR_FOR_CALLWEAVER
#include <asterisk/phone_no_utils.h>
#endif

#include <asterisk/indications.h>
#include <asterisk/app.h>
#include <asterisk/features.h>
#include <asterisk/sched.h>
#if ASTERISK_VERSION_NUM < 110000
#include <asterisk/version.h>
#endif
#include "extension.h"
#include "message.h"
#include "callerid.h"
#include "lcrsocket.h"
#include "cause.h"
#include "select.h"
#include "options.h"
#include "chan_lcr.h"

CHAN_LCR_STATE // state description structure
MESSAGES // message text

#ifdef LCR_FOR_CALLWEAVER
AST_MUTEX_DEFINE_STATIC(rand_lock);
#endif

unsigned char flip_bits[256];

#ifdef LCR_FOR_CALLWEAVER
static struct ast_frame nullframe = { AST_FRAME_NULL, };
#endif

int lcr_debug=1;

char lcr_type[]="lcr";

#ifdef LCR_FOR_CALLWEAVER
static ast_mutex_t usecnt_lock;
static int usecnt=0;
static char *desc = "Channel driver for mISDN/LCR Support (Bri/Pri)";
#endif

pthread_t chan_tid;
ast_mutex_t chan_lock; /* global lock */
ast_mutex_t log_lock; /* logging log */
/* global_change:
 * used to indicate change in file descriptors, so select function's result may
 * be obsolete.
 */
int global_change = 0;
int wake_global = 0;
int wake_pipe[2];
struct lcr_fd wake_fd;

int glob_channel = 0;

int lcr_sock = -1;
struct lcr_fd socket_fd;
struct lcr_timer socket_retry;

struct admin_list {
	struct admin_list *next;
	struct admin_message msg;
} *admin_first = NULL;

static struct ast_channel_tech lcr_tech;

/*
 * logging
 */
void chan_lcr_log(int type, const char *file, int line, const char *function, struct chan_call *call, struct ast_channel *ast, const char *fmt, ...)
{
	char buffer[1024];
	char call_text[128] = "NULL";
	char ast_text[128] = "NULL";
	va_list args;

	ast_mutex_lock(&log_lock);

	va_start(args,fmt);
	vsnprintf(buffer,sizeof(buffer)-1,fmt,args);
	buffer[sizeof(buffer)-1]=0;
	va_end(args);

	if (call)
		sprintf(call_text, "%d", call->ref);
	if (ast)
#if ASTERISK_VERSION_NUM < 110000
		strncpy(ast_text, ast->name, sizeof(ast_text)-1);
#else
		strncpy(ast_text, ast_channel_name(ast), sizeof(ast_text)-1);
#endif
	ast_text[sizeof(ast_text)-1] = '\0';

//	ast_log(type, file, line, function, "[call=%s ast=%s] %s", call_text, ast_text, buffer);
	printf("[call=%s ast=%s line=%d] %s", call_text, ast_text, line, buffer);

	ast_mutex_unlock(&log_lock);
}

/*
 * channel and call instances
 */
struct chan_call *call_first;

/*
 * find call by ref
 * special case: 0: find new ref, that has not been assigned a ref yet
 */

struct chan_call *find_call_ref(unsigned int ref)
{
	struct chan_call *call = call_first;
	int assigned = (ref > 0);

	while(call) {
		if (call->ref == ref && call->ref_was_assigned == assigned)
			break;
		call = call->next;
	}
	return call;
}

void free_call(struct chan_call *call)
{
	struct chan_call **temp = &call_first;

	while(*temp) {
		if (*temp == call) {
			*temp = (*temp)->next;
			if (call->pipe[0] > -1)
				close(call->pipe[0]);
			if (call->pipe[1] > -1)
				close(call->pipe[1]);
			if (call->bridge_call) {
				if (call->bridge_call->bridge_call != call)
					CERROR(call, NULL, "Linked call structure has no link to us.\n");
				call->bridge_call->bridge_call = NULL;
			}
			if (call->trans)
				ast_translator_free_path(call->trans);
			if (call->dsp)
				ast_dsp_free(call->dsp);
			CDEBUG(call, NULL, "Call instance freed.\n");
			free(call);
			global_change = 1;
			return;
		}
		temp = &((*temp)->next);
	}
	CERROR(call, NULL, "Call instance not found in list.\n");
}

struct chan_call *alloc_call(void)
{
	struct chan_call **callp = &call_first;

	while(*callp)
		callp = &((*callp)->next);

	*callp = (struct chan_call *)calloc(1, sizeof(struct chan_call));
	if (*callp)
		memset(*callp, 0, sizeof(struct chan_call));
	if (pipe((*callp)->pipe) < 0) {
		CERROR(*callp, NULL, "Failed to create pipe.\n");
		free_call(*callp);
		return NULL;
	}
	fcntl((*callp)->pipe[0], F_SETFL, O_NONBLOCK);
	CDEBUG(*callp, NULL, "Call instance allocated.\n");
	return *callp;
}

unsigned short new_bridge_id(void)
{
	struct chan_call *call;
	unsigned short id = 1;

	/* search for lowest bridge id that is not in use and not 0 */
	while(id) {
		call = call_first;
		while(call) {
			if (call->bridge_id == id)
				break;
			call = call->next;
		}
		if (!call)
			break;
		id++;
	}
	CDEBUG(NULL, NULL, "New bridge ID %d.\n", id);
	return id;
}

/*
 * enque message to LCR
 */
int send_message(int message_type, unsigned int ref, union parameter *param)
{
	struct admin_list *admin, **adminp;

	if (lcr_sock < 0) {
		CDEBUG(NULL, NULL, "Ignoring message %d, because socket is closed.\n", message_type);
		return -1;
	}
	if (message_type != MESSAGE_TRAFFIC)
		CDEBUG(NULL, NULL, "Sending %s to socket. (ref=%d)\n", messages_txt[message_type], ref);

	adminp = &admin_first;
	while(*adminp)
		adminp = &((*adminp)->next);
	admin = (struct admin_list *)calloc(1, sizeof(struct admin_list));
	if (!admin) {
		CERROR(NULL, NULL, "No memory for message to LCR.\n");
		return -1;
	}
	*adminp = admin;

	admin->msg.message = ADMIN_MESSAGE;
	admin->msg.u.msg.type = message_type;
	admin->msg.u.msg.ref = ref;
	memcpy(&admin->msg.u.msg.param, param, sizeof(union parameter));
	socket_fd.when |= LCR_FD_WRITE;
	if (!wake_global) {
		wake_global = 1;
		char byte = 0;
		int rc;
		rc = write(wake_pipe[1], &byte, 1);
	}

	return 0;
}

/*
 * apply options (in locked state)
 */
void apply_opt(struct chan_call *call, char *data)
{
	union parameter newparam;
	char string[1024], *p = string, *opt;//, *key;
//	int gain, i;

	if (!data[0])
		return; // no opts

	strncpy(string, data, sizeof(string)-1);
	string[sizeof(string)-1] = '\0';

	/* parse options */
	while((opt = strsep(&p, ":"))) {
		switch(opt[0]) {
		case 'd':
			if (opt[1] == '\0') {
				CERROR(call, call->ast, "Option 'd' (display) expects parameter.\n", opt);
				break;
			}
			CDEBUG(call, call->ast, "Option 'd' (display) with text '%s'.\n", opt+1);
			if (call->state == CHAN_LCR_STATE_OUT_PREPARE)
				strncpy(call->display, opt+1, sizeof(call->display)-1);
			else {
				memset(&newparam, 0, sizeof(union parameter));
				strncpy(newparam.notifyinfo.display, opt+1, sizeof(newparam.notifyinfo.display)-1);
				send_message(MESSAGE_NOTIFY, call->ref, &newparam);
			}
			break;
		case 'n':
			if (opt[1] != '\0') {
				CERROR(call, call->ast, "Option 'n' (no DTMF) expects no parameter.\n", opt);
				break;
			}
			CDEBUG(call, call->ast, "Option 'n' (no DTMF).\n");
			call->dsp_dtmf = 0;
			break;
#if 0
		case 'c':
			if (opt[1] == '\0') {
				CERROR(call, call->ast, "Option 'c' (encrypt) expects key parameter.\n", opt);
				break;
			}
			key = opt+1;
			/* check for 0xXXXX... type of key */
			if (!!strncmp((char *)key, "0x", 2)) {
				CERROR(call, call->ast, "Option 'c' (encrypt) expects key parameter starting with '0x'.\n", opt);
				break;
			}
			key+=2;
			if (strlen(key) > 56*2 || (strlen(key) % 1)) {
				CERROR(call, call->ast, "Option 'c' (encrypt) expects key parameter with max 56 bytes ('0x' + 112 characters)\n", opt);
				break;
			}
			i = 0;
			while(*key) {
				if (*key>='0' && *key<='9')
					call->bf_key[i] = (*key-'0') << 8;
				else if (*key>='a' && *key<='f')
					call->bf_key[i] = (*key-'a'+10) << 8;
				else if (*key>='A' && *key<='F')
					call->bf_key[i] = (*key-'A'+10) << 8;
				else
					break;
				key++;
				if (*key>='0' && *key<='9')
					call->bf_key[i] += (*key - '0');
				else if (*key>='a' && *key<='f')
					call->bf_key[i] += (*key - 'a' + 10);
				else if (*key>='A' && *key<='F')
					call->bf_key[i] += (*key - 'A' + 10);
				else
					break;
				key++;
				i++;
			}
			if (*key) {
				CERROR(call, call->ast, "Option 'c' (encrypt) expects key parameter with hex values 0-9,a-f.\n");
				break;
			}
			call->bf_len = i;
			CDEBUG(call, call->ast, "Option 'c' (encrypt) blowfish key '%s' (len=%d).\n", opt+1, i);
			if (call->bchannel)
				bchannel_blowfish(call->bchannel, call->bf_key, call->bf_len);
			break;
#endif
		case 'h':
			if (opt[1] != '\0') {
				CERROR(call, call->ast, "Option 'h' (HDLC) expects no parameter.\n", opt);
				break;
			}
			CDEBUG(call, call->ast, "Option 'h' (HDLC).\n");
			if (!call->hdlc)
				call->hdlc = 1;
			break;
		case 'q':
			if (opt[1] == '\0') {
				CERROR(call, call->ast, "Option 'q' (queue) expects parameter.\n", opt);
				break;
			}
			CDEBUG(call, call->ast, "Option 'q' (queue).\n");
			call->tx_queue = atoi(opt+1);
			break;
#if 0
		case 'e':
			if (opt[1] == '\0') {
				CERROR(call, call->ast, "Option 'e' (echo cancel) expects parameter.\n", opt);
				break;
			}
			CDEBUG(call, call->ast, "Option 'e' (echo cancel) with config '%s'.\n", opt+1);
			strncpy(call->pipeline, opt+1, sizeof(call->pipeline)-1);
			if (call->bchannel)
				bchannel_pipeline(call->bchannel, call->pipeline);
			break;
#endif
		case 'f':
			if (opt[1] != '\0') {
				CERROR(call, call->ast, "Option 'f' (faxdetect) expects no parameter.\n", opt);
				break;
			}
			call->faxdetect = 1;
			call->dsp_dtmf = 0;
			CDEBUG(call, call->ast, "Option 'f' (faxdetect).\n");
			break;
		case 'a':
			if (opt[1] != '\0') {
				CERROR(call, call->ast, "Option 'a' (asterisk DTMF) expects no parameter.\n", opt);
				break;
			}
			call->ast_dsp = 1;
			call->dsp_dtmf = 0;
			CDEBUG(call, call->ast, "Option 'a' (Asterisk DTMF detection).\n");
			break;
		case 'r':
			if (opt[1] != '\0') {
				CERROR(call, call->ast, "Option 'r' (re-buffer 160 bytes) expects no parameter.\n", opt);
				break;
			}
			CDEBUG(call, call->ast, "Option 'r' (re-buffer 160 bytes)");
			call->rebuffer = 1;
			call->framepos = 0;
			break;
		case 's':
			if (opt[1] != '\0') {
				CERROR(call, call->ast, "Option 's' (inband DTMF) expects no parameter.\n", opt);
				break;
			}
			CDEBUG(call, call->ast, "Option 's' (inband DTMF).\n");
			call->inband_dtmf = 1;
			break;
#if 0
		case 'v':
			if (opt[1] != 'r' && opt[1] != 't') {
				CERROR(call, call->ast, "Option 'v' (volume) expects parameter.\n", opt);
				break;
			}
			gain = atoi(opt+2);
			if (gain < -8 || gain >8) {
				CERROR(call, call->ast, "Option 'v' (volume) expects parameter in range of -8 through 8.\n");
				break;
			}
			CDEBUG(call, call->ast, "Option 'v' (volume) with gain 2^%d.\n", gain);
			if (opt[1] == 'r') {
				call->rx_gain = gain;
				if (call->bchannel)
					bchannel_gain(call->bchannel, call->rx_gain, 0);
			} else {
				call->tx_gain = gain;
				if (call->bchannel)
					bchannel_gain(call->bchannel, call->tx_gain, 1);
			}
			break;
#endif
		case 'k':
			if (opt[1] != '\0') {
				CERROR(call, call->ast, "Option 'k' (keypad) expects no parameter.\n", opt);
				break;
			}
			CDEBUG(call, call->ast, "Option 'k' (keypad).\n");
			if (!call->keypad)
				call->keypad = 1;
			break;
		default:
			CERROR(call, call->ast, "Option '%s' unknown.\n", opt);
		}
	}

	if (call->faxdetect || call->ast_dsp) {
		if (!call->dsp)
			call->dsp=ast_dsp_new();
		if (call->dsp) {
			#ifdef LCR_FOR_CALLWEAVER
			ast_dsp_set_features(call->dsp, DSP_FEATURE_DTMF_DETECT | ((call->faxdetect) ? DSP_FEATURE_FAX_CNG_DETECT : 0));
			#endif
			#ifdef LCR_FOR_ASTERISK
			#ifdef DSP_FEATURE_DTMF_DETECT
			ast_dsp_set_features(call->dsp, DSP_FEATURE_DTMF_DETECT | ((call->faxdetect) ? DSP_FEATURE_FAX_DETECT : 0));
			#else
			ast_dsp_set_features(call->dsp, DSP_FEATURE_DIGIT_DETECT | ((call->faxdetect) ? DSP_FEATURE_FAX_DETECT : 0));
			#endif

			#endif
			if (!call->trans) {
				#ifdef LCR_FOR_CALLWEAVER
				call->trans=ast_translator_build_path(AST_FORMAT_SLINEAR, 8000, (options.law=='a')?AST_FORMAT_ALAW:AST_FORMAT_ULAW, 8000);
				#endif
				#ifdef LCR_FOR_ASTERISK
				#if ASTERISK_VERSION_NUM < 100000
				call->trans=ast_translator_build_path(AST_FORMAT_SLINEAR, (options.law=='a')?AST_FORMAT_ALAW:AST_FORMAT_ULAW);
//				#else
//				struct ast_format src;
//				struct ast_format dst;
//				ast_format_set(&dst, AST_FORMAT_SLINEAR, 0);
//				ast_format_set(&dst,(options.law=='a')?AST_FORMAT_ALAW:AST_FORMAT_ULAW , 0);
//				call->trans=ast_translator_build_path(&dst, &src);
				#endif
				#endif
			}
		}
	}
}

/*
 * send setup info to LCR
 * this function is called, when asterisk call is received and ref is received
 */
static void send_setup_to_lcr(struct chan_call *call)
{
	union parameter newparam;
	struct ast_channel *ast = call->ast;
//	const char *tmp;

	if (!call->ast || !call->ref)
		return;

#ifdef AST_1_8_OR_HIGHER
	CDEBUG(call, call->ast, "Sending setup to LCR. (interface=%s dialstring=%s, cid=%s)\n", call->interface, call->dialstring, call->callerinfo.id);
#else
	CDEBUG(call, call->ast, "Sending setup to LCR. (interface=%s dialstring=%s, cid=%s)\n", call->interface, call->dialstring, call->cid_num);
#endif

	/* send setup message to LCR */
	memset(&newparam, 0, sizeof(union parameter));
	newparam.setup.dialinginfo.itype = INFO_ITYPE_ISDN;
	newparam.setup.dialinginfo.ntype = INFO_NTYPE_UNKNOWN;
	if (call->keypad)
		strncpy(newparam.setup.dialinginfo.keypad, call->dialstring, sizeof(newparam.setup.dialinginfo.keypad)-1);
	else
		strncpy(newparam.setup.dialinginfo.id, call->dialstring, sizeof(newparam.setup.dialinginfo.id)-1);
	if (!!strcmp(call->interface, "pbx"))
		strncpy(newparam.setup.dialinginfo.interfaces, call->interface, sizeof(newparam.setup.dialinginfo.interfaces)-1);
	newparam.setup.callerinfo.itype = INFO_ITYPE_CHAN;
	newparam.setup.callerinfo.ntype = INFO_NTYPE_UNKNOWN;
	strncpy(newparam.setup.callerinfo.display, call->display, sizeof(newparam.setup.callerinfo.display)-1);
	call->display[0] = '\0';

#ifdef AST_1_8_OR_HIGHER
	/* set stored call information */
	memcpy(&newparam.setup.callerinfo, &call->callerinfo, sizeof(struct caller_info));
	memcpy(&newparam.setup.redirinfo, &call->redirinfo, sizeof(struct redir_info));
#else
	if (call->cid_num[0])
		strncpy(newparam.setup.callerinfo.id, call->cid_num, sizeof(newparam.setup.callerinfo.id)-1);
	if (call->cid_name[0])
		strncpy(newparam.setup.callerinfo.name, call->cid_name, sizeof(newparam.setup.callerinfo.name)-1);
	if (call->cid_rdnis[0]) {
		strncpy(newparam.setup.redirinfo.id, call->cid_rdnis, sizeof(newparam.setup.redirinfo.id)-1);
		newparam.setup.redirinfo.itype = INFO_ITYPE_CHAN;
		newparam.setup.redirinfo.ntype = INFO_NTYPE_UNKNOWN;
	}
	switch(ast->cid.cid_pres & AST_PRES_RESTRICTION) {
		case AST_PRES_RESTRICTED:
		newparam.setup.callerinfo.present = INFO_PRESENT_RESTRICTED;
		break;
		case AST_PRES_UNAVAILABLE:
		newparam.setup.callerinfo.present = INFO_PRESENT_NOTAVAIL;
		break;
		case AST_PRES_ALLOWED:
		default:
		newparam.setup.callerinfo.present = INFO_PRESENT_ALLOWED;
	}
	switch(ast->cid.cid_ton) {
		case 4:
		newparam.setup.callerinfo.ntype = INFO_NTYPE_SUBSCRIBER;
		break;
		case 2:
		newparam.setup.callerinfo.ntype = INFO_NTYPE_NATIONAL;
		break;
		case 1:
		newparam.setup.callerinfo.ntype = INFO_NTYPE_INTERNATIONAL;
		break;
		default:
		newparam.setup.callerinfo.ntype = INFO_NTYPE_UNKNOWN;
	}
#endif
#warning DISABLED DUE TO DOUBLE LOCKING PROBLEM
//	tmp = pbx_builtin_getvar_helper(ast, "LCR_TRANSFERCAPABILITY");
//	if (tmp && *tmp)
#if ASTERISK_VERSION_NUM < 110000
//		ast->transfercapability = atoi(tmp);
	newparam.setup.capainfo.bearer_capa = ast->transfercapability;
#else
//		ast_channel_transfercapability_set(ast, atoi(tmp));
	newparam.setup.capainfo.bearer_capa = ast_channel_transfercapability(ast);
#endif
	newparam.setup.capainfo.bearer_mode = INFO_BMODE_CIRCUIT;
	if (call->hdlc)
		newparam.setup.capainfo.source_mode = B_MODE_HDLC;
	else {
		newparam.setup.capainfo.source_mode = B_MODE_TRANSPARENT;
		newparam.setup.capainfo.bearer_info1 = (options.law=='a')?3:2;
	}
	newparam.setup.capainfo.hlc = INFO_HLC_NONE;
	newparam.setup.capainfo.exthlc = INFO_HLC_NONE;
	send_message(MESSAGE_SETUP, call->ref, &newparam);
	if (call->tx_queue) {
		memset(&newparam, 0, sizeof(union parameter));
		newparam.queue = call->tx_queue * 8;
		send_message(MESSAGE_DISABLE_DEJITTER, call->ref, &newparam);
	}

	/* change to outgoing setup state */
	call->state = CHAN_LCR_STATE_OUT_SETUP;
}

/*
 * send dialing info to LCR
 * this function is called, when setup acknowledge is received and dialing
 * info is available.
 */
static void send_dialque_to_lcr(struct chan_call *call)
{
	union parameter newparam;

	if (!call->ast || !call->ref || !call->dialque[0])
		return;

	CDEBUG(call, call->ast, "Sending dial queue to LCR. (dialing=%s)\n", call->dialque);

	/* send setup message to LCR */
	memset(&newparam, 0, sizeof(union parameter));
	if (call->keypad)
		strncpy(newparam.information.keypad, call->dialque, sizeof(newparam.information.keypad)-1);
	else
		strncpy(newparam.information.id, call->dialque, sizeof(newparam.information.id)-1);
	call->dialque[0] = '\0';
	send_message(MESSAGE_INFORMATION, call->ref, &newparam);
}

/*
 * in case of a bridge, the unsupported message can be forwarded directly
 * to the remote call.
 */
static void bridge_message_if_bridged(struct chan_call *call, int message_type, union parameter *param)
{
	/* check bridge */
	if (!call) return;
	if (!call->bridge_call) return;
	CDEBUG(call, NULL, "Sending message due bridging.\n");
	send_message(message_type, call->bridge_call->ref, param);
}

/*
 * send release message to LCR
 */
static void send_release(struct chan_call *call, int cause, int location)
{
	union parameter newparam;

	/* sending release */
	memset(&newparam, 0, sizeof(union parameter));
	newparam.disconnectinfo.cause = cause;
	newparam.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
	send_message(MESSAGE_RELEASE, call->ref, &newparam);
}

/*
 * check if extension matches and start asterisk
 * if it can match, proceed
 * if not, release
 */
static void lcr_start_pbx(struct chan_call *call, struct ast_channel *ast, int complete)
{
	int cause, ret;
	union parameter newparam;
#if ASTERISK_VERSION_NUM < 110000
	char *exten = ast->exten;
#else
	char s_exten[AST_MAX_EXTENSION];
	char *exten=s_exten;

	strncpy(exten, ast_channel_exten(ast), AST_MAX_EXTENSION-1);
#endif

if (!*exten)
		exten = "s";

#if ASTERISK_VERSION_NUM < 110000
	CDEBUG(call, ast, "Try to start pbx. (exten=%s context=%s complete=%s)\n", exten, ast->context, complete?"yes":"no");
#else
	CDEBUG(call, ast, "Try to start pbx. (exten=%s context=%s complete=%s)\n", exten, ast_channel_context(ast), complete?"yes":"no");
#endif

	if (complete) {
		/* if not match */
#if ASTERISK_VERSION_NUM < 110000
		if (!ast_canmatch_extension(ast, ast->context, exten, 1, call->oad)) {
CDEBUG(call, ast, "Got 'sending complete', but extension '%s' will not match at context '%s' - releasing.\n", exten, ast->context);
#else
		if (!ast_canmatch_extension(ast, ast_channel_context(ast), exten, 1, call->oad)) {
CDEBUG(call, ast, "Got 'sending complete', but extension '%s' will not match at context '%s' - releasing.\n", exten, ast_channel_context(ast));
#endif
			cause = 1;
			goto release;
		}
#if ASTERISK_VERSION_NUM < 110000
		if (!ast_exists_extension(ast, ast->context, exten, 1, call->oad)) {
			CDEBUG(call, ast, "Got 'sending complete', but extension '%s' would match at context '%s', if more digits would be dialed - releasing.\n", exten, ast->context);
#else
		if (!ast_exists_extension(ast, ast_channel_context(ast), exten, 1, call->oad)) {
			CDEBUG(call, ast, "Got 'sending complete', but extension '%s' would match at context '%s', if more digits would be dialed - releasing.\n", exten, ast_channel_context(ast));
#endif
			cause = 28;
			goto release;
		}
		CDEBUG(call, ast, "Got 'sending complete', extensions matches.\n");
		/* send setup acknowledge to lcr */
		memset(&newparam, 0, sizeof(union parameter));
		send_message(MESSAGE_PROCEEDING, call->ref, &newparam);

		/* change state */
		call->state = CHAN_LCR_STATE_IN_PROCEEDING;

		goto start;
	}

#if ASTERISK_VERSION_NUM < 110000
	if (ast_canmatch_extension(ast, ast->context, exten, 1, call->oad)) {
#else
	if (ast_canmatch_extension(ast, ast_channel_context(ast), exten, 1, call->oad)) {
#endif
		/* send setup acknowledge to lcr */
		if (call->state != CHAN_LCR_STATE_IN_DIALING) {
			memset(&newparam, 0, sizeof(union parameter));
			send_message(MESSAGE_OVERLAP, call->ref, &newparam);
		}

		/* change state */
		call->state = CHAN_LCR_STATE_IN_DIALING;

		/* if match, start pbx */
#if ASTERISK_VERSION_NUM < 110000
	if (ast_exists_extension(ast, ast->context, exten, 1, call->oad)) {
#else
	if (ast_exists_extension(ast, ast_channel_context(ast), exten, 1, call->oad)) {
#endif
			CDEBUG(call, ast, "Extensions matches.\n");
			goto start;
		}

		/* send setup acknowledge to lcr */
		if (call->state != CHAN_LCR_STATE_IN_DIALING) {
			memset(&newparam, 0, sizeof(union parameter));
			send_message(MESSAGE_OVERLAP, call->ref, &newparam);
		}

		/* change state */
		call->state = CHAN_LCR_STATE_IN_DIALING;

		/* if can match */
		CDEBUG(call, ast, "Extensions may match, if more digits are dialed.\n");
		return;
	}

#if ASTERISK_VERSION_NUM < 110000
	if (!*ast->exten) {
#else
	if (!*ast_channel_exten(ast)) {
#endif
		/* send setup acknowledge to lcr */
		if (call->state != CHAN_LCR_STATE_IN_DIALING) {
			memset(&newparam, 0, sizeof(union parameter));
			send_message(MESSAGE_OVERLAP, call->ref, &newparam);
		}

		/* change state */
		call->state = CHAN_LCR_STATE_IN_DIALING;

		/* if can match */
		CDEBUG(call, ast, "There is no 's' extension (and we tried to match it implicitly). Extensions may match, if more digits are dialed.\n");
		return;
	}

	/* if not match */
	cause = 1;
	release:
	/* release lcr */
	CDEBUG(call, ast, "Releasing due to extension missmatch.\n");
	send_release(call, cause, LOCATION_PRIVATE_LOCAL);
	call->ref = 0;
	/* release asterisk */
#if ASTERISK_VERSION_NUM < 110000
	ast->hangupcause = call->cause;
#else
	ast_channel_hangupcause_set(ast, call->cause);
#endif
	/* change to release state */
	call->state = CHAN_LCR_STATE_RELEASE;
	ast_hangup(ast); // call will be destroyed here
	return;

	start:
	/* send setup to asterisk */
	CDEBUG(call, ast, "Starting call to Asterisk due to matching extension.\n");

	#ifdef LCR_FOR_CALLWEAVER
	ast->type = "LCR";
	snprintf(ast->name, sizeof(ast->name), "%s/%s-%04x",lcr_type ,ast->cid.cid_num, ast_random() & 0xffff);
	#endif

	ret = ast_pbx_start(ast);
	if (ret < 0) {
		cause = (ret==-2)?34:27;
		goto release;
	}
	call->pbx_started = 1;
		ast_setstate(ast, AST_STATE_RING);
}

/*
 * incoming setup from LCR
 */
static void lcr_in_setup(struct chan_call *call, int message_type, union parameter *param)
{
	struct ast_channel *ast;
#ifdef AST_1_8_OR_HIGHER
	struct ast_party_redirecting *ast_redir;
	struct ast_party_caller *ast_caller;
#else
	struct ast_callerid *ast_caller;
#endif
#if ASTERISK_VERSION_NUM >= 110000
	struct ast_party_redirecting s_ast_redir;
	struct ast_party_caller s_ast_caller;
	ast_party_redirecting_init(&s_ast_redir);
	ast_party_caller_init(&s_ast_caller);
#endif
	CDEBUG(call, NULL, "Incomming setup from LCR. (callerid %s, dialing %s)\n", param->setup.callerinfo.id, param->setup.dialinginfo.id);

	/* create asterisk channel instrance */

	#ifdef LCR_FOR_CALLWEAVER
	ast = ast_channel_alloc(1);
	#endif

	#ifdef LCR_FOR_ASTERISK
#ifdef AST_1_8_OR_HIGHER
	ast = ast_channel_alloc(1, AST_STATE_RESERVED, NULL, NULL, "", NULL, "", "", 0, "%s/%d", lcr_type, ++glob_channel);
#else
	ast = ast_channel_alloc(1, AST_STATE_RESERVED, NULL, NULL, "", NULL, "", 0, "%s/%d", lcr_type, ++glob_channel);
#endif
	#endif

#if ASTERISK_VERSION_NUM < 110000
#ifdef AST_1_8_OR_HIGHER
	ast_redir = &ast->redirecting;
	ast_caller = &ast->caller;
#else
	ast_caller = &ast->cid;
#endif
#else
	ast_redir = &s_ast_redir;
	ast_caller = &s_ast_caller;
#endif

	if (!ast) {
		/* release */
		CERROR(call, NULL, "Failed to create Asterisk channel - releasing.\n");
		send_release(call, CAUSE_RESSOURCEUNAVAIL, LOCATION_PRIVATE_LOCAL);
		/* remove call */
		free_call(call);
		return;
	}
	/* link together */
	call->ast = ast;
#if ASTERISK_VERSION_NUM < 110000
	ast->tech_pvt = call;
	ast->tech = &lcr_tech;
	ast->fds[0] = call->pipe[0];
#else
	ast_channel_tech_pvt_set(ast, call);
	ast_channel_tech_set(ast, &lcr_tech);
	ast_channel_set_fd(ast, 0, call->pipe[0]);
#endif

	/* fill setup information */
	if (param->setup.dialinginfo.id)
#if ASTERISK_VERSION_NUM < 110000
		strncpy(ast->exten, param->setup.dialinginfo.id, AST_MAX_EXTENSION-1);
	if (param->setup.dialinginfo.context[0])
		strncpy(ast->context, param->setup.dialinginfo.context, AST_MAX_CONTEXT-1);
	else
		strncpy(ast->context, param->setup.callerinfo.interface, AST_MAX_CONTEXT-1);
#else
		ast_channel_exten_set(ast, param->setup.dialinginfo.id);
	if (param->setup.context[0])
		ast_channel_context_set(ast, param->setup.context);
	else
		ast_channel_context_set(ast, param->setup.callerinfo.interface);
#endif


#ifdef AST_1_8_OR_HIGHER
	if (param->setup.callerinfo.id[0]) {
		ast_caller->id.number.valid = 1;
		ast_caller->id.number.str = strdup(param->setup.callerinfo.id);
		if (!param->setup.callerinfo.id[0]) {
			ast_caller->id.number.presentation = AST_PRES_RESTRICTED;
			ast_caller->id.number.plan = (0 << 4) | 1;
		}
		switch (param->setup.callerinfo.present) {
			case INFO_PRESENT_ALLOWED:
				ast_caller->id.number.presentation = AST_PRES_ALLOWED;
			break;
			case INFO_PRESENT_RESTRICTED:
				ast_caller->id.number.presentation = AST_PRES_RESTRICTED;
			break;
			default:
				ast_caller->id.number.presentation = AST_PRES_UNAVAILABLE;
		}
		switch (param->setup.callerinfo.screen) {
			case INFO_SCREEN_USER:
				ast_caller->id.number.presentation |= AST_PRES_USER_NUMBER_UNSCREENED;
			break;
			case INFO_SCREEN_USER_VERIFIED_PASSED:
				ast_caller->id.number.presentation |= AST_PRES_USER_NUMBER_PASSED_SCREEN;
			break;
			case INFO_SCREEN_USER_VERIFIED_FAILED:
				ast_caller->id.number.presentation |= AST_PRES_USER_NUMBER_FAILED_SCREEN;
			break;
			default:
				ast_caller->id.number.presentation |= AST_PRES_NETWORK_NUMBER;
		}
		switch (param->setup.callerinfo.ntype) {
			case INFO_NTYPE_SUBSCRIBER:
				ast_caller->id.number.plan = (4 << 4) | 1;
			break;
			case INFO_NTYPE_NATIONAL:
				ast_caller->id.number.plan = (2 << 4) | 1;
			break;
			case INFO_NTYPE_INTERNATIONAL:
				ast_caller->id.number.plan = (1 << 4) | 1;
			break;
			default:
				ast_caller->id.number.plan = (0 << 4) | 1;
		}
	}
	if (param->setup.callerinfo.id2[0]) {
		ast_caller->ani.number.valid = 1;
		ast_caller->ani.number.str = strdup(param->setup.callerinfo.id2);
		switch (param->setup.callerinfo.present2) {
			case INFO_PRESENT_ALLOWED:
				ast_caller->ani.number.presentation = AST_PRES_ALLOWED;
			break;
			case INFO_PRESENT_RESTRICTED:
				ast_caller->ani.number.presentation = AST_PRES_RESTRICTED;
			break;
			default:
				ast_caller->ani.number.presentation = AST_PRES_UNAVAILABLE;
		}
		switch (param->setup.callerinfo.screen2) {
			case INFO_SCREEN_USER:
				ast_caller->ani.number.presentation |= AST_PRES_USER_NUMBER_UNSCREENED;
			break;
			case INFO_SCREEN_USER_VERIFIED_PASSED:
				ast_caller->ani.number.presentation |= AST_PRES_USER_NUMBER_PASSED_SCREEN;
			break;
			case INFO_SCREEN_USER_VERIFIED_FAILED:
				ast_caller->ani.number.presentation |= AST_PRES_USER_NUMBER_FAILED_SCREEN;
			break;
			default:
				ast_caller->ani.number.presentation |= AST_PRES_NETWORK_NUMBER;
		}
		switch (param->setup.callerinfo.ntype2) {
			case INFO_NTYPE_SUBSCRIBER:
				ast_caller->ani.number.plan = (4 << 4) | 1;
			break;
			case INFO_NTYPE_NATIONAL:
				ast_caller->ani.number.plan = (2 << 4) | 1;
			break;
			case INFO_NTYPE_INTERNATIONAL:
				ast_caller->ani.number.plan = (1 << 4) | 1;
			break;
			default:
				ast_caller->ani.number.plan = (0 << 4) | 1;
		}
	}
	if (param->setup.callerinfo.name[0]) {
		ast_caller->id.name.valid = 1;
		ast_caller->id.name.str = strdup(param->setup.callerinfo.name);
	}
#if ASTERISK_VERSION_NUM >= 110000
	ast_channel_caller_set(ast, ast_caller);
#endif
	if (param->setup.redirinfo.id[0]) {
		ast_redir->from.number.valid = 1;
		ast_redir->from.number.str = strdup(param->setup.redirinfo.id);
		switch (param->setup.redirinfo.present) {
			case INFO_PRESENT_ALLOWED:
				ast_redir->from.number.presentation = AST_PRES_ALLOWED;
			break;
			case INFO_PRESENT_RESTRICTED:
				ast_redir->from.number.presentation = AST_PRES_RESTRICTED;
			break;
			default:
				ast_redir->from.number.presentation = AST_PRES_UNAVAILABLE;
		}
		switch (param->setup.redirinfo.screen) {
			case INFO_SCREEN_USER:
				ast_redir->from.number.presentation |= AST_PRES_USER_NUMBER_UNSCREENED;
			break;
			case INFO_SCREEN_USER_VERIFIED_PASSED:
				ast_redir->from.number.presentation |= AST_PRES_USER_NUMBER_PASSED_SCREEN;
			break;
			case INFO_SCREEN_USER_VERIFIED_FAILED:
				ast_redir->from.number.presentation |= AST_PRES_USER_NUMBER_FAILED_SCREEN;
			break;
			default:
				ast_redir->from.number.presentation |= AST_PRES_NETWORK_NUMBER;
		}
		switch (param->setup.redirinfo.ntype) {
			case INFO_NTYPE_SUBSCRIBER:
				ast_redir->from.number.plan = (4 << 4) | 1;
			break;
			case INFO_NTYPE_NATIONAL:
				ast_redir->from.number.plan = (2 << 4) | 1;
			break;
			case INFO_NTYPE_INTERNATIONAL:
				ast_redir->from.number.plan = (1 << 4) | 1;
			break;
			default:
				ast_redir->from.number.plan = (0 << 4) | 1;
		}
#if ASTERISK_VERSION_NUM >= 110000
		ast_channel_redirecting_set(ast, ast_redir);
#endif
	}
#else
	memset(&ast->cid, 0, sizeof(ast->cid));
	if (param->setup.callerinfo.id[0])
		ast->cid.cid_num = strdup(param->setup.callerinfo.id);
	if (param->setup.callerinfo.id2[0])
		ast->cid.cid_ani = strdup(param->setup.callerinfo.id2);
	if (param->setup.callerinfo.name[0])
		ast->cid.cid_name = strdup(param->setup.callerinfo.name);
	if (param->setup.redirinfo.id[0])
		ast->cid.cid_rdnis = strdup(numberrize_callerinfo(param->setup.redirinfo.id, param->setup.redirinfo.ntype, options.national, options.international));
	switch (param->setup.callerinfo.present) {
		case INFO_PRESENT_ALLOWED:
			ast->cid.cid_pres = AST_PRES_ALLOWED;
		break;
		case INFO_PRESENT_RESTRICTED:
			ast->cid.cid_pres = AST_PRES_RESTRICTED;
		break;
		default:
			ast->cid.cid_pres = AST_PRES_UNAVAILABLE;
	}
	switch (param->setup.callerinfo.ntype) {
		case INFO_NTYPE_SUBSCRIBER:
			ast->cid.cid_ton = 4;
		break;
		case INFO_NTYPE_NATIONAL:
			ast->cid.cid_ton = 2;
		break;
		case INFO_NTYPE_INTERNATIONAL:
			ast->cid.cid_ton = 1;
		break;
		default:
			ast->cid.cid_ton = 0;
	}
#endif

#if ASTERISK_VERSION_NUM < 110000
	ast->transfercapability = param->setup.capainfo.bearer_capa;
#else
	ast_channel_transfercapability_set(ast, param->setup.capainfo.bearer_capa);
#endif
	/* enable hdlc if transcap is data */
	if (param->setup.capainfo.source_mode == B_MODE_HDLC)
		call->hdlc = 1;
	strncpy(call->oad, numberrize_callerinfo(param->setup.callerinfo.id, param->setup.callerinfo.ntype, options.national, options.international), sizeof(call->oad)-1);

	/* configure channel */
#if ASTERISK_VERSION_NUM < 100000
	ast->nativeformats = (options.law=='a')?AST_FORMAT_ALAW:AST_FORMAT_ULAW;
	ast->readformat = ast->rawreadformat = ast->nativeformats;
	ast->writeformat = ast->rawwriteformat =  ast->nativeformats;
#else
#if ASTERISK_VERSION_NUM < 110000
	ast_format_set(&ast->rawwriteformat ,(options.law=='a')?AST_FORMAT_ALAW:AST_FORMAT_ULAW , 0);
	ast_format_copy(&ast->rawreadformat, &ast->rawwriteformat);
	ast_format_cap_set(ast->nativeformats, &ast->rawwriteformat);
	ast_set_write_format(ast, &ast->rawwriteformat);
	ast_set_read_format(ast, &ast->rawreadformat);
#else
	ast_format_set(ast_channel_rawwriteformat(ast) ,(options.law=='a')?AST_FORMAT_ALAW:AST_FORMAT_ULAW , 0);
	ast_format_copy(ast_channel_rawreadformat(ast), ast_channel_rawwriteformat(ast));
	ast_format_cap_set(ast_channel_nativeformats(ast), ast_channel_rawwriteformat(ast));
	ast_set_write_format(ast, ast_channel_rawwriteformat(ast));
	ast_set_read_format(ast, ast_channel_rawreadformat(ast));
#endif
#endif
#if ASTERISK_VERSION_NUM < 110000
	ast->priority = 1;
	ast->hangupcause = 0;
#else
	ast_channel_priority_set(ast, 1);
	ast_channel_hangupcause_set(ast, 0);
#endif

	/* change state */
	call->state = CHAN_LCR_STATE_IN_SETUP;

	if (!call->pbx_started)
		lcr_start_pbx(call, ast, param->setup.dialinginfo.sending_complete);
}

/*
 * incoming setup acknowledge from LCR
 */
static void lcr_in_overlap(struct chan_call *call, int message_type, union parameter *param)
{
	if (!call->ast) return;

	CDEBUG(call, call->ast, "Incomming setup acknowledge from LCR.\n");

	/* send pending digits in dialque */
	if (call->dialque[0])
		send_dialque_to_lcr(call);
	/* change to overlap state */
	call->state = CHAN_LCR_STATE_OUT_DIALING;
}

/*
 * incoming proceeding from LCR
 */
static void lcr_in_proceeding(struct chan_call *call, int message_type, union parameter *param)
{
	CDEBUG(call, call->ast, "Incomming proceeding from LCR.\n");

	/* change state */
	call->state = CHAN_LCR_STATE_OUT_PROCEEDING;
	/* queue event for asterisk */
	if (call->ast && call->pbx_started) {
		if (!wake_global) {
			wake_global = 1;
			char byte = 0;
			int rc;
			rc = write(wake_pipe[1], &byte, 1);
		}
		strncat(call->queue_string, "P", sizeof(call->queue_string)-1);
	}

}

/*
 * incoming alerting from LCR
 */
static void lcr_in_alerting(struct chan_call *call, int message_type, union parameter *param)
{
	CDEBUG(call, call->ast, "Incomming alerting from LCR.\n");

	/* change state */
	call->state = CHAN_LCR_STATE_OUT_ALERTING;
	/* queue event to asterisk */
	if (call->ast && call->pbx_started) {
		if (!wake_global) {
			wake_global = 1;
			char byte = 0;
			int rc;
			rc = write(wake_pipe[1], &byte, 1);
		}
		strncat(call->queue_string, "R", sizeof(call->queue_string)-1);
	}
}

/*
 * incoming connect from LCR
 */
static void lcr_in_connect(struct chan_call *call, int message_type, union parameter *param)
{
	CDEBUG(call, call->ast, "Incomming connect (answer) from LCR.\n");

	/* change state */
	call->state = CHAN_LCR_STATE_CONNECT;
	/* copy connectinfo */
	memcpy(&call->connectinfo, &param->connectinfo, sizeof(struct connect_info));
	/* queue event to asterisk */
	if (call->ast && call->pbx_started) {
		if (!wake_global) {
			wake_global = 1;
			char byte = 0;
			int rc;
			rc = write(wake_pipe[1], &byte, 1);
		}
		strncat(call->queue_string, "N", sizeof(call->queue_string)-1);
	}
}

/*
 * incoming disconnect from LCR
 */
static void lcr_in_disconnect(struct chan_call *call, int message_type, union parameter *param)
{
	struct ast_channel *ast = call->ast;

	CDEBUG(call, call->ast, "Incomming disconnect from LCR. (cause=%d)\n", param->disconnectinfo.cause);

	/* change state */
	call->state = CHAN_LCR_STATE_IN_DISCONNECT;
	/* save cause */
	call->cause = param->disconnectinfo.cause;
	call->location = param->disconnectinfo.location;
	/* if bridge, forward disconnect and return */
#ifdef TODO
	feature flag
	if (call->bridge_call) {
		CDEBUG(call, call->ast, "Only signal disconnect via bridge.\n");
		bridge_message_if_bridged(call, message_type, param);
		return;
	}
#endif
	/* release lcr with same cause */
	send_release(call, call->cause, call->location);
	call->ref = 0;
	/* change to release state */
	call->state = CHAN_LCR_STATE_RELEASE;
	/* queue release asterisk */
	if (ast) {
#if ASTERISK_VERSION_NUM < 110000
		ast->hangupcause = call->cause;
#else
		ast_channel_hangupcause_set(ast, call->cause);
#endif
		if (call->pbx_started) {
			if (!wake_global) {
				wake_global = 1;
				char byte = 0;
				int rc;
				rc = write(wake_pipe[1], &byte, 1);
			}
			strcpy(call->queue_string, "H"); // overwrite other indications
		} else {
			ast_hangup(ast); // call will be destroyed here
		}
	}
}

/*
 * incoming release from LCR
 */
static void lcr_in_release(struct chan_call *call, int message_type, union parameter *param)
{
	struct ast_channel *ast = call->ast;

	CDEBUG(call, call->ast, "Incomming release from LCR, releasing ref. (cause=%d)\n", param->disconnectinfo.cause);

	/* release ref */
	call->ref = 0;
	/* change to release state */
	call->state = CHAN_LCR_STATE_RELEASE;
	/* copy release info */
	if (!call->cause) {
		call->cause = param->disconnectinfo.cause;
		call->location = param->disconnectinfo.location;
	}
	/* if we have an asterisk instance, queue hangup, else we are done */
	if (ast) {
#if ASTERISK_VERSION_NUM < 110000
		ast->hangupcause = call->cause;
#else
		ast_channel_hangupcause_set(ast, call->cause);
#endif
		if (call->pbx_started) {
			if (!wake_global) {
				wake_global = 1;
				char byte = 0;
				int rc;
				rc = write(wake_pipe[1], &byte, 1);
			}
			strcpy(call->queue_string, "H");
		} else {
			ast_hangup(ast); // call will be destroyed here
		}
	} else {
		free_call(call);
	}

}

/*
 * incoming information from LCR
 */
static void lcr_in_information(struct chan_call *call, int message_type, union parameter *param)
{
	struct ast_channel *ast = call->ast;

	CDEBUG(call, call->ast, "Incoming information from LCR. (dialing=%s)\n", param->information.id);

	if (!ast) return;

	/* pbx not started */
	if (!call->pbx_started) {
		CDEBUG(call, call->ast, "Asterisk not started, adding digits to number.\n");
#if ASTERISK_VERSION_NUM < 110000
		strncat(ast->exten, param->information.id, AST_MAX_EXTENSION-1);
#else
		ast_channel_exten_set(ast, param->information.id);
#endif
		lcr_start_pbx(call, ast, param->information.sending_complete);
		return;
	}

	/* change dailing state after setup */
	if (call->state == CHAN_LCR_STATE_IN_SETUP) {
		CDEBUG(call, call->ast, "Changing from SETUP to DIALING state.\n");
		call->state = CHAN_LCR_STATE_IN_DIALING;
//		ast_setstate(ast, AST_STATE_DIALING);
	}

	/* queue digits */
	if (call->state == CHAN_LCR_STATE_IN_DIALING && param->information.id[0]) {
		if (!wake_global) {
			wake_global = 1;
			char byte = 0;
			int rc;
			rc = write(wake_pipe[1], &byte, 1);
		}
		strncat(call->queue_string, param->information.id, sizeof(call->queue_string)-1);
	}

	/* use bridge to forware message not supported by asterisk */
	if (call->state == CHAN_LCR_STATE_CONNECT) {
		if (call->bridge_call) {
			CDEBUG(call, call->ast, "Call is connected, bridging.\n");
			bridge_message_if_bridged(call, message_type, param);
		} else {
			if (call->dsp_dtmf) {
				if (!wake_global) {
					wake_global = 1;
					char byte = 0;
					int rc;
					rc = write(wake_pipe[1], &byte, 1);
				}
				strncat(call->queue_string, param->information.id, sizeof(call->queue_string)-1);
			} else
				CDEBUG(call, call->ast, "LCR's DTMF detection is disabled.\n");
		}
	}
}

/*
 * incoming information from LCR
 */
static void lcr_in_notify(struct chan_call *call, int message_type, union parameter *param)
{
	CDEBUG(call, call->ast, "Incomming notify from LCR. (notify=%d)\n", param->notifyinfo.notify);

	if (!call->ast) return;

	/* use bridge to forware message not supported by asterisk */
	bridge_message_if_bridged(call, message_type, param);
}

/*
 * incoming information from LCR
 */
static void lcr_in_facility(struct chan_call *call, int message_type, union parameter *param)
{
	CDEBUG(call, call->ast, "Incomming facility from LCR.\n");

	if (!call->ast) return;

	/* use bridge to forware message not supported by asterisk */
	bridge_message_if_bridged(call, message_type, param);
}

/*
 * incoming pattern from LCR
 */
static void lcr_in_pattern(struct chan_call *call, int message_type, union parameter *param)
{
	union parameter newparam;

	CDEBUG(call, call->ast, "Incomming pattern indication from LCR.\n");

	if (!call->ast) return;

	/* pattern are indicated only once */
	if (call->has_pattern)
		return;
	call->has_pattern = 1;

	/* request bchannel */
	CDEBUG(call, call->ast, "Requesting audio path (ref=%d)\n", call->ref);
	memset(&newparam, 0, sizeof(union parameter));
	send_message(MESSAGE_AUDIOPATH, call->ref, &newparam);

	/* queue PROGRESS, because tones are available */
	if (call->ast && call->pbx_started) {
		if (!wake_global) {
			wake_global = 1;
			char byte = 0;
			int rc;
			rc = write(wake_pipe[1], &byte, 1);
		}
		strncat(call->queue_string, "T", sizeof(call->queue_string)-1);
	}
}

/*
 * got dtmf from bchannel (locked state)
 */
void lcr_in_dtmf(struct chan_call *call, int val)
{
	struct ast_channel *ast = call->ast;
	char digit[2];

	if (!ast)
		return;
	if (!call->pbx_started)
		return;

	if (!call->dsp_dtmf) {
		CDEBUG(call, call->ast, "Recognised DTMF digit '%c' by LCR, but ignoring. (disabled by option)\n", val);
		return;
	}

	CDEBUG(call, call->ast, "Recognised DTMF digit '%c'.\n", val);
	digit[0] = val;
	digit[1] = '\0';
	if (!wake_global) {
		wake_global = 1;
		char byte = 0;
		int rc;
		rc = write(wake_pipe[1], &byte, 1);
	}
	strncat(call->queue_string, digit, sizeof(call->queue_string)-1);
}

/*
 * message received from LCR
 */
int receive_message(int message_type, unsigned int ref, union parameter *param)
{
	struct chan_call *call;
	union parameter newparam;
	int rc = 0;

	memset(&newparam, 0, sizeof(union parameter));

	/* handle new ref */
	if (message_type == MESSAGE_NEWREF) {
		if (param->newref.direction) {
			/* new ref from lcr */
			CDEBUG(NULL, NULL, "Received new ref by LCR, due to incomming call. (ref=%ld)\n", ref);
			if (!ref || find_call_ref(ref)) {
				CERROR(NULL, NULL, "Illegal new ref %ld received.\n", ref);
				return -1;
			}
			/* allocate new call instance */
			call = alloc_call();
			/* new state */
			call->state = CHAN_LCR_STATE_IN_PREPARE;
			/* set ref */
			call->ref = ref;
			call->ref_was_assigned = 1;
			/* set dtmf (default, use option 'n' to disable */
			call->dsp_dtmf = 1;
			/* wait for setup (or release from asterisk) */
		} else {
			/* new ref, as requested from this remote application */
			CDEBUG(NULL, NULL, "Received new ref by LCR, as requested from chan_lcr. (ref=%ld)\n", ref);
			call = find_call_ref(0);
			if (!call) {
				/* send release, if ref does not exist */
				CERROR(NULL, NULL, "No call found, that requests a ref.\n");
				return 0;
			}
			/* store new ref */
			call->ref = ref;
			call->ref_was_assigned = 1;
			/* set dtmf (default, use option 'n' to disable */
			call->dsp_dtmf = 1;
			/* send pending setup info */
			if (call->state == CHAN_LCR_STATE_OUT_PREPARE)
				send_setup_to_lcr(call);
			/* release if asterisk has signed off */
			else if (call->state == CHAN_LCR_STATE_RELEASE) {
				/* send release */
				if (call->cause)
					send_release(call, call->cause, call->location);
				else
					send_release(call, CAUSE_NORMAL, LOCATION_PRIVATE_LOCAL);
				/* free call */
				free_call(call);
				return 0;
			}
		}
		send_message(MESSAGE_ENABLEKEYPAD, call->ref, &newparam);
		return 0;
	}

	/* check ref */
	if (!ref) {
		CERROR(NULL, NULL, "Received message %d without ref.\n", message_type);
		return -1;
	}
	call = find_call_ref(ref);
	if (!call) {
		/* ignore ref that is not used (anymore) */
		CDEBUG(NULL, NULL, "Message %d from LCR ignored, because no call instance found.\n", message_type);
		return 0;
	}

	/* handle messages */
	switch(message_type) {
		case MESSAGE_SETUP:
		lcr_in_setup(call, message_type, param);
		break;

		case MESSAGE_OVERLAP:
		lcr_in_overlap(call, message_type, param);
		break;

		case MESSAGE_PROCEEDING:
		lcr_in_proceeding(call, message_type, param);
		break;

		case MESSAGE_ALERTING:
		lcr_in_alerting(call, message_type, param);
		break;

		case MESSAGE_CONNECT:
		lcr_in_connect(call, message_type, param);
		break;

		case MESSAGE_DISCONNECT:
		lcr_in_disconnect(call, message_type, param);
		break;

		case MESSAGE_RELEASE:
		lcr_in_release(call, message_type, param);
		break;

		case MESSAGE_INFORMATION:
		lcr_in_information(call, message_type, param);
		break;

		case MESSAGE_NOTIFY:
		lcr_in_notify(call, message_type, param);
		break;

		case MESSAGE_FACILITY:
		lcr_in_facility(call, message_type, param);
		break;

		case MESSAGE_PATTERN: // audio available from LCR
		if (!call->has_pattern)
			lcr_in_pattern(call, message_type, param);
		break;

		case MESSAGE_NOPATTERN: // audio not available from LCR
		break;

		case MESSAGE_AUDIOPATH: // if remote audio connected or hold
		call->audiopath = param->audiopath;
		break;

		case MESSAGE_TRAFFIC: // if remote audio connected or hold
		{
			unsigned char *p = param->traffic.data;
			int i, len = param->traffic.len;
			for (i = 0; i < len; i++, p++)
				*p = flip_bits[*p];
		}
		rc = write(call->pipe[1], param->traffic.data, param->traffic.len);
		break;

		case MESSAGE_DTMF:
		lcr_in_dtmf(call, param->dtmf);
		break;

		default:
		CDEBUG(call, call->ast, "Message %d from LCR unhandled.\n", message_type);
		break;
	}
	return rc;
}

/*
 * release all calls (due to broken socket)
 */
static void release_all_calls(void)
{
	struct chan_call *call;

again:
	call = call_first;
	while(call) {
		/* no ast, so we may directly free call */
		if (!call->ast) {
			CDEBUG(call, NULL, "Freeing call, because no Asterisk channel is linked.\n");
			free_call(call);
			goto again;
		}
		/* already in release process */
		if (call->state == CHAN_LCR_STATE_RELEASE) {
			call = call->next;
			continue;
		}
		/* release or queue release */
		call->ref = 0;
		call->state = CHAN_LCR_STATE_RELEASE;
		if (!call->pbx_started) {
			CDEBUG(call, call->ast, "Releasing call, because no Asterisk channel is not started.\n");
			ast_hangup(call->ast); // call will be destroyed here
			goto again;
		}
		CDEBUG(call, call->ast, "Queue call release, because Asterisk channel is running.\n");
		if (!wake_global) {
			wake_global = 1;
			char byte = 0;
			int rc;
			rc = write(wake_pipe[1], &byte, 1);
		}
		strcpy(call->queue_string, "H");
		call = call->next;
	}
}

void close_socket(void);

/* asterisk handler
 * warning! not thread safe
 * returns -1 for socket error, 0 for no work, 1 for work
 */
static int handle_socket(struct lcr_fd *fd, unsigned int what, void *instance, int index)
{
	int len;
	struct admin_list *admin;
	struct admin_message msg;

	if ((what & LCR_FD_READ)) {
		/* read from socket */
		len = read(lcr_sock, &msg, sizeof(msg));
		if (len == 0) {
			CERROR(NULL, NULL, "Socket closed.(read)\n");
			error:
			CERROR(NULL, NULL, "Handling of socket failed - closing for some seconds.\n");
			close_socket();
			release_all_calls();
			schedule_timer(&socket_retry, SOCKET_RETRY_TIMER, 0);
			return 0;
		}
		if (len > 0) {
			if (len != sizeof(msg)) {
				CERROR(NULL, NULL, "Socket short read. (len %d)\n", len);
				goto error;
			}
			if (msg.message != ADMIN_MESSAGE) {
				CERROR(NULL, NULL, "Socket received illegal message %d.\n", msg.message);
				goto error;
			}
			receive_message(msg.u.msg.type, msg.u.msg.ref, &msg.u.msg.param);
		} else {
			CERROR(NULL, NULL, "Socket failed (errno %d).\n", errno);
			goto error;
		}
	}

	if ((what & LCR_FD_WRITE)) {
		/* write to socket */
		if (!admin_first) {
			socket_fd.when &= ~LCR_FD_WRITE;
			return 0;
		}
		admin = admin_first;
		len = write(lcr_sock, &admin->msg, sizeof(msg));
		if (len == 0) {
			CERROR(NULL, NULL, "Socket closed.(write)\n");
			goto error;
		}
		if (len > 0) {
			if (len != sizeof(msg)) {
				CERROR(NULL, NULL, "Socket short write. (len %d)\n", len);
				goto error;
			}
			/* free head */
			admin_first = admin->next;
			free(admin);
			global_change = 1;
		} else {
			CERROR(NULL, NULL, "Socket failed (errno %d).\n", errno);
			goto error;
		}
	}

	return 0;
}

/*
 * open and close socket and thread
 */
int open_socket(void)
{
	int conn;
	struct sockaddr_un sock_address;
	union parameter param;

	/* open socket */
	if ((lcr_sock = socket(PF_UNIX, SOCK_STREAM, 0)) < 0) {
		CERROR(NULL, NULL, "Failed to create socket.\n");
		return lcr_sock;
	}

	/* set socket address and name */
	memset(&sock_address, 0, sizeof(sock_address));
	sock_address.sun_family = PF_UNIX;
	sprintf(sock_address.sun_path, SOCKET_NAME, options.lock);

	/* connect socket */
	if ((conn = connect(lcr_sock, (struct sockaddr *)&sock_address, SUN_LEN(&sock_address))) < 0) {
		close(lcr_sock);
		lcr_sock = -1;
		CDEBUG(NULL, NULL, "Failed to connect to socket '%s'. Is LCR running?\n", sock_address.sun_path);
		return conn;
	}

	/* register socket fd */
	memset(&socket_fd, 0, sizeof(socket_fd));
	socket_fd.fd = lcr_sock;
	register_fd(&socket_fd, LCR_FD_READ | LCR_FD_EXCEPT, handle_socket, NULL, 0);

	/* enque hello message */
	memset(&param, 0, sizeof(param));
	strcpy(param.hello.application, "asterisk");
	send_message(MESSAGE_HELLO, 0, &param);

	return lcr_sock;
}

void close_socket(void)
{
	struct admin_list *admin, *temp;

	/* socket not created */
	if (lcr_sock < 0)
		return;

	unregister_fd(&socket_fd);

	/* flush pending messages */
	admin = admin_first;
	while(admin) {
		temp = admin;
		admin = admin->next;
		free(temp);
	}
	admin_first = NULL;

	/* close socket */
	close(lcr_sock);
	lcr_sock = -1;
	global_change = 1;
}


/* sending queue to asterisk */
static int wake_event(struct lcr_fd *fd, unsigned int what, void *instance, int index)
{
	char byte;
	int rc;

	rc = read(wake_pipe[0], &byte, 1);

	wake_global = 0;

	return 0;
}

static void handle_queue()
{
	struct chan_call *call;
	struct ast_channel *ast;
	struct ast_frame fr;
	char *p;

again:
	call = call_first;
	while(call) {
		p = call->queue_string;
		ast = call->ast;
		if (*p && ast) {
			if (ast_channel_trylock(ast)) {
				ast_mutex_unlock(&chan_lock);
				usleep(1);
				ast_mutex_lock(&chan_lock);
				goto again;
			}
			while(*p) {
				switch (*p) {
				case 'T':
					CDEBUG(call, ast, "Sending queued PROGRESS to Asterisk.\n");
					ast_queue_control(ast, AST_CONTROL_PROGRESS);
					break;
				case 'P':
					CDEBUG(call, ast, "Sending queued PROCEEDING to Asterisk.\n");
					ast_queue_control(ast, AST_CONTROL_PROCEEDING);
					break;
				case 'R':
					CDEBUG(call, ast, "Sending queued RINGING to Asterisk.\n");
					ast_queue_control(ast, AST_CONTROL_RINGING);
					ast_setstate(ast, AST_STATE_RINGING);
					break;
				case 'N':
					CDEBUG(call, ast, "Sending queued ANSWER to Asterisk.\n");
					ast_queue_control(ast, AST_CONTROL_ANSWER);
					break;
				case 'H':
					CDEBUG(call, ast, "Sending queued HANGUP to Asterisk.\n");
					ast_queue_hangup(ast);
					break;
				case '1': case '2': case '3': case 'A':
				case '4': case '5': case '6': case 'B':
				case '7': case '8': case '9': case 'C':
				case '*': case '0': case '#': case 'D':
					CDEBUG(call, ast, "Sending queued digit '%c' to Asterisk.\n", *p);
					/* send digit to asterisk */
					memset(&fr, 0, sizeof(fr));

					#ifdef LCR_FOR_ASTERISK
					fr.frametype = AST_FRAME_DTMF_BEGIN;
					#endif

					#ifdef LCR_FOR_CALLWEAVER
					fr.frametype = AST_FRAME_DTMF;
					#endif

#ifdef AST_1_8_OR_HIGHER
					fr.subclass.integer = *p;
#else
					fr.subclass = *p;
#endif
					fr.delivery = ast_tv(0, 0);
					ast_queue_frame(ast, &fr);

					#ifdef LCR_FOR_ASTERISK
					fr.frametype = AST_FRAME_DTMF_END;
					ast_queue_frame(ast, &fr);
					#endif

					break;
				default:
					CDEBUG(call, ast, "Ignoring queued digit 0x%02x.\n", *p);
				}
				p++;
			}
			call->queue_string[0] = '\0';
			ast_channel_unlock(ast);
		}
		call = call->next;
	}
}

static int handle_retry(struct lcr_timer *timer, void *instance, int index)
{
	CDEBUG(NULL, NULL, "Retry to open socket.\n");
	if (open_socket() < 0)
		schedule_timer(&socket_retry, SOCKET_RETRY_TIMER, 0);

	return 0;
}

void lock_chan(void)
{
	ast_mutex_lock(&chan_lock);
}

void unlock_chan(void)
{
	ast_mutex_unlock(&chan_lock);
}

/* chan_lcr thread */
static void *chan_thread(void *arg)
{
	if (pipe(wake_pipe) < 0) {
		CERROR(NULL, NULL, "Failed to open pipe.\n");
		return NULL;
	}
	memset(&wake_fd, 0, sizeof(wake_fd));
	wake_fd.fd = wake_pipe[0];
	register_fd(&wake_fd, LCR_FD_READ, wake_event, NULL, 0);

	memset(&socket_retry, 0, sizeof(socket_retry));
	add_timer(&socket_retry, handle_retry, NULL, 0);

	/* open socket the first time */
	handle_retry(NULL, NULL, 0);

	ast_mutex_lock(&chan_lock);

	while(1) {
		handle_queue();
		select_main(0, &global_change, lock_chan, unlock_chan);
	}

	return NULL;
}

/*
 * new asterisk instance
 */
static
#ifdef AST_1_8_OR_HIGHER
#if ASTERISK_VERSION_NUM < 100000
struct ast_channel *lcr_request(const char *type, format_t format, const struct ast_channel *requestor, void *data, int *cause)
#else
struct ast_channel *lcr_request(const char *type, struct ast_format_cap *format, const struct ast_channel *requestor, void *data, int *cause)
#endif
#else
struct ast_channel *lcr_request(const char *type, int format, void *data, int *cause)
#endif
{
	char exten[256], *dial, *interface, *opt;
	struct ast_channel *ast;
	struct chan_call *call;
#ifdef AST_1_8_OR_HIGHER
	const struct ast_party_redirecting *req_redir;
	const struct ast_party_caller *req_caller;
#endif

	ast_mutex_lock(&chan_lock);
	CDEBUG(NULL, NULL, "Received request from Asterisk. (data=%s)\n", (char *)data);

	/* if socket is closed */
	if (lcr_sock < 0) {
		CERROR(NULL, NULL, "Rejecting call from Asterisk, because LCR not running.\n");
		ast_mutex_unlock(&chan_lock);
		return NULL;
	}

	/* create call instance */
	call = alloc_call();
	if (!call) {
		/* failed to create instance */
		ast_mutex_unlock(&chan_lock);
		return NULL;
	}

	/* create asterisk channel instrance */

	#ifdef LCR_FOR_ASTERISK
#ifdef AST_1_8_OR_HIGHER
	ast = ast_channel_alloc(1, AST_STATE_RESERVED, NULL, NULL, NULL, NULL, NULL, NULL, 0, "%s/%d", lcr_type, ++glob_channel);
#else
	ast = ast_channel_alloc(1, AST_STATE_RESERVED, NULL, NULL, "", NULL, "", 0, "%s/%d", lcr_type, ++glob_channel);
#endif
	#endif

	#ifdef LCR_FOR_CALLWEAVER
	ast = ast_channel_alloc(1);
	#endif

	if (!ast) {
		CERROR(NULL, NULL, "Failed to create Asterisk channel.\n");
		free_call(call);
		/* failed to create instance */
		ast_mutex_unlock(&chan_lock);
		return NULL;
	}
#if ASTERISK_VERSION_NUM < 110000
	ast->tech = &lcr_tech;
	ast->tech_pvt = (void *)1L; // set pointer or asterisk will not call
#ifdef AST_1_8_OR_HIGHER
	req_redir = &requestor->redirecting;
	req_caller = &requestor->caller;
#endif
#else
	ast_channel_tech_set(ast, &lcr_tech);
	ast_channel_tech_pvt_set(ast, (void *)1L); // set pointer or asterisk will not call
	req_redir = ast_channel_redirecting(requestor);
	req_caller = ast_channel_caller(requestor);
#endif
	/* configure channel */
#if ASTERISK_VERSION_NUM < 100000
#if ASTERISK_VERSION_NUM < 110000
	ast->nativeformats = (options.law=='a')?AST_FORMAT_ALAW:AST_FORMAT_ULAW;
	ast->readformat = ast->rawreadformat = ast->nativeformats;
	ast->writeformat = ast->rawwriteformat =  ast->nativeformats;
#else
	ast_channel_nativeformats_set(ast, (options.law=='a')?AST_FORMAT_ALAW:AST_FORMAT_ULAW);
	ast->readformat = ast->rawreadformat = ast_channel_nativeformats(ast);
	ast->writeformat = ast->rawwriteformat =  ast_channel_nativeformats(ast);
#endif
#else
#if ASTERISK_VERSION_NUM < 110000
	ast_format_set(&ast->rawwriteformat ,(options.law=='a')?AST_FORMAT_ALAW:AST_FORMAT_ULAW , 0);
	ast_format_copy(&ast->rawreadformat, &ast->rawwriteformat);
	ast_format_cap_set(ast->nativeformats, &ast->rawwriteformat);
	ast_set_write_format(ast, &ast->rawwriteformat);
	ast_set_read_format(ast, &ast->rawreadformat);
#else
	ast_format_set(ast_channel_rawwriteformat(ast) ,(options.law=='a')?AST_FORMAT_ALAW:AST_FORMAT_ULAW , 0);
	ast_format_copy(ast_channel_rawreadformat(ast), ast_channel_rawwriteformat(ast));
	ast_format_cap_set(ast_channel_nativeformats(ast), ast_channel_rawwriteformat(ast));
	ast_set_write_format(ast, ast_channel_rawwriteformat(ast));
	ast_set_read_format(ast, ast_channel_rawreadformat(ast));
#endif
#endif
#if ASTERISK_VERSION_NUM < 110000
	ast->priority = 1;
	ast->hangupcause = 0;
#else
	ast_channel_priority_set(ast, 1);
	ast_channel_hangupcause_set(ast, 0);
#endif

	/* link together */
	call->ast = ast;
#if ASTERISK_VERSION_NUM < 110000
	ast->tech_pvt = call;
	ast->fds[0] = call->pipe[0];
#else
	ast_channel_tech_pvt_set(ast, call);
	ast_channel_set_fd(ast, 0, call->pipe[0]);
#endif
	call->pbx_started = 0;
	/* set state */
	call->state = CHAN_LCR_STATE_OUT_PREPARE;

	/*
	 * Extract interface, dialstring, options from data.
	 * Formats can be:
	 * 	<dialstring>
	 * 	<interface>/<dialstring>
	 * 	<interface>/<dialstring>/options
	 */
	strncpy(exten, (char *)data, sizeof(exten)-1);
	exten[sizeof(exten)-1] = '\0';
	if ((dial = strchr(exten, '/'))) {
		*dial++ = '\0';
		interface = exten;
		if ((opt = strchr(dial, '/')))
			*opt++ = '\0';
		else
			opt = "";
	} else {
		dial = exten;
		interface = "";
		opt = "";
	}
	strncpy(call->interface, interface, sizeof(call->interface)-1);
	strncpy(call->dialstring, dial, sizeof(call->dialstring)-1);
	apply_opt(call, (char *)opt);

#ifdef AST_1_8_OR_HIGHER
//	clone_variables(requestor, ast);

#if 0
	ast->caller.ani.number.valid=			req_caller->ani.number.valid;
	if (req_caller->ani.number.valid)
	  if (req_caller->ani.number.str)
	    if (req_caller->ani.number.str[0])
		ast->caller.ani.number.str=		strdup(req_caller->ani.number.str);
	ast->caller.ani.number.plan=			req_caller->ani.number.plan;
	ast->caller.ani.number.presentation=		req_caller->ani.number.presentation;

	ast->caller.ani.name.valid=			req_caller->ani.name.valid;
	if (req_caller->ani.name.valid)
	  if (req_caller->ani.name.str)
	    if (req_caller->ani.name.str[0])
		ast->caller.ani.name.str=		strdup(req_caller->ani.name.str);
	ast->caller.ani.name.presentation=		req_caller->ani.name.presentation;

	ast->caller.ani.subaddress.valid=		req_caller->ani.subaddress.valid;
	if (req_caller->ani.subaddress.valid)
	  if (req_caller->ani.subaddress.str)
	    if (req_caller->ani.subaddress.str[0])
		ast->caller.ani.subaddress.str=		strdup(req_caller->ani.subaddress.str);
	ast->caller.ani.subaddress.type=		req_caller->ani.subaddress.type;

	ast->caller.id.number.valid=			req_caller->id.number.valid;
	if (req_caller->id.number.valid)
	  if (req_caller->id.number.str)
	    if (req_caller->id.number.str[0])
		ast->caller.id.number.str=		strdup(req_caller->id.number.str);
	ast->caller.id.number.plan=			req_caller->id.number.plan;
	ast->caller.id.number.presentation=		req_caller->id.number.presentation;

	ast->caller.id.name.valid=			req_caller->id.name.valid;
	if (req_caller->id.name.valid)
	  if (req_caller->id.name.str)
	    if (req_caller->id.name.str[0])
		ast->caller.id.name.str=		strdup(req_caller->id.name.str);
	ast->caller.id.name.presentation=		req_caller->id.name.presentation;

	ast->caller.id.subaddress.valid=		req_caller->id.subaddress.valid;
	if (req_caller->id.subaddress.valid)
	  if (req_caller->id.subaddress.str)
	    if (req_caller->id.subaddress.str[0])
		ast->caller.id.subaddress.str=		strdup(req_caller->id.subaddress.str);
	ast->caller.id.subaddress.type=			req_caller->id.subaddress.type;

	if (requestor->dialed.number.str)
	  if (requestor->dialed.number.str[0])
		ast->dialed.number.str=			strdup(requestor->dialed.number.str);
	ast->dialed.number.plan=			requestor->dialed.number.plan;

	ast->dialed.subaddress.valid=			requestor->dialed.subaddress.valid;
	if (requestor->dialed.subaddress.valid)
	  if (requestor->dialed.subaddress.str)
	    if (requestor->dialed.subaddress.str[0])
		ast->dialed.subaddress.str=		strdup(requestor->dialed.subaddress.str);
	ast->dialed.subaddress.type=			requestor->dialed.subaddress.type;

	ast->dialed.transit_network_select=		requestor->dialed.transit_network_select;
	ast->redirecting.count=				req_redir->count;
	ast->redirecting.reason=			req_redir->reason;

	ast->redirecting.from.number.valid=		req_redir->from.number.valid;
	if (req_redir->from.number.valid)
	  if (req_redir->from.number.str)
	    if (req_redir->from.number.str[0])
		ast->redirecting.from.number.str=	strdup(req_redir->from.number.str);
	ast->redirecting.from.number.plan=		req_redir->from.number.plan;
	ast->redirecting.from.number.presentation=	req_redir->from.number.presentation;

	ast->redirecting.to.number.valid=		req_redir->to.number.valid;
	if (req_redir->to.number.valid)
	  if (req_redir->to.number.str)
	    if (req_redir->to.number.str[0])
		ast->redirecting.to.number.str=		strdup(req_redir->to.number.str);
	ast->redirecting.to.number.plan=		req_redir->to.number.plan;
	ast->redirecting.to.number.presentation=	req_redir->to.number.presentation;
#endif
	/* store call information for setup */

	/* caller ID */
	if (requestor && req_caller->id.number.valid) {
		if (req_caller->id.number.str)
			strncpy(call->callerinfo.id, req_caller->id.number.str, sizeof(call->callerinfo.id)-1);
		switch(req_caller->id.number.presentation & AST_PRES_RESTRICTION) {
			case AST_PRES_RESTRICTED:
			call->callerinfo.present = INFO_PRESENT_RESTRICTED;
			break;
			case AST_PRES_UNAVAILABLE:
			call->callerinfo.present = INFO_PRESENT_NOTAVAIL;
			break;
			case AST_PRES_ALLOWED:
			default:
			call->callerinfo.present = INFO_PRESENT_ALLOWED;
		}
		switch(req_caller->id.number.presentation & AST_PRES_NUMBER_TYPE) {
			case AST_PRES_USER_NUMBER_UNSCREENED:
			call->callerinfo.screen = INFO_SCREEN_USER;
			break;
			case AST_PRES_USER_NUMBER_PASSED_SCREEN:
			call->callerinfo.screen = INFO_SCREEN_USER_VERIFIED_PASSED;
			break;
			case AST_PRES_USER_NUMBER_FAILED_SCREEN:
			call->callerinfo.screen = INFO_SCREEN_USER_VERIFIED_FAILED;
			break;
			default:
			call->callerinfo.screen = INFO_SCREEN_NETWORK;
		}
		switch((req_caller->id.number.plan >> 4) & 7) {
			case 4:
			call->callerinfo.ntype = INFO_NTYPE_SUBSCRIBER;
			break;
			case 2:
			call->callerinfo.ntype = INFO_NTYPE_NATIONAL;
			break;
			case 1:
			call->callerinfo.ntype = INFO_NTYPE_INTERNATIONAL;
			break;
			default:
			call->callerinfo.ntype = INFO_NTYPE_UNKNOWN;
		}
	} else
		call->callerinfo.present = INFO_PRESENT_NOTAVAIL;

	/* caller ID 2 */
	if (requestor && req_caller->ani.number.valid) {
		if (req_caller->ani.number.str)
			strncpy(call->callerinfo.id2, req_caller->ani.number.str, sizeof(call->callerinfo.id2)-1);
		switch(req_caller->ani.number.presentation & AST_PRES_RESTRICTION) {
			case AST_PRES_RESTRICTED:
			call->callerinfo.present2 = INFO_PRESENT_RESTRICTED;
			break;
			case AST_PRES_UNAVAILABLE:
			call->callerinfo.present2 = INFO_PRESENT_NOTAVAIL;
			break;
			case AST_PRES_ALLOWED:
			default:
			call->callerinfo.present2 = INFO_PRESENT_ALLOWED;
		}
		switch(req_caller->ani.number.presentation & AST_PRES_NUMBER_TYPE) {
			case AST_PRES_USER_NUMBER_UNSCREENED:
			call->callerinfo.screen2 = INFO_SCREEN_USER;
			break;
			case AST_PRES_USER_NUMBER_PASSED_SCREEN:
			call->callerinfo.screen2 = INFO_SCREEN_USER_VERIFIED_PASSED;
			break;
			case AST_PRES_USER_NUMBER_FAILED_SCREEN:
			call->callerinfo.screen2 = INFO_SCREEN_USER_VERIFIED_FAILED;
			break;
			default:
			call->callerinfo.screen2 = INFO_SCREEN_NETWORK;
		}
		switch((req_caller->ani.number.plan >> 4) & 7) {
			case 4:
			call->callerinfo.ntype2 = INFO_NTYPE_SUBSCRIBER;
			break;
			case 2:
			call->callerinfo.ntype2 = INFO_NTYPE_NATIONAL;
			break;
			case 1:
			call->callerinfo.ntype2 = INFO_NTYPE_INTERNATIONAL;
			break;
			default:
			call->callerinfo.ntype2 = INFO_NTYPE_UNKNOWN;
		}
	} else
		call->callerinfo.present2 = INFO_PRESENT_NOTAVAIL;

	/* caller name */
	if (requestor && req_caller->id.name.valid) {
		if (req_caller->id.name.str)
			strncpy(call->callerinfo.name, req_caller->id.name.str, sizeof(call->callerinfo.name)-1);
	}

	/* redir number */
	if (requestor && req_redir->from.number.valid) {
		call->redirinfo.itype = INFO_ITYPE_CHAN;
		if (req_redir->from.number.str)
			strncpy(call->redirinfo.id, req_redir->from.number.str, sizeof(call->redirinfo.id)-1);
		switch(req_redir->from.number.presentation & AST_PRES_RESTRICTION) {
			case AST_PRES_RESTRICTED:
			call->redirinfo.present = INFO_PRESENT_RESTRICTED;
			break;
			case AST_PRES_UNAVAILABLE:
			call->redirinfo.present = INFO_PRESENT_NOTAVAIL;
			break;
			case AST_PRES_ALLOWED:
			default:
			call->redirinfo.present = INFO_PRESENT_ALLOWED;
		}
		switch(req_redir->from.number.presentation & AST_PRES_NUMBER_TYPE) {
			case AST_PRES_USER_NUMBER_UNSCREENED:
			call->redirinfo.screen = INFO_SCREEN_USER;
			break;
			case AST_PRES_USER_NUMBER_PASSED_SCREEN:
			call->redirinfo.screen = INFO_SCREEN_USER_VERIFIED_PASSED;
			break;
			case AST_PRES_USER_NUMBER_FAILED_SCREEN:
			call->redirinfo.screen = INFO_SCREEN_USER_VERIFIED_FAILED;
			break;
			default:
			call->redirinfo.screen = INFO_SCREEN_NETWORK;
		}
		switch((req_redir->from.number.plan >> 4) & 7) {
			case 4:
			call->redirinfo.ntype = INFO_NTYPE_SUBSCRIBER;
			break;
			case 2:
			call->redirinfo.ntype = INFO_NTYPE_NATIONAL;
			break;
			case 1:
			call->redirinfo.ntype = INFO_NTYPE_INTERNATIONAL;
			break;
			default:
			call->redirinfo.ntype = INFO_NTYPE_UNKNOWN;
		}
	}
#endif

	ast_mutex_unlock(&chan_lock);
	return ast;
}

/*
 * call from asterisk
 */
static int lcr_call(struct ast_channel *ast, char *dest, int timeout)
{
	union parameter newparam;
	struct chan_call *call;
#if ASTERISK_VERSION_NUM >= 110000
	int transfercapability;
#endif

	ast_mutex_lock(&chan_lock);
#if ASTERISK_VERSION_NUM < 110000
	call = ast->tech_pvt;
#else
	call = ast_channel_tech_pvt(ast);
#endif

	#ifdef LCR_FOR_CALLWEAVER
	ast->type = "LCR";
	snprintf(ast->name, sizeof(ast->name), "%s/%s-%04x",lcr_type, call->dialstring, ast_random() & 0xffff);
	#endif

	if (!call) {
		CERROR(NULL, ast, "Received call from Asterisk, but call instance does not exist.\n");
		ast_mutex_unlock(&chan_lock);
		return -1;
	}

	CDEBUG(NULL, ast, "Received call from Asterisk.\n");

	/* pbx process is started */
	call->pbx_started = 1;
	/* send MESSAGE_NEWREF */
	memset(&newparam, 0, sizeof(union parameter));
	newparam.newref.direction = 0; /* request from app */
	send_message(MESSAGE_NEWREF, 0, &newparam);

	/* set hdlc if capability requires hdlc */
#if ASTERISK_VERSION_NUM < 110000
	if (ast->transfercapability == INFO_BC_DATAUNRESTRICTED
	 || ast->transfercapability == INFO_BC_DATARESTRICTED
	 || ast->transfercapability == INFO_BC_VIDEO)
#else
	transfercapability=ast_channel_transfercapability(ast);
	if (transfercapability == INFO_BC_DATAUNRESTRICTED
	 || transfercapability == INFO_BC_DATARESTRICTED
	 || transfercapability == INFO_BC_VIDEO)
#endif
		call->hdlc = 1;
	/* if hdlc is forced by option, we change transcap to data */
	if (call->hdlc
#if ASTERISK_VERSION_NUM < 110000
	 && ast->transfercapability != INFO_BC_DATAUNRESTRICTED
	 && ast->transfercapability != INFO_BC_DATARESTRICTED
	 && ast->transfercapability != INFO_BC_VIDEO)
		ast->transfercapability = INFO_BC_DATAUNRESTRICTED;
#else
	 && transfercapability != INFO_BC_DATAUNRESTRICTED
	 && transfercapability != INFO_BC_DATARESTRICTED
	 && transfercapability != INFO_BC_VIDEO)
		transfercapability = INFO_BC_DATAUNRESTRICTED;
#endif

#ifndef AST_1_8_OR_HIGHER
	call->cid_num[0] = 0;
	call->cid_name[0] = 0;
	call->cid_rdnis[0] = 0;

	if (ast->cid.cid_num) if (ast->cid.cid_num[0])
		strncpy(call->cid_num, ast->cid.cid_num,
			sizeof(call->cid_num)-1);
	if (ast->cid.cid_name) if (ast->cid.cid_name[0])
		strncpy(call->cid_name, ast->cid.cid_name,
			sizeof(call->cid_name)-1);
	if (ast->cid.cid_rdnis) if (ast->cid.cid_rdnis[0])
		strncpy(call->cid_rdnis, ast->cid.cid_rdnis,
			sizeof(call->cid_rdnis)-1);
#endif

	ast_mutex_unlock(&chan_lock);
	return 0;
}

static void send_digit_to_chan(struct ast_channel * ast, char digit )
{
	static const char* dtmf_tones[] = {
		"!941+1336/100,!0/100", /* 0 */
		"!697+1209/100,!0/100", /* 1 */
		"!697+1336/100,!0/100", /* 2 */
		"!697+1477/100,!0/100", /* 3 */
		"!770+1209/100,!0/100", /* 4 */
		"!770+1336/100,!0/100", /* 5 */
		"!770+1477/100,!0/100", /* 6 */
		"!852+1209/100,!0/100", /* 7 */
		"!852+1336/100,!0/100", /* 8 */
		"!852+1477/100,!0/100", /* 9 */
		"!697+1633/100,!0/100", /* A */
		"!770+1633/100,!0/100", /* B */
		"!852+1633/100,!0/100", /* C */
		"!941+1633/100,!0/100", /* D */
		"!941+1209/100,!0/100", /* * */
		"!941+1477/100,!0/100" };       /* # */

	if (digit >= '0' && digit <='9')
		ast_playtones_start(ast,0,dtmf_tones[digit-'0'], 0);
	else if (digit >= 'A' && digit <= 'D')
		ast_playtones_start(ast,0,dtmf_tones[digit-'A'+10], 0);
	else if (digit == '*')
		ast_playtones_start(ast,0,dtmf_tones[14], 0);
	else if (digit == '#')
		ast_playtones_start(ast,0,dtmf_tones[15], 0);
	else {
#if ASTERISK_VERSION_NUM < 110000
		CDEBUG(NULL, ast, "Unable to handle DTMF tone '%c' for '%s'\n", digit, ast->name);
#else
		CDEBUG(NULL, ast, "Unable to handle DTMF tone '%c' for '%s'\n", digit, ast_channel_name(ast));
#endif
	}
}

#ifdef LCR_FOR_ASTERISK
static int lcr_digit_begin(struct ast_channel *ast, char digit)
#endif
#ifdef LCR_FOR_CALLWEAVER
static int lcr_digit(struct ast_channel *ast, char digit)
#endif
{
	struct chan_call *call;
	union parameter newparam;
	char buf[]="x";

#ifdef LCR_FOR_CALLWEAVER
	int inband_dtmf = 0;
#endif

	/* only pass IA5 number space */
	if (digit > 126 || digit < 32)
		return 0;

	ast_mutex_lock(&chan_lock);
#if ASTERISK_VERSION_NUM < 110000
	call = ast->tech_pvt;
#else
	call = ast_channel_tech_pvt(ast);
#endif
	if (!call) {
		CERROR(NULL, ast, "Received digit from Asterisk, but no call instance exists.\n");
		ast_mutex_unlock(&chan_lock);
		return -1;
	}

	CDEBUG(call, ast, "Received digit '%c' from Asterisk.\n", digit);

	/* send information or queue them */
	if (call->ref && call->state == CHAN_LCR_STATE_OUT_DIALING) {
		CDEBUG(call, ast, "Sending digit to LCR, because we are in dialing state.\n");
		memset(&newparam, 0, sizeof(union parameter));
		if (call->keypad) {
			newparam.information.keypad[0] = digit;
			newparam.information.keypad[1] = '\0';
		} else {
			newparam.information.id[0] = digit;
			newparam.information.id[1] = '\0';
		}
		send_message(MESSAGE_INFORMATION, call->ref, &newparam);
	} else
	if (!call->ref
	 && (call->state == CHAN_LCR_STATE_OUT_PREPARE || call->state == CHAN_LCR_STATE_OUT_SETUP)) {
		CDEBUG(call, ast, "Queue digits, because we are in setup/dialing state and have no ref yet.\n");
		*buf = digit;
		strncat(call->dialque, buf, strlen(call->dialque)-1);
	}

	ast_mutex_unlock(&chan_lock);

#ifdef LCR_FOR_ASTERISK
	return 0;
}

static int lcr_digit_end(struct ast_channel *ast, char digit, unsigned int duration)
{
	int inband_dtmf = 0;
	struct chan_call *call;
#endif

	ast_mutex_lock(&chan_lock);

#if ASTERISK_VERSION_NUM < 110000
	call = ast->tech_pvt;
#else
	call = ast_channel_tech_pvt(ast);
#endif

	if (!call) {
		CERROR(NULL, ast,
			"Received digit from Asterisk, "
			"but no call instance exists.\n");
		ast_mutex_unlock(&chan_lock);
		return -1;
	}

	CDEBUG(call, ast, "DIGIT END '%c' from Asterisk.\n", digit);

	if (call->state == CHAN_LCR_STATE_CONNECT && call->inband_dtmf) {
		inband_dtmf = 1;
	}

	ast_mutex_unlock(&chan_lock);

	if (inband_dtmf) {
		CDEBUG(call, ast, "-> sending '%c' inband.\n", digit);
		send_digit_to_chan(ast, digit);
	}

	return 0;
}

static int lcr_answer(struct ast_channel *ast)
{
	union parameter newparam;
	struct chan_call *call;

	ast_mutex_lock(&chan_lock);
#if ASTERISK_VERSION_NUM < 110000
	call = ast->tech_pvt;
#else
	call = ast_channel_tech_pvt(ast);
#endif
	if (!call) {
		CERROR(NULL, ast, "Received answer from Asterisk, but no call instance exists.\n");
		ast_mutex_unlock(&chan_lock);
		return -1;
	}

	CDEBUG(call, ast, "Received answer from Asterisk (maybe during lcr_bridge).\n");

	/* copy connectinfo, if bridged */
	if (call->bridge_call)
		memcpy(&call->connectinfo, &call->bridge_call->connectinfo, sizeof(struct connect_info));
	/* send connect message to lcr */
	if (call->state != CHAN_LCR_STATE_CONNECT) {
		memset(&newparam, 0, sizeof(union parameter));
		memcpy(&newparam.connectinfo, &call->connectinfo, sizeof(struct connect_info));
		send_message(MESSAGE_CONNECT, call->ref, &newparam);
		call->state = CHAN_LCR_STATE_CONNECT;
	}
	/* change state */
	/* enable keypad */
//	memset(&newparam, 0, sizeof(union parameter));
//	send_message(MESSAGE_ENABLEKEYPAD, call->ref, &newparam);

  	ast_mutex_unlock(&chan_lock);
	return 0;
}

static int lcr_hangup(struct ast_channel *ast)
{
	struct chan_call *call;
	pthread_t tid = pthread_self();

	if (!pthread_equal(tid, chan_tid)) {
		ast_mutex_lock(&chan_lock);
	}
#if ASTERISK_VERSION_NUM < 110000
	call = ast->tech_pvt;
#else
	call = ast_channel_tech_pvt(ast);
#endif
	if (!call) {
		CERROR(NULL, ast, "Received hangup from Asterisk, but no call instance exists.\n");
		if (!pthread_equal(tid, chan_tid)) {
			ast_mutex_unlock(&chan_lock);
		}
		return -1;
	}

	if (!pthread_equal(tid, chan_tid))
		CDEBUG(call, ast, "Received hangup from Asterisk thread.\n");
	else
		CDEBUG(call, ast, "Received hangup from LCR thread.\n");

	/* disconnect asterisk, maybe not required */
#if ASTERISK_VERSION_NUM < 110000
	ast->tech_pvt = NULL;
	ast->fds[0] = -1;
#else
	ast_channel_tech_pvt_set(ast, NULL);
	ast_channel_set_fd(ast, 0, -1);
#endif
	if (call->ref) {
		/* release */
		CDEBUG(call, ast, "Releasing ref and freeing call instance.\n");
#if ASTERISK_VERSION_NUM < 110000
		if (ast->hangupcause > 0)
			send_release(call, ast->hangupcause, LOCATION_PRIVATE_LOCAL);
#else
		if (ast_channel_hangupcause(ast) > 0)
			send_release(call, ast_channel_hangupcause(ast), LOCATION_PRIVATE_LOCAL);
#endif
		else
			send_release(call, CAUSE_NORMAL, LOCATION_PRIVATE_LOCAL);
		/* remove call */
		free_call(call);
		if (!pthread_equal(tid, chan_tid)) {
			ast_mutex_unlock(&chan_lock);
		}
		return 0;
	} else {
		/* ref is not set, due to prepare setup or release */
		if (call->state == CHAN_LCR_STATE_RELEASE) {
			/* we get the response to our release */
			CDEBUG(call, ast, "Freeing call instance, because we have no ref AND we are requesting no ref.\n");
			free_call(call);
		} else {
			/* during prepare, we change to release state */
			CDEBUG(call, ast, "We must wait until we received our ref, until we can free call instance.\n");
			call->state = CHAN_LCR_STATE_RELEASE;
			call->ast = NULL;
		}
	}
	if (!pthread_equal(tid, chan_tid)) {
		ast_mutex_unlock(&chan_lock);
	}
	return 0;
}

static int lcr_write(struct ast_channel *ast, struct ast_frame *fr)
{
	union parameter newparam;
	struct chan_call *call;
	struct ast_frame * f = fr;
	unsigned char *p, *q;
	int len, l;

#if ASTERISK_VERSION_NUM < 100000
#ifdef AST_1_8_OR_HIGHER
	if (!f->subclass.codec)
#else
	if (!f->subclass)
#endif
		CDEBUG(NULL, ast, "No subclass\n");
#endif
#ifdef AST_1_8_OR_HIGHER
#if ASTERISK_VERSION_NUM < 100000
#if ASTERISK_VERSION_NUM < 110000
	if (!(f->subclass.codec & ast->nativeformats)) {
#else
	if (!(f->subclass.codec & ast_channel_nativeformats(ast))) {
#endif
#else
#if ASTERISK_VERSION_NUM < 110000
	if (!ast_format_cap_iscompatible(ast->nativeformats, &f->subclass.format)) {
#else
	if (!ast_format_cap_iscompatible(ast_channel_nativeformats(ast), &f->subclass.format)) {
#endif
#endif
#else
#if ASTERISK_VERSION_NUM < 110000
	if (!(f->subclass & ast->nativeformats)) {
#else
	if (!(f->subclass & ast_channel_nativeformats(ast))) {
#endif
#endif
		CDEBUG(NULL, ast,
	        	       "Unexpected format. "
		       "Activating emergency conversion...\n");

#ifdef AST_1_8_OR_HIGHER
#if ASTERISK_VERSION_NUM < 100000
		ast_set_write_format(ast, f->subclass.codec);
#else
		ast_set_write_format(ast, &f->subclass.format);
#endif
#else
		ast_set_write_format(ast, f->subclass);
#endif
#if ASTERISK_VERSION_NUM < 110000
		f = (ast->writetrans) ? ast_translate(
			ast->writetrans, fr, 0) : fr;
#else
		f = (ast_channel_writetrans(ast)) ? ast_translate(
			ast_channel_writetrans(ast), fr, 0) : fr;
#endif
	}

	ast_mutex_lock(&chan_lock);
#if ASTERISK_VERSION_NUM < 110000
	call = ast->tech_pvt;
#else
	call = ast_channel_tech_pvt(ast);
#endif
	if (!call || !call->ref) {
		ast_mutex_unlock(&chan_lock);
		if (f != fr) {
			ast_frfree(f);
		}
		return -1;
	}
	len = f->samples;
	p = *((unsigned char **)&(f->data));
	q = newparam.traffic.data;
	memset(&newparam, 0, sizeof(union parameter));
	while (len) {
		l = (len > sizeof(newparam.traffic.data)) ? sizeof(newparam.traffic.data) : len;
		newparam.traffic.len = l;
		len -= l;
		for (; l; l--)
			*q++ = flip_bits[*p++];
		send_message(MESSAGE_TRAFFIC, call->ref, &newparam);
	}
	ast_mutex_unlock(&chan_lock);
	if (f != fr) {
		ast_frfree(f);
	}
	return 0;
}


static struct ast_frame *lcr_read(struct ast_channel *ast)
{
	struct chan_call *call;
	int len = 0;
	struct ast_frame *f = NULL;

	ast_mutex_lock(&chan_lock);
#if ASTERISK_VERSION_NUM < 110000
	call = ast->tech_pvt;
#else
	call = ast_channel_tech_pvt(ast);
#endif
	if (!call) {
		ast_mutex_unlock(&chan_lock);
		return NULL;
	}
	if (call->pipe[0] > -1) {
		if (call->rebuffer && !call->hdlc) {
			/* Make sure we have a complete 20ms (160byte) frame */
			len=read(call->pipe[0],call->read_buff + call->framepos, 160 - call->framepos);
			if (len > 0) {
				call->framepos += len;
			}
		} else {
			len = read(call->pipe[0], call->read_buff, sizeof(call->read_buff));
		}
		if (len < 0 && errno == EAGAIN) {
			ast_mutex_unlock(&chan_lock);

			#ifdef LCR_FOR_ASTERISK
			return &ast_null_frame;
			#endif

			#ifdef LCR_FOR_CALLWEAVER
			return &nullframe;
			#endif

		}
		if (len <= 0) {
			close(call->pipe[0]);
			call->pipe[0] = -1;
			global_change = 1;
			ast_mutex_unlock(&chan_lock);
			return NULL;
		} else if (call->rebuffer && call->framepos < 160) {
			/* Not a complete frame, so we send a null-frame */
			ast_mutex_unlock(&chan_lock);
			return &ast_null_frame;
		}
	}

	call->read_fr.frametype = AST_FRAME_VOICE;
#ifdef AST_1_8_OR_HIGHER
#if ASTERISK_VERSION_NUM < 100000
#if ASTERISK_VERSION_NUM < 110000
	call->read_fr.subclass.codec = ast->nativeformats;
#else
	call->read_fr.subclass.codec = ast_channel_nativeformats(ast);
#endif
#else
#if ASTERISK_VERSION_NUM < 110000
	ast_best_codec(ast->nativeformats, &call->read_fr.subclass.format);
#else
	ast_best_codec(ast_channel_nativeformats(ast), &call->read_fr.subclass.format);
#endif
	call->read_fr.subclass.integer = call->read_fr.subclass.format.id;
#endif
#else
#if ASTERISK_VERSION_NUM < 110000
	call->read_fr.subclass = ast->nativeformats;
#else
	call->read_fr.subclass = ast_channel_nativeformats(ast);
#endif
#endif
	if (call->rebuffer) {
		call->read_fr.datalen = call->framepos;
		call->read_fr.samples = call->framepos;
		call->framepos = 0;
	} else {
		call->read_fr.datalen = len;
		call->read_fr.samples = len;
	}
	call->read_fr.delivery = ast_tv(0,0);
	*((unsigned char **)&(call->read_fr.data)) = call->read_buff;

	if (call->dsp)
		f = ast_dsp_process(ast, call->dsp, &call->read_fr);
	if (f && f->frametype == AST_FRAME_DTMF)
		CDEBUG(call, ast, "Asterisk detected inband DTMF: %c.\n", f->subclass.integer);

	ast_mutex_unlock(&chan_lock);

	if (f && f->frametype == AST_FRAME_DTMF)
		return f;

	return &call->read_fr;
}

static int lcr_indicate(struct ast_channel *ast, int cond, const void *data, size_t datalen)
{
	union parameter newparam;
	int res = 0;
	struct chan_call *call;
	const struct ast_tone_zone_sound *ts = NULL;

	ast_mutex_lock(&chan_lock);
#if ASTERISK_VERSION_NUM < 110000
	call = ast->tech_pvt;
#else
	call = ast_channel_tech_pvt(ast);
#endif
	if (!call) {
		CERROR(NULL, ast, "Received indicate from Asterisk, but no call instance exists.\n");
		ast_mutex_unlock(&chan_lock);
		return -1;
	}

	switch (cond) {
		case AST_CONTROL_BUSY:
			CDEBUG(call, ast, "Received indicate AST_CONTROL_BUSY from Asterisk.\n");
			ast_setstate(ast, AST_STATE_BUSY);
			if (call->state != CHAN_LCR_STATE_OUT_DISCONNECT) {
				/* send message to lcr */
				memset(&newparam, 0, sizeof(union parameter));
				newparam.disconnectinfo.cause = 17;
				newparam.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
				send_message(MESSAGE_DISCONNECT, call->ref, &newparam);
				/* change state */
				call->state = CHAN_LCR_STATE_OUT_DISCONNECT;
			} else {
				CDEBUG(call, ast, "Using Asterisk 'busy' indication\n");
#if ASTERISK_VERSION_NUM < 110000
				ts = ast_get_indication_tone(ast->zone, "busy");
#else
				ts = ast_get_indication_tone(ast_channel_zone(ast), "busy");
#endif
			}
			break;
		case AST_CONTROL_CONGESTION:
#if ASTERISK_VERSION_NUM < 110000
			CDEBUG(call, ast, "Received indicate AST_CONTROL_CONGESTION from Asterisk. (cause %d)\n", ast->hangupcause);
#else
			CDEBUG(call, ast, "Received indicate AST_CONTROL_CONGESTION from Asterisk. (cause %d)\n", ast_channel_hangupcause(ast));
#endif
			if (call->state != CHAN_LCR_STATE_OUT_DISCONNECT) {
				/* send message to lcr */
				memset(&newparam, 0, sizeof(union parameter));
#if ASTERISK_VERSION_NUM < 110000
				newparam.disconnectinfo.cause = ast->hangupcause;
#else
				newparam.disconnectinfo.cause = ast_channel_hangupcause(ast);
#endif
				newparam.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
				send_message(MESSAGE_DISCONNECT, call->ref, &newparam);
				/* change state */
				call->state = CHAN_LCR_STATE_OUT_DISCONNECT;
			} else {
				CDEBUG(call, ast, "Using Asterisk 'congestion' indication\n");
#if ASTERISK_VERSION_NUM < 110000
				ts = ast_get_indication_tone(ast->zone, "congestion");
#else
				ts = ast_get_indication_tone(ast_channel_zone(ast), "congestion");
#endif
			}
			break;
		case AST_CONTROL_PROCEEDING:
			CDEBUG(call, ast, "Received indicate AST_CONTROL_PROCEEDING from Asterisk.\n");
			if (call->state == CHAN_LCR_STATE_IN_SETUP
			 || call->state == CHAN_LCR_STATE_IN_DIALING) {
				/* send message to lcr */
				memset(&newparam, 0, sizeof(union parameter));
				send_message(MESSAGE_PROCEEDING, call->ref, &newparam);
				/* change state */
				call->state = CHAN_LCR_STATE_IN_PROCEEDING;
			}
			break;
		case AST_CONTROL_RINGING:
			CDEBUG(call, ast, "Received indicate AST_CONTROL_RINGING from Asterisk.\n");
			ast_setstate(ast, AST_STATE_RING);
			if (call->state == CHAN_LCR_STATE_IN_SETUP
			 || call->state == CHAN_LCR_STATE_IN_DIALING
			 || call->state == CHAN_LCR_STATE_IN_PROCEEDING) {
				/* send message to lcr */
				memset(&newparam, 0, sizeof(union parameter));
				send_message(MESSAGE_ALERTING, call->ref, &newparam);
				/* change state */
				call->state = CHAN_LCR_STATE_IN_ALERTING;
			} else {
				CDEBUG(call, ast, "Using Asterisk 'ring' indication\n");
#if ASTERISK_VERSION_NUM < 110000
				ts = ast_get_indication_tone(ast->zone, "ring");
#else
				ts = ast_get_indication_tone(ast_channel_zone(ast), "ring");
#endif
			}
			break;
		case AST_CONTROL_PROGRESS:
			CDEBUG(call, ast, "Received indicate AST_CONTROL_PROGRESS from Asterisk.\n");
			/* request bchannel */
			CDEBUG(call, ast, "Requesting audio path.\n");
			memset(&newparam, 0, sizeof(union parameter));
			send_message(MESSAGE_AUDIOPATH, call->ref, &newparam);
			break;
		case -1:
			CDEBUG(call, ast, "Received indicate -1.\n");
			ast_playtones_stop(ast);
			res = -1;
			break;

		case AST_CONTROL_VIDUPDATE:
			CDEBUG(call, ast, "Received indicate AST_CONTROL_VIDUPDATE.\n");
			res = -1;
			break;
		case AST_CONTROL_HOLD:
			CDEBUG(call, ast, "Received indicate AST_CONTROL_HOLD from Asterisk.\n");
			/* send message to lcr */
			memset(&newparam, 0, sizeof(union parameter));
			newparam.notifyinfo.notify = INFO_NOTIFY_REMOTE_HOLD;
			send_message(MESSAGE_NOTIFY, call->ref, &newparam);

			/*start music onhold*/
			#ifdef LCR_FOR_ASTERISK
			#if ASTERISK_VERSION_NUM <110000
			ast_moh_start(ast,data,ast->musicclass);
			#else
			ast_moh_start(ast,data,ast_channel_musicclass(ast));
			#endif
			#endif

			#ifdef LCR_FOR_CALLWEAVER
			ast_moh_start(ast, NULL);
			#endif

			call->on_hold = 1;
			break;
		case AST_CONTROL_UNHOLD:
			CDEBUG(call, ast, "Received indicate AST_CONTROL_UNHOLD from Asterisk.\n");
			/* send message to lcr */
			memset(&newparam, 0, sizeof(union parameter));
			newparam.notifyinfo.notify = INFO_NOTIFY_REMOTE_RETRIEVAL;
			send_message(MESSAGE_NOTIFY, call->ref, &newparam);

			/*stop moh*/
			ast_moh_stop(ast);
			call->on_hold = 0;
			break;
#ifdef AST_CONTROL_SRCUPDATE
		case AST_CONTROL_SRCUPDATE:
#else
		case 20:
#endif
			CDEBUG(call, ast, "Received AST_CONTROL_SRCUPDATE from Asterisk.\n");
			break;
		default:
			CERROR(call, ast, "Received indicate from Asterisk with unknown condition %d.\n", cond);
			res = -1;
			break;
	}

	if (ts && ts->data[0]) {
		ast_playtones_start(ast, 0, ts->data, 1);
	}

	/* return */
	ast_mutex_unlock(&chan_lock);
	return res;
}

/*
 * fixup asterisk
 */
static int lcr_fixup(struct ast_channel *oldast, struct ast_channel *ast)
{
	struct chan_call *call;

	if (!ast) {
		return -1;
	}

	ast_mutex_lock(&chan_lock);
#if ASTERISK_VERSION_NUM < 110000
	call = ast->tech_pvt;
#else
	call = ast_channel_tech_pvt(ast);
#endif
	if (!call) {
		CERROR(NULL, ast, "Received fixup from Asterisk, but no call instance exists.\n");
		ast_mutex_unlock(&chan_lock);
		return -1;
	}

	CDEBUG(call, ast, "Received fixup from Asterisk.\n");
	call->ast = ast;
	ast_mutex_unlock(&chan_lock);
	return 0;
}

/*
 * send_text asterisk
 */
static int lcr_send_text(struct ast_channel *ast, const char *text)
{
	struct chan_call *call;
	union parameter newparam;

	ast_mutex_lock(&chan_lock);
#if ASTERISK_VERSION_NUM < 110000
	call = ast->tech_pvt;
#else
	call = ast_channel_tech_pvt(ast);
#endif
	if (!call) {
		CERROR(NULL, ast, "Received send_text from Asterisk, but no call instance exists.\n");
		ast_mutex_unlock(&chan_lock);
		return -1;
	}

	CDEBUG(call, ast, "Received send_text from Asterisk. (text=%s)\n", text);
	memset(&newparam, 0, sizeof(union parameter));
	strncpy(newparam.notifyinfo.display, text, sizeof(newparam.notifyinfo.display)-1);
	send_message(MESSAGE_NOTIFY, call->ref, &newparam);
	ast_mutex_unlock(&chan_lock);
	return 0;
}

/*
 * bridge process
 */
enum ast_bridge_result lcr_bridge(struct ast_channel *ast1,
				  struct ast_channel *ast2, int flags,
				  struct ast_frame **fo,
				  struct ast_channel **rc, int timeoutms)

{
	struct chan_call	*call1, *call2;
	struct ast_channel	*carr[2], *who;
	int			to;
	struct ast_frame	*f;
	int			bridge_id;

/* bridge is disabled, because there is currerntly no concept to bridge mISDN channels */
return AST_BRIDGE_FAILED;

	CDEBUG(NULL, NULL, "Received bridging request from Asterisk.\n");

	carr[0] = ast1;
	carr[1] = ast2;

	/* join via dsp (if the channels are currently open) */
	ast_mutex_lock(&chan_lock);
#if ASTERISK_VERSION_NUM < 110000
	call1 = ast1->tech_pvt;
	call2 = ast2->tech_pvt;
#else
	call1 = ast_channel_tech_pvt(ast1);
	call2 = ast_channel_tech_pvt(ast2);
#endif
	if (!call1 || !call2) {
		CDEBUG(NULL, NULL, "Bridge, but we don't have two call instances, exitting.\n");
		ast_mutex_unlock(&chan_lock);
		return AST_BRIDGE_COMPLETE;
	}

	/* join, if both call instances uses dsp
	   ignore the case of fax detection here it may be benificial for ISDN fax machines or pass through.
	*/
	CDEBUG(NULL, NULL, "Both calls use DSP, bridging via DSP.\n");

	/* get bridge id and join */
	bridge_id = new_bridge_id();

	call1->bridge_id = bridge_id;
	call2->bridge_id = bridge_id;
	// FIXME: do bridiging
	// bchannel_join(call1->bchannel, bridge_id);
	// bchannel_join(call2->bchannel, bridge_id);

	call1->bridge_call = call2;
	call2->bridge_call = call1;

	if (call1->state == CHAN_LCR_STATE_IN_SETUP
	 || call1->state == CHAN_LCR_STATE_IN_DIALING
	 || call1->state == CHAN_LCR_STATE_IN_PROCEEDING
	 || call1->state == CHAN_LCR_STATE_IN_ALERTING) {
		CDEBUG(call1, ast1, "Bridge established before lcr_answer, so we call it ourself: Calling lcr_answer...\n");
		lcr_answer(ast1);
	}
	if (call2->state == CHAN_LCR_STATE_IN_SETUP
	 || call2->state == CHAN_LCR_STATE_IN_DIALING
	 || call2->state == CHAN_LCR_STATE_IN_PROCEEDING
	 || call2->state == CHAN_LCR_STATE_IN_ALERTING) {
		CDEBUG(call2, ast2, "Bridge established before lcr_answer, so we call it ourself: Calling lcr_answer...\n");
		lcr_answer(ast2);
	}

	/* sometimes SIP phones forget to send RETRIEVE before TRANSFER
	   so let's do it for them. Hmpf.
	*/

	if (call1->on_hold) {
		union parameter newparam;

		memset(&newparam, 0, sizeof(union parameter));
		newparam.notifyinfo.notify = INFO_NOTIFY_REMOTE_RETRIEVAL;
		send_message(MESSAGE_NOTIFY, call1->ref, &newparam);

		call1->on_hold = 0;
	}

	if (call2->on_hold) {
		union parameter newparam;

		memset(&newparam, 0, sizeof(union parameter));
		newparam.notifyinfo.notify = INFO_NOTIFY_REMOTE_RETRIEVAL;
		send_message(MESSAGE_NOTIFY, call2->ref, &newparam);

		call2->on_hold = 0;
	}

	ast_mutex_unlock(&chan_lock);

	while(1) {
		to = -1;
		who = ast_waitfor_n(carr, 2, &to);

		if (!who) {
			CDEBUG(NULL, NULL, "Empty read on bridge, breaking out.\n");
			break;
		}
		f = ast_read(who);

		if (!f || f->frametype == AST_FRAME_CONTROL) {
			if (!f)
				CDEBUG(NULL, NULL, "Got hangup.\n");
			else
				CDEBUG(NULL, NULL, "Got CONTROL.\n");
			/* got hangup .. */
			*fo=f;
			*rc=who;
			break;
		}

		if ( f->frametype == AST_FRAME_DTMF ) {
			CDEBUG(NULL, NULL, "Got DTMF.\n");
			*fo=f;
			*rc=who;
			break;
		}


		if (who == ast1) {
			ast_write(ast2,f);
		}
		else {
			ast_write(ast1,f);
		}

	}

	CDEBUG(NULL, NULL, "Releasing bridge.\n");

	/* split channels */
	ast_mutex_lock(&chan_lock);
#if ASTERISK_VERSION_NUM < 110000
	call1 = ast1->tech_pvt;
	call2 = ast2->tech_pvt;
#else
	call1 = ast_channel_tech_pvt(ast1);
	call2 = ast_channel_tech_pvt(ast2);
#endif
	if (call1 && call1->bridge_id) {
		call1->bridge_id = 0;
		if (call1->bridge_call)
			call1->bridge_call->bridge_call = NULL;
	}
	if (call2 && call1->bridge_id) {
		call2->bridge_id = 0;
		if (call2->bridge_call)
			call2->bridge_call->bridge_call = NULL;
	}
	call1->bridge_call = NULL;
	call2->bridge_call = NULL;

	ast_mutex_unlock(&chan_lock);
	return AST_BRIDGE_COMPLETE;
}
static struct ast_channel_tech lcr_tech = {
	.type= lcr_type,
	.description = "Channel driver for connecting to Linux-Call-Router",
	#if ASTERISK_VERSION_NUM < 100000
	.capabilities = AST_FORMAT_ALAW,
	#endif
	.requester = lcr_request,

	#ifdef LCR_FOR_ASTERISK
	.send_digit_begin = lcr_digit_begin,
	.send_digit_end = lcr_digit_end,
	#endif

	#ifdef LCR_FOR_CALLWEAVER
	.send_digit = lcr_digit,
	#endif

	.call = lcr_call,
	.bridge = lcr_bridge,
	.hangup = lcr_hangup,
	.answer = lcr_answer,
	.read = lcr_read,
	.write = lcr_write,
	.indicate = lcr_indicate,
	.fixup = lcr_fixup,
	.send_text = lcr_send_text,
	.properties = 0
};


/*
 * cli
 */
#if 0
static int lcr_show_lcr (int fd, int argc, char *argv[])
{
	return 0;
}

static int lcr_show_calls (int fd, int argc, char *argv[])
{
	return 0;
}

static int lcr_reload_routing (int fd, int argc, char *argv[])
{
	return 0;
}

static int lcr_reload_interfaces (int fd, int argc, char *argv[])
{
	return 0;
}

static int lcr_port_block (int fd, int argc, char *argv[])
{
	return 0;
}

static int lcr_port_unblock (int fd, int argc, char *argv[])
{
	return 0;
}

static int lcr_port_unload (int fd, int argc, char *argv[])
{
	return 0;
}

static struct ast_cli_entry cli_show_lcr =
{ {"lcr", "show", "lcr", NULL},
 lcr_show_lcr,
 "Shows current states of LCR core",
 "Usage: lcr show lcr\n",
};

static struct ast_cli_entry cli_show_calls =
{ {"lcr", "show", "calls", NULL},
 lcr_show_calls,
 "Shows current calls made by LCR and Asterisk",
 "Usage: lcr show calls\n",
};

static struct ast_cli_entry cli_reload_routing =
{ {"lcr", "reload", "routing", NULL},
 lcr_reload_routing,
 "Reloads routing conf of LCR, current uncomplete calls will be disconnected",
 "Usage: lcr reload routing\n",
};

static struct ast_cli_entry cli_reload_interfaces =
{ {"lcr", "reload", "interfaces", NULL},
 lcr_reload_interfaces,
 "Reloads interfaces conf of LCR",
 "Usage: lcr reload interfaces\n",
};

static struct ast_cli_entry cli_port_block =
{ {"lcr", "port", "block", NULL},
 lcr_port_block,
 "Blocks LCR port for further calls",
 "Usage: lcr port block \"<port>\"\n",
};

static struct ast_cli_entry cli_port_unblock =
{ {"lcr", "port", "unblock", NULL},
 lcr_port_unblock,
 "Unblocks or loads LCR port, port is opened my mISDN",
 "Usage: lcr port unblock \"<port>\"\n",
};

static struct ast_cli_entry cli_port_unload =
{ {"lcr", "port", "unload", NULL},
 lcr_port_unload,
 "Unloads LCR port, port is closes by mISDN",
 "Usage: lcr port unload \"<port>\"\n",
};
#endif


#ifdef LCR_FOR_ASTERISK
#ifdef AST_1_8_OR_HIGHER
static int lcr_config_exec(struct ast_channel *ast, const char *data)
#else
static int lcr_config_exec(struct ast_channel *ast, void *data)
#endif
#endif

#ifdef LCR_FOR_CALLWEAVER
static int lcr_config_exec(struct ast_channel *ast, void *data, char **argv)
#endif
{
	struct chan_call *call;

	ast_mutex_lock(&chan_lock);

	#ifdef LCR_FOR_ASTERISK
	CDEBUG(NULL, ast, "Received lcr_config (data=%s)\n", (char *)data);
	#endif

	#ifdef LCR_FOR_CALLWEAVER
	CDEBUG(NULL, ast, "Received lcr_config (data=%s)\n", argv[0]);
	#endif

	/* find channel */
	call = call_first;
	while(call) {
		if (call->ast == ast)
			break;
		call = call->next;
	}
	if (call)

		#ifdef LCR_FOR_ASTERISK
		apply_opt(call, (char *)data);
		#endif

		#ifdef LCR_FOR_CALLWEAVER
		apply_opt(call, (char *)argv[0]);
		#endif

		/* send options */
		if (call->tx_queue) {
			union parameter newparam;

			memset(&newparam, 0, sizeof(union parameter));
			newparam.queue = call->tx_queue * 8;
			send_message(MESSAGE_DISABLE_DEJITTER, call->ref, &newparam);
		}
	else
		CERROR(NULL, ast, "lcr_config app not called by chan_lcr channel.\n");

	ast_mutex_unlock(&chan_lock);
	return 0;
}

/*
 * module loading and destruction
 */
int load_module(void)
{
	u_short i;
	char options_error[256];

	for (i = 0; i < 256; i++) {
		flip_bits[i] = (i>>7) | ((i>>5)&2) | ((i>>3)&4) | ((i>>1)&8)
			     | (i<<7) | ((i&2)<<5) | ((i&4)<<3) | ((i&8)<<1);
	}

	if (read_options(options_error) == 0) {
		CERROR(NULL, NULL, "%s", options_error);

		#ifdef LCR_FOR_ASTERISK
		return AST_MODULE_LOAD_DECLINE;
		#endif

		#ifdef LCR_FOR_CALLWEAVER
		return 0;
		#endif

	}

	ast_mutex_init(&chan_lock);
	ast_mutex_init(&log_lock);

	#if ASTERISK_VERSION_NUM < 100000
	lcr_tech.capabilities = (options.law=='a')?AST_FORMAT_ALAW:AST_FORMAT_ULAW;
	#else
	struct ast_format tmp;
	ast_format_set(&tmp ,(options.law=='a')?AST_FORMAT_ALAW:AST_FORMAT_ULAW , 0);
	if (!(lcr_tech.capabilities = ast_format_cap_alloc())) {
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_format_cap_add(lcr_tech.capabilities, &tmp);
	#endif
	if (ast_channel_register(&lcr_tech)) {
		CERROR(NULL, NULL, "Unable to register channel class\n");
		close_socket();

		#ifdef LCR_FOR_ASTERISK
		return AST_MODULE_LOAD_DECLINE;
		#endif

		#ifdef LCR_FOR_CALLWEAVER
		return 0;
		#endif
	}

	ast_register_application("lcr_config", lcr_config_exec, "lcr_config",

				 #ifdef LCR_FOR_ASTERISK
				 "lcr_config(<opt><optarg>:<opt>:...)\n"
				 #endif

				 #ifdef LCR_FOR_CALLWEAVER
				 "lcr_config(<opt><optarg>:<opt>:...)\n",
				 #endif

				 "Sets LCR opts. and optargs\n"
				 "\n"
				 "The available options are:\n"
				 "    d - Send display text on called phone, text is the optarg.\n"
				 "    n - Don't detect dtmf tones from LCR.\n"
				 "    h - Force data call (HDLC).\n"
				 "    q - Add queue to make fax stream seamless (required for fax app).\n"
				 "        Use queue size in miliseconds for optarg. (try 250)\n"
				 "    a - Adding DTMF detection.\n"
				 "    f - Adding fax detection.\n"
#if 0
				 "    c - Make crypted outgoing call, optarg is keyindex.\n"
				 "    e - Perform echo cancelation on this channel.\n"
#endif
				 "        Takes mISDN pipeline option as optarg.\n"
				 "    s - Send Non Inband DTMF as inband. (disables LCR's DTMF)\n"
				 "    r - re-buffer packets (160 bytes). Required for some SIP-phones and fax applications.\n"
#if 0
				 "   vr - rxgain control\n"
				 "   vt - txgain control\n"
#endif
				 "        Volume changes at factor 2 ^ optarg.\n"
				 "    k - use keypad to dial this call.\n"
				 "\n"
				 "set LCR_TRANSFERCAPABILITY to the numerical bearer capabilty in order to alter caller's capability\n"
				 " -> use 16 for fax (3.1k audio)\n"
				 "\n"
				 "To send a fax, you need to set LCR_TRANSFERCAPABILITY environment to 16, also you need to set\n"
				 "options: \"n:t:q250\" for seamless audio transmission.\n"
		);


#if 0
	ast_cli_register(&cli_show_lcr);
	ast_cli_register(&cli_show_calls);
	ast_cli_register(&cli_reload_routing);
	ast_cli_register(&cli_reload_interfaces);
	ast_cli_register(&cli_port_block);
	ast_cli_register(&cli_port_unblock);
	ast_cli_register(&cli_port_unload);
#endif

	if ((pthread_create(&chan_tid, NULL, chan_thread, NULL)<0)) {
		/* failed to create thread */
		close_socket();
		ast_channel_unregister(&lcr_tech);

		#ifdef LCR_FOR_ASTERISK
		return AST_MODULE_LOAD_DECLINE;
		#endif

		#ifdef LCR_FOR_CALLWEAVER
		return 0;
		#endif

	}
	return 0;
}

int unload_module(void)
{
	/* First, take us out of the channel loop */
	CDEBUG(NULL, NULL, "-- Unregistering Linux-Call-Router Channel Driver --\n");

	pthread_cancel(chan_tid);

	close_socket();

	del_timer(&socket_retry);

	unregister_fd(&wake_fd);
	close(wake_pipe[0]);
	close(wake_pipe[1]);

//	ast_mutex_unlock(&chan_lock);

	ast_channel_unregister(&lcr_tech);

	ast_unregister_application("lcr_config");

	if (lcr_sock >= 0) {
		close(lcr_sock);
		lcr_sock = -1;
	}

#if ASTERISK_VERSION_NUM >= 100000
	lcr_tech.capabilities = ast_format_cap_destroy(lcr_tech.capabilities);
#endif
	return 0;
}

int reload_module(void)
{
//	reload_config();
	return 0;
}

#ifdef LCR_FOR_ASTERISK
#define AST_MODULE "chan_lcr"
#endif

#ifdef LCR_FOR_CALLWEAVER
int usecount(void)
hae
{
	int res;
	ast_mutex_lock(&usecnt_lock);
	res = usecnt;
	ast_mutex_unlock(&usecnt_lock);
	return res;
}
#endif

#ifdef LCR_FOR_ASTERISK
AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Channel driver for Linux-Call-Router Support (ISDN BRI/PRI)",
		.load = load_module,
		.unload = unload_module,
		.reload = reload_module,
	       );
#endif

#ifdef LCR_FOR_CALLWEAVER
char *description(void)
{
	return desc;
}
#endif
