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

How does it work:

To connect, open a socket and send a MESSAGE_HELLO to admin socket with
the application name. This name is unique an can be used for routing calls.

To make a call, send a MESSAGE_NEWREF and a new reference is received.
When receiving a call, a new reference is received.
The reference is received with MESSAGE_NEWREF.

Make a MESSAGE_SETUP or receive a MESSAGE_SETUP with the reference.

To release call and reference, send or receive MESSAGE_RELEASE.
From that point on, the ref is not valid, so no other message may be sent
with that reference.

*/
bchannel-handling muss noch

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
#include "extension.h"
#include "message.h"
#include "lcrsocket.h"
#include "cause.h"
#include "bchannel.h"
#include "chan_lcr.h"

int lcr_sock = -1;

struct admin_list {
	struct admin_list *next;
	struct admin_msg msg;
} *admin_first = NULL;

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

struct chan_call *alloc_call(void)
{
	struct chan_call **callp = &call_first;

	while(*callp)
		callp = &((*callp)->next);

	*callp = (struct chan_call *)malloc(sizeof(struct chan_call));
	return(*callp);
}

void free_call(struct chan_call *call)
{
	struct chan_call **temp = &call_first;

	while(*temp)
	{
		if (*temp == call)
		{
			*temp = (*temp)->next;
			free(call);
			return;
		}
		temp = &((*temp)->next);
	}
}

unsigned short new_brige_id(void)
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
	return(id);
}


/*
 * receive bchannel data
 */
void rx_data(struct bchannel *bchannel, unsigned char *data, int len)
{
}

void rx_dtmf(struct bchannel *bchannel, char tone)
{
}

/*
 * enque message to LCR
 */
int send_message(int message_type, unsigned long ref, union parameter *param)
{
	struct admin_list *admin, **adminp;

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
			if ((bchannel = find_bchannel_handle(param->bchannel.handle)))
			{
				fprintf(stderr, "error: bchannel handle %x already assigned.\n", param->bchannel.handle);
				return(-1);
			}
			/* create bchannel */
			bchannel = alloc_bchannel(param->bchannel.handle);
			if (!bchannel)
			{
				fprintf(stderr, "error: alloc bchannel handle %x failed.\n", param->bchannel.handle);
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
				bchannel->ref = ref;
				call->bchannel_handle = param->bchannel.handle;
#warning hier muesen alle stati gesetzt werden falls sie vor dem b-kanal verfügbar waren
				bchannel_join(call->bridge_id);
			}
			if (bchannel_create(bchannel))
				bchannel_activate(bchannel, 1);

			/* acknowledge */
			newparam.bchannel.type = BCHANNEL_ASSIGN_ACK;
			newparam.bchannel.handle = param->bchannel.handle;
			send_message(MESSAGE_BCHANNEL, 0, &newparam);
			break;

			case BCHANNEL_REMOVE:
			if (!(bchannel = find_bchannel_handle(param->bchannel.handle)))
			{
				alle fprintf nach ast_log
				fprintf(stderr, "error: bchannel handle %x not assigned.\n", param->bchannel.handle);
				return(-1);
			}
			/* unlink from call */
			if ((call = find_call_ref(bchannel->ref)))
			{
				call->bchannel_handle = 0;
			}
			/* destroy and remove bchannel */
			free_bchannel(bchannel);

			/* acknowledge */
			newparam.bchannel.type = BCHANNEL_REMOVE_ACK;
			newparam.bchannel.handle = param->bchannel.handle;
			send_message(MESSAGE_BCHANNEL, 0, &newparam);
			
			break;

			default:
			fprintf(stderr, "received unknown bchannel message %d\n", param->bchannel.type);
		}
		return(0);
	}

	/* handle new ref */
	if (message_type == MESSAGE_NEWREF)
	{
		if (param->direction)
		{
			/* new ref from lcr */
			if (!ref || find_call_ref(ref))
			{
				fprintf(stderr, "illegal new ref %d received\n", ref);
				return(-1);
			}
			call = alloc_call();
			call->ref = ref;
		} else
		{
			/* new ref, as requested from this remote application */
			call = find_call_ref(0);
			if (!call)
			{
				/* send release, if ref does not exist */
				newparam.disconnectinfo.cause = CAUSE_NORMAL;
				newparam.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
				send_message(MESSAGE_RELEASE, ref, &newparam);
				return(0);
			}
			call->ref = ref;
#warning process call (send setup, if pending)
		}
		return(0);
	}

	/* check ref */
	if (!ref)
	{
		fprintf(stderr, "received message %d without ref\n", message_type);
		return(-1);
	}
	call = find_call_ref(ref);
	if (!call)
	{
		/* ignore ref that is not used (anymore) */
		return(0);
	}

	/* handle messages */
	switch(message_type)
	{
		case MESSAGE_SETUP:
todo
		break;

		case MESSAGE_OVERLAP:
todo
		break;

		case MESSAGE_PROCEEDING:
todo
		break;

		case MESSAGE_ALERTING:
todo
		break;

		case MESSAGE_CONNECT:
todo
		break;

		case MESSAGE_DISCONNECT:
todo
		break;

		case MESSAGE_RELEASE:
todo
		free_call(call);
		return(0);

		case MESSAGE_INFORMATION:
todo
		break;

		case MESSAGE_FACILITY:
todo
		break;

		case MESSAGE_PATTERN:
todo
		break;

		case MESSAGE_NOPATTERN:
todo
		break;

		case MESSAGE_AUDIOPATH:
todo
		break;

		default:
unhandled
	}
	return(0);
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

	/* read from socket */
	len = read(sock, &msg, sizeof(msg));
	if (len == 0)
	{
		printf("Socket closed\n");
		return(-1); // socket closed
	}
	if (len > 0)
	{
		if (len != sizeof(msg))
		{
			fprintf(stderr, "Socket short read (%d)\n", len);
			return(-1); // socket error
		}
		if (msg.message != ADMIN_MESSAGE)
		{
			fprintf(stderr, "Socket received illegal message %d\n", msg.message);
			return(-1); // socket error
		}
		receive_message(msg.u.msg.type, msg.u.msg.ref, &msg.u.msg.param);
		printf("message received %d\n", msg.u.msg.type);
		work = 1;
	} else
	{
		if (errno != EWOULDBLOCK)
		{
			fprintf(stderr, "Socket error %d\n", errno);
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
		printf("Socket closed\n");
		return(-1); // socket closed
	}
	if (len > 0)
	{
		if (len != sizeof(msg))
		{
			fprintf(stderr, "Socket short write (%d)\n", len);
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
			fprintf(stderr, "Socket error %d\n", errno);
			return(-1);
		}
	}

	return(work);
}

/*
 * open and close socket
 */
int open_socket(void)
{
	int ret;
	int sock;
	char *socket_name = SOCKET_NAME;
	int conn;
	struct sockaddr_un sock_address;
	int ret;
	unsigned long on = 1;
	union parameter param;

	/* open socket */
	if ((sock = socket(PF_UNIX, SOCK_STREAM, 0)) < 0)
	{
		ast_log(LOG_ERROR, "Failed to create socket.\n");
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
		ast_log(LOG_ERROR, "Failed to connect to socket \"%s\". Is LCR running?\n", sock_address.sun_path);
		return(conn);
	}

	/* set non-blocking io */
	if ((ret = ioctl(sock, FIONBIO, (unsigned char *)(&on))) < 0)
	{
		close(sock);
		ast_log(LOG_ERROR, "Failed to set socket into non-blocking IO.\n");
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


void lcr_thread(void)
{
	int work;

	while(42)
	{
		work = 0;

		/* handle socket */
		ret = handle_socket();
		if (ret < 0)
			break;
		if (ret)
			work = 1;

		/* handle mISDN */
		ret = bchannel_handle();
		if (ret)
			work = 1;
		
		if (!work)
			usleep(30000);
	}
}

/* call from asterisk (new instance) */
static int lcr_call(struct ast_channel *ast, char *dest, int timeout)
{
	int port=0;
	int r;
	struct chan_list *ch=MISDN_ASTERISK_TECH_PVT(ast);
	struct misdn_bchannel *newbc;
	char *opts=NULL, *ext;
	char dest_cp[256];
	
	{
		strncpy(dest_cp,dest,sizeof(dest_cp)-1);
		dest_cp[sizeof(dest_cp)]=0;

		ext=dest_cp;
		strsep(&ext,"/");
		if (ext) {
			opts=ext;
			strsep(&opts,"/");
		}  else {
			ast_log(LOG_WARNING, "Malformed dialstring\n");
			return -1;
		}
	}

	if (!ast) {
		ast_log(LOG_WARNING, " --> ! misdn_call called on ast_channel *ast where ast == NULL\n");
		return -1;
	}

	if (((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) || !dest  ) {
		ast_log(LOG_WARNING, " --> ! misdn_call called on %s, neither down nor reserved (or dest==NULL)\n", ast->name);
		ast->hangupcause=41;
		ast_setstate(ast, AST_STATE_DOWN);
		return -1;
	}

	if (!ch) {
		ast_log(LOG_WARNING, " --> ! misdn_call called on %s, neither down nor reserved (or dest==NULL)\n", ast->name);
		ast->hangupcause=41;
		ast_setstate(ast, AST_STATE_DOWN);
		return -1;
	}
	
	newbc=ch->bc;
	
	if (!newbc) {
		ast_log(LOG_WARNING, " --> ! misdn_call called on %s, neither down nor reserved (or dest==NULL)\n", ast->name);
		ast->hangupcause=41;
		ast_setstate(ast, AST_STATE_DOWN);
		return -1;
	}
	
	port=newbc->port;

	
	chan_misdn_log(1, port, "* CALL: %s\n",dest);
	
	chan_misdn_log(2, port, " --> * dad:%s tech:%s ctx:%s\n",ast->exten,ast->name, ast->context);
	
	chan_misdn_log(3, port, " --> * adding2newbc ext %s\n",ast->exten);
	if (ast->exten) {
		int l = sizeof(newbc->dad);
		strncpy(ast->exten,ext,sizeof(ast->exten));

		strncpy(newbc->dad,ext,l);

		newbc->dad[l-1] = 0;
	}
	newbc->rad[0]=0;
	chan_misdn_log(3, port, " --> * adding2newbc callerid %s\n",AST_CID_P(ast));
	if (ast_strlen_zero(newbc->oad) && AST_CID_P(ast) ) {

		if (AST_CID_P(ast)) {
			int l = sizeof(newbc->oad);
			strncpy(newbc->oad,AST_CID_P(ast), l);
			newbc->oad[l-1] = 0;
		}
	}

	{
		struct chan_list *ch=MISDN_ASTERISK_TECH_PVT(ast);
		if (!ch) { ast_verbose("No chan_list in misdn_call\n"); return -1;}
		
		newbc->capability=ast->transfercapability;
		pbx_builtin_setvar_helper(ast,"TRANSFERCAPABILITY",ast_transfercapability2str(newbc->capability));
		if ( ast->transfercapability == INFO_CAPABILITY_DIGITAL_UNRESTRICTED) {
			chan_misdn_log(2, port, " --> * Call with flag Digital\n");
		}
		

		/* update screening and presentation */ 
		update_config(ch,ORG_AST);
		
		/* fill in some ies from channel vary*/
		import_ch(ast, newbc, ch);
		
		/* Finally The Options Override Everything */
		if (opts)
			misdn_set_opt_exec(ast,opts);
		else
			chan_misdn_log(2,port,"NO OPTS GIVEN\n");

		/*check for bridging*/
		int bridging;
		misdn_cfg_get( 0, MISDN_GEN_BRIDGING, &bridging, sizeof(int));
		if (bridging && ch->other_ch) {
			chan_misdn_log(1, port, "Disabling EC (aka Pipeline) on both Sides\n");
			*ch->bc->pipeline=0;
			*ch->other_ch->bc->pipeline=0;
		}
		
		r=misdn_lib_send_event( newbc, EVENT_SETUP );
		
		/** we should have l3id after sending setup **/
		ch->l3id=newbc->l3_id;
	}
	
	if ( r == -ENOCHAN  ) {
		chan_misdn_log(0, port, " --> * Theres no Channel at the moment .. !\n");
		chan_misdn_log(1, port, " --> * SEND: State Down pid:%d\n",newbc?newbc->pid:-1);
		ast->hangupcause=34;
		ast_setstate(ast, AST_STATE_DOWN);
		return -1;
	}
	
	chan_misdn_log(2, port, " --> * SEND: State Dialing pid:%d\n",newbc?newbc->pid:1);

	ast_setstate(ast, AST_STATE_DIALING);
	ast->hangupcause=16;

wenn pattern available soll gestoppt werden, sonst nicht:	
	if (newbc->nt) stop_bc_tones(ch);

	ch->state=MISDN_CALLING;
	
	return 0; 
}


static struct ast_channel_tech misdn_tech = {
	.type="lcr",
	.description="Channel driver for connecting to Linux-Call-Router",
	.capabilities= je nach option?AST_FORMAT_ALAW:AST_FORMAT_ULAW ,
	.requester=lcr_request,
	.send_digit=lcr_digit,
	.call=lcr_call,
	.bridge=lcr_bridge, 
	.hangup=lcr_hangup,
	.answer=lcr_answer,
	.read=lcr_read,
	.write=lcr_write,
	.indicate=lcr_indication,
	.fixup=lcr_fixup,
	.send_text=lcr_send_text,
	.properties=0
};


/*
 * module loading and destruction
 */
int load_module(void)
{
//	ast_mutex_init(&release_lock);

//	lcr_cfg_update_ptp();

	if (!(lcr_sock = open_socket())) {
		ast_log(LOG_ERROR, "Unable to connect %s\n", misdn_type);
		lcr_sock = -1;
		/* continue with closed socket */
	}

	if (!bchannel_initialize()) {
		ast_log(LOG_ERROR, "Unable to open mISDN device\n");
		unload_module();
		return -1;
	}
	mISDN_created = 1;

	if (ast_channel_register(&lcr_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", misdn_type);
		unload_module();
		return -1;
	}
  
	ast_cli_register(&cli_show_lcr);
	ast_cli_register(&cli_show_calls);

	ast_cli_register(&cli_reload_routing);
	ast_cli_register(&cli_reload_interfaces);
	ast_cli_register(&cli_port_block);
	ast_cli_register(&cli_port_unblock);
  
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

	chan_lcr_log(0, 0, "-- mISDN Channel Driver Registred -- (BE AWARE THIS DRIVER IS EXPERIMENTAL!)\n");

	return 0;
}

int unload_module(void)
{
	/* First, take us out of the channel loop */
	ast_log(LOG_VERBOSE, "-- Unregistering mISDN Channel Driver --\n");
	
	misdn_tasks_destroy();
	
	if (!g_config_initialized) return 0;
	
	ast_cli_unregister(&cli_show_lcr);
	ast_cli_unregister(&cli_show_calls);
	ast_cli_unregister(&cli_reload_routing);
	ast_cli_unregister(&cli_reload_interfaces);
	ast_cli_unregister(&cli_port_block);
	ast_cli_unregister(&cli_port_unblock);
	ast_unregister_application("misdn_set_opt");
  
	ast_channel_unregister(&lcr_tech);

	if (mISDN_created) {
		bchannel_deinitialize();
		mISDN_created = 0;
	}

	if (lcr_sock >= 0) {
		close(lcr_sock);
		lcr_sock = -1;
	}

	was ist mit dem mutex
	
	return 0;
}

int reload(void)
{
	reload_config();

	return 0;
}

int usecount(void)
{
	int res;
	ast_mutex_lock(&usecnt_lock);
	res = usecnt;
	ast_mutex_unlock(&usecnt_lock);
	return res;
}

char *description(void)
{
	return desc;
}

char *key(void)
{
	return ASTERISK_GPL_KEY;
}


