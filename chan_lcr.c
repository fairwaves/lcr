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
A new chan_call instance is created. The call reference (ref) is given by
MESSAGE_NEWREF. The state is CHAN_LCR_STATE_IN_PREPARE.
After receiving MESSAGE_SETUP from LCR, the ast_channel instance is created
using ast_channel_alloc(1).  The setup information is given to asterisk.
The new Asterisk instance pointer (ast) is stored to chan_call structure.
The state changes to CHAN_LCR_STATE_IN_SETUP.


Call is initiated by Asterisk:

If a call is reveiced from Asterisk, a new chan_call instance is created.
The new Asterisk instance pointer (ast) is stored to chan_call structure.
A MESSASGE_NEWREF is sent to LCR requesting a new call reference (ref).
The current call ref is set to 0, the state is CHAN_LCR_STATE_OUT_PREPARE.
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
Later, if the MESSAGE_NEWREF reply is received, a MESSAGE_RELEASE is sent to
LCR and the chan_call instance is destroyed.
If the ref is 0 and the state is CHAN_LCR_STATE_RELEASE, see the proceedure
"Call is released by LCR".

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <semaphore.h>

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
#include "lcrsocket.h"
#include "cause.h"
#include "bchannel.h"
#include "chan_lcr.h"
#include "callerid.h"

CHAN_LCR_STATE // state description structure

u_char flip_bits[256];

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
	struct admin_msg msg;
} *admin_first = NULL;

static struct ast_channel_tech lcr_tech;

/*
 * logging
 */
void chan_lcr_log(int type, const char *file, int line, struct chan_call *call, struct ast_channel *ast, const char *fmt, ...)
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
		sprintf(call_text, "%ld", call->ref);
	if (ast)
		strncpy(ast_text, ast->name, sizeof(ast_text)-1);
	ast_text[sizeof(ast_text)-1] = '\0';
	
	ast_log(type, file, line, "[call=%s ast=%s] %s", call_text, ast_text, buffer);

	ast_mutex_unlock(&log_lock);
}

/*
 * channel and call instances
 */
struct chan_call *call_first;

struct chan_call *find_call_ref(unsigned long ref)
{
	struct chan_call *call = call_first;

	while(call)
	{
		if (call->ref == ref)
			break;
		call = call->next;
	}
	return(call);
}

#if 0
struct chan_call *find_call_ast(struct ast_channel *ast)
{
	struct chan_call *call = call_first;

	while(call)
	{
		if (call->ast == ast)
			break;
		call = call->next;
	}
	return(call);
}

struct chan_call *find_call_handle(unsigned long handle)
{
	struct chan_call *call = call_first;

	while(call)
	{
		if (call->bchannel_handle == handle)
			break;
		call = call->next;
	}
	return(call);
}
#endif

void free_call(struct chan_call *call)
{
	struct chan_call **temp = &call_first;

	while(*temp)
	{
		if (*temp == call)
		{
			*temp = (*temp)->next;
			if (call->pipe[0])
				close(call->pipe[0]);
			if (call->pipe[1])
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

	*callp = (struct chan_call *)malloc(sizeof(struct chan_call));
	if (*callp)
		memset(*callp, 0, sizeof(struct chan_call));
	if (pipe((*callp)->pipe) < 0) {
		CERROR(*callp, NULL, "Failed to create pipe.\n");
		free_call(*callp);
		return(NULL);
	}
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
int send_message(int message_type, unsigned long ref, union parameter *param)
{
	struct admin_list *admin, **adminp;

	if (lcr_sock < 0) {
		CDEBUG(NULL, NULL, "Ignoring message %d, because socket is closed.\n", message_type);
		return -1;
	}
	CDEBUG(NULL, NULL, "Sending message %d to socket.\n", message_type);

	adminp = &admin_first;
	while(*adminp)
		adminp = &((*adminp)->next);
	admin = (struct admin_list *)malloc(sizeof(struct admin_list));
	*adminp = admin;

	admin->msg.type = message_type;
	admin->msg.ref = ref;
	memcpy(&admin->msg.param, param, sizeof(union parameter));

	return(0);
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

	CDEBUG(call, call->ast, "Sending setup to LCR.\n");

	/* send setup message to LCR */
	memset(&newparam, 0, sizeof(union parameter));
       	newparam.setup.callerinfo.itype = INFO_ITYPE_CHAN;	
       	newparam.setup.callerinfo.ntype = INFO_NTYPE_UNKNOWN;	
	if (ast->cid.cid_num) if (ast->cid.cid_num[0])
		strncpy(newparam.setup.callerinfo.id, ast->cid.cid_num, sizeof(newparam.setup.callerinfo.id)-1);
	if (ast->cid.cid_name) if (ast->cid.cid_name[0])
		strncpy(newparam.setup.callerinfo.name, ast->cid.cid_name, sizeof(newparam.setup.callerinfo.name)-1);
	if (ast->cid.cid_rdnis) if (ast->cid.cid_rdnis[0])
	{
		strncpy(newparam.setup.redirinfo.id, ast->cid.cid_rdnis, sizeof(newparam.setup.redirinfo.id)-1);
       		newparam.setup.redirinfo.itype = INFO_ITYPE_CHAN;	
 	      	newparam.setup.redirinfo.ntype = INFO_NTYPE_UNKNOWN;	
	}
	switch(ast->cid.cid_pres & AST_PRES_RESTRICTION)
	{
		case AST_PRES_ALLOWED:
		newparam.setup.callerinfo.present = INFO_PRESENT_ALLOWED;
		break;
		case AST_PRES_RESTRICTED:
		newparam.setup.callerinfo.present = INFO_PRESENT_RESTRICTED;
		break;
		case AST_PRES_UNAVAILABLE:
		newparam.setup.callerinfo.present = INFO_PRESENT_NOTAVAIL;
		break;
		default:
		newparam.setup.callerinfo.present = INFO_PRESENT_NULL;
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
#ifdef TODO
	newparam.setup.capainfo.bearer_info1 = alaw 3, ulaw 2;
#endif
	newparam.setup.capainfo.bearer_info1 = 3;
	newparam.setup.capainfo.bearer_mode = INFO_BMODE_CIRCUIT;
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

	if (!call->ast || !call->ref || !call->dialque)
		return;
	
	CDEBUG(call, call->ast, "Sending dial queue to LCR.\n");

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
	CDEBUG(call, NULL, "Sending message due briding.\n");
	send_message(message_type, call->bridge_call->ref, param);
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

	if (complete)
	{
		/* if not match */
		if (!ast_canmatch_extension(ast, ast->context, ast->exten, 1, call->oad))
		{
			CDEBUG(call, ast, "Got 'sending complete', but extension '%s' will not match at context '%s' - releasing.\n", ast->exten, ast->context);
			cause = 1;
			goto release;
		if (!ast_exists_extension(ast, ast->context, ast->exten, 1, call->oad))
		{
			CDEBUG(call, ast, "Got 'sending complete', but extension '%s' would match at context '%s', if more digits would be dialed - releasing.\n", ast->exten, ast->context);
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

	if (ast_canmatch_extension(ast, ast->context, ast->exten, 1, call->oad))
	{
		/* send setup acknowledge to lcr */
		memset(&newparam, 0, sizeof(union parameter));
		send_message(MESSAGE_OVERLAP, call->ref, &newparam);

		/* change state */
		call->state = CHAN_LCR_STATE_IN_DIALING;

		/* if match, start pbx */
		if (ast_exists_extension(ast, ast->context, ast->exten, 1, call->oad)) {
			CDEBUG(call, ast, "Extensions matches.\n");
			goto start;
		}

		/* if can match */
		CDEBUG(call, ast, "Extensions may match, if more digits are dialed.\n");
		return;
	}

	/* if not match */
	cause = 1;
	release:
	/* release lcr */
	CDEBUG(call, ast, "Releasing due to extension missmatch.\n");
	memset(&newparam, 0, sizeof(union parameter));
	newparam.disconnectinfo.cause = cause;
	newparam.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
	send_message(MESSAGE_RELEASE, call->ref, &newparam);
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
	return;
}

/*
 * incoming setup from LCR
 */
static void lcr_in_setup(struct chan_call *call, int message_type, union parameter *param)
{
	struct ast_channel *ast;
	union parameter newparam;

	CDEBUG(call, NULL, "Incomming setup from LCR. (callerid %d, dialing %d)\n", param->setup.callerinfo.id, param->setup.dialinginfo.id);

	/* create asterisk channel instrance */
	ast = ast_channel_alloc(1, AST_STATE_RESERVED, NULL, NULL, "", NULL, "", 0, "%s/%d", lcr_type, ++glob_channel);
	if (!ast)
	{
		/* release */
		CERROR(call, NULL, "Failed to create Asterisk channel - releasing.\n");
		memset(&newparam, 0, sizeof(union parameter));
		newparam.disconnectinfo.cause = CAUSE_RESSOURCEUNAVAIL;
		newparam.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		send_message(MESSAGE_RELEASE, call->ref, &newparam);
		/* remove call */
		free_call(call);
		return;
	}
	/* set ast pointer */
	call->ast = ast;
	ast->tech_pvt = call;
	ast->tech = &lcr_tech;
	ast->fds[0] = call->pipe[0];
	
	/* fill setup information */
	if (param->setup.dialinginfo.id)
		strncpy(ast->exten, param->setup.dialinginfo.id, AST_MAX_EXTENSION-1);
	if (param->setup.context[0])
		strncpy(ast->context, param->setup.context, AST_MAX_CONTEXT-1);
	if (param->setup.callerinfo.id[0])
		ast->cid.cid_num = strdup(param->setup.callerinfo.id);
	if (param->setup.callerinfo.name[0])
		ast->cid.cid_name = strdup(param->setup.callerinfo.name);
#ifdef TODO
	if (param->setup.redirinfo.id[0])
		ast->cid.cid_name = strdup(numberrize_callerinfo(param->setup.callerinfo.id, param->setup.callerinfo.ntype, configfile->prefix_nat, configfile->prefix_inter));
#endif
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
#ifdef TODO
	strncpy(call->oad, numberrize_callerinfo(param->setup.callerinfo.id, param->setup.callerinfo.ntype, configfile->prefix_nat, configfile->prefix_inter), sizeof(call->oad)-1);
#else
	strncpy(call->oad, numberrize_callerinfo(param->setup.callerinfo.id, param->setup.callerinfo.ntype, "0", "00"), sizeof(call->oad)-1);
#endif

	/* configure channel */
#ifdef TODO
	ast->nativeformats = configfile->lawformat;
	ast->readformat = ast->rawreadformat = configfile->lawformat;
	ast->writeformat = ast->rawwriteformat = configfile->lawformat;
#else
	ast->nativeformats = AST_FORMAT_ALAW;
	ast->readformat = ast->rawreadformat = AST_FORMAT_ALAW;
	ast->writeformat = ast->rawwriteformat = AST_FORMAT_ALAW;
#endif
	ast->hangupcause = 0;

	/* change state */
	call->state = CHAN_LCR_STATE_IN_SETUP;

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
	if (call->dialque)
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
	/* send event to asterisk */
	if (call->ast && call->pbx_started)
		ast_queue_control(call->ast, AST_CONTROL_PROCEEDING);
}

/*
 * incoming alerting from LCR
 */
static void lcr_in_alerting(struct chan_call *call, int message_type, union parameter *param)
{
	CDEBUG(call, call->ast, "Incomming alerting from LCR.\n");

	/* change state */
	call->state = CHAN_LCR_STATE_OUT_ALERTING;
	/* send event to asterisk */
	if (call->ast && call->pbx_started)
		ast_queue_control(call->ast, AST_CONTROL_RINGING);
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
	/* send event to asterisk */
	if (call->ast && call->pbx_started)
		ast_queue_control(call->ast, AST_CONTROL_ANSWER);
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
	send_message(MESSAGE_RELEASE, call->ref, param);
	call->ref = 0;
	/* change to release state */
	call->state = CHAN_LCR_STATE_RELEASE;
	/* release asterisk */
	if (ast)
	{
		ast->hangupcause = call->cause;
		if (call->pbx_started)
			ast_queue_hangup(ast);
		else {
			ast_hangup(ast); // call will be destroyed here
		}
	}
}

/*
 * incoming setup acknowledge from LCR
 */
static void lcr_in_release(struct chan_call *call, int message_type, union parameter *param)
{
	struct ast_channel *ast = call->ast;

	CDEBUG(call, call->ast, "Incomming release from LCR. (cause=%d)\n", param->disconnectinfo.cause);

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
	/* if we have an asterisk instance, send hangup, else we are done */
	if (ast)
	{
		ast->hangupcause = call->cause;
		if (call->pbx_started)
			ast_queue_hangup(ast);
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
	struct ast_frame fr;
	char *p;

	CDEBUG(call, call->ast, "Incomming information from LCR. (dialing=%d)\n", param->information.id);
	
	if (!call->ast) return;

	/* pbx not started */
	if (!call->pbx_started)
	{
		CDEBUG(call, call->ast, "Asterisk not started, adding digits to number.\n");
		strncat(ast->exten, param->information.id, AST_MAX_EXTENSION-1);
		lcr_start_pbx(call, ast, param->information.sending_complete);
		return;
	}
	
	/* copy digits */
	p = param->information.id;
	if (call->state == CHAN_LCR_STATE_IN_DIALING && *p)
	{
		CDEBUG(call, call->ast, "Asterisk is started, sending DTMF frame.\n");
		while (*p)
		{
			/* send digit to asterisk */
			memset(&fr, 0, sizeof(fr));
			fr.frametype = AST_FRAME_DTMF;
			fr.subclass = *p;
			fr.delivery = ast_tv(0, 0);
			ast_queue_frame(call->ast, &fr);
			p++;
		}
	}
	/* use bridge to forware message not supported by asterisk */
	if (call->state == CHAN_LCR_STATE_CONNECT) {
		CDEBUG(call, call->ast, "Call is connected, briding.\n");
		bridge_message_if_bridged(call, message_type, param);
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
 * message received from LCR
 */
int receive_message(int message_type, unsigned long ref, union parameter *param)
{
	union parameter newparam;
	struct bchannel *bchannel;
	struct chan_call *call;

	memset(&newparam, 0, sizeof(union parameter));

	/* handle bchannel message*/
	if (message_type == MESSAGE_BCHANNEL)
	{
		switch(param->bchannel.type)
		{
			case BCHANNEL_ASSIGN:
			CDEBUG(NULL, NULL, "Received BCHANNEL_ASSIGN message. (handle=%08lx)\n", param->bchannel.handle);
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
			if (param->bchannel.crypt_len)
			{
				bchannel->b_crypt_len = param->bchannel.crypt_len;
				bchannel->b_crypt_type = param->bchannel.crypt_type;
				memcpy(bchannel->b_crypt_key, param->bchannel.crypt, param->bchannel.crypt_len);
			}
			bchannel->b_txdata = 0;
			bchannel->b_dtmf = 1;
			bchannel->b_tx_dejitter = 1;

			/* in case, ref is not set, this bchannel instance must
			 * be created until it is removed again by LCR */
			/* link to call */
			if ((call = find_call_ref(ref)))
			{
				bchannel->call = call;
				call->bchannel = bchannel;
#ifdef TODO
hier muesen alle bchannel-features gesetzt werden (pipeline...) falls sie vor dem b-kanal verfügbar waren
#endif
				if (call->bridge_id) {
					CDEBUG(call, call->ast, "Join bchannel, because call is already bridged.\n");
					bchannel_join(bchannel, call->bridge_id);
				}
			}
			if (bchannel_create(bchannel))
				bchannel_activate(bchannel, 1);

			/* acknowledge */
			newparam.bchannel.type = BCHANNEL_ASSIGN_ACK;
			newparam.bchannel.handle = param->bchannel.handle;
			send_message(MESSAGE_BCHANNEL, 0, &newparam);
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
			CDEBUG(NULL, NULL, "Received new ref by LCR, of call from LCR. (ref=%ld)\n", ref);
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
				newparam.disconnectinfo.cause = CAUSE_NORMAL;
				newparam.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
				send_message(MESSAGE_RELEASE, ref, &newparam);
				return(0);
			}
			/* store new ref */
			call->ref = ref;
			/* send pending setup info */
			if (call->state == CHAN_LCR_STATE_OUT_PREPARE)
				send_setup_to_lcr(call);
			/* release if asterisk has signed off */
			else if (call->state == CHAN_LCR_STATE_RELEASE)
			{
				/* send release */
				if (call->cause)
				{
					newparam.disconnectinfo.cause = call->cause;
					newparam.disconnectinfo.location = call->location;
				} else
				{
					newparam.disconnectinfo.cause = 16;
					newparam.disconnectinfo.location = 5;
				}
				send_message(MESSAGE_RELEASE, ref, &newparam);
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
	while(call)
	{
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
		if (!call->pbx_started)
		{
			CDEBUG(call, call->ast, "Releasing call, because no Asterisk channel is not started.\n");
			ast_hangup(call->ast); // call will be destroyed here
			goto again;
		}
		CDEBUG(call, call->ast, "Queue call release, because Asterisk channel is running.\n");
		ast_queue_hangup(call->ast);
		call = call->next;
	}
}


/* asterisk handler
 * warning! not thread safe
 * returns -1 for socket error, 0 for no work, 1 for work
 */
int handle_socket(void)
{
	int work = 0;
	int len;
	struct admin_message msg;
	struct admin_list *admin;

	int sock;

	/* read from socket */
	len = read(sock, &msg, sizeof(msg));
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
			return(-1); // socket error
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
	len = write(sock, &admin->msg, sizeof(msg));
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
	int sock;
	char *socket_name = SOCKET_NAME;
	int conn;
	struct sockaddr_un sock_address;
	unsigned long on = 1;
	union parameter param;

	/* open socket */
	if ((sock = socket(PF_UNIX, SOCK_STREAM, 0)) < 0)
	{
		CERROR(NULL, NULL, "Failed to create socket.\n");
		return(sock);
	}

	/* set socket address and name */
	memset(&sock_address, 0, sizeof(sock_address));
	sock_address.sun_family = PF_UNIX;
	strcpy(sock_address.sun_path, socket_name);

	/* connect socket */
	if ((conn = connect(sock, (struct sockaddr *)&sock_address, SUN_LEN(&sock_address))) < 0)
	{
		close(sock);
		CERROR(NULL, NULL, "Failed to connect to socket '%s'. Is LCR running?\n", sock_address.sun_path);
		return(conn);
	}

	/* set non-blocking io */
	if ((ret = ioctl(sock, FIONBIO, (unsigned char *)(&on))) < 0)
	{
		close(sock);
		CERROR(NULL, NULL, "Failed to set socket into non-blocking IO.\n");
		return(ret);
	}

	/* enque hello message */
	memset(&param, 0, sizeof(param));
	strcpy(param.hello.application, "asterisk");
	send_message(MESSAGE_HELLO, 0, &param);

	return(sock);
}

void close_socket(int sock)
{
	/* close socket */
	if (socket >= 0)	
		close(sock);
}

static void *chan_thread(void *arg)
{
	int work;
	int ret;
	union parameter param;
	time_t retry = 0, now;

	bchannel_pid = getpid();
	
	memset(&param, 0, sizeof(union parameter));
	if (lcr_sock > 0)
		time(&retry);

	ast_mutex_lock(&chan_lock);

	while(!quit) {
		work = 0;

		if (lcr_sock > 0) {
			/* handle socket */
			ret = handle_socket();
			if (ret < 0) {
				CERROR(NULL, NULL, "Handling of socket failed - closing for some seconds.\n");
				close_socket(lcr_sock);
				lcr_sock = -1;
				release_all_calls();
				time(&retry);
			}
			if (ret)
				work = 1;
		} else {
			time(&now);
			if (retry && now-retry > 5) {
				CERROR(NULL, NULL, "Retry to open socket.\n");
				retry = 0;
				if (!(lcr_sock = open_socket())) {
					time(&retry);
				}
				work = 1;
			}
					
		}

		/* handle mISDN */
		ret = bchannel_handle();
		if (ret)
			work = 1;
		
		if (!work) {
			ast_mutex_unlock(&chan_lock);
			usleep(30000);
			ast_mutex_lock(&chan_lock);
		}
	}

	CERROR(NULL, NULL, "Thread exit.\n");
	
	ast_mutex_unlock(&chan_lock);

	return NULL;
}

/*
 * new asterisk instance
 */
static
struct ast_channel *lcr_request(const char *type, int format, void *data, int *cause)
{
	union parameter newparam;
	struct ast_channel *ast;
        struct chan_call *call;

	ast_mutex_lock(&chan_lock);

	CDEBUG(NULL, NULL, "Received request from Asterisk.\n");

	/* if socket is closed */
	if (lcr_sock < 0)
	{
		CERROR(NULL, NULL, "Rejecting call from Asterisk, because LCR not running.\n");
		return NULL;
	}
	
	/* create call instance */
	call = alloc_call();
	if (!call)
	{
		/* failed to create instance */
		return NULL;
	}
	/* create asterisk channel instrance */
	ast = ast_channel_alloc(1, AST_STATE_RESERVED, NULL, NULL, "", NULL, "", 0, "%s/%d", lcr_type, ++glob_channel);
	if (!ast)
	{
		CERROR(NULL, NULL, "Failed to create Asterisk channel.\n");
		free_call(call);
		/* failed to create instance */
		return NULL;
	}
	/* link together */
	ast->tech_pvt = call;
	call->ast = ast;
	ast->tech = &lcr_tech;
	/* configure channel */
#ifdef TODO
	snprintf(ast->name, sizeof(ast->name), "%s/%d", lcr_type, ++glob_channel);
	ast->name[sizeof(ast->name)-1] = '\0';
#endif
#ifdef TODO
	ast->nativeformats = configfile->lawformat;
	ast->readformat = ast->rawreadformat = configfile->lawformat;
	ast->writeformat = ast->rawwriteformat = configfile->lawformat;
#else
	ast->nativeformats = AST_FORMAT_ALAW;
	ast->readformat = ast->rawreadformat = AST_FORMAT_ALAW;
	ast->writeformat = ast->rawwriteformat = AST_FORMAT_ALAW;
#endif
	ast->hangupcause = 0;
	/* send MESSAGE_NEWREF */
	memset(&newparam, 0, sizeof(union parameter));
	newparam.direction = 0; /* request from app */
	send_message(MESSAGE_NEWREF, 0, &newparam);
	/* set state */
	call->state = CHAN_LCR_STATE_OUT_PREPARE;

	ast_mutex_unlock(&chan_lock);

	return ast;
}

/*
 * call from asterisk
 */
static int lcr_call(struct ast_channel *ast, char *dest, int timeout)
{
        struct chan_call *call;

	ast_mutex_lock(&chan_lock);
        call = ast->tech_pvt;
        if (!call) {
		CERROR(NULL, ast, "Received call from Asterisk, but no call instance exists.\n");
		ast_mutex_unlock(&chan_lock);
		return -1;
	}

	CDEBUG(call, ast, "Received call from Asterisk.\n");

	call->pbx_started = 1;

	/* send setup message, if we already have a callref */
	if (call->ref)
		send_setup_to_lcr(call);

	ast_mutex_unlock(&chan_lock);
	return 0; 
}

static int lcr_digit(struct ast_channel *ast, char digit)
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

	CDEBUG(call, ast, "Received digit Asterisk.\n");

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
	 && (call->state == CHAN_LCR_STATE_OUT_PREPARE || call->state == CHAN_LCR_STATE_OUT_SETUP));
	{
		CDEBUG(call, ast, "Queue digits, because we are in setup/dialing state and have no ref yet.\n");
		*buf = digit;
		strncat(call->dialque, buf, strlen(call->dialque)-1);
	}

	ast_mutex_unlock(&chan_lock);
	
	return(0);
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
	
	CDEBUG(call, ast, "Received answer from Asterisk.\n");
		
	/* copy connectinfo, if bridged */
	if (call->bridge_call)
		memcpy(&call->connectinfo, &call->bridge_call->connectinfo, sizeof(struct connect_info));
	/* send connect message to lcr */
	memset(&newparam, 0, sizeof(union parameter));
	memcpy(&newparam.connectinfo, &call->connectinfo, sizeof(struct connect_info));
	send_message(MESSAGE_CONNECT, call->ref, &newparam);
	/* change state */
	call->state = CHAN_LCR_STATE_CONNECT;
	
   	ast_mutex_unlock(&chan_lock);
        return 0;
}

static int lcr_hangup(struct ast_channel *ast)
{
	union parameter newparam;
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
		memset(&newparam, 0, sizeof(union parameter));
		newparam.disconnectinfo.cause = CAUSE_RESSOURCEUNAVAIL;
		newparam.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		send_message(MESSAGE_RELEASE, call->ref, &newparam);
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
		}
	} 
	if (!pthread_equal(tid, chan_tid))
		ast_mutex_unlock(&chan_lock);
	return 0;
}

static int lcr_write(struct ast_channel *ast, struct ast_frame *f)
{
        struct chan_call *call;
	unsigned char buffer[1024], *s, *d = buffer;
	int i, ii;

	ast_mutex_lock(&chan_lock);
        call = ast->tech_pvt;
        if (!call) {
		ast_mutex_unlock(&chan_lock);
		return -1;
	}
	if (call->bchannel && ((ii = f->samples)))
	{
		if (ii > sizeof(buffer))
			ii = sizeof(buffer);
		s = f->data;
		for (i = 0; i < ii; i++)
			*d++ = flip_bits[*s++];
		bchannel_transmit(call->bchannel, buffer, ii);
	}
	ast_mutex_unlock(&chan_lock);
	return 0;
}


static struct ast_frame *lcr_read(struct ast_channel *ast)
{
        struct chan_call *call;
	int i, len;
	unsigned char *p;

	ast_mutex_lock(&chan_lock);
        call = ast->tech_pvt;
        if (!call) {
		ast_mutex_unlock(&chan_lock);
		return NULL;
	}
	len = read(call->pipe[0], call->read_buff, sizeof(call->read_buff));
	if (len <= 0)
		return NULL;

	p = call->read_buff;
	for (i = 0; i < len; i++) {
		*p = flip_bits[*p];
		p++;
	}

	call->read_fr.frametype = AST_FRAME_VOICE;
#ifdef TODO
	format aus config
#endif
	call->read_fr.subclass = AST_FORMAT_ALAW;
	call->read_fr.datalen = len;
	call->read_fr.samples = len;
	call->read_fr.delivery = ast_tv(0,0);
	call->read_fr.data = call->read_buff;
	ast_mutex_unlock(&chan_lock);

	return &call->read_fr;
}

static int lcr_indicate(struct ast_channel *ast, int cond, const void *data, size_t datalen)
{
	union parameter newparam;
        int res = -1;
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
			/* send message to lcr */
			memset(&newparam, 0, sizeof(union parameter));
			newparam.disconnectinfo.cause = 17;
			newparam.disconnectinfo.location = 5;
			send_message(MESSAGE_DISCONNECT, call->ref, &newparam);
			/* change state */
			call->state = CHAN_LCR_STATE_OUT_DISCONNECT;
			/* return */
			ast_mutex_unlock(&chan_lock);
                        return 0;
                case AST_CONTROL_CONGESTION:
			CDEBUG(call, ast, "Received indicate AST_CONTROL_CONGESTION from Asterisk.\n");
			/* return */
			ast_mutex_unlock(&chan_lock);
                        return -1;
                case AST_CONTROL_RINGING:
			CDEBUG(call, ast, "Received indicate AST_CONTROL_RINGING from Asterisk.\n");
			/* send message to lcr */
			memset(&newparam, 0, sizeof(union parameter));
			send_message(MESSAGE_ALERTING, call->ref, &newparam);
			/* change state */
			call->state = CHAN_LCR_STATE_OUT_ALERTING;
			/* return */
			ast_mutex_unlock(&chan_lock);
                        return 0;
                case -1:
			/* return */
			ast_mutex_unlock(&chan_lock);
                        return 0;

                case AST_CONTROL_VIDUPDATE:
                        res = -1;
                        break;
                case AST_CONTROL_HOLD:
			CDEBUG(call, ast, "Received indicate AST_CONTROL_HOLD from Asterisk.\n");
			/* send message to lcr */
			memset(&newparam, 0, sizeof(union parameter));
			newparam.notifyinfo.notify = INFO_NOTIFY_REMOTE_HOLD;
			send_message(MESSAGE_NOTIFY, call->ref, &newparam);
                        break;
                case AST_CONTROL_UNHOLD:
			CDEBUG(call, ast, "Received indicate AST_CONTROL_UNHOLD from Asterisk.\n");
			/* send message to lcr */
			memset(&newparam, 0, sizeof(union parameter));
			newparam.notifyinfo.notify = INFO_NOTIFY_REMOTE_RETRIEVAL;
			send_message(MESSAGE_NOTIFY, call->ref, &newparam);
                        break;

                default:
			CERROR(call, ast, "Received indicate from Asterisk with unknown condition %d.\n", cond);
			/* return */
			ast_mutex_unlock(&chan_lock);
                        return -1;
        }

	/* return */
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
	int			to = -1;
	struct ast_frame	*f;
	int			bridge_id;

	CDEBUG(NULL, ast, "Received briding request from Asterisk.\n");
	
	/* join via dsp (if the channels are currently open) */
	ast_mutex_lock(&chan_lock);
	bridge_id = new_bridge_id();
	call1 = ast1->tech_pvt;
	call2 = ast2->tech_pvt;
	if (call1 && call2)
	{
		call1->bridge_id = bridge_id;
		if (call1->bchannel)
			bchannel_join(call1->bchannel, bridge_id);
		call1->bridge_call = call2;
	}
	if (call2)
	{
		call2->bridge_id = bridge_id;
		if (call2->bchannel)
			bchannel_join(call2->bchannel, bridge_id);
		call2->bridge_call = call1;
	}
	ast_mutex_unlock(&chan_lock);
	
	while(1) {
		who = ast_waitfor_n(carr, 2, &to);

		if (!who) {
			CDEBUG(NULL, ast, "Empty read on bridge, breaking out.\n");
			break;
		}
		f = ast_read(who);
    
		if (!f || f->frametype == AST_FRAME_CONTROL) {
			/* got hangup .. */
			*fo=f;
			*rc=who;
			break;
		}
		
		if ( f->frametype == AST_FRAME_DTMF ) {
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
	
	CDEBUG(NULL, ast, "Releasing bride.\n");

	/* split channels */
	ast_mutex_lock(&chan_lock);
	call1 = ast1->tech_pvt;
	call2 = ast2->tech_pvt;
	if (call1)
	{
		call1->bridge_id = 0;
		if (call1->bchannel)
			bchannel_join(call1->bchannel, 0);
		if (call1->bridge_call)
			call1->bridge_call->bridge_call = NULL;
		call1->bridge_call = NULL;
	}
	if (call2)
	{
		call2->bridge_id = 0;
		if (call2->bchannel)
			bchannel_join(call2->bchannel, 0);
		if (call2->bridge_call)
			call2->bridge_call->bridge_call = NULL;
		call2->bridge_call = NULL;
	}
	ast_mutex_unlock(&chan_lock);
	
	
	return AST_BRIDGE_COMPLETE;
}
static struct ast_channel_tech lcr_tech = {
	.type="LCR",
	.description="Channel driver for connecting to Linux-Call-Router",
#ifdef TODO
	law from config
#else
	.capabilities=AST_FORMAT_ALAW,
#endif
	.requester=lcr_request,
	.send_digit_begin=lcr_digit,
	.call=lcr_call,
	.bridge=lcr_bridge, 
	.hangup=lcr_hangup,
	.answer=lcr_answer,
	.read=lcr_read,
	.write=lcr_write,
	.indicate=lcr_indicate,
//	.fixup=lcr_fixup,
//	.send_text=lcr_send_text,
	.properties=0
};


/*
 * cli
 */
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


/*
 * module loading and destruction
 */
int load_module(void)
{
	int i;

	for (i = 0, i < 256, i++)
		flip_bits[i] = (i>>7) | ((i>>5)&2) | ((i>>3)&4) | ((i>>1)&8)
			  || = (i<<7) | ((i&2)<<5) | ((i&4)<<3) | ((i&8)<<1);

	ast_mutex_init(&chan_lock);
	ast_mutex_init(&log_lock);
	
	if (!(lcr_sock = open_socket())) {
		/* continue with closed socket */
	}

	if (!bchannel_initialize()) {
		CERROR(NULL, NULL, "Unable to open mISDN device\n");
		close_socket(lcr_sock);
		return -1;
	}
	mISDN_created = 1;

	if (ast_channel_register(&lcr_tech)) {
		CERROR(NULL, NULL, "Unable to register channel class\n");
		bchannel_deinitialize();
		close_socket(lcr_sock);
		return -1;
	}
 
#if 0	
	ast_cli_register(&cli_show_lcr);
	ast_cli_register(&cli_show_calls);

	ast_cli_register(&cli_reload_routing);
	ast_cli_register(&cli_reload_interfaces);
	ast_cli_register(&cli_port_block);
	ast_cli_register(&cli_port_unblock);
	ast_cli_register(&cli_port_unload);
  
	ast_register_application("misdn_set_opt", misdn_set_opt_exec, "misdn_set_opt",
				 "misdn_set_opt(:<opt><optarg>:<opt><optarg>..):\n"
				 "Sets mISDN opts. and optargs\n"
				 "\n"
				 "The available options are:\n"
				 "    d - Send display text on called phone, text is the optparam\n"
				 "    n - don't detect dtmf tones on called channel\n"
				 "    h - make digital outgoing call\n" 
				 "    c - make crypted outgoing call, param is keyindex\n"
				 "    e - perform echo cancelation on this channel,\n"
				 "        takes taps as arguments (32,64,128,256)\n"
				 "    s - send Non Inband DTMF as inband\n"
				 "   vr - rxgain control\n"
				 "   vt - txgain control\n"
		);

	
	lcr_cfg_get( 0, LCR_GEN_TRACEFILE, global_tracefile, BUFFERSIZE);

=======
	//lcr_cfg_get( 0, LCR_GEN_TRACEFILE, global_tracefile, BUFFERSIZE);
#endif

	quit = 0;	
	if ((pthread_create(&chan_tid, NULL, chan_thread, NULL)<0))
	{
		/* failed to create thread */
		bchannel_deinitialize();
		close_socket(lcr_sock);
		ast_channel_unregister(&lcr_tech);
		return -1;
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

static int reload_module(void)
{
//	reload_config();
	return 0;
}

#ifdef TODO
mutex init fehlt noch
ast_mutex_t usecnt_lock;
int usecnt;

int usecount(void)
{
	int res;
	ast_mutex_lock(&usecnt_lock);
	res = usecnt;
	ast_mutex_unlock(&usecnt_lock);
	return res;
}
#endif


char *desc="Channel driver for lcr";

char *description(void)
{
	return desc;
}

char *key(void)
{
	return ASTERISK_GPL_KEY;
}

#define AST_MODULE "chan_lcr"
AST_MODULE_INFO(ASTERISK_GPL_KEY,
                                AST_MODFLAG_DEFAULT,
                                "Channel driver for LCR",
                                .load = load_module,
                                .unload = unload_module,
                                .reload = reload_module,
                           );

