/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** mISDN port abstraction for dss1                                           **
**                                                                           **
\*****************************************************************************/ 

#include "main.h"
#include "myisdn.h"
#ifndef SOCKET_MISDN
// old mISDN
extern "C" {
#include <mISDNuser/net_l2.h>
}

#ifndef CMX_TXDATA_ON
#define OLD_MISDN
#endif
#ifndef CMX_TXDATA_OFF
#define OLD_MISDN
#endif
#ifndef CMX_DELAY
#define OLD_MISDN
#endif
#ifndef PIPELINE_CFG
#define OLD_MISDN
#endif
#ifndef CMX_TX_DATA
#define OLD_MISDN
#endif

#ifdef OLD_MISDN
#warning
#warning *********************************************************
#warning *
#warning * It seems that you use an older version of mISDN.
#warning * Features like voice recording or echo will not work.
#warning * Also it causes problems with loading modules and may
#warning * not work at all.
#warning *
#warning * Please upgrade to newer version. A working version can
#warning * be found at www.linux-call-router.de.
#warning *
#warning * Do not use the mISDN_1_1 branch, it does not have all
#warning * the features that are required. Use the master branch
#warning * instead.
#warning *
#warning *********************************************************
#warning
#error
#endif

#ifndef ISDN_PID_L4_B_USER
#define ISDN_PID_L4_B_USER 0x440000ff
#endif

#else
// socket mISDN
extern "C" {
}
#include <q931.h>

#undef offsetof
#ifdef __compiler_offsetof
#define offsetof(TYPE,MEMBER) __compiler_offsetof(TYPE,MEMBER)
#else
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})
#endif

// timeouts if activating/deactivating response from mISDN got lost
#define B_TIMER_ACTIVATING 1 // seconds
#define B_TIMER_DEACTIVATING 1 // seconds

/* list of mISDN ports */
struct mISDNport *mISDNport_first;

/* noise randomizer */
unsigned char mISDN_rand[256];
int mISDN_rand_count = 0;

#ifdef SOCKET_MISDN
unsigned long mt_assign_pid = ~0;

int mISDNsocket = -1;

int mISDN_initialize(void)
{
	char filename[256];

	/* try to open raw socket to check kernel */
	mISDNsocket = socket(PF_ISDN, SOCK_RAW, ISDN_P_BASE);
	if (mISDNsocket < 0)
	{
		fprintf(stderr, "Cannot open mISDN due to '%s'. (Does your Kernel support socket based mISDN?)\n", strerror(errno));
		return(-1);
	}

	/* init mlayer3 */
	init_layer3(4); // buffer of 4

	/* open debug, if enabled and not only stack debugging */
	if (options.deb)
	{
		SPRINT(filename, "%s/debug.log", INSTALL_DATA);
		debug_fp = fopen(filename, "a");
	}

	if (options.deb & DEBUG_STACK)
	{
		SPRINT(filename, "%s/debug_mISDN.log", INSTALL_DATA);
		mISDN_debug_init(0xffffffff, filename, filename, filename);
	} else
		mISDN_debug_init(0, NULL, NULL, NULL);

	return(0);
}

void mISDN_deinitialize(void)
{
	cleanup_layer3();

	mISDN_debug_close();

	if (debug_fp)
		fclose(debug_fp);
	debug_fp = NULL;

	if (mISDNsocket > -1)
		close(mISDNsocket);
}
#else
int entity = 0; /* used for udevice */
int mISDNdevice = -1; /* the device handler and port list */

int mISDN_initialize(void)
{
	char debug_log[128];
	unsigned char buff[1025];
	iframe_t *frm = (iframe_t *)buff;
	int ret;

	/* initialize stuff of the NT lib */
	if (options.deb & DEBUG_STACK)
	{
		global_debug = 0xffffffff & ~DBGM_MSG;
//		global_debug = DBGM_L3DATA;
	} else
		global_debug = DBGM_MAN;
	SPRINT(debug_log, "%s/debug.log", INSTALL_DATA);
	if (options.deb & DEBUG_LOG)
		debug_init(global_debug, debug_log, debug_log, debug_log);
	else
		debug_init(global_debug, NULL, NULL, NULL);
	msg_init();

	/* open mISDNdevice if not already open */
	if (mISDNdevice < 0)
	{
		ret = mISDN_open();
		if (ret < 0)
		{
			fprintf(stderr, "cannot open mISDN device ret=%d errno=%d (%s) Check for mISDN modules!\nAlso did you create \"/dev/mISDN\"? Do: \"mknod /dev/mISDN c 46 0\"\n", ret, errno, strerror(errno));
			return(-1);
		}
		mISDNdevice = ret;
		PDEBUG(DEBUG_ISDN, "mISDN device opened.\n");

		/* create entity for layer 3 TE-mode */
		mISDN_write_frame(mISDNdevice, buff, 0, MGR_NEWENTITY | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);
		ret = mISDN_read_frame(mISDNdevice, frm, sizeof(iframe_t), 0, MGR_NEWENTITY | CONFIRM, TIMEOUT_1SEC);
		if (ret < (int)mISDN_HEADER_LEN)
		{
			noentity:
			FATAL("Cannot request MGR_NEWENTITY from mISDN. Exitting due to software bug.");
		}
		entity = frm->dinfo & 0xffff;
		if (!entity)
			goto noentity;
		PDEBUG(DEBUG_ISDN, "our entity for l3-processes is %d.\n", entity);
	}
	return(0);
}

void mISDN_deinitialize(void)
{
	unsigned char buff[1025];

	debug_close();
	global_debug = 0;

	if (mISDNdevice > -1)
	{
		/* free entity */
		mISDN_write_frame(mISDNdevice, buff, 0, MGR_DELENTITY | REQUEST, entity, 0, NULL, TIMEOUT_1SEC);
		/* close device */
		mISDN_close(mISDNdevice);
		mISDNdevice = -1;
		PDEBUG(DEBUG_ISDN, "mISDN device closed.\n");
	}
}
#endif

/*
 * constructor
 */
PmISDN::PmISDN(int type, mISDNport *mISDNport, char *portname, struct port_settings *settings, int channel, int exclusive) : Port(type, portname, settings)
{
	p_m_mISDNport = mISDNport;
	p_m_portnum = mISDNport->portnum;
	p_m_b_index = -1;
	p_m_b_channel = 0;
	p_m_b_exclusive = 0;
	p_m_b_reserve = 0;
	p_m_delete = 0;
	p_m_hold = 0;
	p_m_tx_gain = mISDNport->ifport->interface->tx_gain;
	p_m_rx_gain = mISDNport->ifport->interface->rx_gain;
	p_m_conf = 0;
	p_m_txdata = 0;
	p_m_delay = 0;
	p_m_echo = 0;
	p_m_tone = 0;
	p_m_rxoff = 0;
	p_m_joindata = 0;
	p_m_dtmf = !mISDNport->ifport->nodtmf;
	p_m_timeout = 0;
	p_m_timer = 0;
	p_m_remote_ref = 0; /* channel shall be exported to given remote */
	p_m_remote_id = 0; /* channel shall be exported to given remote */
	SCPY(p_m_pipeline, mISDNport->ifport->interface->pipeline);
	
	/* audio */
	p_m_load = 0;
	p_m_last_tv_sec = 0;

	/* crypt */
	p_m_crypt = 0;
	p_m_crypt_listen = 0;
	p_m_crypt_msg_loops = 0;
	p_m_crypt_msg_loops = 0;
	p_m_crypt_msg_len = 0;
	p_m_crypt_msg[0] = '\0';
	p_m_crypt_msg_current = 0;
	p_m_crypt_key_len = 0;
	p_m_crypt_listen = 0;
	p_m_crypt_listen_state = 0;
	p_m_crypt_listen_len = 0;
	p_m_crypt_listen_msg[0] = '\0';
	p_m_crypt_listen_crc = 0;
	if (mISDNport->ifport->interface->bf_len >= 4 && mISDNport->ifport->interface->bf_len <= 56)
	{
		memcpy(p_m_crypt_key, mISDNport->ifport->interface->bf_key, p_m_crypt_key_len);
		p_m_crypt_key_len = mISDNport->ifport->interface->bf_len;
		p_m_crypt = 1;
	}

	/* if any channel requested by constructor */
	if (channel == CHANNEL_ANY)
	{
		/* reserve channel */
		p_m_b_reserve = 1;
		mISDNport->b_reserved++;
	}

	/* reserve channel */
	if (channel > 0) // only if constructor was called with a channel resevation
		seize_bchannel(channel, exclusive);

	/* we increase the number of objects: */
	mISDNport->use++;
	PDEBUG(DEBUG_ISDN, "Created new mISDNPort(%s). Currently %d objects use, port #%d\n", portname, mISDNport->use, p_m_portnum);
}


/*
 * destructor
 */
PmISDN::~PmISDN()
{
	struct lcr_msg *message;

	/* remove bchannel relation */
	drop_bchannel();

	/* release epoint */
	while (p_epointlist)
	{
		PDEBUG(DEBUG_ISDN, "destroy mISDNPort(%s). endpoint still exists, releaseing.\n", p_name);
		message = message_create(p_serial, p_epointlist->epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = 16;
		message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		message_put(message);
		/* remove from list */
		free_epointlist(p_epointlist);
	}

	/* we decrease the number of objects: */
	p_m_mISDNport->use--;
	PDEBUG(DEBUG_ISDN, "destroyed mISDNPort(%s). Currently %d objects\n", p_name, p_m_mISDNport->use);
}


/*
 * trace
 */
void chan_trace_header(struct mISDNport *mISDNport, class PmISDN *port, char *msgtext, int direction)
{
	/* init trace with given values */
	start_trace(mISDNport?mISDNport->portnum:0,
		    (mISDNport)?((mISDNport->ifport)?mISDNport->ifport->interface:NULL):NULL,
		    port?numberrize_callerinfo(port->p_callerinfo.id, port->p_callerinfo.ntype, options.national, options.international):NULL,
		    port?port->p_dialinginfo.id:NULL,
		    direction,
		    CATEGORY_CH,
		    port?port->p_serial:0,
		    msgtext);
}


/*
 * layer trace header
 */
static struct isdn_message {
	char *name;
	unsigned long value;
} isdn_message[] = {
	{"PH_ACTIVATE", L1_ACTIVATE_REQ},
	{"PH_DEACTIVATE", L1_DEACTIVATE_REQ},
	{"DL_ESTABLISH", L2_ESTABLISH_REQ},
	{"DL_RELEASE", L2_RELEASE_REQ},
	{"UNKNOWN", L3_UNKNOWN},
	{"MT_TIMEOUT", L3_TIMEOUT_REQ},
	{"MT_SETUP", L3_SETUP_REQ},
	{"MT_SETUP_ACK", L3_SETUP_ACKNOWLEDGE_REQ},
	{"MT_PROCEEDING", L3_PROCEEDING_REQ},
	{"MT_ALERTING", L3_ALERTING_REQ},
	{"MT_CONNECT", L3_CONNECT_REQ},
	{"MT_CONNECT_ACK", L3_CONNECT_ACKNOWLEDGE_REQ},
	{"MT_DISCONNECT", L3_DISCONNECT_REQ},
	{"MT_RELEASE", L3_RELEASE_REQ},
	{"MT_RELEASE_COMP", L3_RELEASE_COMPLETE_REQ},
	{"MT_INFORMATION", L3_INFORMATION_REQ},
	{"MT_PROGRESS", L3_PROGRESS_REQ},
	{"MT_NOTIFY", L3_NOTIFY_REQ},
	{"MT_SUSPEND", L3_SUSPEND_REQ},
	{"MT_SUSPEND_ACK", L3_SUSPEND_ACKNOWLEDGE_REQ},
	{"MT_SUSPEND_REJ", L3_SUSPEND_REJECT_REQ},
	{"MT_RESUME", L3_RESUME_REQ},
	{"MT_RESUME_ACK", L3_RESUME_ACKNOWLEDGE_REQ},
	{"MT_RESUME_REJ", L3_RESUME_REJECT_REQ},
	{"MT_HOLD", L3_HOLD_REQ},
	{"MT_HOLD_ACK", L3_HOLD_ACKNOWLEDGE_REQ},
	{"MT_HOLD_REJ", L3_HOLD_REJECT_REQ},
	{"MT_RETRIEVE", L3_RETRIEVE_REQ},
	{"MT_RETRIEVE_ACK", L3_RETRIEVE_ACKNOWLEDGE_REQ},
	{"MT_RETRIEVE_REJ", L3_RETRIEVE_REJECT_REQ},
	{"MT_FACILITY", L3_FACILITY_REQ},
	{"MT_STATUS", L3_STATUS_REQ},
	{"MT_RESTART", L3_RESTART_REQ},
#ifdef SOCKET_MISDN
	{"MT_NEW_L3ID", L3_NEW_L3ID_REQ},
	{"MT_RELEASE_L3ID", L3_RELEASE_L3ID_REQ},
#else
	{"MT_NEW_CR", L3_NEW_CR_REQ},
	{"MT_RELEASE_CR", L3_RELEASE_CR_REQ},
#endif

	{NULL, 0},
};
static char *isdn_prim[4] = {
	" REQUEST",
	" CONFIRM",
	" INDICATION",
	" RESPONSE",
};
void l1l2l3_trace_header(struct mISDNport *mISDNport, class PmISDN *port, unsigned long msg, int direction)
{
	int i;
	char msgtext[64] = "<<UNKNOWN MESSAGE>>";

	/* select message and primitive text */
	i = 0;
	while(isdn_message[i].name)
	{
		if (isdn_message[i].value == (msg&0xffffff00))
		{
			SCPY(msgtext, isdn_message[i].name);
			break;
		}
		i++;
	}
	SCAT(msgtext, isdn_prim[msg&0x00000003]);

	/* add direction */
#ifdef SOCKET_MISDN
	if (direction && (msg&0xffffff00)!=L3_NEW_L3ID_REQ && (msg&0xffffff00)!=L3_RELEASE_L3ID_REQ)
#else
	if (direction && (msg&0xffffff00)!=L3_NEW_CR_REQ && (msg&0xffffff00)!=L3_RELEASE_CR_REQ)
#endif
	{
		if (mISDNport)
		{
			if (mISDNport->ntmode)
			{
				if (direction == DIRECTION_OUT)
					SCAT(msgtext, " N->U");
				else
					SCAT(msgtext, " N<-U");
			} else
			{
				if (direction == DIRECTION_OUT)
					SCAT(msgtext, " U->N");
				else
					SCAT(msgtext, " U<-N");
			}
		}
	}

	/* init trace with given values */
	start_trace(mISDNport?mISDNport->portnum:0,
		    mISDNport?(mISDNport->ifport?mISDNport->ifport->interface:NULL):NULL,
		    port?numberrize_callerinfo(port->p_callerinfo.id, port->p_callerinfo.ntype, options.national, options.international):NULL,
		    port?port->p_dialinginfo.id:NULL,
		    direction,
		    CATEGORY_CH,
		    port?port->p_serial:0,
		    msgtext);
}


/*
 * send control information to the channel (dsp-module)
 */
#ifdef SOCKET_MISDN
void ph_control(struct mISDNport *mISDNport, class PmISDN *isdnport, unsigned long sock, unsigned long c1, unsigned long c2, char *trace_name, int trace_value)
{
	unsigned char buffer[MISDN_HEADER_LEN+sizeof(int)+sizeof(int)];
	struct mISDNhead *ctrl = (struct mISDNhead *)buffer;
	unsigned long *d = (unsigned long *)(buffer+MISDN_HEADER_LEN);
	int ret;

	if (sock < 0)
		return;

	ctrl->prim = PH_CONTROL_REQ;
	ctrl->id = 0;
	*d++ = c1;
	*d++ = c2;
	ret = sendto(sock, buffer, MISDN_HEADER_LEN+sizeof(int)*2, 0, NULL, 0);
	if (ret <= 0)
		PERROR("Failed to send to socket %d\n", sock);
#else
void ph_control(struct mISDNport *mISDNport, class PmISDN *isdnport, unsigned long addr, unsigned long c1, unsigned long c2, char *trace_name, int trace_value)
{
	unsigned char buffer[mISDN_HEADER_LEN+sizeof(int)+sizeof(int)];
	iframe_t *ctrl = (iframe_t *)buffer; 
	unsigned long *d = (unsigned long *)&ctrl->data.p;

	if (!addr)
		return;

	ctrl->prim = PH_CONTROL | REQUEST;
	ctrl->addr = addr | FLG_MSG_DOWN;
	ctrl->dinfo = 0;
	ctrl->len = sizeof(int)*2;
	*d++ = c1;
	*d++ = c2;
	mISDN_write(mISDNdevice, ctrl, mISDN_HEADER_LEN+ctrl->len, TIMEOUT_1SEC);
#endif
	chan_trace_header(mISDNport, isdnport, "BCHANNEL control", DIRECTION_OUT);
#ifdef SOCKET_MISDN
	if (c1 == DSP_CONF_JOIN)
#else
	if (c1 == CMX_CONF_JOIN)
#endif
		add_trace(trace_name, NULL, "0x%08x", trace_value);
	else
		add_trace(trace_name, NULL, "%d", trace_value);
	end_trace();
}

#ifdef SOCKET_MISDN
void ph_control_block(struct mISDNport *mISDNport, class PmISDN *isdnport, int sock, unsigned long c1, void *c2, int c2_len, char *trace_name, int trace_value)
{
	unsigned char buffer[MISDN_HEADER_LEN+sizeof(int)+c2_len];
	struct mISDNhead *ctrl = (struct mISDNhead *)buffer;
	unsigned long *d = (unsigned long *)(buffer+MISDN_HEADER_LEN);
	int ret;

	if (sock < 0)
		return;

	ctrl->prim = PH_CONTROL_REQ;
	ctrl->id = 0;
	*d++ = c1;
	memcpy(d, c2, c2_len);
	ret = sendto(sock, buffer, MISDN_HEADER_LEN+sizeof(int)+c2_len, 0, NULL, 0);
	if (ret <= 0)
		PERROR("Failed to send to socket %d\n", sock);
#else
void ph_control_block(struct mISDNport *mISDNport, class PmISDN *isdnport, unsigned long addr, unsigned long c1, void *c2, int c2_len, char *trace_name, int trace_value)
{
	unsigned char buffer[mISDN_HEADER_LEN+sizeof(int)+c2_len];
	iframe_t *ctrl = (iframe_t *)buffer;
	unsigned long *d = (unsigned long *)&ctrl->data.p;

	if (!addr)
		return;

	ctrl->prim = PH_CONTROL | REQUEST;
	ctrl->addr = addr | FLG_MSG_DOWN;
	ctrl->dinfo = 0;
	ctrl->len = sizeof(int)+c2_len;
	*d++ = c1;
	memcpy(d, c2, c2_len);
	mISDN_write(mISDNdevice, ctrl, mISDN_HEADER_LEN+ctrl->len, TIMEOUT_1SEC);
#endif
	chan_trace_header(mISDNport, isdnport, "BCHANNEL control", DIRECTION_OUT);
	add_trace(trace_name, NULL, "%d", trace_value);
	end_trace();
}


/*
 * subfunction for bchannel_event
 * create stack
 */
static int _bchannel_create(struct mISDNport *mISDNport, int i)
{
	int ret;
#ifdef SOCKET_MISDN
	unsigned long on = 1;
	struct sockaddr_mISDN addr;

	if (mISDNport->b_socket[i] > -1)
	{
		PERROR("Error: Socket already created for index %d\n", i);
		return(0);
	}

	/* open socket */
//#warning testing without DSP
//	mISDNport->b_socket[i] = socket(PF_ISDN, SOCK_DGRAM, ISDN_P_B_RAW);
	mISDNport->b_socket[i] = socket(PF_ISDN, SOCK_DGRAM, ISDN_P_B_L2DSP);
	if (mISDNport->b_socket[i] < 0)
	{
		PERROR("Error: Failed to open bchannel-socket for index %d with mISDN-DSP layer. Did you load mISDNdsp.ko?\n", i);
		return(0);
	}
	
	/* set nonblocking io */
	ret = ioctl(mISDNport->b_socket[i], FIONBIO, &on);
	if (ret < 0)
	{
		PERROR("Error: Failed to set bchannel-socket index %d into nonblocking IO\n", i);
		close(mISDNport->b_socket[i]);
		mISDNport->b_socket[i] = -1;
		return(0);
	}

	/* bind socket to bchannel */
	addr.family = AF_ISDN;
	addr.dev = mISDNport->portnum-1;
	addr.channel = i+1+(i>=15);
	ret = bind(mISDNport->b_socket[i], (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0)
	{
		PERROR("Error: Failed to bind bchannel-socket for index %d with mISDN-DSP layer. Did you load mISDNdsp.ko?\n", i);
		close(mISDNport->b_socket[i]);
		mISDNport->b_socket[i] = -1;
		return(0);
	}

	chan_trace_header(mISDNport, mISDNport->b_port[i], "BCHANNEL create socket", DIRECTION_OUT);
	add_trace("channel", NULL, "%d", i+1+(i>=15));
	add_trace("socket", NULL, "%d", mISDNport->b_socket[i]);
	end_trace();

	return(1);
#else
	unsigned char buff[1024];
	layer_info_t li;
	mISDN_pid_t pid;

	if (!mISDNport->b_stid[i])
	{
		PERROR("Error: no stack for index %d\n", i);
		return(0);
	}
	if (mISDNport->b_addr[i])
	{
		PERROR("Error: stack already created for index %d\n", i);
		return(0);
	}

	/* create new layer */
	PDEBUG(DEBUG_BCHANNEL, "creating new layer for bchannel %d (index %d).\n" , i+1+(i>=15), i);
	memset(&li, 0, sizeof(li));
	memset(&pid, 0, sizeof(pid));
	li.object_id = -1;
	li.extentions = 0;
	li.st = mISDNport->b_stid[i];
	UCPY(li.name, "B L4");
	li.pid.layermask = ISDN_LAYER((4));
	li.pid.protocol[4] = ISDN_PID_L4_B_USER;
	ret = mISDN_new_layer(mISDNdevice, &li);
	if (ret)
	{
		failed_new_layer:
		PERROR("mISDN_new_layer() failed to add bchannel %d (index %d)\n", i+1+(i>=15), i);
		goto failed;
	}
	mISDNport->b_addr[i] = li.id;
	if (!li.id)
	{
		goto failed_new_layer;
	}
	PDEBUG(DEBUG_BCHANNEL, "new layer (b_addr=0x%x)\n", mISDNport->b_addr[i]);

	/* create new stack */
	pid.protocol[1] = ISDN_PID_L1_B_64TRANS;
	pid.protocol[2] = ISDN_PID_L2_B_TRANS;
	pid.protocol[3] = ISDN_PID_L3_B_DSP;
	pid.protocol[4] = ISDN_PID_L4_B_USER;
	pid.layermask = ISDN_LAYER((1)) | ISDN_LAYER((2)) | ISDN_LAYER((3)) | ISDN_LAYER((4));
	ret = mISDN_set_stack(mISDNdevice, mISDNport->b_stid[i], &pid);
	if (ret)
	{
		stack_error:
		PERROR("mISDN_set_stack() failed (ret=%d) to add bchannel (index %d) stid=0x%x\n", ret, i, mISDNport->b_stid[i]);
		mISDN_write_frame(mISDNdevice, buff, mISDNport->b_addr[i], MGR_DELLAYER | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);
		goto failed;
	}
	ret = mISDN_get_setstack_ind(mISDNdevice, mISDNport->b_addr[i]);
	if (ret)
		goto stack_error;

	/* get layer id */
	mISDNport->b_addr[i] = mISDN_get_layerid(mISDNdevice, mISDNport->b_stid[i], 4);
	if (!mISDNport->b_addr[i])
		goto stack_error;
	chan_trace_header(mISDNport, mISDNport->b_port[i], "BCHANNEL create stack", DIRECTION_OUT);
	add_trace("channel", NULL, "%d", i+1+(i>=15));
	add_trace("stack", "id", "0x%08x", mISDNport->b_stid[i]);
	add_trace("stack", "address", "0x%08x", mISDNport->b_addr[i]);
	end_trace();

	return(1);

failed:
	mISDNport->b_addr[i] = 0;
	return(0);
#endif
}


/*
 * subfunction for bchannel_event
 * activate / deactivate request
 */
static void _bchannel_activate(struct mISDNport *mISDNport, int i, int activate)
{
#ifdef SOCKET_MISDN
	struct mISDNhead act;
	int ret;

	if (mISDNport->b_socket[i] < 0)
		return;
	act.prim = (activate)?PH_ACTIVATE_REQ:PH_DEACTIVATE_REQ; 
	act.id = 0;
	ret = sendto(mISDNport->b_socket[i], &act, MISDN_HEADER_LEN, 0, NULL, 0);
	if (ret <= 0)
		PERROR("Failed to send to socket %d\n", mISDNport->b_socket[i]);
#else
	iframe_t act;

	/* activate bchannel */
	act.prim = (activate?DL_ESTABLISH:DL_RELEASE) | REQUEST; 
	act.addr = mISDNport->b_addr[i] | FLG_MSG_DOWN;
	act.dinfo = 0;
	act.len = 0;
	mISDN_write(mISDNdevice, &act, mISDN_HEADER_LEN+act.len, TIMEOUT_1SEC);
#endif

	/* trace */
	chan_trace_header(mISDNport, mISDNport->b_port[i], activate?(char*)"BCHANNEL activate":(char*)"BCHANNEL deactivate", DIRECTION_OUT);
	add_trace("channel", NULL, "%d", i+1+(i>=15));
	if (mISDNport->b_timer[i])
		add_trace("event", NULL, "timeout recovery");
	end_trace();
}


/*
 * subfunction for bchannel_event
 * set features
 */
static void _bchannel_configure(struct mISDNport *mISDNport, int i)
{
	struct PmISDN *port;
#ifdef SOCKET_MISDN
	int handle;

	if (mISDNport->b_socket[i] < 0)
		return;
	handle = mISDNport->b_socket[i];
	port = mISDNport->b_port[i];
	if (!port)
	{
		PERROR("bchannel index i=%d not associated with a port object\n", i);
		return;
	}

	/* set dsp features */
	if (port->p_m_txdata)
		ph_control(mISDNport, port, handle, (port->p_m_txdata)?DSP_TXDATA_ON:DSP_TXDATA_OFF, 0, "DSP-TXDATA", port->p_m_txdata);
	if (port->p_m_delay)
		ph_control(mISDNport, port, handle, DSP_DELAY, port->p_m_delay, "DSP-DELAY", port->p_m_delay);
	if (port->p_m_tx_gain)
		ph_control(mISDNport, port, handle, DSP_VOL_CHANGE_TX, port->p_m_tx_gain, "DSP-TX_GAIN", port->p_m_tx_gain);
	if (port->p_m_rx_gain)
		ph_control(mISDNport, port, handle, DSP_VOL_CHANGE_RX, port->p_m_rx_gain, "DSP-RX_GAIN", port->p_m_rx_gain);
	if (port->p_m_pipeline[0])
		ph_control_block(mISDNport, port, handle, DSP_PIPELINE_CFG, port->p_m_pipeline, strlen(port->p_m_pipeline)+1, "DSP-PIPELINE", 0);
	if (port->p_m_conf)
		ph_control(mISDNport, port, handle, DSP_CONF_JOIN, port->p_m_conf, "DSP-CONF", port->p_m_conf);
	if (port->p_m_echo)
		ph_control(mISDNport, port, handle, DSP_ECHO_ON, 0, "DSP-ECHO", 1);
	if (port->p_m_tone)
		ph_control(mISDNport, port, handle, DSP_TONE_PATT_ON, port->p_m_tone, "DSP-TONE", port->p_m_tone);
	if (port->p_m_rxoff)
		ph_control(mISDNport, port, handle, DSP_RECEIVE_OFF, 0, "DSP-RXOFF", 1);
//	if (port->p_m_txmix)
//		ph_control(mISDNport, port, handle, DSP_MIX_ON, 0, "DSP-MIX", 1);
	if (port->p_m_dtmf)
		ph_control(mISDNport, port, handle, DTMF_TONE_START, 0, "DSP-DTMF", 1);
	if (port->p_m_crypt)
		ph_control_block(mISDNport, port, handle, DSP_BF_ENABLE_KEY, port->p_m_crypt_key, port->p_m_crypt_key_len, "DSP-CRYPT", port->p_m_crypt_key_len);
#else
	unsigned long handle;

	handle = mISDNport->b_addr[i];
	port = mISDNport->b_port[i];
	if (!port)
	{
		PERROR("bchannel index i=%d not associated with a port object\n", i);
		return;
	}

	/* set dsp features */
#ifndef OLD_MISDN
	if (port->p_m_txdata)
		ph_control(mISDNport, port, handle, (port->p_m_txdata)?CMX_TXDATA_ON:CMX_TXDATA_OFF, 0, "DSP-TXDATA", port->p_m_txdata);
	if (port->p_m_delay)
		ph_control(mISDNport, port, handle, CMX_DELAY, port->p_m_delay, "DSP-DELAY", port->p_m_delay);
#endif
	if (port->p_m_tx_gain)
		ph_control(mISDNport, port, handle, VOL_CHANGE_TX, port->p_m_tx_gain, "DSP-TX_GAIN", port->p_m_tx_gain);
	if (port->p_m_rx_gain)
		ph_control(mISDNport, port, handle, VOL_CHANGE_RX, port->p_m_rx_gain, "DSP-RX_GAIN", port->p_m_rx_gain);
#ifndef OLD_MISDN
	if (port->p_m_pipeline[0])
		ph_control_block(mISDNport, port, handle, PIPELINE_CFG, port->p_m_pipeline, strlen(port->p_m_pipeline)+1, "DSP-PIPELINE", 0);
#endif
	if (port->p_m_conf)
		ph_control(mISDNport, port, handle, CMX_CONF_JOIN, port->p_m_conf, "DSP-CONF", port->p_m_conf);
	if (port->p_m_echo)
		ph_control(mISDNport, port, handle, CMX_ECHO_ON, 0, "DSP-ECHO", 1);
	if (port->p_m_tone)
		ph_control(mISDNport, port, handle, TONE_PATT_ON, port->p_m_tone, "DSP-TONE", port->p_m_tone);
	if (port->p_m_rxoff)
		ph_control(mISDNport, port, handle, CMX_RECEIVE_OFF, 0, "DSP-RXOFF", 1);
//	if (port->p_m_txmix)
//		ph_control(mISDNport, port, handle, CMX_MIX_ON, 0, "DSP-MIX", 1);
	if (port->p_m_dtmf)
		ph_control(mISDNport, port, handle, DTMF_TONE_START, 0, "DSP-DTMF", 1);
	if (port->p_m_crypt)
		ph_control_block(mISDNport, port, handle, BF_ENABLE_KEY, port->p_m_crypt_key, port->p_m_crypt_key_len, "DSP-CRYPT", port->p_m_crypt_key_len);
#endif
}

/*
 * subfunction for bchannel_event
 * destroy stack
 */
static void _bchannel_destroy(struct mISDNport *mISDNport, int i)
{
#ifdef SOCKET_MISDN
	if (mISDNport->b_socket[i] < 0)
		return;
	chan_trace_header(mISDNport, mISDNport->b_port[i], "BCHANNEL remove socket", DIRECTION_OUT);
	add_trace("channel", NULL, "%d", i+1+(i>=15));
	add_trace("socket", NULL, "%d", mISDNport->b_socket[i]);
	end_trace();
	close(mISDNport->b_socket[i]);
	mISDNport->b_socket[i] = -1;
#else
	unsigned char buff[1024];

	chan_trace_header(mISDNport, mISDNport->b_port[i], "BCHANNEL remove stack", DIRECTION_OUT);
	add_trace("channel", NULL, "%d", i+1+(i>=15));
	add_trace("stack", "id", "0x%08x", mISDNport->b_stid[i]);
	add_trace("stack", "address", "0x%08x", mISDNport->b_addr[i]);
	end_trace();
	/* remove our stack only if set */
	if (mISDNport->b_addr[i])
	{
		PDEBUG(DEBUG_BCHANNEL, "free stack (b_addr=0x%x)\n", mISDNport->b_addr[i]);
		mISDN_clear_stack(mISDNdevice, mISDNport->b_stid[i]);
		mISDN_write_frame(mISDNdevice, buff, mISDNport->b_addr[i] | FLG_MSG_DOWN, MGR_DELLAYER | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);
		mISDNport->b_addr[i] = 0;
	}
#endif
}


/*
bchannel procedure
------------------

A bchannel goes through the following states in this order:

- B_STATE_IDLE
No one is using the bchannel.
It is available and not linked to Port class, nor reserved.

- B_STATE_ACTIVATING
The bchannel stack is created and an activation request is sent.
It MAY be linked to Port class, but already unlinked due to Port class removal.

- B_STATE_ACTIVE
The bchannel is active and cofigured to the Port class needs.
Also it is linked to a Port class, otherwhise it would be deactivated.

- B_STATE_DEACTIVATING
The bchannel is in deactivating state, due to deactivation request.
It may be linked to a Port class, that likes to reactivate it.

- B_STATE_IDLE
See above.
After deactivating bchannel, and if not used, the bchannel becomes idle again.

Also the bchannel may be exported, but only if the state is or becomes idle:

- B_STATE_EXPORTING
The bchannel assignment has been sent to the remove application.

- B_STATE_REMOTE
The bchannel assignment is acknowledged by the remote application.

- B_STATE_IMPORTING
The bchannel is re-imported by mISDN port object.

- B_STATE_IDLE
See above.
After re-importing bchannel, and if not used, the bchannel becomes idle again.


A bchannel can have the following events:

- B_EVENT_USE
A bchannel is required by a Port class.
The bchannel shall be exported to the remote application.

- B_EVENT_ACTIVATED
The bchannel beomes active.

- B_EVENT_DROP
The bchannel is not required by Port class anymore

- B_EVENT_DEACTIVATED
The bchannel becomes inactive.

- B_EVENT_EXPORTED
The bchannel is now used by remote application.

- B_EVENT_IMPORTED
The bchannel is not used by remote application.

All actions taken on these events depend on the current bchannel's state and if it is linked to a Port class.

if an export request is receive by remote application, p_m_remote_* is set.
the b_remote_*[index] indicates if and where the channel is exported to. (set from the point on, where export is initiated, until imported is acknowledged.)
- set on export request from remote application (if port is assigned)
- set on channel use, if requested by remote application (p_m_remote_*)
- cleared on drop request

the bchannel will be exported with ref and stack given. remote application uses the ref to link bchannel to the call.
the bchannel will be imported with stack given only. remote application must store stack id with the bchannel process.
the bchannel import/export is acknowledged with stack given.

if exporting, b_remote_*[index] is set to the remote socket id.
if importing has been acknowledged. b_remote_*[index] is cleared.

*/

/*
 * process bchannel events
 * - mISDNport is a pointer to the port's structure
 * - i is the index of the bchannel
 * - event is the B_EVENT_* value
 * - port is the PmISDN class pointer
 */
void bchannel_event(struct mISDNport *mISDNport, int i, int event)
{
	class PmISDN *b_port = mISDNport->b_port[i];
	int state = mISDNport->b_state[i];
	double timer = mISDNport->b_timer[i];
	unsigned long p_m_remote_ref = 0;
	unsigned long p_m_remote_id = 0;
	int p_m_tx_gain = 0;
	int p_m_rx_gain = 0;
	char *p_m_pipeline = NULL;
	unsigned char *p_m_crypt_key = NULL;
	int p_m_crypt_key_len = 0;
	int p_m_crypt_key_type = 0;
#ifdef SOCKET_MISDN
	unsigned long portid = (mISDNport->portnum<<8) + i+1+(i>=15);
#else
	unsigned long portid = mISDNport->b_stid[i];
#endif

	if (b_port)
	{
		p_m_remote_id = b_port->p_m_remote_id;
		p_m_remote_ref = b_port->p_m_remote_ref;
		p_m_tx_gain = b_port->p_m_tx_gain;
		p_m_rx_gain = b_port->p_m_rx_gain;
		p_m_pipeline = b_port->p_m_pipeline;
		p_m_crypt_key = b_port->p_m_crypt_key;
		p_m_crypt_key_len = b_port->p_m_crypt_key_len;
		p_m_crypt_key_type = /*b_port->p_m_crypt_key_type*/1;
	}

	switch(event)
	{
		case B_EVENT_USE:
		/* port must be linked in order to allow activation */
		if (!b_port)
			FATAL("bchannel must be linked to a Port class\n");
		switch(state)
		{
			case B_STATE_IDLE:
			if (p_m_remote_ref)
			{
				/* export bchannel */
				message_bchannel_to_join(p_m_remote_id, p_m_remote_ref, BCHANNEL_ASSIGN, portid, p_m_tx_gain, p_m_rx_gain, p_m_pipeline, p_m_crypt_key, p_m_crypt_key_len, p_m_crypt_key_type);
				chan_trace_header(mISDNport, b_port, "MESSAGE_BCHANNEL (to remote application)", DIRECTION_NONE);
				add_trace("type", NULL, "assign");
#ifdef SOCKET_MISDN
				add_trace("channel", NULL, "%d.%d", portid>>8, portid&0xff);
#else
				add_trace("socket", "id", "%d", portid);
#endif
				end_trace();
				state = B_STATE_EXPORTING;
				mISDNport->b_remote_id[i] = p_m_remote_id;
				mISDNport->b_remote_ref[i] = p_m_remote_ref;
			} else
			{
				/* create stack and send activation request */
				if (_bchannel_create(mISDNport, i))
				{
					_bchannel_activate(mISDNport, i, 1);
					state = B_STATE_ACTIVATING;
					timer = now_d + B_TIMER_ACTIVATING;
				}
			}
			break;

			case B_STATE_ACTIVATING:
			case B_STATE_EXPORTING:
			/* do nothing, because it is already activating */
			break;

			case B_STATE_DEACTIVATING:
			case B_STATE_IMPORTING:
			/* do nothing, because we must wait until we can reactivate */
			break;

			default:
			/* problems that might ocurr:
			 * B_EVENT_USE is received when channel already in use.
			 * bchannel exported, but not freed by other port
			 */
			PERROR("Illegal event %d at state %d, please correct.\n", event, state);
		}
		break;

		case B_EVENT_EXPORTREQUEST:
		/* special case where the bchannel is requested by remote */
		if (!p_m_remote_ref)
		{
			PERROR("export request without remote channel set, please correct.\n");
			break;
		}
		switch(state)
		{
			case B_STATE_IDLE:
			/* in case, the bchannel is exported right after seize_bchannel */
			/* export bchannel */
			/* p_m_remote_id is set, when this event happens. */
			message_bchannel_to_join(p_m_remote_id, p_m_remote_ref, BCHANNEL_ASSIGN, portid, p_m_tx_gain, p_m_rx_gain, p_m_pipeline, p_m_crypt_key, p_m_crypt_key_len, p_m_crypt_key_type);
			chan_trace_header(mISDNport, b_port, "MESSAGE_BCHANNEL (to remote application)", DIRECTION_NONE);
			add_trace("type", NULL, "assign");
#ifdef SOCKET_MISDN
			add_trace("channel", NULL, "%d.%d", portid>>8, portid&0xff);
#else
			add_trace("socket", "id", "%d", portid);
#endif
			end_trace();
			state = B_STATE_EXPORTING;
			mISDNport->b_remote_id[i] = p_m_remote_id;
			mISDNport->b_remote_ref[i] = p_m_remote_ref;
			break;

			case B_STATE_ACTIVATING:
			case B_STATE_EXPORTING:
			/* do nothing, because it is already activating */
			break;

			case B_STATE_DEACTIVATING:
			case B_STATE_IMPORTING:
			/* do nothing, because we must wait until we can reactivate */
			break;

			case B_STATE_ACTIVE:
			/* bchannel is active, so we deactivate */
			_bchannel_activate(mISDNport, i, 0);
			state = B_STATE_DEACTIVATING;
			timer = now_d + B_TIMER_DEACTIVATING;
			break;

			default:
			/* problems that might ocurr:
			 * ... when channel already in use.
			 * bchannel exported, but not freed by other port
			 */
			PERROR("Illegal event %d at state %d, please correct.\n", event, state);
		}
		break;

		case B_EVENT_ACTIVATED:
		timer = 0;
		switch(state)
		{
			case B_STATE_ACTIVATING:
			if (b_port && !p_m_remote_id)
			{
				/* bchannel is active and used by Port class, so we configure bchannel */
				_bchannel_configure(mISDNport, i);
				state = B_STATE_ACTIVE;
			} else
			{
				/* bchannel is active, but exported OR not used anymore (or has wrong stack config), so we deactivate */
				_bchannel_activate(mISDNport, i, 0);
				state = B_STATE_DEACTIVATING;
				timer = now_d + B_TIMER_DEACTIVATING;
			}
			break;

			default:
			PERROR("Illegal event %d at state %d, please correct.\n", event, state);
		}
		break;

		case B_EVENT_EXPORTED:
		switch(state)
		{
			case B_STATE_EXPORTING:
			if (b_port && p_m_remote_ref && p_m_remote_ref==mISDNport->b_remote_ref[i])
			{
				/* remote export done */
				state = B_STATE_REMOTE;
			} else
			{
				/* bchannel is now exported, but we need bchannel back
				 * OR bchannel is not used anymore
				 * OR bchannel has been exported to an obsolete ref,
				 * so reimport, to later export to new remote */
				message_bchannel_to_join(mISDNport->b_remote_id[i], 0, BCHANNEL_REMOVE, portid, 0,0,0,0,0,0);
				chan_trace_header(mISDNport, b_port, "MESSAGE_BCHANNEL (to remote application)", DIRECTION_NONE);
				add_trace("type", NULL, "remove");
#ifdef SOCKET_MISDN
				add_trace("channel", NULL, "%d.%d", portid>>8, portid&0xff);
#else
				add_trace("socket", "id", "%d", portid);
#endif
				end_trace();
				state = B_STATE_IMPORTING;
			}
			break;

			default:
			PERROR("Illegal event %d at state %d, please correct.\n", event, state);
		}
		break;

		case B_EVENT_DROP:
		if (!b_port)
			FATAL("bchannel must be linked to a Port class\n");
		switch(state)
		{
			case B_STATE_IDLE:
			/* bchannel is idle due to an error, so we do nothing */
			break;

			case B_STATE_ACTIVATING:
			case B_STATE_EXPORTING:
			/* do nothing because we must wait until bchanenl is active before deactivating */
			break;

			case B_STATE_ACTIVE:
			/* bchannel is active, so we deactivate */
			_bchannel_activate(mISDNport, i, 0);
			state = B_STATE_DEACTIVATING;
			timer = now_d + B_TIMER_DEACTIVATING;
			break;

			case B_STATE_REMOTE:
			/* bchannel is exported, so we re-import */
			message_bchannel_to_join(mISDNport->b_remote_id[i], 0, BCHANNEL_REMOVE, portid, 0,0,0,0,0,0);
			chan_trace_header(mISDNport, b_port, "MESSAGE_BCHANNEL (to remote application)", DIRECTION_NONE);
			add_trace("type", NULL, "remove");
#ifdef SOCKET_MISDN
			add_trace("channel", NULL, "%d.%d", portid>>8, portid&0xff);
#else
			add_trace("socket", "id", "%d", portid);
#endif
			end_trace();
			state = B_STATE_IMPORTING;
			break;

			case B_STATE_DEACTIVATING:
			case B_STATE_IMPORTING:
			/* we may have taken an already deactivating bchannel, but do not require it anymore, so we do nothing */
			break;

			default:
			PERROR("Illegal event %d at state %d, please correct.\n", event, state);
		}
		break;

		case B_EVENT_DEACTIVATED:
		timer = 0;
		switch(state)
		{
			case B_STATE_IDLE:
			/* ignore due to deactivation confirm after unloading */
			break;

			case B_STATE_DEACTIVATING:
			_bchannel_destroy(mISDNport, i);
			state = B_STATE_IDLE;
			if (b_port)
			{
				/* bchannel is now deactivate, but is requied by Port class, so we reactivate / export */
				if (p_m_remote_ref)
				{
					message_bchannel_to_join(p_m_remote_id, p_m_remote_ref, BCHANNEL_ASSIGN, portid, p_m_tx_gain, p_m_rx_gain, p_m_pipeline, p_m_crypt_key, p_m_crypt_key_len, p_m_crypt_key_type);
					chan_trace_header(mISDNport, b_port, "MESSAGE_BCHANNEL (to remote application)", DIRECTION_NONE);
					add_trace("type", NULL, "assign");
#ifdef SOCKET_MISDN
					add_trace("channel", NULL, "%d.%d", portid>>8, portid&0xff);
#else
					add_trace("socket", "id", "%d", portid);
#endif
					end_trace();
					state = B_STATE_EXPORTING;
					mISDNport->b_remote_id[i] = p_m_remote_id;
					mISDNport->b_remote_ref[i] = p_m_remote_ref;
				} else
				{
					if (_bchannel_create(mISDNport, i))
					{
						_bchannel_activate(mISDNport, i, 1);
						state = B_STATE_ACTIVATING;
						timer = now_d + B_TIMER_ACTIVATING;
					}
				}
			}
			break;

			default:
			PERROR("Illegal event %d at state %d, please correct.\n", event, state);
		}
		break;

		case B_EVENT_IMPORTED:
		switch(state)
		{
			case B_STATE_IMPORTING:
			state = B_STATE_IDLE;
			mISDNport->b_remote_id[i] = 0;
			mISDNport->b_remote_ref[i] = 0;
			if (b_port)
			{
				/* bchannel is now imported, but is requied by Port class, so we reactivate / export */
				if (p_m_remote_ref)
				{
					message_bchannel_to_join(p_m_remote_id, p_m_remote_ref, BCHANNEL_ASSIGN, portid, p_m_tx_gain, p_m_rx_gain, p_m_pipeline, p_m_crypt_key, p_m_crypt_key_len, p_m_crypt_key_type);
					chan_trace_header(mISDNport, b_port, "MESSAGE_BCHANNEL (to remote application)", DIRECTION_NONE);
					add_trace("type", NULL, "assign");
#ifdef SOCKET_MISDN
					add_trace("channel", NULL, "%d.%d", portid>>8, portid&0xff);
#else
					add_trace("socket", "id", "%d", portid);
#endif
					end_trace();
					state = B_STATE_EXPORTING;
					mISDNport->b_remote_id[i] = p_m_remote_id;
					mISDNport->b_remote_ref[i] = p_m_remote_ref;
				} else
				{
					if (_bchannel_create(mISDNport, i))
					{
						_bchannel_activate(mISDNport, i, 1);
						state = B_STATE_ACTIVATING;
						timer = now_d + B_TIMER_ACTIVATING;
					}
				}
			}
			break;

			default:
			/* ignore, because not assigned */
			;
		}
		break;

		case B_EVENT_TIMEOUT:
		timer = 0;
		switch(state)
		{
			case B_STATE_IDLE:
			/* ignore due to deactivation confirm after unloading */
			break;

			case B_STATE_ACTIVATING:
			_bchannel_activate(mISDNport, i, 1);
			timer = now_d + B_TIMER_ACTIVATING;
			break;

			case B_STATE_DEACTIVATING:
			_bchannel_activate(mISDNport, i, 0);
			timer = now_d + B_TIMER_DEACTIVATING;
			break;

			default:
			PERROR("Illegal event %d at state %d, please correct.\n", event, state);
		}
		break;

		default:
		PERROR("Illegal event %d, please correct.\n", event);
	}

	mISDNport->b_state[i] = state;
	mISDNport->b_timer[i] = timer;
}




/*
 * check for available channel and reserve+set it.
 * give channel number or SEL_CHANNEL_ANY or SEL_CHANNEL_NO
 * give exclusiv flag
 * returns -(cause value) or x = channel x or 0 = no channel
 * NOTE: no activation is done here
 */
int PmISDN::seize_bchannel(int channel, int exclusive)
{
	int i;

	/* the channel is what we have */
	if (p_m_b_channel == channel)
		return(channel);

	/* if channel already in use, release it */
	if (p_m_b_channel)
		drop_bchannel();

	/* if CHANNEL_NO */
	if (channel==CHANNEL_NO || channel==0)
		return(0);
	
	/* is channel in range ? */
	if (channel==16
	 || (channel>p_m_mISDNport->b_num && channel<16)
	 || ((channel-1)>p_m_mISDNport->b_num && channel>16)) /* channel-1 because channel 16 is not counted */
		return(-6); /* channel unacceptable */

	/* request exclusive channel */
	if (exclusive && channel>0)
	{
		i = channel-1-(channel>16);
		if (p_m_mISDNport->b_port[i])
			return(-44); /* requested channel not available */
		goto seize;
	}

	/* ask for channel */
	if (channel>0)
	{
		i = channel-1-(channel>16);
		if (p_m_mISDNport->b_port[i] == NULL)
			goto seize;
	}

	/* search for channel */
	i = 0;
	while(i < p_m_mISDNport->b_num)
	{
		if (!p_m_mISDNport->b_port[i])
		{
			channel = i+1+(i>=15);
			goto seize;
		}
		i++;
	}
	return(-34); /* no free channel */

seize:
	PDEBUG(DEBUG_BCHANNEL, "PmISDN(%s) seizing bchannel %d (index %d)\n", p_name, channel, i);

	/* link Port */
	p_m_mISDNport->b_port[i] = this;
	p_m_b_index = i;
	p_m_b_channel = channel;
	p_m_b_exclusive = exclusive;

	/* reserve channel */
	if (!p_m_b_reserve)
	{
		p_m_b_reserve = 1;
		p_m_mISDNport->b_reserved++;
	}

	return(channel);
}

/*
 * drop reserved channel and unset it.
 * deactivation is also done
 */
void PmISDN::drop_bchannel(void)
{
	/* unreserve channel */
	if (p_m_b_reserve)
		p_m_mISDNport->b_reserved--;
	p_m_b_reserve = 0;

	/* if not in use */
	if (p_m_b_index < 0)
		return;
	if (!p_m_b_channel)
		return;

	PDEBUG(DEBUG_BCHANNEL, "PmISDN(%s) dropping bchannel\n", p_name);

	if (p_m_mISDNport->b_state[p_m_b_index] != B_STATE_IDLE)
		bchannel_event(p_m_mISDNport, p_m_b_index, B_EVENT_DROP);
	p_m_mISDNport->b_port[p_m_b_index] = NULL;
	p_m_b_index = -1;
	p_m_b_channel = 0;
	p_m_b_exclusive = 0;
}

/* process bchannel export/import message from join */
void message_bchannel_from_join(class JoinRemote *joinremote, int type, unsigned long handle)
{
	class Endpoint *epoint;
	class Port *port;
	class PmISDN *isdnport;
	struct mISDNport *mISDNport;
	int i, ii;

	switch(type)
	{
		case BCHANNEL_REQUEST:
		/* find the port object for the join object ref */
		if (!(epoint = find_epoint_id(joinremote->j_epoint_id)))
		{
			PDEBUG(DEBUG_BCHANNEL, "join %d has no endpoint (anymore)\n", joinremote->j_serial);
			return;
		}
		if (!epoint->ep_portlist)
		{
			PDEBUG(DEBUG_BCHANNEL, "join %d has no port (anymore in portlist)\n", joinremote->j_serial);
			return;
		}
		if (epoint->ep_portlist->next)
		{
			PERROR("join %d has enpoint %d with more than one port. this shall not happen to remote joins.\n", joinremote->j_serial, epoint->ep_serial);
		}
		if (!(port = find_port_id(epoint->ep_portlist->port_id)))
		{
			PDEBUG(DEBUG_BCHANNEL, "join %d has no port (anymore as object)\n", joinremote->j_serial);
			return;
		}
		if (!((port->p_type&PORT_CLASS_MASK) != PORT_CLASS_mISDN))
		{
			PERROR("join %d has port %d not of mISDN type. This shall not happen.\n", joinremote->j_serial, port->p_serial);
		}
		isdnport = (class PmISDN *)port;

		/* assign */
		if (isdnport->p_m_remote_id)
		{
			PERROR("join %d recevied bchannel request from remote, but channel is already assinged.\n", joinremote->j_serial);
			break;
		}
		mISDNport = isdnport->p_m_mISDNport;
		i = isdnport->p_m_b_index;
		chan_trace_header(mISDNport, isdnport, "MESSAGE_BCHANNEL (from remote application)", DIRECTION_NONE);
		add_trace("type", NULL, "export request");
		isdnport->p_m_remote_ref = joinremote->j_serial;
		isdnport->p_m_remote_id = joinremote->j_remote_id;
		if (mISDNport && i>=0)
		{
			bchannel_event(mISDNport, i, B_EVENT_EXPORTREQUEST);
		}
		end_trace();
		break;

		case BCHANNEL_ASSIGN_ACK:
		case BCHANNEL_REMOVE_ACK:
		/* find mISDNport for stack ID */
		mISDNport = mISDNport_first;
		while(mISDNport)
		{
			i = 0;
			ii = mISDNport->b_num;
			while(i < ii)
			{
#ifdef SOCKET_MISDN
				if ((unsigned long)(mISDNport->portnum<<8)+i+1+(i>=15) == handle)
#else
				if (mISDNport->b_addr[i] == handle)
#endif
					break;
				i++;
			}
			if (i != ii)
				break;
			mISDNport = mISDNport->next;
		}
		if (!mISDNport)
		{
			PERROR("received assign/remove ack for bchannel's handle=%x, but handle does not exist in any mISDNport structure.\n", handle);
			break;
		}
		/* mISDNport may now be set or NULL */
		
		/* set */
		chan_trace_header(mISDNport, mISDNport->b_port[i], "MESSAGE_BCHANNEL (from remote application)", DIRECTION_NONE);
		add_trace("type", NULL, (type==BCHANNEL_ASSIGN_ACK)?"assign_ack":"remove_ack");
		if (mISDNport && i>=0)
			bchannel_event(mISDNport, i, (type==BCHANNEL_ASSIGN_ACK)?B_EVENT_EXPORTED:B_EVENT_IMPORTED);
		end_trace();
		break;
		default:
		PERROR("received wrong bchannel message type %d from remote\n", type);
	}
}


/*
 * handler

audio transmission procedure:
-----------------------------

* priority
three sources of audio transmission:
- crypto-data high priority
- tones high priority (also high)
- remote-data low priority

* elapsed
a variable that temporarily shows the number of samples elapsed since last transmission process.
p_m_last_tv_* is used to store that last timestamp. this is used to calculate the time elapsed.

* load
a variable that is increased whenever data is transmitted.
it is decreased while time elapses. it stores the number of samples that
are currently loaded to dsp module.
since clock in dsp module is the same clock for user space process, these 
times have no skew.

* levels
there are two levels:
ISDN_LOAD will give the load that have to be kept in dsp.
ISDN_MAXLOAD will give the maximum load before dropping.

* procedure for low priority data
see txfromup() for procedure
in short: remote data is ignored during high priority tones

* procedure for high priority data
whenever load is below ISDN_LOAD, load is filled up to ISDN_LOAD
if no more data is available, load becomes empty again.

'load' variable:
0                    ISDN_LOAD           ISDN_MAXLOAD
+--------------------+----------------------+
|                    |                      |
+--------------------+----------------------+

on empty load or on load below ISDN_LOAD, the load is inceased to ISDN_LOAD:
0                    ISDN_LOAD           ISDN_MAXLOAD
+--------------------+----------------------+
|TTTTTTTTTTTTTTTTTTTT|                      |
+--------------------+----------------------+

on empty load, remote-audio causes the load with the remote audio to be increased to ISDN_LOAD.
0                    ISDN_LOAD           ISDN_MAXLOAD
+--------------------+----------------------+
|TTTTTTTTTTTTTTTTTTTTRRRRR                  |
+--------------------+----------------------+

 */
int PmISDN::handler(void)
{
	struct lcr_msg *message;
	int elapsed = 0;
	int ret;

	if ((ret = Port::handler()))
		return(ret);

	/* get elapsed */
	if (p_m_last_tv_sec)
	{
		elapsed = 8000 * (now_tv.tv_sec - p_m_last_tv_sec)
			+ 8 * (now_tv.tv_usec/1000 - p_m_last_tv_msec);
	} else
	{
		/* set clock of first process ever in this instance */
		p_m_last_tv_sec = now_tv.tv_sec;
		p_m_last_tv_msec = now_tv.tv_usec/1000;
	}
	/* process only if we have a minimum of samples, to make packets not too small */
	if (elapsed >= ISDN_TRANSMIT)
	{
		/* set clock of last process! */
		p_m_last_tv_sec = now_tv.tv_sec;
		p_m_last_tv_msec = now_tv.tv_usec/1000;

		/* update load */
		if (elapsed < p_m_load)
			p_m_load -= elapsed;
		else
			p_m_load = 0;

		/* to send data, tone must be active OR crypt messages must be on */
		if ((p_tone_name[0] || p_m_crypt_msg_loops)
		 && (p_m_load < ISDN_LOAD)
		 && (p_state==PORT_STATE_CONNECT || p_m_mISDNport->tones))
		{
			int tosend = ISDN_LOAD - p_m_load, length; 
#ifdef SOCKET_MISDN
			unsigned char buf[MISDN_HEADER_LEN+tosend];
			struct mISDNhead *frm = (struct mISDNhead *)buf;
			unsigned char *p = buf+MISDN_HEADER_LEN;
#else
			unsigned char buf[mISDN_HEADER_LEN+tosend];
			iframe_t *frm = (iframe_t *)buf;
			unsigned char *p = buf+mISDN_HEADER_LEN;
#endif

			/* copy crypto loops */
			while (p_m_crypt_msg_loops && tosend)
			{
				/* how much do we have to send */
				length = p_m_crypt_msg_len - p_m_crypt_msg_current;

				/* clip tosend */
				if (length > tosend)
					length = tosend;

				/* copy message (part) to buffer */
				memcpy(p, p_m_crypt_msg+p_m_crypt_msg_current, length);

				/* new position */
				p_m_crypt_msg_current += length;
				if (p_m_crypt_msg_current == p_m_crypt_msg_len)
				{
					/* next loop */
					p_m_crypt_msg_current = 0;
					p_m_crypt_msg_loops--;
//					puts("eine loop weniger");
				}

				/* new length */
				tosend -= length;
			}

			/* copy tones */
			if (p_tone_name[0] && tosend)
			{
				tosend -= read_audio(p, tosend);
			}

			/* send data */
			if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE && ISDN_LOAD-p_m_load-tosend > 0)
			{
#ifdef SOCKET_MISDN
				frm->prim = PH_DATA_REQ;
				frm->id = 0;
				ret = sendto(p_m_mISDNport->b_socket[p_m_b_index], buf, MISDN_HEADER_LEN+ISDN_LOAD-p_m_load-tosend, 0, NULL, 0);
				if (ret <= 0)
					PERROR("Failed to send to socket %d (samples = %d)\n", p_m_mISDNport->b_socket[p_m_b_index], ISDN_LOAD-p_m_load-tosend);
#else
				frm->prim = DL_DATA | REQUEST; 
				frm->addr = p_m_mISDNport->b_addr[p_m_b_index] | FLG_MSG_DOWN;
				frm->dinfo = 0;
				frm->len = ISDN_LOAD - p_m_load - tosend;
				if (frm->len)
					mISDN_write(mISDNdevice, frm, mISDN_HEADER_LEN+frm->len, TIMEOUT_1SEC);
#endif
			}
			p_m_load += ISDN_LOAD - p_m_load - tosend;
		}
	}

	// NOTE: deletion is done by the child class

	/* handle timeouts */
	if (p_m_timeout)
	{
		if (p_m_timer+p_m_timeout < now_d)
		{
			PDEBUG(DEBUG_ISDN, "(%s) timeout after %d seconds detected (state=%d).\n", p_name, p_m_timeout, p_state);
			p_m_timeout = 0;
			/* send timeout to endpoint */
			message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_TIMEOUT);
			message->param.state = p_state;
			message_put(message);
			return(1);
		}
	}
	
	return(0); /* nothing done */
}


/*
 * whenever we get audio data from bchannel, we process it here
 */
#ifdef SOCKET_MISDN
void PmISDN::bchannel_receive(struct mISDNhead *hh, unsigned char *data, int len)
{
	unsigned long cont = *((unsigned long *)data);
#else
void PmISDN::bchannel_receive(iframe_t *frm)
{
	int len = frm->len;
	unsigned long cont = *((unsigned long *)&frm->data.p);
	unsigned char *data =(unsigned char *)&frm->data.p;
#endif
	unsigned char *data_temp;
	unsigned long length_temp;
	struct lcr_msg *message;
	unsigned char *p;
	int l;

#ifdef SOCKET_MISDN
	if (hh->prim == PH_CONTROL_IND)
#else
	if (frm->prim == (PH_CONTROL | INDICATION))
#endif
	{
		if (len < 4)
		{
			PERROR("SHORT READ OF PH_CONTROL INDICATION\n");
			return;
		}
		if ((cont&(~DTMF_TONE_MASK)) == DTMF_TONE_VAL)
		{
			chan_trace_header(p_m_mISDNport, this, "BCHANNEL control", DIRECTION_IN);
			add_trace("DTMF", NULL, "%c", cont & DTMF_TONE_MASK);
			end_trace();
			message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_DTMF);
			message->param.dtmf = cont & DTMF_TONE_MASK;
			PDEBUG(DEBUG_PORT, "PmISDN(%s) PH_CONTROL INDICATION  DTMF digit '%c'\n", p_name, message->param.dtmf);
			message_put(message);
			return;
		}
		switch(cont)
		{
#ifdef SOCKET_MISDN
			case DSP_BF_REJECT:
#else
			case BF_REJECT:
#endif
			chan_trace_header(p_m_mISDNport, this, "BCHANNEL control", DIRECTION_IN);
			add_trace("DSP-CRYPT", NULL, "error");
			end_trace();
			message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_CRYPT);
			message->param.crypt.type = CC_ERROR_IND;
			PDEBUG(DEBUG_PORT, "PmISDN(%s) PH_CONTROL INDICATION  reject of blowfish.\n", p_name);
			message_put(message);
			break;

#ifdef SOCKET_MISDN
			case DSP_BF_ACCEPT:
#else
			case BF_ACCEPT:
#endif
			chan_trace_header(p_m_mISDNport, this, "BCHANNEL control", DIRECTION_IN);
			add_trace("DSP-CRYPT", NULL, "ok");
			end_trace();
			message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_CRYPT);
			message->param.crypt.type = CC_ACTBF_CONF;
			PDEBUG(DEBUG_PORT, "PmISDN(%s) PH_CONTROL INDICATION  accept of blowfish.\n", p_name);
			message_put(message);
			break;

			default:
			chan_trace_header(p_m_mISDNport, this, "BCHANNEL control", DIRECTION_IN);
			add_trace("unknown", NULL, "0x%x", cont);
			end_trace();
		}
		return;
	}
#ifdef SOCKET_MISDN
	if (hh->prim == PH_CONTROL_IND)
	{
		switch(hh->id)
#else
	if (frm->prim == (PH_SIGNAL | INDICATION)
	 || frm->prim == (PH_CONTROL | INDICATION))
	{
		switch(frm->dinfo)
#endif
		{
#ifndef OLD_MISDN
#ifndef SOCKET_MISDN
			case CMX_TX_DATA:
			if (!p_m_txdata)
			{
				/* if tx is off, it may happen that fifos send us pending informations, we just ignore them */
				PDEBUG(DEBUG_BCHANNEL, "PmISDN(%s) ignoring tx data, because 'txdata' is turned off\n", p_name);
				return;
			}
			/* see below (same condition) */
			if (p_state!=PORT_STATE_CONNECT
				 && !p_m_mISDNport->tones)
				break;
//			printf(".");fflush(stdout);return;
			if (p_record)
				record(data, len, 1); // from up
			break;
#endif
#endif

			default:
			chan_trace_header(p_m_mISDNport, this, "BCHANNEL control", DIRECTION_IN);
#ifdef SOCKET_MISDN
			add_trace("unknown", NULL, "0x%x", hh->id);
#else
			add_trace("unknown", NULL, "0x%x", frm->dinfo);
#endif
			end_trace();
		}
		return;
	}
#ifdef SOCKET_MISDN
	if (hh->prim == PH_DATA_REQ || hh->prim == DL_DATA_REQ)
	{
		if (!p_m_txdata)
		{
			/* if tx is off, it may happen that fifos send us pending informations, we just ignore them */
			PDEBUG(DEBUG_BCHANNEL, "PmISDN(%s) ignoring tx data, because 'txdata' is turned off\n", p_name);
			return;
		}
		/* see below (same condition) */
		if (p_state!=PORT_STATE_CONNECT
			 && !p_m_mISDNport->tones)
			return;
//		printf(".");fflush(stdout);return;
		if (p_record)
			record(data, len, 1); // from up
		return;
	}
	if (hh->prim != PH_DATA_IND && hh->prim != DL_DATA_IND)
	{
		PERROR("Bchannel received unknown primitve: 0x%x\n", hh->prim);
#else
	if (frm->prim != (PH_DATA | INDICATION) && frm->prim != (DL_DATA | INDICATION))
	{
		PERROR("Bchannel received unknown primitve: 0x%x\n", frm->prim);
#endif
		return;
	}
	/* calls will not process any audio data unless
	 * the call is connected OR interface features audio during call setup.
	 */
//printf("%d -> %d prim=%x joindata=%d tones=%d\n", p_serial, ACTIVE_EPOINT(p_epointlist), frm->prim, p_m_joindata, p_m_mISDNport->earlyb);	
#ifndef DEBUG_COREBRIDGE
	if (p_state!=PORT_STATE_CONNECT
	 && !p_m_mISDNport->tones)
		return;
#endif

	/* if rx is off, it may happen that fifos send us pending informations, we just ignore them */
	if (p_m_rxoff)
	{
		PDEBUG(DEBUG_BCHANNEL, "PmISDN(%s) ignoring data, because rx is turned off\n", p_name);
		return;
	}

	/* record data */
	if (p_record)
		record(data, len, 0); // from down

	/* randomize and listen to crypt message if enabled */
	if (p_m_crypt_listen)
	{
		/* the noisy randomizer */
		p = data;
		l = len;
		while(l--)
			mISDN_rand[mISDN_rand_count & 0xff] += *p++;

		cryptman_listen_bch(data, len);
	}

	p = data;

	/* send data to epoint */
	if (p_m_joindata && ACTIVE_EPOINT(p_epointlist)) /* only if we have an epoint object */
	{
		length_temp = len;
		data_temp = p;
		while(length_temp)
		{
			message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_DATA);
			message->param.data.len = (length_temp>sizeof(message->param.data.data))?sizeof(message->param.data.data):length_temp;
			memcpy(message->param.data.data, data_temp, message->param.data.len);
			message_put(message);
			if (length_temp <= sizeof(message->param.data.data))
				break;
			data_temp += sizeof(message->param.data.data);
			length_temp -= sizeof(message->param.data.data);
		}
	}
}


/*
 * set echotest
 */
void PmISDN::set_echotest(int echo)
{
	if (p_m_echo != echo)
	{
		p_m_echo = echo;
		PDEBUG(DEBUG_ISDN, "we set echo to echo=%d.\n", p_m_echo);
		if (p_m_b_channel)
			if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE)
#ifdef SOCKET_MISDN
				ph_control(p_m_mISDNport, this, p_m_mISDNport->b_socket[p_m_b_index], p_m_echo?DSP_ECHO_ON:DSP_ECHO_OFF, 0, "DSP-ECHO", p_m_echo);
#else
				ph_control(p_m_mISDNport, this, p_m_mISDNport->b_addr[p_m_b_index], p_m_echo?CMX_ECHO_ON:CMX_ECHO_OFF, 0, "DSP-ECHO", p_m_echo);
#endif
	}
}

/*
 * set tone
 */
void PmISDN::set_tone(char *dir, char *tone)
{
	int id;

	if (!tone)
		tone = "";
	PDEBUG(DEBUG_ISDN, "isdn port now plays tone:'%s'.\n", tone);
	if (!tone[0])
	{
		id = TONE_OFF;
		goto setdsp;
	}

	/* check if we NOT really have to use a dsp-tone */
	if (!options.dsptones)
	{
		nodsp:
		if (p_m_tone)
		if (p_m_b_index > -1)
		if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE)
		{
			PDEBUG(DEBUG_ISDN, "we reset tone from id=%d to OFF.\n", p_m_tone);
#ifdef SOCKET_MISDN
			ph_control(p_m_mISDNport, this, p_m_mISDNport->b_socket[p_m_b_index], DSP_TONE_PATT_OFF, 0, "DSP-TONE", 0);
#else
			ph_control(p_m_mISDNport, this, p_m_mISDNport->b_addr[p_m_b_index], TONE_PATT_OFF, 0, "DSP-TONE", 0);
#endif
		}
		p_m_tone = 0;
		Port::set_tone(dir, tone);
		return;
	}
	if (p_tone_dir[0])
		goto nodsp;

	/* now we USE dsp-tone, convert name */
	else if (!strcmp(tone, "dialtone"))
	{
		switch(options.dsptones) {
		case DSP_AMERICAN: id = TONE_AMERICAN_DIALTONE; break;
		case DSP_GERMAN: id = TONE_GERMAN_DIALTONE; break;
		case DSP_OLDGERMAN: id = TONE_GERMAN_OLDDIALTONE; break;
		}
	} else if (!strcmp(tone, "dialpbx"))
	{
		switch(options.dsptones) {
		case DSP_AMERICAN: id = TONE_AMERICAN_DIALPBX; break;
		case DSP_GERMAN: id = TONE_GERMAN_DIALPBX; break;
		case DSP_OLDGERMAN: id = TONE_GERMAN_OLDDIALPBX; break;
		}
	} else if (!strcmp(tone, "ringing"))
	{
		switch(options.dsptones) {
		case DSP_AMERICAN: id = TONE_AMERICAN_RINGING; break;
		case DSP_GERMAN: id = TONE_GERMAN_RINGING; break;
		case DSP_OLDGERMAN: id = TONE_GERMAN_OLDRINGING; break;
		}
	} else if (!strcmp(tone, "ringpbx"))
	{
		switch(options.dsptones) {
		case DSP_AMERICAN: id = TONE_AMERICAN_RINGPBX; break;
		case DSP_GERMAN: id = TONE_GERMAN_RINGPBX; break;
		case DSP_OLDGERMAN: id = TONE_GERMAN_OLDRINGPBX; break;
		}
	} else if (!strcmp(tone, "busy"))
	{
		busy:
		switch(options.dsptones) {
		case DSP_AMERICAN: id = TONE_AMERICAN_BUSY; break;
		case DSP_GERMAN: id = TONE_GERMAN_BUSY; break;
		case DSP_OLDGERMAN: id = TONE_GERMAN_OLDBUSY; break;
		}
	} else if (!strcmp(tone, "release"))
	{
		hangup:
		switch(options.dsptones) {
		case DSP_AMERICAN: id = TONE_AMERICAN_HANGUP; break;
		case DSP_GERMAN: id = TONE_GERMAN_HANGUP; break;
		case DSP_OLDGERMAN: id = TONE_GERMAN_OLDHANGUP; break;
		}
	} else if (!strcmp(tone, "cause_10"))
		goto hangup;
	else if (!strcmp(tone, "cause_11"))
		goto busy;
	else if (!strcmp(tone, "cause_22"))
	{
		switch(options.dsptones) {
		case DSP_AMERICAN: id = TONE_SPECIAL_INFO; break;
		case DSP_GERMAN: id = TONE_GERMAN_GASSENBESETZT; break;
		case DSP_OLDGERMAN: id = TONE_GERMAN_OLDBUSY; break;
		}
	} else if (!strncmp(tone, "cause_", 6))
		id = TONE_SPECIAL_INFO;
	else
		id = TONE_OFF;

	/* if we have a tone that is not supported by dsp */
	if (id==TONE_OFF && tone[0])
		goto nodsp;

	setdsp:
	if (p_m_tone != id)
	{
		/* set new tone */
		p_m_tone = id;
		PDEBUG(DEBUG_ISDN, "we set tone to id=%d.\n", p_m_tone);
		if (p_m_b_index > -1)
		if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE)
#ifdef SOCKET_MISDN
			ph_control(p_m_mISDNport, this, p_m_mISDNport->b_socket[p_m_b_index], p_m_tone?DSP_TONE_PATT_ON:DSP_TONE_PATT_OFF, p_m_tone, "DSP-TONE", p_m_tone);
#else
			ph_control(p_m_mISDNport, this, p_m_mISDNport->b_addr[p_m_b_index], p_m_tone?TONE_PATT_ON:TONE_PATT_OFF, p_m_tone, "DSP-TONE", p_m_tone);
#endif
	}
	/* turn user-space tones off in cases of no tone OR dsp tone */
	Port::set_tone("",NULL);
}


/* MESSAGE_mISDNSIGNAL */
//extern struct lcr_msg *dddebug;
void PmISDN::message_mISDNsignal(unsigned long epoint_id, int message_id, union parameter *param)
{
	switch(param->mISDNsignal.message)
	{
		case mISDNSIGNAL_VOLUME:
		if (p_m_tx_gain != param->mISDNsignal.tx_gain)
		{
			p_m_tx_gain = param->mISDNsignal.tx_gain;
			PDEBUG(DEBUG_BCHANNEL, "we change tx-volume to shift=%d.\n", p_m_tx_gain);
			if (p_m_b_index > -1)
			if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE)
#ifdef SOCKET_MISDN
				ph_control(p_m_mISDNport, this, p_m_mISDNport->b_socket[p_m_b_index], DSP_VOL_CHANGE_TX, p_m_tx_gain, "DSP-TX_GAIN", p_m_tx_gain);
#else
				ph_control(p_m_mISDNport, this, p_m_mISDNport->b_addr[p_m_b_index], VOL_CHANGE_TX, p_m_tx_gain, "DSP-TX_GAIN", p_m_tx_gain);
#endif
		} else
			PDEBUG(DEBUG_BCHANNEL, "we already have tx-volume shift=%d.\n", p_m_rx_gain);
		if (p_m_rx_gain != param->mISDNsignal.rx_gain)
		{
			p_m_rx_gain = param->mISDNsignal.rx_gain;
			PDEBUG(DEBUG_BCHANNEL, "we change rx-volume to shift=%d.\n", p_m_rx_gain);
			if (p_m_b_index > -1)
			if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE)
#ifdef SOCKET_MISDN
				ph_control(p_m_mISDNport, this, p_m_mISDNport->b_socket[p_m_b_index], DSP_VOL_CHANGE_RX, p_m_rx_gain, "DSP-RX_GAIN", p_m_rx_gain);
#else
				ph_control(p_m_mISDNport, this, p_m_mISDNport->b_addr[p_m_b_index], VOL_CHANGE_RX, p_m_rx_gain, "DSP-RX_GAIN", p_m_rx_gain);
#endif
		} else
			PDEBUG(DEBUG_BCHANNEL, "we already have rx-volume shift=%d.\n", p_m_rx_gain);
		break;

		case mISDNSIGNAL_CONF:
//if (dddebug) PDEBUG(DEBUG_ISDN, "dddebug = %d\n", dddebug->type);
//tone		if (!p_m_tone && p_m_conf!=param->mISDNsignal.conf)
		if (p_m_conf != param->mISDNsignal.conf)
		{
			p_m_conf = param->mISDNsignal.conf;
			PDEBUG(DEBUG_BCHANNEL, "we change conference to conf=%d.\n", p_m_conf);
			if (p_m_b_index > -1)
			if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE)
#ifdef SOCKET_MISDN
				ph_control(p_m_mISDNport, this, p_m_mISDNport->b_socket[p_m_b_index], (p_m_conf)?DSP_CONF_JOIN:DSP_CONF_SPLIT, p_m_conf, "DSP-CONF", p_m_conf);
#else
				ph_control(p_m_mISDNport, this, p_m_mISDNport->b_addr[p_m_b_index], (p_m_conf)?CMX_CONF_JOIN:CMX_CONF_SPLIT, p_m_conf, "DSP-CONF", p_m_conf);
#endif
		} else
			PDEBUG(DEBUG_BCHANNEL, "we already have conf=%d.\n", p_m_conf);
		/* we must set, even if currently tone forbids conf */
		p_m_conf = param->mISDNsignal.conf;
//if (dddebug) PDEBUG(DEBUG_ISDN, "dddebug = %d\n", dddebug->type);
		break;

		case mISDNSIGNAL_JOINDATA:
		if (p_m_joindata != param->mISDNsignal.joindata)
		{
			p_m_joindata = param->mISDNsignal.joindata;
			PDEBUG(DEBUG_BCHANNEL, "we change to joindata=%d.\n", p_m_joindata);
		} else
			PDEBUG(DEBUG_BCHANNEL, "we already have joindata=%d.\n", p_m_joindata);
		break;
		
		case mISDNSIGNAL_DELAY:
		if (p_m_delay != param->mISDNsignal.delay)
		{
			p_m_delay = param->mISDNsignal.delay;
			PDEBUG(DEBUG_BCHANNEL, "we change delay mode to delay=%d.\n", p_m_delay);
#ifndef OLD_MISDN
			if (p_m_b_index > -1)
			if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE)
#ifdef SOCKET_MISDN
				ph_control(p_m_mISDNport, this, p_m_mISDNport->b_socket[p_m_b_index], p_m_delay?DSP_DELAY:DSP_JITTER, p_m_delay, "DSP-DELAY", p_m_delay);
#else
				ph_control(p_m_mISDNport, this, p_m_mISDNport->b_addr[p_m_b_index], p_m_delay?CMX_DELAY:CMX_JITTER, p_m_delay, "DSP-DELAY", p_m_delay);
#endif
#endif
		} else
			PDEBUG(DEBUG_BCHANNEL, "we already have delay=%d.\n", p_m_delay);
		break;

		default:
		PERROR("PmISDN(%s) unsupported signal message %d.\n", p_name, param->mISDNsignal.message);
	}
}

/* MESSAGE_CRYPT */
void PmISDN::message_crypt(unsigned long epoint_id, int message_id, union parameter *param)
{
	struct lcr_msg *message;

	switch(param->crypt.type)
	{
		case CC_ACTBF_REQ:           /* activate blowfish */
		p_m_crypt = 1;
		p_m_crypt_key_len = param->crypt.len;
		if (p_m_crypt_key_len > (int)sizeof(p_m_crypt_key))
		{
			PERROR("PmISDN(%s) key too long %d > %d\n", p_name, p_m_crypt_key_len, sizeof(p_m_crypt_key));
			message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_CRYPT);
			message->param.crypt.type = CC_ERROR_IND;
			message_put(message);
			break;
		}
		memcpy(p_m_crypt_key, param->crypt.data, p_m_crypt_key_len);
		crypt_off:
		PDEBUG(DEBUG_BCHANNEL, "we set encryption to crypt=%d. (0 means OFF)\n", p_m_crypt);
		if (p_m_b_index > -1)
		if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE)
#ifdef SOCKET_MISDN
			ph_control_block(p_m_mISDNport, this, p_m_mISDNport->b_socket[p_m_b_index], p_m_crypt?DSP_BF_ENABLE_KEY:DSP_BF_DISABLE, p_m_crypt_key, p_m_crypt_key_len, "DSP-CRYPT", p_m_crypt_key_len);
#else
			ph_control_block(p_m_mISDNport, this, p_m_mISDNport->b_addr[p_m_b_index], p_m_crypt?BF_ENABLE_KEY:BF_DISABLE, p_m_crypt_key, p_m_crypt_key_len, "DSP-CRYPT", p_m_crypt_key_len);
#endif
		break;

		case CC_DACT_REQ:            /* deactivate session encryption */
		p_m_crypt = 0;
		goto crypt_off;
		break;

		case CR_LISTEN_REQ:          /* start listening to messages */
		p_m_crypt_listen = 1;
		p_m_crypt_listen_state = 0;
		break;

		case CR_UNLISTEN_REQ:        /* stop listening to messages */
		p_m_crypt_listen = 0;
		break;

		case CR_MESSAGE_REQ:         /* send message */
		p_m_crypt_msg_len = cryptman_encode_bch(param->crypt.data, param->crypt.len, p_m_crypt_msg, sizeof(p_m_crypt_msg));
		if (!p_m_crypt_msg_len)
		{
			PERROR("PmISDN(%s) message too long %d > %d\n", p_name, param->crypt.len-1, sizeof(p_m_crypt_msg));
			break;
		}
		p_m_crypt_msg_current = 0; /* reset */
		p_m_crypt_msg_loops = 6; /* enable */
#if 0
		/* disable txmix, or we get corrupt data due to audio process */
		if (p_m_txmix && p_m_b_index>=0)
		{
			PDEBUG(DEBUG_BCHANNEL, "for sending CR_MESSAGE_REQ, we reset txmix from txmix=%d.\n", p_m_txmix);
#ifdef SOCKET_MISDN
			ph_control(p_m_mISDNport, this, p_mISDNport->b_socket[p_m_b_index], DSP_MIX_OFF, 0, "DSP-TXMIX", 0);
#else
			ph_control(p_m_mISDNport, this, p_mISDNport->b_addr[p_m_b_index], CMX_MIX_OFF, 0, "DSP-TXMIX", 0);
#endif
		}
#endif
		break;

		default:
		PERROR("PmISDN(%s) unknown crypt message %d\n", p_name, param->crypt.type);
	}

}

/*
 * endpoint sends messages to the port
 */
int PmISDN::message_epoint(unsigned long epoint_id, int message_id, union parameter *param)
{
	if (Port::message_epoint(epoint_id, message_id, param))
		return(1);

	switch(message_id)
	{
		case MESSAGE_DATA: /* tx-data from upper layer */
		txfromup(param->data.data, param->data.len);
		return(1);

		case MESSAGE_mISDNSIGNAL: /* user command */
		PDEBUG(DEBUG_ISDN, "PmISDN(%s) received special ISDN SIGNAL %d.\n", p_name, param->mISDNsignal.message);
		message_mISDNsignal(epoint_id, message_id, param);
		return(1);

		case MESSAGE_CRYPT: /* crypt control command */
		PDEBUG(DEBUG_ISDN, "PmISDN(%s) received encryption command '%d'.\n", p_name, param->crypt.type);
		message_crypt(epoint_id, message_id, param);
		return(1);
	}

	return(0);
}


/*
 * main loop for processing messages from mISDN
 */
#ifdef SOCKET_MISDN
int mISDN_handler(void)
{
	int ret, work = 0;
	struct mISDNport *mISDNport;
	class PmISDN *isdnport;
	int i;
	unsigned char buffer[2048+MISDN_HEADER_LEN];
	struct mISDNhead *hh = (struct mISDNhead *)buffer;
	struct mbuffer *mb;
	struct l3_msg *l3m;

	/* process all ports */
	mISDNport = mISDNport_first;
	while(mISDNport)
	{
		/* process all bchannels */
		i = 0;
		while(i < mISDNport->b_num)
		{
			/* process timer events for bchannel handling */
			if (mISDNport->b_timer[i])
			{
				if (mISDNport->b_timer[i] <= now_d)
					bchannel_event(mISDNport, i, B_EVENT_TIMEOUT);
			}
			/* handle port of bchannel */
			isdnport=mISDNport->b_port[i];
			if (isdnport)
			{
				/* call bridges in user space OR crypto OR recording */
				if (isdnport->p_m_joindata || isdnport->p_m_crypt_msg_loops || isdnport->p_m_crypt_listen || isdnport->p_record)
				{
					/* rx IS required */
					if (isdnport->p_m_rxoff)
					{
						/* turn on RX */
						isdnport->p_m_rxoff = 0;
						PDEBUG(DEBUG_BCHANNEL, "%s: receive data is required, so we turn them on\n", __FUNCTION__);
						if (mISDNport->b_port[i] && mISDNport->b_state[i] == B_STATE_ACTIVE)
							ph_control(mISDNport, isdnport, mISDNport->b_socket[isdnport->p_m_b_index], DSP_RECEIVE_ON, 0, "DSP-RXOFF", 0);
						return(1);
					}
				} else
				{
					/* rx NOT required */
					if (!isdnport->p_m_rxoff)
					{
						/* turn off RX */
						isdnport->p_m_rxoff = 1;
						PDEBUG(DEBUG_BCHANNEL, "%s: receive data is not required, so we turn them off\n", __FUNCTION__);
						if (mISDNport->b_port[i] && mISDNport->b_state[i] == B_STATE_ACTIVE)
							ph_control(mISDNport, isdnport, mISDNport->b_socket[isdnport->p_m_b_index], DSP_RECEIVE_OFF, 0, "DSP-RXOFF", 1);
						return(1);
					}
				}
				/* recording */
				if (isdnport->p_record)
				{
					/* txdata IS required */
					if (!isdnport->p_m_txdata)
					{
						/* turn on RX */
						isdnport->p_m_txdata = 1;
						PDEBUG(DEBUG_BCHANNEL, "%s: transmit data is required, so we turn them on\n", __FUNCTION__);
						if (mISDNport->b_port[i] && mISDNport->b_state[i] == B_STATE_ACTIVE)
							ph_control(mISDNport, isdnport, mISDNport->b_socket[isdnport->p_m_b_index], DSP_TXDATA_ON, 0, "DSP-TXDATA", 1);
						return(1);
					}
				} else
				{
					/* txdata NOT required */
					if (isdnport->p_m_txdata)
					{
						/* turn off RX */
						isdnport->p_m_txdata = 0;
						PDEBUG(DEBUG_BCHANNEL, "%s: transmit data is not required, so we turn them off\n", __FUNCTION__);
						if (mISDNport->b_port[i] && mISDNport->b_state[i] == B_STATE_ACTIVE)
							ph_control(mISDNport, isdnport, mISDNport->b_socket[isdnport->p_m_b_index], DSP_TXDATA_OFF, 0, "DSP-TXDATA", 0);
						return(1);
					}
				}
			}

			/* handle message from bchannel */
			if (mISDNport->b_socket[i] > -1)
			{
				ret = recv(mISDNport->b_socket[i], buffer, sizeof(buffer), 0);
				if (ret >= (int)MISDN_HEADER_LEN)
				{
					work = 1;
					switch(hh->prim)
					{
						/* we don't care about confirms, we use rx data to sync tx */
						case PH_DATA_CNF:
						break;

						/* we receive audio data, we respond to it AND we send tones */
						case PH_DATA_IND:
						case DL_DATA_IND:
						case PH_DATA_REQ:
						case DL_DATA_REQ:
						case PH_CONTROL_IND:
						if (mISDNport->b_port[i])
							mISDNport->b_port[i]->bchannel_receive(hh, buffer+MISDN_HEADER_LEN, ret-MISDN_HEADER_LEN);
						else
							PDEBUG(DEBUG_BCHANNEL, "b-channel is not associated to an ISDNPort (socket %d), ignoring.\n", mISDNport->b_socket[i]);
						break;

						case PH_ACTIVATE_IND:
						case DL_ESTABLISH_IND:
						case PH_ACTIVATE_CNF:
						case DL_ESTABLISH_CNF:
						PDEBUG(DEBUG_BCHANNEL, "DL_ESTABLISH confirm: bchannel is now activated (socket %d).\n", mISDNport->b_socket[i]);
						bchannel_event(mISDNport, i, B_EVENT_ACTIVATED);
						break;

						case PH_DEACTIVATE_IND:
						case DL_RELEASE_IND:
						case PH_DEACTIVATE_CNF:
						case DL_RELEASE_CNF:
						PDEBUG(DEBUG_BCHANNEL, "DL_RELEASE confirm: bchannel is now de-activated (socket %d).\n", mISDNport->b_socket[i]);
						bchannel_event(mISDNport, i, B_EVENT_DEACTIVATED);
						break;

						default:
						PERROR("child message not handled: prim(0x%x) socket(%d) msg->len(%d)\n", hh->prim, mISDNport->b_socket[i], ret-MISDN_HEADER_LEN);
					}
				} else
				{
					if (ret < 0 && errno != EWOULDBLOCK)
						PERROR("Read from port %d, index %d failed with return code %d\n", mISDNport->portnum, i, ret);
				}
			}
			
			i++;
		}

		/* handle queued up-messages (d-channel) */
		while ((mb = mdequeue(&mISDNport->upqueue)))
		{
			l3m = &mb->l3;
			switch(l3m->type)
			{
				case MPH_ACTIVATE_IND:
				l1l2l3_trace_header(mISDNport, NULL, L1_ACTIVATE_IND, DIRECTION_IN);
				end_trace();
				mISDNport->l1link = 1;
				break;
	
				case MPH_DEACTIVATE_IND:
				l1l2l3_trace_header(mISDNport, NULL, L1_DEACTIVATE_IND, DIRECTION_IN);
				end_trace();
				mISDNport->l1link = 0;
				break;

				case MPH_INFORMATION_IND:
				PDEBUG(DEBUG_ISDN, "Received MPH_INFORMATION_IND for port %d (%s).\n", mISDNport->portnum, mISDNport->ifport->interface->name);
				switch (l3m->pid)
				{
					case L1_SIGNAL_LOS_ON:
					mISDNport->los = 1;
					break;
					case L1_SIGNAL_LOS_OFF:
					mISDNport->los = 0;
					break;
					case L1_SIGNAL_AIS_ON:
					mISDNport->ais = 1;
					break;
					case L1_SIGNAL_AIS_OFF:
					mISDNport->ais = 0;
					break;
					case L1_SIGNAL_RDI_ON:
					mISDNport->rdi = 1;
					break;
					case L1_SIGNAL_RDI_OFF:
					mISDNport->rdi = 0;
					break;
					case L1_SIGNAL_SLIP_TX:
					mISDNport->slip_tx++;
					break;
					case L1_SIGNAL_SLIP_RX:
					mISDNport->slip_rx++;
					break;
				}
				break;

				case MT_L2ESTABLISH:
				l1l2l3_trace_header(mISDNport, NULL, L2_ESTABLISH_IND, DIRECTION_IN);
				add_trace("tei", NULL, "%d", l3m->pid);
				end_trace();
				if ((!mISDNport->ntmode || mISDNport->ptp) && l3m->pid < 127)
				{
					if (mISDNport->l2establish)
					{
						mISDNport->l2establish = 0;
						PDEBUG(DEBUG_ISDN, "the link became active before l2establish timer expiry.\n");
					}
					mISDNport->l2link = 1;
				}
				break;

				case MT_L2RELEASE:
				l1l2l3_trace_header(mISDNport, NULL, L2_RELEASE_IND, DIRECTION_IN);
				add_trace("tei", NULL, "%d", l3m->pid);
				end_trace();
				if ((!mISDNport->ntmode || mISDNport->ptp) && l3m->pid < 127)
				{
					mISDNport->l2link = 0;
					if (mISDNport->l2hold)
					{
						time(&mISDNport->l2establish);
						PDEBUG(DEBUG_ISDN, "because we are ptp, we set a l2establish timer.\n");
					}
				}
				break;

				default:
				/* l3-data is sent to LCR */
				stack2manager(mISDNport, l3m->type, l3m->pid, l3m);
			}
			/* free message */
			free_l3_msg(l3m);
		}

#if 0
		if (mISDNport->l1timeout && now>mISDNport->l1timeout)
		{ ---}
			PDEBUG(DEBUG_ISDN, "the L1 establish timer expired, we release all pending messages.\n", mISDNport->portnum);
			mISDNport->l1timeout = 0;
#endif

		/* layer 2 establish timer */
		if (mISDNport->l2establish)
		{
			if (now-mISDNport->l2establish > 5)
			{
				mISDNport->l2establish = 0;
				if (mISDNport->l2hold && (mISDNport->ptp || !mISDNport->ntmode))
				{

					PDEBUG(DEBUG_ISDN, "the L2 establish timer expired, we try to establish the link portnum=%d.\n", mISDNport->portnum);
					mISDNport->ml3->to_layer3(mISDNport->ml3, MT_L2ESTABLISH, 0, NULL);
					l1l2l3_trace_header(mISDNport, NULL, L2_ESTABLISH_REQ, DIRECTION_OUT);
					add_trace("tei", NULL, "%d", 0);
					end_trace();
					time(&mISDNport->l2establish);
					return(1);
				}
			}
		}


		mISDNport = mISDNport->next;
	}

	/* if we received at least one b-frame, we will return 1 */
	return(work);
}
#else
int mISDN_handler(void)
{
	int ret;
	struct mISDNport *mISDNport;
	class PmISDN *isdnport;
	int i;
	msg_t *msg;
	iframe_t *frm;
	msg_t *dmsg;
	mISDNuser_head_t *hh;
	net_stack_t *nst;

	/* the que avoids loopbacks when replying to stack after receiving
         * from stack. */
	mISDNport = mISDNport_first;
	while(mISDNport)
	{
		/* process turning on/off rx */
		i = 0;
		while(i < mISDNport->b_num)
		{
			/* process timer events for bchannel handling */
			if (mISDNport->b_timer[i])
			{
				if (mISDNport->b_timer[i] <= now_d)
					bchannel_event(mISDNport, i, B_EVENT_TIMEOUT);
			}
			isdnport=mISDNport->b_port[i];
			if (isdnport)
			{
				/* call bridges in user space OR crypto OR recording */
				if (isdnport->p_m_joindata || isdnport->p_m_crypt_msg_loops || isdnport->p_m_crypt_listen || isdnport->p_record)
				{
					/* rx IS required */
					if (isdnport->p_m_rxoff)
					{
						/* turn on RX */
						isdnport->p_m_rxoff = 0;
						PDEBUG(DEBUG_BCHANNEL, "%s: receive data is required, so we turn them on\n", __FUNCTION__);
						if (mISDNport->b_port[i] && mISDNport->b_state[i] == B_STATE_ACTIVE)
							ph_control(mISDNport, isdnport, mISDNport->b_addr[isdnport->p_m_b_index], CMX_RECEIVE_ON, 0, "DSP-RXOFF", 0);
						return(1);
					}
				} else
				{
					/* rx NOT required */
					if (!isdnport->p_m_rxoff)
					{
						/* turn off RX */
						isdnport->p_m_rxoff = 1;
						PDEBUG(DEBUG_BCHANNEL, "%s: receive data is not required, so we turn them off\n", __FUNCTION__);
						if (mISDNport->b_port[i] && mISDNport->b_state[i] == B_STATE_ACTIVE)
							ph_control(mISDNport, isdnport, mISDNport->b_addr[isdnport->p_m_b_index], CMX_RECEIVE_OFF, 0, "DSP-RXOFF", 1);
						return(1);
					}
				}
				/* recording */
				if (isdnport->p_record)
				{
					/* txdata IS required */
					if (!isdnport->p_m_txdata)
					{
						/* turn on RX */
						isdnport->p_m_txdata = 1;
						PDEBUG(DEBUG_BCHANNEL, "%s: transmit data is required, so we turn them on\n", __FUNCTION__);
#ifndef OLD_MISDN
						if (mISDNport->b_port[i] && mISDNport->b_state[i] == B_STATE_ACTIVE)
							ph_control(mISDNport, isdnport, mISDNport->b_addr[isdnport->p_m_b_index], CMX_TXDATA_ON, 0, "DSP-TXDATA", 1);
#endif
						return(1);
					}
				} else
				{
					/* txdata NOT required */
					if (isdnport->p_m_txdata)
					{
						/* turn off RX */
						isdnport->p_m_txdata = 0;
						PDEBUG(DEBUG_BCHANNEL, "%s: transmit data is not required, so we turn them off\n", __FUNCTION__);
#ifndef OLD_MISDN
						if (mISDNport->b_port[i] && mISDNport->b_state[i] == B_STATE_ACTIVE)
							ph_control(mISDNport, isdnport, mISDNport->b_addr[isdnport->p_m_b_index], CMX_TXDATA_OFF, 0, "DSP-TXDATA", 0);
#endif
						return(1);
					}
				}
			}
			i++;
		}
#if 0
		if (mISDNport->l1timeout && now>mISDNport->l1timeout)
		{ ---}
			PDEBUG(DEBUG_ISDN, "the L1 establish timer expired, we release all pending messages.\n", mISDNport->portnum);
			mISDNport->l1timeout = 0;
#endif

		if (mISDNport->l2establish)
		{
			if (now-mISDNport->l2establish > 5)
			{
				mISDNport->l2establish = 0;
				if (mISDNport->l2hold && (mISDNport->ptp || !mISDNport->ntmode))
				{
					if (mISDNport->ntmode)
					{
						PDEBUG(DEBUG_ISDN, "the L2 establish timer expired, we try to establish the link NT portnum=%d.\n", mISDNport->portnum);
						time(&mISDNport->l2establish);
						/* establish */
						dmsg = create_l2msg(DL_ESTABLISH | REQUEST, 0, 0);
						if (mISDNport->nst.manager_l3(&mISDNport->nst, dmsg))
							free_msg(dmsg);
					} else {
						iframe_t act;

						PDEBUG(DEBUG_ISDN, "the L2 establish timer expired, we try to establish the link TE portnum=%d.\n", mISDNport->portnum);
						time(&mISDNport->l2establish);
						/* establish */
						act.prim = DL_ESTABLISH | REQUEST; 
						act.addr = (mISDNport->upper_id & ~LAYER_ID_MASK) | 3 | FLG_MSG_DOWN;
						act.dinfo = 0;
						act.len = 0;
						mISDN_write(mISDNdevice, &act, mISDN_HEADER_LEN+act.len, TIMEOUT_1SEC);
					}
					l1l2l3_trace_header(mISDNport, NULL, L2_ESTABLISH_REQ, DIRECTION_OUT);
					add_trace("tei", NULL, "%d", 0);
					end_trace();
					time(&mISDNport->l2establish);
					return(1);
				}
			}
		}
		if ((dmsg = msg_dequeue(&mISDNport->downqueue)))
		{
			if (mISDNport->ntmode)
			{
				hh = (mISDNuser_head_t *)dmsg->data;
				PDEBUG(DEBUG_ISDN, "sending queued NT l3-down-message: prim(0x%x) dinfo(0x%x) msg->len(%d)\n", hh->prim, hh->dinfo, dmsg->len);
				if (mISDNport->nst.manager_l3(&mISDNport->nst, dmsg))
					free_msg(dmsg);
			} else
			{
				frm = (iframe_t *)dmsg->data;
				frm->addr = mISDNport->upper_id | FLG_MSG_DOWN;
				frm->len = (dmsg->len) - mISDN_HEADER_LEN;
				PDEBUG(DEBUG_ISDN, "sending queued TE l3-down-message: prim(0x%x) dinfo(0x%x) msg->len(%d)\n", frm->prim, frm->dinfo, dmsg->len);
				mISDN_write(mISDNdevice, dmsg->data, dmsg->len, TIMEOUT_1SEC);
				free_msg(dmsg);
			}
			return(1);
		}
		mISDNport = mISDNport->next;
	} 

	/* no device, no read */
	if (mISDNdevice < 0)
		return(0);

	/* get message from kernel */
	if (!(msg = alloc_msg(MAX_MSG_SIZE)))
		return(1);
	ret = mISDN_read(mISDNdevice, msg->data, MAX_MSG_SIZE, 0);
	if (ret < 0)
	{
		free_msg(msg);
		if (errno == EAGAIN)
			return(0);
		FATAL("Failed to do mISDN_read()\n");
	}
	if (!ret)
	{
		free_msg(msg);
//		printf("%s: ERROR: mISDN_read() returns nothing\n");
		return(0);
	}
	msg->len = ret;
	frm = (iframe_t *)msg->data;

	/* global prim */
	switch(frm->prim)
	{
		case MGR_DELLAYER | CONFIRM:
		case MGR_INITTIMER | CONFIRM:
		case MGR_ADDTIMER | CONFIRM:
		case MGR_DELTIMER | CONFIRM:
		case MGR_REMOVETIMER | CONFIRM:
		free_msg(msg);
		return(1);
	}

	/* handle timer events from mISDN for NT-stack
	 * note: they do not associate with a stack */
	if (frm->prim == (MGR_TIMER | INDICATION))
	{
		itimer_t *it;

		/* find mISDNport */
		mISDNport = mISDNport_first;
		while(mISDNport)
		{
			/* nt mode only */
			if (mISDNport->ntmode)
			{
				it = mISDNport->nst.tlist;
				/* find timer */
				while(it)
				{
					if (it->id == (int)frm->addr)
						break;
					it = it->next;
				}
				if (it)
					break;
			}
			mISDNport = mISDNport->next;
		}
		if (mISDNport)
		{
			mISDN_write_frame(mISDNdevice, msg->data, mISDNport->upper_id | FLG_MSG_DOWN,
				MGR_TIMER | RESPONSE, 0, 0, NULL, TIMEOUT_1SEC);

			PDEBUG(DEBUG_ISDN, "timer-indication port %d it=%p\n", mISDNport->portnum, it);
			test_and_clear_bit(FLG_TIMER_RUNING, (long unsigned int *)&it->Flags);
			ret = it->function(it->data);
		} else
		{
			PDEBUG(DEBUG_ISDN, "timer-indication not handled\n");
		}
		goto out;
	}

	/* find the mISDNport that belongs to the stack */
	mISDNport = mISDNport_first;
	while(mISDNport)
	{
		if ((frm->addr&MASTER_ID_MASK) == (unsigned int)(mISDNport->upper_id&MASTER_ID_MASK))
			break;
		mISDNport = mISDNport->next;
	} 
	if (!mISDNport)
	{
		PERROR("message belongs to no mISDNport: prim(0x%x) addr(0x%x) msg->len(%d)\n", frm->prim, frm->addr, msg->len);
		// show a list of all mISDNports and their address
#if 0
		mISDNport = mISDNport_first;
		while(mISDNport)
		{
			PERROR(" port %s  %x -> %x\n", mISDNport->name, (frm->addr&MASTER_ID_MASK), (unsigned int)(mISDNport->upper_id&MASTER_ID_MASK));
			mISDNport = mISDNport->next;
		} 
#endif
		goto out;
	}

	/* master stack */
	if (!(frm->addr&FLG_CHILD_STACK))
	{
		/* d-message */
		switch(frm->prim)
		{
			case MGR_SHORTSTATUS | INDICATION:
			case MGR_SHORTSTATUS | CONFIRM:
			switch(frm->dinfo) {
				case SSTATUS_L1_ACTIVATED:
				l1l2l3_trace_header(mISDNport, NULL, L1_ACTIVATE_REQ | (frm->prim & 0x3), DIRECTION_IN);
				end_trace();
				goto ss_act;
				case SSTATUS_L1_DEACTIVATED:
				l1l2l3_trace_header(mISDNport, NULL, L1_DEACTIVATE_REQ | (frm->prim & 0x3), DIRECTION_IN);
				end_trace();
				goto ss_deact;
				case SSTATUS_L2_ESTABLISHED:
				l1l2l3_trace_header(mISDNport, NULL, L2_ESTABLISH_REQ | (frm->prim & 0x3), DIRECTION_IN);
				end_trace();
				goto ss_estab;
				case SSTATUS_L2_RELEASED:
				l1l2l3_trace_header(mISDNport, NULL, L2_RELEASE_REQ | (frm->prim & 0x3), DIRECTION_IN);
				end_trace();
				goto ss_rel;
			}
			break;

			case PH_ACTIVATE | CONFIRM:
			case PH_ACTIVATE | INDICATION:
			l1l2l3_trace_header(mISDNport, NULL, L1_ACTIVATE_REQ | (frm->prim & 0x3), DIRECTION_IN);
			end_trace();
			if (mISDNport->ntmode)
			{
				mISDNport->l1link = 1;
				setup_queue(mISDNport, 1);
				goto l1_msg;
			}
			ss_act:
			mISDNport->l1link = 1;
			setup_queue(mISDNport, 1);
			break;

			case PH_DEACTIVATE | CONFIRM:
			case PH_DEACTIVATE | INDICATION:
			l1l2l3_trace_header(mISDNport, NULL, L1_DEACTIVATE_REQ | (frm->prim & 0x3), DIRECTION_IN);
			end_trace();
			if (mISDNport->ntmode)
			{
				mISDNport->l1link = 0;
				setup_queue(mISDNport, 0);
				goto l1_msg;
			}
			ss_deact:
			mISDNport->l1link = 0;
			setup_queue(mISDNport, 0);
			break;

			case PH_CONTROL | CONFIRM:
			case PH_CONTROL | INDICATION:
			PDEBUG(DEBUG_ISDN, "Received PH_CONTROL for port %d (%s).\n", mISDNport->portnum, mISDNport->ifport->interface->name);
			break;

			case DL_ESTABLISH | INDICATION:
			case DL_ESTABLISH | CONFIRM:
			l1l2l3_trace_header(mISDNport, NULL, DL_ESTABLISH_REQ | (frm->prim & 0x3), DIRECTION_IN);
			end_trace();
			if (!mISDNport->ntmode) break; /* !!!!!!!!!!!!!!!! */
			ss_estab:
			if (!mISDNport->ntmode || mISDNport->ptp)
			{
				if (mISDNport->l2establish)
				{
					mISDNport->l2establish = 0;
					PDEBUG(DEBUG_ISDN, "the link became active before l2establish timer expiry.\n");
				}
				mISDNport->l2link = 1;
			}
			break;

			case DL_RELEASE | INDICATION:
			case DL_RELEASE | CONFIRM:
			l1l2l3_trace_header(mISDNport, NULL, DL_RELEASE_REQ | (frm->prim & 0x3), DIRECTION_IN);
			end_trace();
			if (!mISDNport->ntmode) break; /* !!!!!!!!!!!!!!!! */
			ss_rel:
			if (!mISDNport->ntmode || mISDNport->ptp)
			{
				mISDNport->l2link = 0;
				if (mISDNport->l2hold)
				{
					time(&mISDNport->l2establish);
					PDEBUG(DEBUG_ISDN, "because we are ptp, we set a l2establish timer.\n");
				}
			}
			break;

			default:
			l1_msg:
			PDEBUG(DEBUG_STACK, "GOT d-msg from %s port %d prim 0x%x dinfo 0x%x addr 0x%x\n", (mISDNport->ntmode)?"NT":"TE", mISDNport->portnum, frm->prim, frm->dinfo, frm->addr);
			if (frm->dinfo==(signed long)0xffffffff && frm->prim==(PH_DATA|CONFIRM))
			{
				PERROR("SERIOUS BUG, dinfo == 0xffffffff, prim == PH_DATA | CONFIRM !!!!\n");
			}
			/* d-message */
			if (mISDNport->ntmode)
			{
				/* l1-data enters the nt-mode library */
				nst = &mISDNport->nst;
				if (nst->l1_l2(nst, msg))
					free_msg(msg);
				return(1);
			} else
			{
				/* l3-data is sent to pbx */
				if (stack2manager_te(mISDNport, msg))
					free_msg(msg);
				return(1);
			}
		}
	} else
	/* child stack */
	{
		/* b-message */
		switch(frm->prim)
		{
			/* we don't care about confirms, we use rx data to sync tx */
			case PH_DATA | CONFIRM:
			case DL_DATA | CONFIRM:
			break;

			/* we receive audio data, we respond to it AND we send tones */
			case PH_DATA | INDICATION:
			case DL_DATA | INDICATION:
			case PH_CONTROL | INDICATION:
			case PH_SIGNAL | INDICATION:
			i = 0;
			while(i < mISDNport->b_num)
			{
				if ((unsigned int)(mISDNport->b_addr[i]&STACK_ID_MASK) == (frm->addr&STACK_ID_MASK))
					break;
				i++;
			}
			if (i == mISDNport->b_num)
			{
				PERROR("unhandled b-message (prim 0x%x address 0x%x).\n", frm->prim, frm->addr);
				break;
			}
			if (mISDNport->b_port[i])
			{
//PERROR("port sech: %s data\n", mISDNport->b_port[i]->p_name);
				mISDNport->b_port[i]->bchannel_receive(frm);
			} else
				PDEBUG(DEBUG_BCHANNEL, "b-channel is not associated to an ISDNPort (address 0x%x), ignoring.\n", frm->addr);
			break;

			case PH_ACTIVATE | INDICATION:
			case DL_ESTABLISH | INDICATION:
			case PH_ACTIVATE | CONFIRM:
			case DL_ESTABLISH | CONFIRM:
			PDEBUG(DEBUG_BCHANNEL, "DL_ESTABLISH confirm: bchannel is now activated (address 0x%x).\n", frm->addr);
			i = 0;
			while(i < mISDNport->b_num)
			{
				if ((unsigned int)(mISDNport->b_addr[i]&STACK_ID_MASK) == (frm->addr&STACK_ID_MASK))
					break;
				i++;
			}
			if (i == mISDNport->b_num)
			{
				PERROR("unhandled b-establish (prim 0x%x address 0x%x).\n", frm->prim, frm->addr);
				break;
			}
			bchannel_event(mISDNport, i, B_EVENT_ACTIVATED);
			break;

			case PH_DEACTIVATE | INDICATION:
			case DL_RELEASE | INDICATION:
			case PH_DEACTIVATE | CONFIRM:
			case DL_RELEASE | CONFIRM:
			PDEBUG(DEBUG_BCHANNEL, "DL_RELEASE confirm: bchannel is now de-activated (address 0x%x).\n", frm->addr);
			i = 0;
			while(i < mISDNport->b_num)
			{
				if ((unsigned int)(mISDNport->b_addr[i]&STACK_ID_MASK) == (frm->addr&STACK_ID_MASK))
					break;
				i++;
			}
			if (i == mISDNport->b_num)
			{
				PERROR("unhandled b-release (prim 0x%x address 0x%x).\n", frm->prim, frm->addr);
				break;
			}
			bchannel_event(mISDNport, i, B_EVENT_DEACTIVATED);
			break;

			default:
			PERROR("child message not handled: prim(0x%x) addr(0x%x) msg->len(%d)\n", frm->prim, frm->addr, msg->len);
		}
	}

	out:
	free_msg(msg);
	return(1);
}
#endif

#ifdef SOCKET_MISDN
int do_layer3(struct mlayer3 *ml3, unsigned int cmd, unsigned int pid, struct l3_msg *l3m)
{
	/* IMPORTAINT:
	 *
	 * l3m must be queued, except for MT_ASSIGN
	 *
	 */
	struct mISDNport *mISDNport = (struct mISDNport *)ml3->priv;
	struct mbuffer *mb;

	/* special MT_ASSIGN handling:
	 *
	 * if we request a PID from mlayer, we always do it while lcr is locked.
	 * therefore we must check the MT_ASSIGN reply first before we lock.
	 * this is because the MT_ASSIGN reply is received with the requesting
	 * process, not by the mlayer thread!
	 * this means, that the reply is sent during call of the request.
	 * we must check if we get a reply and we know that we lcr is currently
	 * locked.
	 */
	if (cmd==MT_ASSIGN && (pid&MISDN_PID_CR_FLAG) && (pid>>16)==MISDN_CES_MASTER)
	{
		/* let's do some checking if someone changes stack behaviour */
		if (mt_assign_pid != 0)
			FATAL("someone played with the mISDNuser stack. MT_ASSIGN not currently expected.\n");
		mt_assign_pid = pid;
		return(0);
	}
	
	/* queue message, create, if required */
	if (!l3m)
	{
		l3m = alloc_l3_msg();
		if (!l3m)
			FATAL("No memory for layer 3 message\n");
	}
	mb = container_of(l3m, struct mbuffer, l3);
	l3m->type = cmd;
	l3m->pid = pid;
	mqueue_tail(&mISDNport->upqueue, mb);
	return 0;
}

#endif


/*
 * global function to add a new card (port)
 */
struct mISDNport *mISDNport_open(int port, int ptp, int force_nt, int l2hold, struct interface *interface)
{
	int ret;
	struct mISDNport *mISDNport, **mISDNportp;
	int i, cnt;
	int pri, bri, pots;
	int nt, te;
#ifdef SOCKET_MISDN
//	struct mlayer3 *ml3;
	struct mISDN_devinfo devinfo;
	unsigned int protocol, prop;

	ret = ioctl(mISDNsocket, IMGETCOUNT, &cnt);
	if (ret < 0)
	{
		fprintf(stderr, "Cannot get number of mISDN devices. (ioctl IMGETCOUNT failed ret=%d)\n", ret);
		return(NULL);
	}
#else
	unsigned char buff[1025];
	iframe_t *frm = (iframe_t *)buff;
//	interface_info_t ii;
	net_stack_t *nst;
	manager_t *mgr;
	layer_info_t li;
	stack_info_t *stinf;

	/* query port's requirements */
	cnt = mISDN_get_stack_count(mISDNdevice);
#endif

	if (cnt <= 0)
	{
		PERROR_RUNTIME("Found no card. Please be sure to load card drivers.\n");
		return(NULL);
	}
	if (port>cnt || port<1)
	{
		PERROR_RUNTIME("Port (%d) given at 'ports' (options.conf) is out of existing port range (%d-%d)\n", port, 1, cnt);
		return(NULL);
	}

	pri = bri = pots = nt = te = 0;
#ifdef SOCKET_MISDN
	devinfo.id = port - 1;
	ret = ioctl(mISDNsocket, IMGETDEVINFO, &devinfo);
	if (ret < 0)
	{
		PERROR_RUNTIME("Cannot get device information for port %d. (ioctl IMGETDEVINFO failed ret=%d)\n", i, ret);
		return(NULL);
	}
	/* output the port info */
	if (devinfo.Dprotocols & (1 << ISDN_P_TE_S0))
	{
		bri = 1;
		te = 1;
	}
	if (devinfo.Dprotocols & (1 << ISDN_P_NT_S0))
	{
		bri = 1;
		nt = 1;
	}
	if (devinfo.Dprotocols & (1 << ISDN_P_TE_E1))
	{
		pri = 1;
		te = 1;
	}
	if (devinfo.Dprotocols & (1 << ISDN_P_NT_E1))
	{
		pri = 1;
		nt = 1;
	}
#ifdef ISDN_P_FXS
	if (devinfo.Dprotocols & (1 << ISDN_P_FXS))
	{
		pots = 1;
		te = 1;
	}
#endif
#ifdef ISDN_P_FXO
	if (devinfo.Dprotocols & (1 << ISDN_P_FXO))
	{
		pots = 1;
		nt = 1;
	}
#endif
	if (force_nt && !nt)
	{
		PERROR_RUNTIME("Port %d does not support NT-mode\n", port);
		return(NULL);
	}
	if (bri && pri)
	{
		PERROR_RUNTIME("Port %d supports BRI and PRI?? What kind of controller is that?. (Can't use this!)\n", port);
		return(NULL);
	}
	if (pots && !bri && !pri)
	{
		PERROR_RUNTIME("Port %d supports POTS, LCR does not!\n", port);
		return(NULL);
	}
	if (!bri && !pri)
	{
		PERROR_RUNTIME("Port %d does not support BRI nor PRI!\n", port);
		return(NULL);
	}
	if (!nt && !te)
	{
		PERROR_RUNTIME("Port %d does not support NT-mode nor TE-mode!\n", port);
		return(NULL);
	}
	/* set NT by turning off TE */
	if (force_nt && nt)
		te = 0;
	/* if TE an NT is supported (and not forced to NT), turn off NT */
	if (te && nt)
		nt = 0;
#else
	ret = mISDN_get_stack_info(mISDNdevice, port, buff, sizeof(buff));
	if (ret < 0)
	{
		PERROR_RUNTIME("Cannot get stack info for port %d (ret=%d)\n", port, ret);
		return(NULL);
	}
	stinf = (stack_info_t *)&frm->data.p;
	switch(stinf->pid.protocol[0] & ~ISDN_PID_FEATURE_MASK)
	{
		case ISDN_PID_L0_TE_S0:
		PDEBUG(DEBUG_ISDN, "TE-mode BRI S/T interface line\n");
		break;
		case ISDN_PID_L0_NT_S0:
		PDEBUG(DEBUG_ISDN, "NT-mode BRI S/T interface port\n");
		nt = 1;
		break;
		case ISDN_PID_L0_TE_E1:
		PDEBUG(DEBUG_ISDN, "TE-mode PRI E1  interface line\n");
		pri = 1;
		break;
		case ISDN_PID_L0_NT_E1:
		PDEBUG(DEBUG_ISDN, "LT-mode PRI E1  interface port\n");
		pri = 1;
		nt = 1;
		break;
		default:
		PERROR_RUNTIME("unknown port(%d) type 0x%08x\n", port, stinf->pid.protocol[0]);
		return(NULL);
	}
	if (nt)
	{
		/* NT */
		if (stinf->pid.protocol[1] == 0)
		{
			PERROR_RUNTIME("Given port %d: Missing layer 1 NT-mode protocol.\n", port);
			return(NULL);
		}
		if (stinf->pid.protocol[2])
		{
			PERROR_RUNTIME("Given port %d: Layer 2 protocol 0x%08x is detected, but not allowed for NT lib.\n", port, stinf->pid.protocol[2]);
			return(NULL);
		}
	}
	if (te)
	{
		/* TE */
		if (stinf->pid.protocol[1] == 0)
		{
			PERROR_RUNTIME("Given port %d: Missing layer 1 protocol.\n", port);
			return(NULL);
		}
		if (stinf->pid.protocol[2] == 0)
		{
			PERROR_RUNTIME("Given port %d: Missing layer 2 protocol.\n", port);
			return(NULL);
		}
		if (stinf->pid.protocol[3] == 0)
		{
			PERROR_RUNTIME("Given port %d: Missing layer 3 protocol.\n", port);
			return(NULL);
		} else
		{
			switch(stinf->pid.protocol[3] & ~ISDN_PID_FEATURE_MASK)
			{
				case ISDN_PID_L3_DSS1USER:
				break;

				default:
				PERROR_RUNTIME("Given port %d: own protocol 0x%08x", port,stinf->pid.protocol[3]);
				return(NULL);
			}
		}
		if (stinf->pid.protocol[4])
		{
			PERROR_RUNTIME("Given port %d: Layer 4 protocol not allowed.\n", port);
			return(NULL);
		}
	}
#endif

	/* add mISDNport structure */
	mISDNportp = &mISDNport_first;
	while(*mISDNportp)
		mISDNportp = &((*mISDNportp)->next);
	mISDNport = (struct mISDNport *)MALLOC(sizeof(struct mISDNport));
	pmemuse++;
	*mISDNportp = mISDNport;

	/* if pri, must set PTP */
	if (pri)
		ptp = 1;
	
	/* set l2hold */
	switch (l2hold)
	{
		case -1: // off
		l2hold = 0;
		break;
		case 1: // on
		l2hold = 1;
		break;
		default:
		if (ptp)
			l2hold = 1;
		else
			l2hold = 0;
		break;
	}
		
	/* allocate ressources of port */
#ifdef SOCKET_MISDN
	/* open layer 3 and init upqueue */
	protocol = (nt)?L3_PROTOCOL_DSS1_NET:L3_PROTOCOL_DSS1_USER;
	prop = (1 << MISDN_FLG_L2_CLEAN);
	if (ptp) // ptp forced
	       prop |= (1 << MISDN_FLG_PTP);
	if (nt) // supports hold/retrieve on nt-mode
	       prop |= (1 << MISDN_FLG_NET_HOLD);
	if (l2hold) // supports layer 2 hold
	       prop |= (1 << MISDN_FLG_L2_HOLD);
	/* queue must be initializes, because l3-thread may send messages during open_layer3() */
	mqueue_init(&mISDNport->upqueue);
	mISDNport->ml3 = open_layer3(port-1, protocol, prop , do_layer3, mISDNport);
	if (!mISDNport->ml3)
	{
		mqueue_purge(&mISDNport->upqueue);
		PERROR_RUNTIME("open_layer3() failed for port %d\n", port);
		start_trace(port,
		    	interface,
			NULL,
			NULL,
			DIRECTION_NONE,
			CATEGORY_CH,
			0,
			"PORT (open failed)");
		end_trace();
		mISDNport_close(mISDNport);
		return(NULL);
	}

#if 0
	/* if ntmode, establish L1 to send the tei removal during start */
	if (mISDNport->ntmode)
	{
		iframe_t act;
		/* L1 */
		act.prim = PH_ACTIVATE | REQUEST; 
		act.addr = mISDNport->upper_id | FLG_MSG_DOWN;
		printf("UPPER ID 0x%x, addr 0x%x\n",mISDNport->upper_id, act.addr);
		act.dinfo = 0;
		act.len = 0;
		mISDN_write(mISDNdevice, &act, mISDN_HEADER_LEN+act.len, TIMEOUT_1SEC);
		usleep(10000); /* to be sure, that l1 is up */
	}
#endif

	SCPY(mISDNport->name, devinfo.name);
	mISDNport->b_num = devinfo.nrbchan;
#else
	msg_queue_init(&mISDNport->downqueue);
	mISDNport->d_stid = stinf->id;
	PDEBUG(DEBUG_ISDN, "d_stid = 0x%x.\n", mISDNport->d_stid);

	/* create layer intance */
	memset(&li, 0, sizeof(li));
	UCPY(&li.name[0], (nt)?"net l2":"pbx l4");
	li.object_id = -1;
	li.extentions = 0;
	li.pid.protocol[nt?2:4] = (nt)?ISDN_PID_L2_LAPD_NET:ISDN_PID_L4_CAPI20;
	li.pid.layermask = ISDN_LAYER((nt?2:4));
	li.st = mISDNport->d_stid;
	ret = mISDN_new_layer(mISDNdevice, &li);
	if (ret)
	{
		PERROR("Cannot add layer %d of port %d (ret %d)\n", nt?2:4, port, ret);
		closeport:
		mISDNport_close(mISDNport);
		return(NULL);
	}
	mISDNport->upper_id = li.id;
	ret = mISDN_register_layer(mISDNdevice, mISDNport->d_stid, mISDNport->upper_id);
	if (ret)
	{
		PERROR("Cannot register layer %d of port %d\n", nt?2:4, port);
		goto closeport;
	}
	mISDNport->lower_id = mISDN_get_layerid(mISDNdevice, mISDNport->d_stid, nt?1:3); // id of lower layer (nt=1, te=3)
	if (mISDNport->lower_id < 0)
	{
		PERROR("Cannot get layer(%d) id of port %d\n", nt?1:3, port);
		goto closeport;
	}
	mISDNport->upper_id = mISDN_get_layerid(mISDNdevice, mISDNport->d_stid, nt?2:4); // id of uppermost layer (nt=2, te=4)
	if (mISDNport->upper_id < 0)
	{
		PERROR("Cannot get layer(%d) id of port %d\n", nt?2:4, port);
		goto closeport;
	}
	PDEBUG(DEBUG_ISDN, "Layer %d of port %d added.\n", nt?2:4, port);

	/* if ntmode, establish L1 to send the tei removal during start */
	if (mISDNport->ntmode)
	{
		iframe_t act;
		/* L1 */
		act.prim = PH_ACTIVATE | REQUEST; 
		act.addr = mISDNport->upper_id | FLG_MSG_DOWN;
		printf("UPPER ID 0x%x, addr 0x%x\n",mISDNport->upper_id, act.addr);
		act.dinfo = 0;
		act.len = 0;
		mISDN_write(mISDNdevice, &act, mISDN_HEADER_LEN+act.len, TIMEOUT_1SEC);
		usleep(10000); /* to be sure, that l1 is up */
	}

	/* create nst (nt-mode only) */
	if (nt)
	{
		mgr = &mISDNport->mgr;
		nst = &mISDNport->nst;

		mgr->nst = nst;
		nst->manager = mgr;

		nst->l3_manager = stack2manager_nt; /* messages from nt-mode */
		nst->device = mISDNdevice;
		nst->cardnr = port;
		nst->d_stid = mISDNport->d_stid;

		nst->feature = FEATURE_NET_HOLD;
		if (ptp)
			nst->feature |= FEATURE_NET_PTP;
		if (pri)
			nst->feature |= FEATURE_NET_CRLEN2 | FEATURE_NET_EXTCID;
#if 0
		i = 0;
		while(i < mISDNport->b_num)
		{
			nst->b_stid[i] = mISDNport->b_stid[i];
			i++;
		}
#endif
		nst->l1_id = mISDNport->lower_id;
		nst->l2_id = mISDNport->upper_id;

		/* phd */	
		msg_queue_init(&nst->down_queue);

		Isdnl2Init(nst);
		Isdnl3Init(nst);
	}

	mISDNport->b_num = stinf->childcnt;
#endif
	mISDNport->portnum = port;
	mISDNport->ntmode = nt;
	mISDNport->pri = pri;
	mISDNport->ptp = ptp;
	mISDNport->l2hold = l2hold;
	PDEBUG(DEBUG_ISDN, "Port has %d b-channels.\n", mISDNport->b_num);
	i = 0;
	while(i < mISDNport->b_num)
	{
		mISDNport->b_state[i] = B_STATE_IDLE;
#ifdef SOCKET_MISDN
		mISDNport->b_socket[i] = -1;
#else
		mISDNport->b_stid[i] = stinf->child[i];
		PDEBUG(DEBUG_ISDN, "b_stid[%d] = 0x%x.\n", i, mISDNport->b_stid[i]);
#endif
		i++;
	}

#ifdef SOCKET_MISDN
	/* if ptp, pull up the link */
	if (mISDNport->l2hold && (mISDNport->ptp || !mISDNport->ntmode))
	{
		mISDNport->ml3->to_layer3(mISDNport->ml3, MT_L2ESTABLISH, 0, NULL);
		l1l2l3_trace_header(mISDNport, NULL, L2_ESTABLISH_REQ, DIRECTION_OUT);
		add_trace("tei", NULL, "%d", 0);
		end_trace();
		time(&mISDNport->l2establish);
	}
#else
	/* if te-mode, query state link */
	if (!mISDNport->ntmode)
	{
		iframe_t act;
		/* L2 */
		PDEBUG(DEBUG_ISDN, "sending short status request for port %d.\n", port);
		act.prim = MGR_SHORTSTATUS | REQUEST; 
		act.addr = mISDNport->upper_id | MSG_BROADCAST;
		act.dinfo = SSTATUS_BROADCAST_BIT | SSTATUS_ALL;
		act.len = 0;
		mISDN_write(mISDNdevice, &act, mISDN_HEADER_LEN+act.len, TIMEOUT_1SEC);
	}
	/* if ptp AND te-mode, pull up the link */
	if (mISDNport->l2hold && !mISDNport->ntmode)
	{
		iframe_t act;
		/* L2 */
		act.prim = DL_ESTABLISH | REQUEST; 
		act.addr = (mISDNport->upper_id & ~LAYER_ID_MASK) | 4 | FLG_MSG_DOWN;
		act.dinfo = 0;
		act.len = 0;
		mISDN_write(mISDNdevice, &act, mISDN_HEADER_LEN+act.len, TIMEOUT_1SEC);

		l1l2l3_trace_header(mISDNport, NULL, L2_ESTABLISH_REQ, DIRECTION_OUT);
		end_trace();
		time(&mISDNport->l2establish);
	}
	/* if ptp AND nt-mode, pull up the link */
	if (mISDNport->l2hold && mISDNport->ntmode && mISDNport->ptp)
	{
		msg_t *dmsg;
		/* L2 */
		dmsg = create_l2msg(DL_ESTABLISH | REQUEST, 0, 0);
		if (mISDNport->nst.manager_l3(&mISDNport->nst, dmsg))
			free_msg(dmsg);
		l1l2l3_trace_header(mISDNport, NULL, L2_ESTABLISH_REQ, DIRECTION_OUT);
		end_trace();
		time(&mISDNport->l2establish);
	}
#endif

	/* initially, we assume that the link is down, exept for nt-ptmp */
	mISDNport->l2link = (mISDNport->ntmode && !mISDNport->ptp)?1:0;

	PDEBUG(DEBUG_BCHANNEL, "using 'mISDN_dsp.o' module\n");

	start_trace(mISDNport->portnum,
		    interface,
		    NULL,
		    NULL,
		    DIRECTION_NONE,
		    CATEGORY_CH,
		    0,
		    "PORT (open)");
	add_trace("mode", NULL, (mISDNport->ntmode)?"network":"terminal");
	add_trace("channels", NULL, "%d", mISDNport->b_num);
	end_trace();
	return(mISDNport);
}


/*
 * function to free ALL cards (ports)
 */
void mISDNport_close_all(void)
{
	/* free all ports */
	while(mISDNport_first)
		mISDNport_close(mISDNport_first);
}

/*
 * free only one port
 */
void mISDNport_close(struct mISDNport *mISDNport)
{
	struct mISDNport **mISDNportp;
	class Port *port;
	class PmISDN *isdnport;
#ifdef SOCKET_MISDN
#else
	net_stack_t *nst;
	unsigned char buf[32];
#endif
	int i;

	/* remove all port instance that are linked to this mISDNport */
	port = port_first;
	while(port)
	{
		if ((port->p_type&PORT_CLASS_MASK) == PORT_CLASS_mISDN)
		{
			isdnport = (class PmISDN *)port;
			if (isdnport->p_m_mISDNport)
			{
				PDEBUG(DEBUG_ISDN, "port %s uses mISDNport %d, destroying it.\n", isdnport->p_name, mISDNport->portnum);
				delete isdnport;
			}
		}
		port = port->next;
	}

	/* only if we are already part of interface */
	if (mISDNport->ifport)
	{
		start_trace(mISDNport->portnum,
			    mISDNport->ifport->interface,
			    NULL,
			    NULL,
			    DIRECTION_NONE,
			    CATEGORY_CH,
			    0,
			    "PORT (close)");
		end_trace();
	}

	/* free bchannels */
	i = 0;
	while(i < mISDNport->b_num)
	{
#ifdef SOCKET_MISDN
		if (mISDNport->b_socket[i] > -1)
#else
		if (mISDNport->b_addr[i])
#endif
		{
			_bchannel_destroy(mISDNport, i);
			PDEBUG(DEBUG_BCHANNEL, "freeing %s port %d bchannel (index %d).\n", (mISDNport->ntmode)?"NT":"TE", mISDNport->portnum, i);
		}
		i++;
	}

#ifdef SOCKET_MISDN
	/* close layer 3, if open and purge upqueue */
	if (mISDNport->ml3)
	{
		close_layer3(mISDNport->ml3);
		mqueue_purge(&mISDNport->upqueue);
	}
#else
	/* free ressources of port */
	msg_queue_purge(&mISDNport->downqueue);

	/* free stacks */
	if (mISDNport->ntmode)
	{
		nst = &mISDNport->nst;
		if (nst->manager) /* to see if initialized */
		{
			PDEBUG(DEBUG_STACK, "the following messages are ok: one L3 process always exists (broadcast process) and some L2 instances (broadcast + current telephone's instances)\n");
			cleanup_Isdnl3(nst);
			cleanup_Isdnl2(nst);

			/* phd */
			msg_queue_purge(&nst->down_queue);
			if (nst->phd_down_msg)
				FREE(nst->phd_down_msg, 0);
		}
	}

	PDEBUG(DEBUG_BCHANNEL, "freeing d-stack.\n");
	if (mISDNport->d_stid)
	{
		if (mISDNport->upper_id)
			mISDN_write_frame(mISDNdevice, buf, mISDNport->upper_id | FLG_MSG_DOWN, MGR_DELLAYER | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);
	}
#endif

	/* remove from list */
	mISDNportp = &mISDNport_first;
	while(*mISDNportp)
	{
		if (*mISDNportp == mISDNport)
		{
			*mISDNportp = (*mISDNportp)->next;
			mISDNportp = NULL;
			break;
		}
		mISDNportp = &((*mISDNportp)->next);
	}

	if (mISDNportp)
		FATAL("mISDNport not in list\n");
	
	FREE(mISDNport, sizeof(struct mISDNport));
	pmemuse--;

}


/*
 * global function to show all available isdn ports
 */
void mISDN_port_info(void)
{
	int ret;
	int i, ii;
	int useable, nt, te, pri, bri, pots;
#ifdef SOCKET_MISDN
	struct mISDN_devinfo devinfo;
	int sock;

	/* open mISDN */
	sock = socket(PF_ISDN, SOCK_RAW, ISDN_P_BASE);
	if (sock < 0)
	{
		fprintf(stderr, "Cannot open mISDN due to %s. (Does your Kernel support socket based mISDN?)\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* get number of stacks */
	i = 1;
	ret = ioctl(sock, IMGETCOUNT, &ii);
	if (ret < 0)
	{
		fprintf(stderr, "Cannot get number of mISDN devices. (ioctl IMGETCOUNT failed ret=%d)\n", ret);
		goto done;
	}
#else
	int p;
	unsigned char buff[1025];
	iframe_t *frm = (iframe_t *)buff;
	stack_info_t *stinf;
	int device;

	/* open mISDN */
	if ((device = mISDN_open()) < 0)
	{
		fprintf(stderr, "Cannot open mISDN device ret=%d errno=%d (%s) Check for mISDN modules!\nAlso did you create \"/dev/mISDN\"? Do: \"mknod /dev/mISDN c 46 0\"\n", device, errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* get number of stacks */
	i = 1;
	ii = mISDN_get_stack_count(device);
#endif
	printf("\n");
	if (ii <= 0)
	{
		printf("Found no card. Please be sure to load card drivers.\n");
		goto done;
	}

	/* loop the number of cards and get their info */
	while(i <= ii)
	{
		nt = te = bri = pri = pots = 0;
		useable = 0;

#ifdef SOCKET_MISDN
		devinfo.id = i - 1;
		ret = ioctl(sock, IMGETDEVINFO, &devinfo);
		if (ret < 0)
		{
			fprintf(stderr, "Cannot get device information for port %d. (ioctl IMGETDEVINFO failed ret=%d)\n", i, ret);
			break;
		}

		/* output the port info */
		printf("Port %2d name='%s': ", i, devinfo.name);
		if (devinfo.Dprotocols & (1 << ISDN_P_TE_S0))
		{
			bri = 1;
			te = 1;
		}
		if (devinfo.Dprotocols & (1 << ISDN_P_NT_S0))
		{
			bri = 1;
			nt = 1;
		}
		if (devinfo.Dprotocols & (1 << ISDN_P_TE_E1))
		{
			pri = 1;
			te = 1;
		}
		if (devinfo.Dprotocols & (1 << ISDN_P_NT_E1))
		{
			pri = 1;
			nt = 1;
		}
#ifdef ISDN_P_FXS
		if (devinfo.Dprotocols & (1 << ISDN_P_FXS))
		{
			pots = 1;
			te = 1;
		}
#endif
#ifdef ISDN_P_FXO
		if (devinfo.Dprotocols & (1 << ISDN_P_FXO))
		{
			pots = 1;
			nt = 1;
		}
#endif
		if ((te || nt) && (bri || pri || pots))
			useable = 1;

		if (te && nt && bri)
			printf("TE/NT-mode BRI S/T (for phone lines & phones)");
		if (te && !nt && bri)
			printf("TE-mode    BRI S/T (for phone lines)");
		if (nt && !te && bri)
			printf("NT-mode    BRI S/T (for phones)");
		if (te && nt && pri)
			printf("TE/NT-mode PRI E1  (for phone lines & E1 devices)");
		if (te && !nt && pri)
			printf("TE-mode    PRI E1  (for phone lines)");
		if (nt && !te && pri)
			printf("NT-mode    PRI E1  (for E1 devices)");
		if (te && nt && pots)
			printf("FXS/FXO    POTS    (for analog lines & phones)");
		if (te && !nt && pots)
			printf("FXS        POTS    (for analog lines)");
		if (nt && !te && pots)
			printf("FXO        POTS    (for analog phones)");
		if (pots)
		{
			useable = 0;
			printf("\n -> Analog interfaces are not supported.");
		} else
		if (!useable)
		{
			printf("unsupported interface protocol bits 0x%016x", devinfo.Dprotocols);
		}
		printf("\n");

		printf("  - %d B-channels\n", devinfo.nrbchan);
#else
		ret = mISDN_get_stack_info(device, i, buff, sizeof(buff));
		if (ret <= 0)
		{
			fprintf(stderr, "mISDN_get_stack_info() failed: port=%d error=%d\n", i, ret);
			break;
		}
		stinf = (stack_info_t *)&frm->data.p;

		/* output the port info */
		printf("Port %2d: ", i);
		switch(stinf->pid.protocol[0] & ~ISDN_PID_FEATURE_MASK)
		{
			case ISDN_PID_L0_TE_S0:
			useable = 1;
			te = 1;
			bri = 1;
			printf("TE-mode BRI S/T interface line (for phone lines)");
			break;
			case ISDN_PID_L0_NT_S0:
			useable = 1;
			nt = 1;
			bri = 1;
			printf("NT-mode BRI S/T interface port (for phones)");
			break;
			case ISDN_PID_L0_TE_E1:
			useable = 1;
			te = 1;
			pri = 1;
			printf("TE-mode PRI E1  interface line (for phone lines)");
			break;
			case ISDN_PID_L0_NT_E1:
			useable = 1;
			nt = 1;
			pri = 1;
			printf("NT-mode PRI E1  interface port (for E1 devices)");
			break;
			default:
			useable = 0;
			printf("unknown type 0x%08x",stinf->pid.protocol[0]);
		}
		printf("\n");

		if (nt)
		{
			if (stinf->pid.protocol[1] == 0)
			{
				useable = 0;
				printf(" -> Missing layer 1 NT-mode protocol.\n");
			}
			p = 2;
			while(p <= MAX_LAYER_NR) {
				if (stinf->pid.protocol[p])
				{
					useable = 0;
					printf(" -> Layer %d protocol 0x%08x is detected, port already in use by another application.\n", p, stinf->pid.protocol[p]);
				}
				p++;
			}
			if (useable)
			{
				if (pri)
					printf(" -> Interface is Point-To-Point (PRI).\n");
				else
					printf(" -> Interface can be Poin-To-Point/Multipoint.\n");
			}
		}
		if (te)
		{
			if (stinf->pid.protocol[1] == 0)
			{
				useable = 0;
				printf(" -> Missing layer 1 protocol.\n");
			}
			if (stinf->pid.protocol[2] == 0)
			{
				useable = 0;
				printf(" -> Missing layer 2 protocol.\n");
			}
			if (stinf->pid.protocol[2] & ISDN_PID_L2_DF_PTP)
			{
				printf(" -> Interface is Poin-To-Point.\n");
			}
			if (stinf->pid.protocol[3] == 0)
			{
				useable = 0;
				printf(" -> Missing layer 3 protocol.\n");
			} else
			{
				printf(" -> Protocol: ");
				switch(stinf->pid.protocol[3] & ~ISDN_PID_FEATURE_MASK)
				{
					case ISDN_PID_L3_DSS1USER:
					printf("DSS1 (Euro ISDN)");
					break;

					default:
					useable = 0;
					printf("unknown protocol 0x%08x",stinf->pid.protocol[3]);
				}
				printf("\n");
			}
			p = 4;
			while(p <= MAX_LAYER_NR) {
				if (stinf->pid.protocol[p])
				{
					useable = 0;
					printf(" -> Layer %d protocol 0x%08x is detected, port already in use by another application.\n", p, stinf->pid.protocol[p]);
				}
				p++;
			}
		}
		printf("  - %d B-channels\n", stinf->childcnt);
#endif

		if (!useable)
			printf(" * Port NOT useable for LCR\n");

		printf("--------\n");

		i++;
	}
	printf("\n");

done:
#ifdef SOCKET_MISDN
	close(sock);
#else
	/* close mISDN */
	if ((ret = mISDN_close(device)))
		FATAL("mISDN_close() failed: err=%d '%s'\n", ret, strerror(ret));
#endif
}


/*
 * enque data from upper buffer
 */
void PmISDN::txfromup(unsigned char *data, int length)
{
#ifdef SOCKET_MISDN
	unsigned char buf[MISDN_HEADER_LEN+((length>ISDN_LOAD)?length:ISDN_LOAD)];
	struct mISDNhead *hh = (struct mISDNhead *)buf;
	int ret;

	if (p_m_b_index < 0)
		return;
	if (p_m_mISDNport->b_state[p_m_b_index] != B_STATE_ACTIVE)
		return;
#else
	unsigned char buf[mISDN_HEADER_LEN+((length>ISDN_LOAD)?length:ISDN_LOAD)];
	iframe_t *frm = (iframe_t *)buf;

	if (p_m_b_index < 0)
		return;
	if (!p_m_mISDNport->b_addr[p_m_b_index])
		return;
#endif

	/* check if high priority tones exist
	 * ignore data in this case
	 */
	if (p_tone_name[0] || p_m_crypt_msg_loops)
		return;

	/* preload procedure
	 * if transmit buffer in DSP module is empty,
	 * preload it to DSP_LOAD to prevent jitter gaps.
	 */
	if (p_m_load==0 && ISDN_LOAD>0)
	{
#ifdef SOCKET_MISDN
		hh->prim = PH_DATA_REQ; 
		hh->id = 0;
		memset(buf+MISDN_HEADER_LEN, (options.law=='a')?0x2a:0xff, ISDN_LOAD);
		ret = sendto(p_m_mISDNport->b_socket[p_m_b_index], buf, MISDN_HEADER_LEN+ISDN_LOAD, 0, NULL, 0);
		if (ret <= 0)
			PERROR("Failed to send to socket %d\n", p_m_mISDNport->b_socket[p_m_b_index]);
#else
		frm->prim = DL_DATA | REQUEST; 
		frm->addr = p_m_mISDNport->b_addr[p_m_b_index] | FLG_MSG_DOWN;
		frm->dinfo = 0;
		frm->len = ISDN_LOAD;
		memset(buf+mISDN_HEADER_LEN, (options.law=='a')?0x2a:0xff, ISDN_LOAD);
		mISDN_write(mISDNdevice, frm, mISDN_HEADER_LEN+ISDN_LOAD, TIMEOUT_1SEC);
#endif
		p_m_load += ISDN_LOAD;
	}

	/* drop if load would exceed ISDN_MAXLOAD
	 * this keeps the delay not too high
	 */
	if (p_m_load+length > ISDN_MAXLOAD)
		return;

	/* make and send frame */
#ifdef SOCKET_MISDN
	hh->prim = PH_DATA_REQ;
	hh->id = 0;
	memcpy(buf+MISDN_HEADER_LEN, data, length);
	ret = sendto(p_m_mISDNport->b_socket[p_m_b_index], buf, MISDN_HEADER_LEN+length, 0, NULL, 0);
	if (ret <= 0)
		PERROR("Failed to send to socket %d\n", p_m_mISDNport->b_socket[p_m_b_index]);
#else
	frm->prim = DL_DATA | REQUEST; 
	frm->addr = p_m_mISDNport->b_addr[p_m_b_index] | FLG_MSG_DOWN;
	frm->dinfo = 0;
	frm->len = length;
	memcpy(buf+mISDN_HEADER_LEN, data, length);
	mISDN_write(mISDNdevice, frm, mISDN_HEADER_LEN+frm->len, TIMEOUT_1SEC);
#endif
	p_m_load += length;
}

