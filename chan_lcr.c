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

#include <asterisk/compiler.h>
#include <asterisk/buildopts.h>
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
#include <asterisk/callerid.h>
#include <asterisk/indications.h>
#include <asterisk/app.h>
#include <asterisk/features.h>
#include <asterisk/sched.h>

#include "extension.h"
#include "message.h"
#include "callerid.h"
#include "lcrsocket.h"
#include "cause.h"
#include "bchannel.h"
#include "options.h"
#include "chan_lcr.h"

CHAN_LCR_STATE // state description structure
MESSAGES // message text

unsigned char flip_bits[256];

int lcr_debug=1;
int mISDN_created=1;

char lcr_type[]="lcr";

pthread_t chan_tid;
ast_mutex_t chan_lock; /* global lock */
ast_mutex_t log_lock; /* logging log */
int quit;

int glob_channel = 0;

int lcr_sock = -1;

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
		strncpy(ast_text, ast->name, sizeof(ast_text)-1);
	ast_text[sizeof(ast_text)-1] = '\0';
	
	ast_log(type, file, line, function, "[call=%s ast=%s] %s", call_text, ast_text, buffer);

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
	
	while(call)
	{
		if (call->ref == ref && call->ref_was_assigned == assigned)
			break;
		call = call->next;
	}
	return(call);
}

void free_call(struct chan_call *call)
{
	struct chan_call **temp = &call_first;

	while(*temp)
	{
		if (*temp == call)
		{
			*temp = (*temp)->next;
			if (call->pipe[0] > -1)
				close(call->pipe[0]);
			if (call->pipe[1] > -1)
				close(call->pipe[1]);
			if (call->bchannel)
			{
				if (call->bchannel->call != call)
					CERROR(call, NULL, "Linked bchannel structure has no link to us.\n");
				call->bchannel->call = NULL;
			}
			if (call->bridge_call)
			{
				if (call->bridge_call->bridge_call != call)
					CERROR(call, NULL, "Linked call structure has no link to us.\n");
				call->bridge_call->bridge_call = NULL;
			}
			CDEBUG(call, NULL, "Call instance freed.\n");
			free(call);
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
		return(NULL);
	}
	fcntl((*callp)->pipe[0], F_SETFL, O_NONBLOCK);
	CDEBUG(*callp, NULL, "Call instance allocated.\n");
	return(*callp);
}

unsigned short new_bridge_id(void)
{
	struct chan_call *call;
	unsigned short id = 1;

	/* search for lowest bridge id that is not in use and not 0 */
	while(id)
	{
		call = call_first;
		while(call)
		{
			if (call->bridge_id == id)
				break;
			call = call->next;
		}
		if (!call)
			break;
		id++;
	}
	CDEBUG(NULL, NULL, "New bridge ID %d.\n", id);
	return(id);
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
	CDEBUG(NULL, NULL, "Sending %s to socket.\n", messages_txt[message_type]);

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

	return(0);
}

/*
 * apply options (in locked state)
 */
void apply_opt(struct chan_call *call, char *data)
{
	union parameter newparam;
	char string[1024], *p = string, *opt, *key;
	int gain, i, newmode = 0;

	if (!data[0])
		return; // no opts

	strncpy(string, data, sizeof(string)-1);
	string[sizeof(string)-1] = '\0';

	/* parse options */
	while((opt = strsep(&p, ":")))
	{
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
			call->no_dtmf = 1;
			break;
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
			while(*key)
			{
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
		case 'h':
			if (opt[1] != '\0') {
				CERROR(call, call->ast, "Option 'h' (HDLC) expects no parameter.\n", opt);
				break;
			}
			CDEBUG(call, call->ast, "Option 'h' (HDLC).\n");
			if (!call->hdlc) {
				call->hdlc = 1;
				newmode = 1;
			}
			break;
		case 't':
			if (opt[1] != '\0') {
				CERROR(call, call->ast, "Option 't' (no_dsp) expects no parameter.\n", opt);
				break;
			}
			CDEBUG(call, call->ast, "Option 't' (no dsp).\n");
			if (!call->nodsp) {
				call->nodsp = 1;
				newmode = 1;
			}
			break;
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
		case 'r':
			if (opt[1] != '\0') {
				CERROR(call, call->ast, "Option 'r' (re-buffer 160 bytes) expects no parameter.\n", opt);
				break;
			}
			CDEBUG(call, call->ast, "Option 'r' (re-buffer 160 bytes)");
			call->rebuffer = 1;
			break;
		case 's':
			if (opt[1] != '\0') {
				CERROR(call, call->ast, "Option 's' (inband DTMF) expects no parameter.\n", opt);
				break;
			}
			CDEBUG(call, call->ast, "Option 's' (inband DTMF).\n");
			call->inband_dtmf = 1;
			break;
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
		default:
			CERROR(call, call->ast, "Option '%s' unknown.\n", opt);
		}
	}		
	
	/* re-open, if bchannel is created */
	if (call->bchannel && call->bchannel->b_sock > -1) {
		bchannel_destroy(call->bchannel);
		if (bchannel_create(call->bchannel, ((call->nodsp)?1:0) + ((call->hdlc)?2:0)))
			bchannel_activate(call->bchannel, 1);
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

	if (!call->ast || !call->ref)
		return;

	CDEBUG(call, call->ast, "Sending setup to LCR. (interface=%s dialstring=%s, cid=%s)\n", call->interface, call->dialstring, call->cid_num);

	/* send setup message to LCR */
	memset(&newparam, 0, sizeof(union parameter));
       	newparam.setup.dialinginfo.itype = INFO_ITYPE_ISDN;	
       	newparam.setup.dialinginfo.ntype = INFO_NTYPE_UNKNOWN;
	strncpy(newparam.setup.dialinginfo.id, call->dialstring, sizeof(newparam.setup.dialinginfo.id)-1);
	strncpy(newparam.setup.dialinginfo.interfaces, call->interface, sizeof(newparam.setup.dialinginfo.interfaces)-1);
       	newparam.setup.callerinfo.itype = INFO_ITYPE_CHAN;	
       	newparam.setup.callerinfo.ntype = INFO_NTYPE_UNKNOWN;
	strncpy(newparam.setup.callerinfo.display, call->display, sizeof(newparam.setup.callerinfo.display)-1);
	call->display[0] = '\0';
	if (call->cid_num[0])
		strncpy(newparam.setup.callerinfo.id, call->cid_num, sizeof(newparam.setup.callerinfo.id)-1);
	if (call->cid_name[0])
		strncpy(newparam.setup.callerinfo.name, call->cid_name, sizeof(newparam.setup.callerinfo.name)-1);
	if (call->cid_rdnis[0])
	{
		strncpy(newparam.setup.redirinfo.id, call->cid_rdnis, sizeof(newparam.setup.redirinfo.id)-1);
       		newparam.setup.redirinfo.itype = INFO_ITYPE_CHAN;	
 	      	newparam.setup.redirinfo.ntype = INFO_NTYPE_UNKNOWN;	
	}
	switch(ast->cid.cid_pres & AST_PRES_RESTRICTION)
	{
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
	switch(ast->cid.cid_ton)
	{
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
	newparam.setup.capainfo.bearer_capa = ast->transfercapability;
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
 * send release message to LCR and import bchannel if exported
 */
static void send_release_and_import(struct chan_call *call, int cause, int location)
{
	union parameter newparam;

	/* importing channel */
	if (call->bchannel) {
		memset(&newparam, 0, sizeof(union parameter));
		newparam.bchannel.type = BCHANNEL_RELEASE;
		newparam.bchannel.handle = call->bchannel->handle;
		send_message(MESSAGE_BCHANNEL, call->ref, &newparam);
	}
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
	char *exten = ast->exten;
	if (!*exten)
		exten = "s";

	CDEBUG(call, ast, "Try to start pbx. (exten=%s context=%s complete=%s)\n", exten, ast->context, complete?"yes":"no");
	
	if (complete)
	{
		/* if not match */
		if (!ast_canmatch_extension(ast, ast->context, exten, 1, call->oad))
		{
			CDEBUG(call, ast, "Got 'sending complete', but extension '%s' will not match at context '%s' - releasing.\n", exten, ast->context);
			cause = 1;
			goto release;
		}
		if (!ast_exists_extension(ast, ast->context, exten, 1, call->oad))
		{
			CDEBUG(call, ast, "Got 'sending complete', but extension '%s' would match at context '%s', if more digits would be dialed - releasing.\n", exten, ast->context);
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

	if (ast_canmatch_extension(ast, ast->context, exten, 1, call->oad))
	{
		/* send setup acknowledge to lcr */
		if (call->state != CHAN_LCR_STATE_IN_DIALING) {
			memset(&newparam, 0, sizeof(union parameter));
			send_message(MESSAGE_OVERLAP, call->ref, &newparam);
		}

		/* change state */
		call->state = CHAN_LCR_STATE_IN_DIALING;

		/* if match, start pbx */
		if (ast_exists_extension(ast, ast->context, exten, 1, call->oad)) {
			CDEBUG(call, ast, "Extensions matches.\n");
			goto start;
		}

		/* if can match */
		CDEBUG(call, ast, "Extensions may match, if more digits are dialed.\n");
		return;
	}

	if (!*ast->exten) {
		/* if can match */
		CDEBUG(call, ast, "There is no 's' extension (and we tried to match it implicitly). Extensions may match, if more digits are dialed.\n");
		return;
	}

	/* if not match */
	cause = 1;
	release:
	/* release lcr */
	CDEBUG(call, ast, "Releasing due to extension missmatch.\n");
	send_release_and_import(call, cause, LOCATION_PRIVATE_LOCAL);
	call->ref = 0;
	/* release asterisk */
	ast->hangupcause = call->cause;
	/* change to release state */
	call->state = CHAN_LCR_STATE_RELEASE;
	ast_hangup(ast); // call will be destroyed here
	return;
	
	start:
	/* send setup to asterisk */
	CDEBUG(call, ast, "Starting call to Asterisk due to matching extension.\n");
	ret = ast_pbx_start(ast);
	if (ret < 0)
	{
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

	CDEBUG(call, NULL, "Incomming setup from LCR. (callerid %s, dialing %s)\n", param->setup.callerinfo.id, param->setup.dialinginfo.id);

	/* create asterisk channel instrance */
	ast = ast_channel_alloc(1, AST_STATE_RESERVED, NULL, NULL, "", NULL, "", 0, "%s/%d", lcr_type, ++glob_channel);
	if (!ast)
	{
		/* release */
		CERROR(call, NULL, "Failed to create Asterisk channel - releasing.\n");
		send_release_and_import(call, CAUSE_RESSOURCEUNAVAIL, LOCATION_PRIVATE_LOCAL);
		/* remove call */
		free_call(call);
		return;
	}
	/* link together */
	call->ast = ast;
	ast->tech_pvt = call;
	ast->tech = &lcr_tech;
	ast->fds[0] = call->pipe[0];
	
	/* fill setup information */
	if (param->setup.dialinginfo.id)
		strncpy(ast->exten, param->setup.dialinginfo.id, AST_MAX_EXTENSION-1);
	if (param->setup.context[0])
		strncpy(ast->context, param->setup.context, AST_MAX_CONTEXT-1);
	else
		strncpy(ast->context, param->setup.callerinfo.interface, AST_MAX_CONTEXT-1);
	if (param->setup.callerinfo.id[0])
		ast->cid.cid_num = strdup(param->setup.callerinfo.id);
	if (param->setup.callerinfo.name[0])
		ast->cid.cid_name = strdup(param->setup.callerinfo.name);
	if (param->setup.redirinfo.id[0])
		ast->cid.cid_name = strdup(numberrize_callerinfo(param->setup.callerinfo.id, param->setup.callerinfo.ntype, options.national, options.international));
	switch (param->setup.callerinfo.present)
	{
		case INFO_PRESENT_ALLOWED:
			ast->cid.cid_pres = AST_PRES_ALLOWED;
		break;
		case INFO_PRESENT_RESTRICTED:
			ast->cid.cid_pres = AST_PRES_RESTRICTED;
		break;
		default:
			ast->cid.cid_pres = AST_PRES_UNAVAILABLE;
	}
	switch (param->setup.callerinfo.ntype)
	{
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
	ast->transfercapability = param->setup.capainfo.bearer_capa;
	/* enable hdlc if transcap is data */
	if (param->setup.capainfo.source_mode == B_MODE_HDLC)
		call->hdlc = 1;
	strncpy(call->oad, numberrize_callerinfo(param->setup.callerinfo.id, param->setup.callerinfo.ntype, options.national, options.international), sizeof(call->oad)-1);

	/* configure channel */
	ast->nativeformats = (options.law=='a')?AST_FORMAT_ALAW:AST_FORMAT_ULAW;
	ast->readformat = ast->rawreadformat = ast->nativeformats;
	ast->writeformat = ast->rawwriteformat =  ast->nativeformats;
	ast->priority = 1;
	ast->hangupcause = 0;

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
	if (call->ast && call->pbx_started)
		strncat(call->queue_string, "P", sizeof(call->queue_string)-1);
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
	if (call->ast && call->pbx_started)
		strncat(call->queue_string, "R", sizeof(call->queue_string)-1);
}

/*
 * incoming connect from LCR
 */
static void lcr_in_connect(struct chan_call *call, int message_type, union parameter *param)
{
	union parameter newparam;

	CDEBUG(call, call->ast, "Incomming connect (answer) from LCR.\n");

	/* change state */
	call->state = CHAN_LCR_STATE_CONNECT;
	/* request bchannel */
	if (!call->bchannel) {
		CDEBUG(call, call->ast, "Requesting B-channel.\n");
		memset(&newparam, 0, sizeof(union parameter));
		newparam.bchannel.type = BCHANNEL_REQUEST;
		send_message(MESSAGE_BCHANNEL, call->ref, &newparam);
	}
	/* copy connectinfo */
	memcpy(&call->connectinfo, &param->connectinfo, sizeof(struct connect_info));
	/* queue event to asterisk */
	if (call->ast && call->pbx_started)
		strncat(call->queue_string, "A", sizeof(call->queue_string)-1);
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
	if (call->bridge_call)
	{
		CDEBUG(call, call->ast, "Only signal disconnect via bridge.\n");
		bridge_message_if_bridged(call, message_type, param);
		return;
	}
#endif
	/* release lcr with same cause */
	send_release_and_import(call, call->cause, call->location);
	call->ref = 0;
	/* change to release state */
	call->state = CHAN_LCR_STATE_RELEASE;
	/* queue release asterisk */
	if (ast)
	{
		ast->hangupcause = call->cause;
		if (call->pbx_started)
			strcpy(call->queue_string, "H"); // overwrite other indications
		else {
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
	if (!call->cause)
	{
	       call->cause = param->disconnectinfo.cause;
	       call->location = param->disconnectinfo.location;
	}
	/* if we have an asterisk instance, queue hangup, else we are done */
	if (ast)
	{
		ast->hangupcause = call->cause;
		if (call->pbx_started)
			strcpy(call->queue_string, "H");
		else {
			ast_hangup(ast); // call will be destroyed here
		}
	} else
	{
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
	if (!call->pbx_started)
	{
		CDEBUG(call, call->ast, "Asterisk not started, adding digits to number.\n");
		strncat(ast->exten, param->information.id, AST_MAX_EXTENSION-1);
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
	if (call->state == CHAN_LCR_STATE_IN_DIALING && param->information.id[0])
		strncat(call->queue_string, param->information.id, sizeof(call->queue_string)-1);

	/* use bridge to forware message not supported by asterisk */
	if (call->state == CHAN_LCR_STATE_CONNECT) {
		CDEBUG(call, call->ast, "Call is connected, bridging.\n");
		bridge_message_if_bridged(call, message_type, param);
	}
}

/*
 * incoming information from LCR
 */
static void lcr_in_notify(struct chan_call *call, int message_type, union parameter *param)
{
	union parameter newparam;

	CDEBUG(call, call->ast, "Incomming notify from LCR. (notify=%d)\n", param->notifyinfo.notify);

	/* request bchannel, if call is resumed and we don't have it */
	if (param->notifyinfo.notify == INFO_NOTIFY_USER_RESUMED && !call->bchannel && call->ref) {
		CDEBUG(call, call->ast, "Reqesting bchannel at resume.\n");
		memset(&newparam, 0, sizeof(union parameter));
		newparam.bchannel.type = BCHANNEL_REQUEST;
		send_message(MESSAGE_BCHANNEL, call->ref, &newparam);
	}

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

	CDEBUG(call, call->ast, "Recognised DTMF digit '%c'.\n", val);
	digit[0] = val;
	digit[1] = '\0';
	strncat(call->queue_string, digit, sizeof(call->queue_string)-1);
}

/*
 * message received from LCR
 */
int receive_message(int message_type, unsigned int ref, union parameter *param)
{
	struct bchannel *bchannel;
	struct chan_call *call;
	union parameter newparam;

	memset(&newparam, 0, sizeof(union parameter));

	/* handle bchannel message*/
	if (message_type == MESSAGE_BCHANNEL)
	{
		switch(param->bchannel.type)
		{
			case BCHANNEL_ASSIGN:
			CDEBUG(NULL, NULL, "Received BCHANNEL_ASSIGN message. (handle=%08lx) for ref %d\n", param->bchannel.handle, ref);
			if ((bchannel = find_bchannel_handle(param->bchannel.handle)))
			{
				CERROR(NULL, NULL, "bchannel handle %x already assigned.\n", (int)param->bchannel.handle);
				return(-1);
			}
			/* create bchannel */
			bchannel = alloc_bchannel(param->bchannel.handle);
			if (!bchannel)
			{
				CERROR(NULL, NULL, "alloc bchannel handle %x failed.\n", (int)param->bchannel.handle);
				return(-1);
			}

			/* configure channel */
			bchannel->b_tx_gain = param->bchannel.tx_gain;
			bchannel->b_rx_gain = param->bchannel.rx_gain;
			strncpy(bchannel->b_pipeline, param->bchannel.pipeline, sizeof(bchannel->b_pipeline)-1);
			if (param->bchannel.crypt_len && param->bchannel.crypt_len <= sizeof(bchannel->b_bf_key))
			{
				bchannel->b_bf_len = param->bchannel.crypt_len;
				memcpy(bchannel->b_bf_key, param->bchannel.crypt, param->bchannel.crypt_len);
			}
			bchannel->b_txdata = 0;
			bchannel->b_dtmf = 1;
			bchannel->b_tx_dejitter = 1;

			/* in case, ref is not set, this bchannel instance must
			 * be created until it is removed again by LCR */
			/* link to call */
			call = find_call_ref(ref);
			if (call)
			{
				bchannel->call = call;
				call->bchannel = bchannel;
				if (call->dtmf)
					bchannel_dtmf(bchannel, 1);
				if (call->bf_len)
					bchannel_blowfish(bchannel, call->bf_key, call->bf_len);
				if (call->pipeline[0])
					bchannel_pipeline(bchannel, call->pipeline);
				if (call->rx_gain)
					bchannel_gain(bchannel, call->rx_gain, 0);
				if (call->tx_gain)
					bchannel_gain(bchannel, call->tx_gain, 1);
				if (call->bridge_id) {
					CDEBUG(call, call->ast, "Join bchannel, because call is already bridged.\n");
					bchannel_join(bchannel, call->bridge_id);
				}
				/* create only, if call exists, othewhise it bchannel is freed below... */
				if (bchannel_create(bchannel, ((call->nodsp)?1:0) + ((call->hdlc)?2:0)))
					bchannel_activate(bchannel, 1);
			}
			/* acknowledge */
			newparam.bchannel.type = BCHANNEL_ASSIGN_ACK;
			newparam.bchannel.handle = param->bchannel.handle;
			send_message(MESSAGE_BCHANNEL, 0, &newparam);
			/* if call has released before bchannel is assigned */
			if (!call) {
				newparam.bchannel.type = BCHANNEL_RELEASE;
				newparam.bchannel.handle = param->bchannel.handle;
				send_message(MESSAGE_BCHANNEL, 0, &newparam);
			}

			break;

			case BCHANNEL_REMOVE:
			CDEBUG(NULL, NULL, "Received BCHANNEL_REMOVE message. (handle=%08lx)\n", param->bchannel.handle);
			if (!(bchannel = find_bchannel_handle(param->bchannel.handle)))
			{
				CERROR(NULL, NULL, "Bchannel handle %x not assigned.\n", (int)param->bchannel.handle);
				return(-1);
			}
			/* unklink from call and destroy bchannel */
			free_bchannel(bchannel);

			/* acknowledge */
			newparam.bchannel.type = BCHANNEL_REMOVE_ACK;
			newparam.bchannel.handle = param->bchannel.handle;
			send_message(MESSAGE_BCHANNEL, 0, &newparam);
			
			break;

			default:
			CDEBUG(NULL, NULL, "Received unknown bchannel message %d.\n", param->bchannel.type);
		}
		return(0);
	}

	/* handle new ref */
	if (message_type == MESSAGE_NEWREF)
	{
		if (param->direction)
		{
			/* new ref from lcr */
			CDEBUG(NULL, NULL, "Received new ref by LCR, due to incomming call. (ref=%ld)\n", ref);
			if (!ref || find_call_ref(ref))
			{
				CERROR(NULL, NULL, "Illegal new ref %ld received.\n", ref);
				return(-1);
			}
			/* allocate new call instance */
			call = alloc_call();
			/* new state */
			call->state = CHAN_LCR_STATE_IN_PREPARE;
			/* set ref */
			call->ref = ref;
			call->ref_was_assigned = 1;
			/* wait for setup (or release from asterisk) */
		} else
		{
			/* new ref, as requested from this remote application */
			CDEBUG(NULL, NULL, "Received new ref by LCR, as requested from chan_lcr. (ref=%ld)\n", ref);
			call = find_call_ref(0);
			if (!call)
			{
				/* send release, if ref does not exist */
				CDEBUG(NULL, NULL, "No call found, that requests a ref.\n");
				send_release_and_import(call, CAUSE_NORMAL, LOCATION_PRIVATE_LOCAL);
				return(0);
			}
			/* store new ref */
			call->ref = ref;
			call->ref_was_assigned = 1;
			/* send pending setup info */
			if (call->state == CHAN_LCR_STATE_OUT_PREPARE)
				send_setup_to_lcr(call);
			/* release if asterisk has signed off */
			else if (call->state == CHAN_LCR_STATE_RELEASE)
			{
				/* send release */
				if (call->cause)
					send_release_and_import(call, call->cause, call->location);
				else
					send_release_and_import(call, CAUSE_NORMAL, LOCATION_PRIVATE_LOCAL);
				/* free call */
				free_call(call);
				return(0);
			}
		}
		return(0);
	}

	/* check ref */
	if (!ref)
	{
		CERROR(NULL, NULL, "Received message %d without ref.\n", message_type);
		return(-1);
	}
	call = find_call_ref(ref);
	if (!call)
	{
		/* ignore ref that is not used (anymore) */
		CDEBUG(NULL, NULL, "Message %d from LCR ignored, because no call instance found.\n", message_type);
		return(0);
	}

	/* handle messages */
	switch(message_type)
	{
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
		break;

		case MESSAGE_NOPATTERN: // audio not available from LCR
		break;

		case MESSAGE_AUDIOPATH: // if remote audio connected or hold
		call->audiopath = param->audiopath;
		break;

		default:
		CDEBUG(call, call->ast, "Message %d from LCR unhandled.\n", message_type);
		break;
	}
	return(0);
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
		strcpy(call->queue_string, "H");
		call = call->next;
	}

	/* release all bchannels */
	while(bchannel_first)
		free_bchannel(bchannel_first);
}


/* asterisk handler
 * warning! not thread safe
 * returns -1 for socket error, 0 for no work, 1 for work
 */
int handle_socket(void)
{
	int work = 0;
	int len;
	struct admin_list *admin;
	struct admin_message msg;

	/* read from socket */
	len = read(lcr_sock, &msg, sizeof(msg));
	if (len == 0)
	{
		CERROR(NULL, NULL, "Socket closed.\n");
		return(-1); // socket closed
	}
	if (len > 0)
	{
		if (len != sizeof(msg))
		{
			CERROR(NULL, NULL, "Socket short read. (len %d)\n", len);
			return(-1); // socket error
		}
		if (msg.message != ADMIN_MESSAGE)
		{
			CERROR(NULL, NULL, "Socket received illegal message %d.\n", msg.message);
			return(-1);
		}
		receive_message(msg.u.msg.type, msg.u.msg.ref, &msg.u.msg.param);
		work = 1;
	} else
	{
		if (errno != EWOULDBLOCK)
		{
			CERROR(NULL, NULL, "Socket failed (errno %d).\n", errno);
			return(-1);
		}
	}

	/* write to socket */
	if (!admin_first)
		return(work);
	admin = admin_first;
	len = write(lcr_sock, &admin->msg, sizeof(msg));
	if (len == 0)
	{
		CERROR(NULL, NULL, "Socket closed.\n");
		return(-1); // socket closed
	}
	if (len > 0)
	{
		if (len != sizeof(msg))
		{
			CERROR(NULL, NULL, "Socket short write. (len %d)\n", len);
			return(-1); // socket error
		}
		/* free head */
		admin_first = admin->next;
		free(admin);

		work = 1;
	} else
	{
		if (errno != EWOULDBLOCK)
		{
			CERROR(NULL, NULL, "Socket failed (errno %d).\n", errno);
			return(-1);
		}
	}

	return(work);
}

/*
 * open and close socket and thread
 */
int open_socket(void)
{
	int ret;
	int conn;
	struct sockaddr_un sock_address;
	unsigned int on = 1;
	union parameter param;

	/* open socket */
	if ((lcr_sock = socket(PF_UNIX, SOCK_STREAM, 0)) < 0)
	{
		CERROR(NULL, NULL, "Failed to create socket.\n");
		return(lcr_sock);
	}

	/* set socket address and name */
	memset(&sock_address, 0, sizeof(sock_address));
	sock_address.sun_family = PF_UNIX;
	sprintf(sock_address.sun_path, SOCKET_NAME, options.lock);

	/* connect socket */
	if ((conn = connect(lcr_sock, (struct sockaddr *)&sock_address, SUN_LEN(&sock_address))) < 0)
	{
		close(lcr_sock);
		lcr_sock = -1;
		CDEBUG(NULL, NULL, "Failed to connect to socket '%s'. Is LCR running?\n", sock_address.sun_path);
		return(conn);
	}

	/* set non-blocking io */
	if ((ret = ioctl(lcr_sock, FIONBIO, (unsigned char *)(&on))) < 0)
	{
		close(lcr_sock);
		lcr_sock = -1;
		CERROR(NULL, NULL, "Failed to set socket into non-blocking IO.\n");
		return(ret);
	}

	/* enque hello message */
	memset(&param, 0, sizeof(param));
	strcpy(param.hello.application, "asterisk");
	send_message(MESSAGE_HELLO, 0, &param);

	return(lcr_sock);
}

void close_socket(void)
{
	struct admin_list *admin, *temp;
	
	/* flush pending messages */
	admin = admin_first;
	while(admin) {
		temp = admin;
		admin = admin->next;
		free(temp);
	}
	admin_first = NULL;

	/* close socket */
	if (lcr_sock >= 0)	
		close(lcr_sock);
	lcr_sock = -1;
}


/* sending queue to asterisk */
static int queue_send(void)
{
	int work = 0;
	struct chan_call *call;
	struct ast_channel *ast;
	struct ast_frame fr;
	char *p;

	call = call_first;
	while(call) {
		p = call->queue_string;
		ast = call->ast;
		if (*p && ast) {
			/* there is something to queue */
			if (!ast_channel_trylock(ast)) { /* succeed */
				while(*p) {
					switch (*p) {
					case 'P':
						CDEBUG(call, ast, "Sending queued PROCEEDING to Asterisk.\n");
						ast_queue_control(ast, AST_CONTROL_PROCEEDING);
						break;
					case 'R':
						CDEBUG(call, ast, "Sending queued RINGING to Asterisk.\n");
						ast_queue_control(ast, AST_CONTROL_RINGING);
						ast_setstate(ast, AST_STATE_RINGING);
						break;
					case 'A':
						CDEBUG(call, ast, "Sending queued ANSWER to Asterisk.\n");
						ast_queue_control(ast, AST_CONTROL_ANSWER);
						break;
					case 'H':
						CDEBUG(call, ast, "Sending queued HANGUP to Asterisk.\n");
						ast_queue_hangup(ast);
						break;
					case '1': case '2': case '3': case 'a':
					case '4': case '5': case '6': case 'b':
					case '7': case '8': case '9': case 'c':
					case '*': case '0': case '#': case 'd':
						CDEBUG(call, ast, "Sending queued digit '%c' to Asterisk.\n", *p);
						/* send digit to asterisk */
						memset(&fr, 0, sizeof(fr));
						fr.frametype = AST_FRAME_DTMF_BEGIN;
						fr.subclass = *p;
						fr.delivery = ast_tv(0, 0);
						ast_queue_frame(ast, &fr);
						fr.frametype = AST_FRAME_DTMF_END;
						ast_queue_frame(ast, &fr);
						break;
					default:
						CDEBUG(call, ast, "Ignoring queued digit 0x%02d.\n", *p);
					}
					p++;
				}
				call->queue_string[0] = '\0';
				ast_channel_unlock(ast);
				work = 1;
			}
		}
		call = call->next;
	}

	return work;
}

/* signal handler */
void sighandler(int sigset)
{
}

/* chan_lcr thread */
static void *chan_thread(void *arg)
{
	int work;
	int ret;
	union parameter param;
	time_t retry = 0, now;

	bchannel_pid = getpid();

//	signal(SIGPIPE, sighandler);
	
	memset(&param, 0, sizeof(union parameter));
	if (lcr_sock < 0)
		time(&retry);

	ast_mutex_lock(&chan_lock);

	while(!quit) {
		work = 0;

		if (lcr_sock > 0) {
			/* handle socket */
			ret = handle_socket();
			if (ret < 0) {
				CERROR(NULL, NULL, "Handling of socket failed - closing for some seconds.\n");
				close_socket();
				release_all_calls();
				time(&retry);
			}
			if (ret)
				work = 1;
		} else {
			time(&now);
			if (retry && now-retry > 5) {
				CDEBUG(NULL, NULL, "Retry to open socket.\n");
				retry = 0;
				if (open_socket() < 0) {
					time(&retry);
				}
				work = 1;
			}
					
		}

		/* handle mISDN */
		ret = bchannel_handle();
		if (ret)
			work = 1;

		/* handle messages to asterisk */
		ret = queue_send();
		if (ret)
			work = 1;

		/* delay if no work done */
		if (!work) {
			ast_mutex_unlock(&chan_lock);
			usleep(30000);
			ast_mutex_lock(&chan_lock);
		}
	}

	close_socket();

	CERROR(NULL, NULL, "Thread exit.\n");
	
	ast_mutex_unlock(&chan_lock);

//	signal(SIGPIPE, SIG_DFL);

	return NULL;
}

/*
 * new asterisk instance
 */
static
struct ast_channel *lcr_request(const char *type, int format, void *data, int *cause)
{
	char exten[256], *dial, *interface, *opt;
	struct ast_channel *ast;
        struct chan_call *call;

	ast_mutex_lock(&chan_lock);
	CDEBUG(NULL, NULL, "Received request from Asterisk. (data=%s)\n", (char *)data);

	/* if socket is closed */
	if (lcr_sock < 0)
	{
		CERROR(NULL, NULL, "Rejecting call from Asterisk, because LCR not running.\n");
		ast_mutex_unlock(&chan_lock);
		return NULL;
	}

	/* create call instance */
	call = alloc_call();
	if (!call)
	{
		/* failed to create instance */
		ast_mutex_unlock(&chan_lock);
		return NULL;
	}

	/* create asterisk channel instrance */
	ast = ast_channel_alloc(1, AST_STATE_RESERVED, NULL, NULL, "", NULL, "", 0, "%s/%d", lcr_type, ++glob_channel);
	if (!ast)
	{
		CERROR(NULL, NULL, "Failed to create Asterisk channel.\n");
		free_call(call);
		/* failed to create instance */
		ast_mutex_unlock(&chan_lock);
		return NULL;
	}
	ast->tech = &lcr_tech;
	ast->tech_pvt = (void *)1L; // set pointer or asterisk will not call
	/* configure channel */
	ast->nativeformats = (options.law=='a')?AST_FORMAT_ALAW:AST_FORMAT_ULAW;
	ast->readformat = ast->rawreadformat = ast->nativeformats;
	ast->writeformat = ast->rawwriteformat =  ast->nativeformats;
	ast->priority = 1;
	ast->hangupcause = 0;

	/* link together */
	call->ast = ast;
	ast->tech_pvt = call;
	ast->fds[0] = call->pipe[0];
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

	ast_mutex_lock(&chan_lock);
        call = ast->tech_pvt;
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
	newparam.direction = 0; /* request from app */
	send_message(MESSAGE_NEWREF, 0, &newparam);

	/* set hdlc if capability requires hdlc */
	if (ast->transfercapability == INFO_BC_DATAUNRESTRICTED
	 || ast->transfercapability == INFO_BC_DATARESTRICTED
	 || ast->transfercapability == INFO_BC_VIDEO)
		call->hdlc = 1;
	/* if hdlc is forced by option, we change transcap to data */
	if (call->hdlc
	 && ast->transfercapability != INFO_BC_DATAUNRESTRICTED
	 && ast->transfercapability != INFO_BC_DATARESTRICTED
	 && ast->transfercapability != INFO_BC_VIDEO)
		ast->transfercapability = INFO_BC_DATAUNRESTRICTED;

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
                /* not handled */
                ast_log(LOG_DEBUG, "Unable to handle DTMF tone "
			"'%c' for '%s'\n", digit, ast->name);
        }
}


static int lcr_digit_begin(struct ast_channel *ast, char digit)
{
        struct chan_call *call;
	union parameter newparam;
	char buf[]="x";

	/* only pass IA5 number space */
	if (digit > 126 || digit < 32)
		return 0;

	ast_mutex_lock(&chan_lock);
        call = ast->tech_pvt;
	if (!call) {
		CERROR(NULL, ast, "Received digit from Asterisk, but no call instance exists.\n");
		ast_mutex_unlock(&chan_lock);
		return -1;
	}

	CDEBUG(call, ast, "Received digit '%c' from Asterisk.\n", digit);

	/* send information or queue them */
	if (call->ref && call->state == CHAN_LCR_STATE_OUT_DIALING)
	{
		CDEBUG(call, ast, "Sending digit to LCR, because we are in dialing state.\n");
		memset(&newparam, 0, sizeof(union parameter));
		newparam.information.id[0] = digit;
		newparam.information.id[1] = '\0';
		send_message(MESSAGE_INFORMATION, call->ref, &newparam);
	} else
	if (!call->ref
	 && (call->state == CHAN_LCR_STATE_OUT_PREPARE || call->state == CHAN_LCR_STATE_OUT_SETUP))
	{
		CDEBUG(call, ast, "Queue digits, because we are in setup/dialing state and have no ref yet.\n");
		*buf = digit;
		strncat(call->dialque, buf, strlen(call->dialque)-1);
	}

	ast_mutex_unlock(&chan_lock);
	return(0);
}

static int lcr_digit_end(struct ast_channel *ast, char digit, unsigned int duration)
{
	int inband_dtmf = 0;
        struct chan_call *call;

	ast_mutex_lock(&chan_lock);

        call = ast->tech_pvt;

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

	return (0);
}

static int lcr_answer(struct ast_channel *ast)
{
	union parameter newparam;
        struct chan_call *call;

	ast_mutex_lock(&chan_lock);
        call = ast->tech_pvt;
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
	/* request bchannel */
	if (!call->bchannel) {
		CDEBUG(call, ast, "Requesting B-channel.\n");
		memset(&newparam, 0, sizeof(union parameter));
		newparam.bchannel.type = BCHANNEL_REQUEST;
		send_message(MESSAGE_BCHANNEL, call->ref, &newparam);
	}
	/* enable keypad */
//	memset(&newparam, 0, sizeof(union parameter));
//	send_message(MESSAGE_ENABLEKEYPAD, call->ref, &newparam);
	/* enable dtmf */
	if (call->no_dtmf)
		CDEBUG(call, ast, "DTMF is disabled by option.\n");
	else
		call->dtmf = 1;
	
   	ast_mutex_unlock(&chan_lock);
        return 0;
}

static int lcr_hangup(struct ast_channel *ast)
{
        struct chan_call *call;
	pthread_t tid = pthread_self();

	if (!pthread_equal(tid, chan_tid))
		ast_mutex_lock(&chan_lock);
        call = ast->tech_pvt;
	if (!call) {
		CERROR(NULL, ast, "Received hangup from Asterisk, but no call instance exists.\n");
		if (!pthread_equal(tid, chan_tid))
			ast_mutex_unlock(&chan_lock);
		return -1;
	}

	if (!pthread_equal(tid, chan_tid))
		CDEBUG(call, ast, "Received hangup from Asterisk thread.\n");
	else
		CDEBUG(call, ast, "Received hangup from LCR thread.\n");

	/* disconnect asterisk, maybe not required */
	ast->tech_pvt = NULL;
	ast->fds[0] = -1;
	if (call->ref)
	{
		/* release */
		CDEBUG(call, ast, "Releasing ref and freeing call instance.\n");
		if (ast->hangupcause > 0)
			send_release_and_import(call, ast->hangupcause, LOCATION_PRIVATE_LOCAL);
		else
			send_release_and_import(call, CAUSE_NORMAL, LOCATION_PRIVATE_LOCAL);
		/* remove call */
		free_call(call);
		if (!pthread_equal(tid, chan_tid))
			ast_mutex_unlock(&chan_lock);
		return 0;
	} else
	{
		/* ref is not set, due to prepare setup or release */
		if (call->state == CHAN_LCR_STATE_RELEASE)
		{
			/* we get the response to our release */
			CDEBUG(call, ast, "Freeing call instance, because we have no ref AND we are requesting no ref.\n");
			free_call(call);
		} else
		{
			/* during prepare, we change to release state */
			CDEBUG(call, ast, "We must wait until we received our ref, until we can free call instance.\n");
			call->state = CHAN_LCR_STATE_RELEASE;
			call->ast = NULL;
		}
	} 
	if (!pthread_equal(tid, chan_tid))
		ast_mutex_unlock(&chan_lock);
	return 0;
}

static int lcr_write(struct ast_channel *ast, struct ast_frame *f)
{
        struct chan_call *call;

	if (!f->subclass)
		CDEBUG(NULL, ast, "No subclass\n");
	if (!(f->subclass & ast->nativeformats))
		CDEBUG(NULL, ast, "Unexpected format.\n");
	
	ast_mutex_lock(&chan_lock);
        call = ast->tech_pvt;
	if (!call) {
		ast_mutex_unlock(&chan_lock);
		return -1;
	}
	if (call->bchannel && f->samples)
		bchannel_transmit(call->bchannel, (unsigned char *)f->data, f->samples);
	ast_mutex_unlock(&chan_lock);
	return 0;
}


static struct ast_frame *lcr_read(struct ast_channel *ast)
{
        struct chan_call *call;
	int len;

	ast_mutex_lock(&chan_lock);
        call = ast->tech_pvt;
	if (!call) {
		ast_mutex_unlock(&chan_lock);
		return NULL;
	}
	if (call->pipe[0] > -1) {
		if (call->rebuffer && !call->hdlc) {
			len = read(call->pipe[0], call->read_buff, 160);
		} else {
			len = read(call->pipe[0], call->read_buff, sizeof(call->read_buff));
		}
		if (len < 0 && errno == EAGAIN) {
			ast_mutex_unlock(&chan_lock);
			return &ast_null_frame;
		}
		if (len <= 0) {
			close(call->pipe[0]);
			call->pipe[0] = -1;
			ast_mutex_unlock(&chan_lock);
			return NULL;
		}
	}

	call->read_fr.frametype = AST_FRAME_VOICE;
	call->read_fr.subclass = ast->nativeformats;
	call->read_fr.datalen = len;
	call->read_fr.samples = len;
	call->read_fr.delivery = ast_tv(0,0);
	(unsigned char *)call->read_fr.data = call->read_buff;
	ast_mutex_unlock(&chan_lock);

	return &call->read_fr;
}

static int lcr_indicate(struct ast_channel *ast, int cond, const void *data, size_t datalen)
{
	union parameter newparam;
        int res = 0;
        struct chan_call *call;

	ast_mutex_lock(&chan_lock);
        call = ast->tech_pvt;
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
			}
			break;
                case AST_CONTROL_CONGESTION:
			CDEBUG(call, ast, "Received indicate AST_CONTROL_CONGESTION from Asterisk. (cause %d)\n", ast->hangupcause);
			if (call->state != CHAN_LCR_STATE_OUT_DISCONNECT) {
				/* send message to lcr */
				memset(&newparam, 0, sizeof(union parameter));
				newparam.disconnectinfo.cause = ast->hangupcause;
				newparam.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
				send_message(MESSAGE_DISCONNECT, call->ref, &newparam);
				/* change state */
				call->state = CHAN_LCR_STATE_OUT_DISCONNECT;
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
			ast_setstate(ast, AST_STATE_RINGING);
			if (call->state == CHAN_LCR_STATE_IN_SETUP
			 || call->state == CHAN_LCR_STATE_IN_DIALING
			 || call->state == CHAN_LCR_STATE_IN_PROCEEDING) {
				/* send message to lcr */
				memset(&newparam, 0, sizeof(union parameter));
				send_message(MESSAGE_ALERTING, call->ref, &newparam);
				/* change state */
				call->state = CHAN_LCR_STATE_IN_ALERTING;
			}
			break;
                case -1:
			CDEBUG(call, ast, "Received indicate -1.\n");
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
			ast_moh_start(ast,data,ast->musicclass);
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
			CDEBUG(call, ast, "Received AST_CONTROL_SRCUPDATE from Asterisk.\n");
                        break;
#endif
                default:
			CERROR(call, ast, "Received indicate from Asterisk with unknown condition %d.\n", cond);
                        res = -1;
			break;
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
	call = ast->tech_pvt;
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
	call = ast->tech_pvt;
	if (!call) {
		CERROR(NULL, ast, "Received send_text from Asterisk, but no call instance exists.\n");
		ast_mutex_unlock(&chan_lock);
		return -1;
	}

	CDEBUG(call, ast, "Received send_text from Asterisk. (text=%s)\n", text);
	memset(&newparam, 0, sizeof(union parameter));
	strncpy(newparam.notifyinfo.display, text, sizeof(newparam.notifyinfo.display)-1);
	send_message(MESSAGE_NOTIFY, call->ref, &newparam);
	ast_mutex_lock(&chan_lock);
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

	CDEBUG(NULL, NULL, "Received bridging request from Asterisk.\n");

	carr[0] = ast1;
	carr[1] = ast2;

	/* join via dsp (if the channels are currently open) */
	ast_mutex_lock(&chan_lock);
	call1 = ast1->tech_pvt;
	call2 = ast2->tech_pvt;
	if (!call1 || !call2) {
		CDEBUG(NULL, NULL, "Bridge, but we don't have two call instances, exitting.\n");
		ast_mutex_unlock(&chan_lock);
		return AST_BRIDGE_COMPLETE;
	}

	/* join, if both call instances uses dsp */
	if (!call1->nodsp && !call2->nodsp) {
		CDEBUG(NULL, NULL, "Both calls use DSP, bridging via DSP.\n");

		/* get bridge id and join */
		bridge_id = new_bridge_id();
		
		call1->bridge_id = bridge_id;
		if (call1->bchannel)
			bchannel_join(call1->bchannel, bridge_id);

		call2->bridge_id = bridge_id;
		if (call2->bchannel)
			bchannel_join(call2->bchannel, bridge_id);
	} else
	if (call1->nodsp && call2->nodsp)
		CDEBUG(NULL, NULL, "Both calls use no DSP, bridging in channel driver.\n");
	else
		CDEBUG(NULL, NULL, "One call uses no DSP, bridging in channel driver.\n");
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
	call1 = ast1->tech_pvt;
	call2 = ast2->tech_pvt;
	if (call1 && call1->bridge_id)
	{
		call1->bridge_id = 0;
		if (call1->bchannel)
			bchannel_join(call1->bchannel, 0);
		if (call1->bridge_call)
			call1->bridge_call->bridge_call = NULL;
	}
	if (call2 && call1->bridge_id)
	{
		call2->bridge_id = 0;
		if (call2->bchannel)
			bchannel_join(call2->bchannel, 0);
		if (call2->bridge_call)
			call2->bridge_call->bridge_call = NULL;
	}
	call1->bridge_call = NULL;
	call2->bridge_call = NULL;

	ast_mutex_unlock(&chan_lock);
	return AST_BRIDGE_COMPLETE;
}
static struct ast_channel_tech lcr_tech = {
	.type="LCR",
	.description = "Channel driver for connecting to Linux-Call-Router",
	.capabilities = AST_FORMAT_ALAW,
	.requester = lcr_request,
	.send_digit_begin = lcr_digit_begin,
	.send_digit_end = lcr_digit_end,
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



static int lcr_config_exec(struct ast_channel *ast, void *data)
{
	struct chan_call *call;

	ast_mutex_lock(&chan_lock);
	CDEBUG(NULL, ast, "Received lcr_config (data=%s)\n", (char *)data);
	/* find channel */
	call = call_first;
	while(call) {
		if (call->ast == ast)
			break;
		call = call->next;
	}
	if (call)
		apply_opt(call, (char *)data);
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

	for (i = 0; i < 256; i++) {
		flip_bits[i] = (i>>7) | ((i>>5)&2) | ((i>>3)&4) | ((i>>1)&8)
			     | (i<<7) | ((i&2)<<5) | ((i&4)<<3) | ((i&8)<<1);
	}

	if (read_options() == 0) {
		CERROR(NULL, NULL, "%s", options_error);
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_mutex_init(&chan_lock);
	ast_mutex_init(&log_lock);

	if (open_socket() < 0) {
		/* continue with closed socket */
	}

	if (bchannel_initialize()) {
		CERROR(NULL, NULL, "Unable to open mISDN device\n");
		close_socket();
		return AST_MODULE_LOAD_DECLINE;
	}
	mISDN_created = 1;

	lcr_tech.capabilities = (options.law=='a')?AST_FORMAT_ALAW:AST_FORMAT_ULAW;
	if (ast_channel_register(&lcr_tech)) {
		CERROR(NULL, NULL, "Unable to register channel class\n");
		bchannel_deinitialize();
		close_socket();
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_register_application("lcr_config", lcr_config_exec, "lcr_config",
				 "lcr_config(<opt><optarg>:<opt>:...)\n"
				 "Sets LCR opts. and optargs\n"
				 "\n"
				 "The available options are:\n"
				 "    d - Send display text on called phone, text is the optarg.\n"
				 "    n - Don't detect dtmf tones on called channel.\n"
				 "    h - Force data call (HDLC).\n" 
				 "    t - Disable mISDN_dsp features (required for fax application).\n"
				 "    c - Make crypted outgoing call, optarg is keyindex.\n"
				 "    e - Perform echo cancelation on this channel.\n"
				 "        Takes mISDN pipeline option as optarg.\n"
				 "    s - Send Non Inband DTMF as inband.\n"
				 "   vr - rxgain control\n"
				 "   vt - txgain control\n"
				 "        Volume changes at factor 2 ^ optarg.\n"
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

	quit = 0;	
	if ((pthread_create(&chan_tid, NULL, chan_thread, NULL)<0))
	{
		/* failed to create thread */
		bchannel_deinitialize();
		close_socket();
		ast_channel_unregister(&lcr_tech);
		return AST_MODULE_LOAD_DECLINE;
	}
	return 0;
}

int unload_module(void)
{
	/* First, take us out of the channel loop */
	CDEBUG(NULL, NULL, "-- Unregistering mISDN Channel Driver --\n");

	quit = 1;
	pthread_join(chan_tid, NULL);	
	
	ast_channel_unregister(&lcr_tech);

        ast_unregister_application("lcr_config");


	if (mISDN_created) {
		bchannel_deinitialize();
		mISDN_created = 0;
	}

	if (lcr_sock >= 0) {
		close(lcr_sock);
		lcr_sock = -1;
	}

	return 0;
}

int reload_module(void)
{
//	reload_config();
	return 0;
}


#define AST_MODULE "chan_lcr"

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Channel driver for Linux-Call-Router Support (ISDN BRI/PRI)",
		.load = load_module,
		.unload = unload_module,
		.reload = reload_module,
	       );

