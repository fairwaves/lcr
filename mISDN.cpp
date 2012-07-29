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
#include <mISDN/q931.h>

#undef offsetof
#ifdef __compiler_offsetof
#define offsetof(TYPE,MEMBER) __compiler_offsetof(TYPE,MEMBER)
#else
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#ifndef container_of
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

#ifdef OLD_MT_ASSIGN
unsigned int mt_assign_pid = ~0;
#endif

int mISDNsocket = -1;
static int upqueue_pipe[2];
static struct lcr_fd upqueue_fd;
int upqueue_avail = 0;

static int mISDN_upqueue(struct lcr_fd *fd, unsigned int what, void *instance, int i);
static int mISDN_timeout(struct lcr_timer *timer, void *instance, int i);

static int my_mISDNlib_debug(const char *file, int line, const char *func, int level, const char *fmt, va_list va)
{
        int ret = 0;

        if (debug_fp > 0)
        	ret = vfprintf(debug_fp, fmt, va);
	return ret;
}

static struct mi_ext_fn_s myfn;

int mISDN_initialize(void)
{
	char filename[256];
	int ver;

	/* try to open raw socket to check kernel */
	mISDNsocket = socket(PF_ISDN, SOCK_RAW, ISDN_P_BASE);
	if (mISDNsocket < 0) {
		fprintf(stderr, "Cannot open mISDN due to '%s'. (Does your Kernel support socket based mISDN? Protocol family is %d.)\n", strerror(errno), PF_ISDN);
		return(-1);
	}

	/* init mlayer3 */
	// set debug printout function
	myfn.prt_debug = my_mISDNlib_debug;

	ver = init_layer3(4, &myfn); // buffer of 4

	/* open debug, if enabled and not only stack debugging */
	if (options.deb) {
		SPRINT(filename, "%s/debug.log", LOG_DIR);
		debug_fp = fopen(filename, "a");
	}

	if (options.deb & DEBUG_STACK)
		mISDN_set_debug_level(0xfffffeff);
	else
		mISDN_set_debug_level(0);

	if (pipe(upqueue_pipe) < 0)
		FATAL("Failed to open pipe\n");
	memset(&upqueue_fd, 0, sizeof(upqueue_fd));
	upqueue_fd.fd = upqueue_pipe[0];
	register_fd(&upqueue_fd, LCR_FD_READ, mISDN_upqueue, NULL, 0);

	return(0);
}

void mISDN_deinitialize(void)
{
	cleanup_layer3();

	if (debug_fp)
		fclose(debug_fp);
	debug_fp = NULL;

	if (mISDNsocket > -1)
		close(mISDNsocket);

	if (upqueue_fd.inuse) {
		unregister_fd(&upqueue_fd);
		close(upqueue_pipe[0]);
		close(upqueue_pipe[1]);
	}
	upqueue_avail = 0;
}

int load_timer(struct lcr_timer *timer, void *instance, int index);

/*
 * constructor
 */
PmISDN::PmISDN(int type, mISDNport *mISDNport, char *portname, struct port_settings *settings, int channel, int exclusive, int mode) : Port(type, portname, settings)
{
	p_m_mISDNport = mISDNport;
	p_m_portnum = mISDNport->portnum;
	p_m_b_index = -1;
	p_m_b_channel = 0;
	p_m_b_exclusive = 0;
	p_m_b_reserve = 0;
	p_m_b_mode = mode;
	p_m_hold = 0;
	p_m_tx_gain = mISDNport->ifport->interface->tx_gain;
	p_m_rx_gain = mISDNport->ifport->interface->rx_gain;
	p_m_conf = 0;
	p_m_mute = 0;
	p_m_txdata = 0;
	p_m_delay = 0;
	p_m_tx_dejitter = 0;
	p_m_preload = ISDN_LOAD;
	p_m_disable_dejitter = 0;
	p_m_echo = 0;
	p_m_tone = 0;
	p_m_rxoff = 0;
	p_m_inband_send_on = 0;
	p_m_inband_receive_on = 0;
	p_m_dtmf = !mISDNport->ifport->nodtmf;
	memset(&p_m_timeout, 0, sizeof(p_m_timeout));
	add_timer(&p_m_timeout, mISDN_timeout, this, 0);
	SCPY(p_m_pipeline, mISDNport->ifport->interface->pipeline);
	
	/* audio */
	memset(&p_m_loadtimer, 0, sizeof(p_m_loadtimer));
	add_timer(&p_m_loadtimer, load_timer, this, 0);
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
	if (mISDNport->ifport->interface->bf_len >= 4 && mISDNport->ifport->interface->bf_len <= 56) {
		memcpy(p_m_crypt_key, mISDNport->ifport->interface->bf_key, p_m_crypt_key_len);
		p_m_crypt_key_len = mISDNport->ifport->interface->bf_len;
		p_m_crypt = 1;
	}

	/* if any channel requested by constructor */
	if (channel == CHANNEL_ANY) {
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
//inband_receive_on();
}


/*
 * destructor
 */
PmISDN::~PmISDN()
{
	struct lcr_msg *message;

	del_timer(&p_m_timeout);
	del_timer(&p_m_loadtimer);

	/* remove bchannel relation */
	drop_bchannel();

	/* release epoint */
	while (p_epointlist) {
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
void chan_trace_header(struct mISDNport *mISDNport, class PmISDN *port, const char *msgtext, int direction)
{
	/* init trace with given values */
	start_trace(mISDNport?mISDNport->portnum:-1,
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
	const char *name;
	unsigned int value;
} isdn_message[] = {
	{"PH_ACTIVATE", L1_ACTIVATE_REQ},
	{"PH_DEACTIVATE", L1_DEACTIVATE_REQ},
	{"DL_ESTABLISH", L2_ESTABLISH_REQ},
	{"DL_RELEASE", L2_RELEASE_REQ},
	{"UNKNOWN", L3_UNKNOWN_REQ},
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
	{"MT_NEW_L3ID", L3_NEW_L3ID_REQ},
	{"MT_RELEASE_L3ID", L3_RELEASE_L3ID_REQ},
	{NULL, 0},
};
static const char *isdn_prim[4] = {
	" REQUEST",
	" CONFIRM",
	" INDICATION",
	" RESPONSE",
};
void l1l2l3_trace_header(struct mISDNport *mISDNport, class PmISDN *port, unsigned int msg, int direction)
{
	int i;
	char msgtext[64];

	SCPY(msgtext, "<<UNKNOWN MESSAGE>>");
	/* select message and primitive text */
	i = 0;
	while(isdn_message[i].name) {
//		if (msg == L3_NOTIFY_REQ) printf("val = %x %s\n", isdn_message[i].value, isdn_message[i].name);
		if (isdn_message[i].value == (msg&0xffffff00)) {
			SCPY(msgtext, isdn_message[i].name);
			break;
		}
		i++;
	}
	SCAT(msgtext, isdn_prim[msg&0x00000003]);

	/* add direction */
	if (direction && (msg&0xffffff00)!=L3_NEW_L3ID_REQ && (msg&0xffffff00)!=L3_RELEASE_L3ID_REQ) {
		if (mISDNport) {
			if (mISDNport->ntmode) {
				if (direction == DIRECTION_OUT)
					SCAT(msgtext, " N->U");
				else
					SCAT(msgtext, " N<-U");
			} else {
				if (direction == DIRECTION_OUT)
					SCAT(msgtext, " U->N");
				else
					SCAT(msgtext, " U<-N");
			}
		}
	}

	/* init trace with given values */
	start_trace(mISDNport?mISDNport->portnum:-1,
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
void ph_control(struct mISDNport *mISDNport, class PmISDN *isdnport, int sock, unsigned int c1, unsigned int c2, const char *trace_name, int trace_value)
{
	unsigned char buffer[MISDN_HEADER_LEN+sizeof(int)+sizeof(int)];
	struct mISDNhead *ctrl = (struct mISDNhead *)buffer;
	unsigned int *d = (unsigned int *)(buffer+MISDN_HEADER_LEN);
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
	chan_trace_header(mISDNport, isdnport, "BCHANNEL control", DIRECTION_OUT);
	if (c1 == DSP_CONF_JOIN)
		add_trace(trace_name, NULL, "0x%08x", trace_value);
	else
		add_trace(trace_name, NULL, "%d", trace_value);
	end_trace();
}

void ph_control_block(struct mISDNport *mISDNport, class PmISDN *isdnport, int sock, unsigned int c1, void *c2, int c2_len, const char *trace_name, int trace_value)
{
	unsigned char buffer[MISDN_HEADER_LEN+sizeof(int)+c2_len];
	struct mISDNhead *ctrl = (struct mISDNhead *)buffer;
	unsigned int *d = (unsigned int *)(buffer+MISDN_HEADER_LEN);
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
	chan_trace_header(mISDNport, isdnport, "BCHANNEL control", DIRECTION_OUT);
	add_trace(trace_name, NULL, "%d", trace_value);
	end_trace();
}

static int b_sock_callback(struct lcr_fd *fd, unsigned int what, void *instance, int i);

/*
 * subfunction for bchannel_event
 * create stack
 */
static int _bchannel_create(struct mISDNport *mISDNport, int i)
{
	int ret;
	struct sockaddr_mISDN addr;

	if (mISDNport->b_sock[i].inuse) {
		PERROR("Error: Socket already created for index %d\n", i);
		return(0);
	}

	/* open socket */
//#warning testing without DSP
//	mISDNport->b_sock[i].fd = socket(PF_ISDN, SOCK_DGRAM, (mISDNport->b_mode[i]==B_MODE_HDLC)?ISDN_P_B_HDLC:ISDN_P_B_RAW);
	mISDNport->b_sock[i].fd = socket(PF_ISDN, SOCK_DGRAM, (mISDNport->b_mode[i]==B_MODE_HDLC)?ISDN_P_B_L2DSPHDLC:ISDN_P_B_L2DSP);
	if (mISDNport->b_sock[i].fd < 0) {
		PERROR("Error: Failed to open bchannel-socket for index %d with mISDN-DSP layer. Did you load mISDN_dsp.ko?\n", i);
		return(0);
	}

	/* register callback for read */
	register_fd(&mISDNport->b_sock[i], LCR_FD_READ, b_sock_callback, mISDNport, i);
	
	/* bind socket to bchannel */
	addr.family = AF_ISDN;
	addr.dev = mISDNport->portnum;
	addr.channel = i+1+(i>=15);
	ret = bind(mISDNport->b_sock[i].fd, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		PERROR("Error: Failed to bind bchannel-socket for index %d with mISDN-DSP layer (errno=%d). Did you load mISDN_dsp.ko?\n", i, errno);
		close(mISDNport->b_sock[i].fd);
		unregister_fd(&mISDNport->b_sock[i]);
		return(0);
	}

	chan_trace_header(mISDNport, mISDNport->b_port[i], "BCHANNEL create socket", DIRECTION_OUT);
	add_trace("channel", NULL, "%d", i+1+(i>=15));
	add_trace("socket", NULL, "%d", mISDNport->b_sock[i].fd);
	end_trace();

	return(1);
}


/*
 * subfunction for bchannel_event
 * activate / deactivate request
 */
static void _bchannel_activate(struct mISDNport *mISDNport, int i, int activate, int timeout)
{
	struct mISDNhead act;
	int ret;

	if (!mISDNport->b_sock[i].inuse)
		return;
	act.prim = (activate)?PH_ACTIVATE_REQ:PH_DEACTIVATE_REQ; 
	act.id = 0;
	ret = sendto(mISDNport->b_sock[i].fd, &act, MISDN_HEADER_LEN, 0, NULL, 0);
	if (ret <= 0)
		PERROR("Failed to send to socket %d\n", mISDNport->b_sock[i].fd);

	/* trace */
	chan_trace_header(mISDNport, mISDNport->b_port[i], activate ? "BCHANNEL activate" : "BCHANNEL deactivate", DIRECTION_OUT);
	add_trace("channel", NULL, "%d", i+1+(i>=15));
	if (timeout)
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
	int handle, mode;

	if (!mISDNport->b_sock[i].inuse)
		return;
	handle = mISDNport->b_sock[i].fd;
	port = mISDNport->b_port[i];
	mode = mISDNport->b_mode[i];
	if (!port) {
		PERROR("bchannel index i=%d not associated with a port object\n", i);
		return;
	}

	/* set dsp features */
	if (port->p_m_txdata)
		ph_control(mISDNport, port, handle, (port->p_m_txdata)?DSP_TXDATA_ON:DSP_TXDATA_OFF, 0, "DSP-TXDATA", port->p_m_txdata);
	if (port->p_m_delay && mode == B_MODE_TRANSPARENT)
		ph_control(mISDNport, port, handle, DSP_DELAY, port->p_m_delay, "DSP-DELAY", port->p_m_delay);
	if (port->p_m_tx_dejitter && mode == B_MODE_TRANSPARENT)
		ph_control(mISDNport, port, handle, DSP_TX_DEJITTER, port->p_m_tx_dejitter, "DSP-TX_DEJITTER", port->p_m_tx_dejitter);
	if (port->p_m_tx_gain && mode == B_MODE_TRANSPARENT)
		ph_control(mISDNport, port, handle, DSP_VOL_CHANGE_TX, port->p_m_tx_gain, "DSP-TX_GAIN", port->p_m_tx_gain);
	if (port->p_m_rx_gain && mode == B_MODE_TRANSPARENT)
		ph_control(mISDNport, port, handle, DSP_VOL_CHANGE_RX, port->p_m_rx_gain, "DSP-RX_GAIN", port->p_m_rx_gain);
	if (port->p_m_pipeline[0] && mode == B_MODE_TRANSPARENT)
		ph_control_block(mISDNport, port, handle, DSP_PIPELINE_CFG, port->p_m_pipeline, strlen(port->p_m_pipeline)+1, "DSP-PIPELINE", 0);
	if (port->p_m_conf && !port->p_m_mute)
		ph_control(mISDNport, port, handle, DSP_CONF_JOIN, port->p_m_conf, "DSP-CONF", port->p_m_conf);
	if (port->p_m_echo)
		ph_control(mISDNport, port, handle, DSP_ECHO_ON, 0, "DSP-ECHO", 1);
	if (port->p_m_tone && mode == B_MODE_TRANSPARENT)
		ph_control(mISDNport, port, handle, DSP_TONE_PATT_ON, port->p_m_tone, "DSP-TONE", port->p_m_tone);
	if (port->p_m_rxoff)
		ph_control(mISDNport, port, handle, DSP_RECEIVE_OFF, 0, "DSP-RXOFF", 1);
//	if (port->p_m_txmix && mode == B_MODE_TRANSPARENT)
//		ph_control(mISDNport, port, handle, DSP_MIX_ON, 0, "DSP-MIX", 1);
	if (port->p_m_dtmf && mode == B_MODE_TRANSPARENT)
		ph_control(mISDNport, port, handle, DTMF_TONE_START, 0, "DSP-DTMF", 1);
	if (port->p_m_crypt && mode == B_MODE_TRANSPARENT)
		ph_control_block(mISDNport, port, handle, DSP_BF_ENABLE_KEY, port->p_m_crypt_key, port->p_m_crypt_key_len, "DSP-CRYPT", port->p_m_crypt_key_len);
}


void PmISDN::set_conf(int oldconf, int newconf)
{
		if (oldconf != newconf) {
			PDEBUG(DEBUG_BCHANNEL, "we change conference from conf=%d to conf=%d.\n", oldconf, newconf);
			if (p_m_b_index > -1)
			if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE)
				ph_control(p_m_mISDNport, this, p_m_mISDNport->b_sock[p_m_b_index].fd, (newconf)?DSP_CONF_JOIN:DSP_CONF_SPLIT, newconf, "DSP-CONF", newconf);
		} else
			PDEBUG(DEBUG_BCHANNEL, "we already have conf=%d.\n", newconf);
}


/*
 * subfunction for bchannel_event
 * destroy stack
 */
static void _bchannel_destroy(struct mISDNport *mISDNport, int i)
{
	if (!mISDNport->b_sock[i].inuse)
		return;
	chan_trace_header(mISDNport, mISDNport->b_port[i], "BCHANNEL remove socket", DIRECTION_OUT);
	add_trace("channel", NULL, "%d", i+1+(i>=15));
	add_trace("socket", NULL, "%d", mISDNport->b_sock[i].fd);
	end_trace();
	close(mISDNport->b_sock[i].fd);
	unregister_fd(&mISDNport->b_sock[i]);
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

A bchannel can have the following events:

- B_EVENT_USE
A bchannel is required by a Port class.

- B_EVENT_ACTIVATED
The bchannel beomes active.

- B_EVENT_DROP
The bchannel is not required by Port class anymore

- B_EVENT_DEACTIVATED
The bchannel becomes inactive.

All actions taken on these events depend on the current bchannel's state and if it is linked to a Port class.

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
	int timer = -1; // no change
	int p_m_tx_gain = 0;
	int p_m_rx_gain = 0;
	char *p_m_pipeline = NULL;
	unsigned char *p_m_crypt_key = NULL;
	int p_m_crypt_key_len = 0;
	int p_m_crypt_key_type = 0;

	if (b_port) {
		p_m_tx_gain = b_port->p_m_tx_gain;
		p_m_rx_gain = b_port->p_m_rx_gain;
		p_m_pipeline = b_port->p_m_pipeline;
		p_m_crypt_key = b_port->p_m_crypt_key;
		p_m_crypt_key_len = b_port->p_m_crypt_key_len;
		p_m_crypt_key_type = /*b_port->p_m_crypt_key_type*/1;
	}

	switch(event) {
		case B_EVENT_USE:
		/* port must be linked in order to allow activation */
		if (!b_port)
			FATAL("bchannel must be linked to a Port class\n");
		switch(state) {
			case B_STATE_IDLE:
			/* create stack and send activation request */
			if (_bchannel_create(mISDNport, i)) {
				_bchannel_activate(mISDNport, i, 1, 0);
				state = B_STATE_ACTIVATING;
				timer = B_TIMER_ACTIVATING;
			}
			break;

			case B_STATE_ACTIVATING:
			/* do nothing, because it is already activating */
			break;

			default:
			/* problems that might ocurr:
			 * B_EVENT_USE is received when channel already in use.
			 */
			PERROR("Illegal event %d at state %d, please correct.\n", event, state);
		}
		break;


		case B_EVENT_ACTIVATED:
		timer = 0;
		switch(state) {
			case B_STATE_ACTIVATING:
			if (b_port) {
				/* bchannel is active and used by Port class, so we configure bchannel */
				_bchannel_configure(mISDNport, i);
				state = B_STATE_ACTIVE;
				b_port->p_m_load = 0;
			} else {
				/* bchannel is active, but not used anymore (or has wrong stack config), so we deactivate */
				_bchannel_activate(mISDNport, i, 0, 0);
				state = B_STATE_DEACTIVATING;
				timer = B_TIMER_DEACTIVATING;
			}
			break;

			default:
			PERROR("Illegal event %d at state %d, please correct.\n", event, state);
		}
		break;

		case B_EVENT_DROP:
		if (!b_port)
			FATAL("bchannel must be linked to a Port class\n");
		switch(state) {
			case B_STATE_IDLE:
			/* bchannel is idle due to an error, so we do nothing */
			break;

			case B_STATE_ACTIVATING:
			/* do nothing because we must wait until bchanenl is active before deactivating */
			break;

			case B_STATE_ACTIVE:
			/* bchannel is active, so we deactivate */
			_bchannel_activate(mISDNport, i, 0, 0);
			state = B_STATE_DEACTIVATING;
			timer = B_TIMER_DEACTIVATING;
			break;

			case B_STATE_DEACTIVATING:
			/* we may have taken an already deactivating bchannel, but do not require it anymore, so we do nothing */
			break;

			default:
			PERROR("Illegal event %d at state %d, please correct.\n", event, state);
		}
		break;

		case B_EVENT_DEACTIVATED:
		timer = 0;
		switch(state) {
			case B_STATE_IDLE:
			/* ignore due to deactivation confirm after unloading */
			break;

			case B_STATE_DEACTIVATING:
			_bchannel_destroy(mISDNport, i);
			state = B_STATE_IDLE;
			if (b_port) {
				/* bchannel is now deactivate, but is requied by Port class, so we reactivate */
				if (_bchannel_create(mISDNport, i)) {
					_bchannel_activate(mISDNport, i, 1, 0);
					state = B_STATE_ACTIVATING;
					timer = B_TIMER_ACTIVATING;
				}
			}
			break;

			default:
			PERROR("Illegal event %d at state %d, please correct.\n", event, state);
		}
		break;

		case B_EVENT_TIMEOUT:
		timer = 0;
		switch(state) {
			case B_STATE_IDLE:
			/* ignore due to deactivation confirm after unloading */
			break;

			case B_STATE_ACTIVATING:
			_bchannel_activate(mISDNport, i, 1, 1);
			timer = B_TIMER_ACTIVATING;
			break;

			case B_STATE_DEACTIVATING:
			_bchannel_activate(mISDNport, i, 0, 1);
			timer = B_TIMER_DEACTIVATING;
			break;

			default:
			PERROR("Illegal event %d at state %d, please correct.\n", event, state);
		}
		break;

		default:
		PERROR("Illegal event %d, please correct.\n", event);
	}

	mISDNport->b_state[i] = state;
	if (timer == 0)
		unsched_timer(&mISDNport->b_timer[i]);
	else if (timer > 0)
		schedule_timer(&mISDNport->b_timer[i], timer, 0);
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
	if (exclusive && channel>0) {
		i = channel-1-(channel>16);
		if (p_m_mISDNport->b_port[i])
			return(-44); /* requested channel not available */
		goto seize;
	}

	/* ask for channel */
	if (channel>0) {
		i = channel-1-(channel>16);
		if (p_m_mISDNport->b_port[i] == NULL)
			goto seize;
	}

	/* search for channel */
	i = 0;
	while(i < p_m_mISDNport->b_num) {
		if (!p_m_mISDNport->b_port[i]) {
			channel = i+1+(i>=15);
			goto seize;
		}
		i++;
	}
	return(-34); /* no free channel */

seize:
	PDEBUG(DEBUG_BCHANNEL, "PmISDN(%s) seizing bchannel %d (index %d)\n", p_name, channel, i);

	/* link Port, set parameters */
	p_m_mISDNport->b_port[i] = this;
	p_m_b_index = i;
	p_m_b_channel = channel;
	p_m_b_exclusive = exclusive;
	p_m_mISDNport->b_mode[i] = p_m_b_mode;

	/* reserve channel */
	if (!p_m_b_reserve) {
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
	p_m_mISDNport->b_mode[p_m_b_index] = 0;
	p_m_b_index = -1;
	p_m_b_channel = 0;
	p_m_b_exclusive = 0;
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
p_m_preload will give the load that have to be kept in dsp.
ISDN_MAXLOAD (2*p_m_preload) will give the maximum load before dropping.

* procedure for low priority data
see txfromup() for procedure
in short: remote data is ignored during high priority tones

* procedure for high priority data
whenever load is below p_m_preload, load is filled up to p_m_preload
if no more data is available, load becomes empty again.

'load' variable:
0                    p_m_preload           ISDN_MAXLOAD
+--------------------+----------------------+
|                    |                      |
+--------------------+----------------------+

on empty load or on load below p_m_preload, the load is inceased to p_m_preload:
0                    p_m_preload           ISDN_MAXLOAD
+--------------------+----------------------+
|TTTTTTTTTTTTTTTTTTTT|                      |
+--------------------+----------------------+

on empty load, remote-audio causes the load with the remote audio to be increased to p_m_preload.
0                    p_m_preload           ISDN_MAXLOAD
+--------------------+----------------------+
|TTTTTTTTTTTTTTTTTTTTRRRRR                  |
+--------------------+----------------------+

 */
void PmISDN::update_load(void)
{
	/* don't trigger load event if: */
	if (!p_tone_name[0] && !p_m_crypt_msg_loops && !p_m_inband_send_on)
		return;

	/* don't trigger load event if event already active */
	if (p_m_loadtimer.active)
		return;

	schedule_timer(&p_m_loadtimer, 0, 0); /* no delay the first time */
}

int load_timer(struct lcr_timer *timer, void *instance, int index)
{
	class PmISDN *isdnport = (class PmISDN *)instance;

	isdnport->load_tx();

	return 0;
}

void PmISDN::load_tx(void)
{
	int elapsed = 0;
	int ret;
	struct timeval current_time;

	/* get elapsed */
	gettimeofday(&current_time, NULL);
	if (p_m_last_tv_sec) {
		elapsed = 8000 * (current_time.tv_sec - p_m_last_tv_sec)
			+ 8 * (current_time.tv_usec/1000 - p_m_last_tv_msec);
	}
	/* set clock of last process! */
	p_m_last_tv_sec = current_time.tv_sec;
	p_m_last_tv_msec = current_time.tv_usec/1000;

	/* process only if we have samples and we are active */
	if (elapsed && p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE) {
		/* update load */
		if (elapsed < p_m_load)
			p_m_load -= elapsed;
		else
			p_m_load = 0;

		/* to send data, tone must be on */
		if ((p_tone_name[0] || p_m_crypt_msg_loops || p_m_inband_send_on) /* what tones? */
		 && (p_m_load < p_m_preload) /* not too much load? */
		 && (p_state==PORT_STATE_CONNECT || p_m_mISDNport->tones || p_m_inband_send_on)) { /* connected or inband-tones? */
			int tosend = p_m_preload - p_m_load, length; 
			unsigned char buf[MISDN_HEADER_LEN+tosend];
			struct mISDNhead *frm = (struct mISDNhead *)buf;
			unsigned char *p = buf+MISDN_HEADER_LEN;

			/* copy inband signalling (e.g. used by ss5) */
			if (p_m_inband_send_on && tosend) {
				tosend -= inband_send(p, tosend);
			}

			/* copy crypto loops */
			while (p_m_crypt_msg_loops && tosend) {
				/* how much do we have to send */
				length = p_m_crypt_msg_len - p_m_crypt_msg_current;

				/* clip tosend */
				if (length > tosend)
					length = tosend;

				/* copy message (part) to buffer */
				memcpy(p, p_m_crypt_msg+p_m_crypt_msg_current, length);

				/* new position */
				p_m_crypt_msg_current += length;
				if (p_m_crypt_msg_current == p_m_crypt_msg_len) {
					/* next loop */
					p_m_crypt_msg_current = 0;
					p_m_crypt_msg_loops--;
					if (!p_m_crypt_msg_loops)
						update_rxoff();
//					puts("eine loop weniger");
				}

				/* new length */
				tosend -= length;
			}

			/* copy tones */
			if (p_tone_name[0] && tosend) {
				tosend -= read_audio(p, tosend);
			}

			/* send data */
			if (p_m_preload - p_m_load - tosend > 0) {
				frm->prim = PH_DATA_REQ;
				frm->id = 0;
				ret = sendto(p_m_mISDNport->b_sock[p_m_b_index].fd, buf, MISDN_HEADER_LEN+p_m_preload-p_m_load-tosend, 0, NULL, 0);
				if (ret <= 0)
					PERROR("Failed to send to socket %d (samples = %d)\n", p_m_mISDNport->b_sock[p_m_b_index].fd, p_m_preload-p_m_load-tosend);
				p_m_load += p_m_preload - p_m_load - tosend;
			}
		}
	}

	if (p_tone_name[0] || p_m_crypt_msg_loops || p_m_inband_send_on || p_m_load) {
		schedule_timer(&p_m_loadtimer, 0, PORT_TRANSMIT * 125);
	}
}

/* handle timeouts */
static int mISDN_timeout(struct lcr_timer *timer, void *instance, int i)
{
	class PmISDN *isdnport = (class PmISDN *)instance;
	struct lcr_msg *message;

	PDEBUG(DEBUG_ISDN, "(%s) timeout after %d seconds detected (state=%d).\n", isdnport->p_name, isdnport->p_m_timeout.timeout.tv_sec, isdnport->p_state);
	/* send timeout to endpoint */
	message = message_create(isdnport->p_serial, ACTIVE_EPOINT(isdnport->p_epointlist), PORT_TO_EPOINT, MESSAGE_TIMEOUT);
	message->param.state = isdnport->p_state;
	message_put(message);

	return 0;
}
	

/*
 * whenever we get audio data from bchannel, we process it here
 */
void PmISDN::bchannel_receive(struct mISDNhead *hh, unsigned char *data, int len)
{
	unsigned int cont = *((unsigned int *)data);
	struct lcr_msg *message;
	unsigned char *p;
	int l;

	if (hh->prim == PH_CONTROL_IND) {
		if (len < 4) {
			PERROR("SHORT READ OF PH_CONTROL INDICATION\n");
			return;
		}
		if ((cont&(~DTMF_TONE_MASK)) == DTMF_TONE_VAL) {
			chan_trace_header(p_m_mISDNport, this, "BCHANNEL control", DIRECTION_IN);
			add_trace("DTMF", NULL, "%c", cont & DTMF_TONE_MASK);
			if (!p_m_dtmf)
				add_trace("info", NULL, "DTMF is disabled");
			end_trace();
			if (!p_m_dtmf)
				return;
			message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_DTMF);
			message->param.dtmf = cont & DTMF_TONE_MASK;
			PDEBUG(DEBUG_PORT, "PmISDN(%s) PH_CONTROL INDICATION  DTMF digit '%c'\n", p_name, message->param.dtmf);
			message_put(message);
			return;
		}
		switch(cont) {
			case DSP_BF_REJECT:
			chan_trace_header(p_m_mISDNport, this, "BCHANNEL control", DIRECTION_IN);
			add_trace("DSP-CRYPT", NULL, "error");
			end_trace();
			message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_CRYPT);
			message->param.crypt.type = CC_ERROR_IND;
			PDEBUG(DEBUG_PORT, "PmISDN(%s) PH_CONTROL INDICATION  reject of blowfish.\n", p_name);
			message_put(message);
			break;

			case DSP_BF_ACCEPT:
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
	if (hh->prim == PH_CONTROL_IND) {
		switch(hh->id) {
			default:
			chan_trace_header(p_m_mISDNport, this, "BCHANNEL control", DIRECTION_IN);
			add_trace("unknown", NULL, "0x%x", hh->id);
			end_trace();
		}
		return;
	}
	if (hh->prim == PH_DATA_REQ || hh->prim == DL_DATA_REQ) {
		if (!p_m_txdata) {
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
	if (hh->prim != PH_DATA_IND && hh->prim != DL_DATA_IND) {
		PERROR("Bchannel received unknown primitve: 0x%x\n", hh->prim);
		return;
	}

	/* inband is processed */
	if (p_m_inband_receive_on)
		inband_receive(data, len);

	/* send to remote, if bridged */
	bridge_tx(data, len);

	/* calls will not process any audio data unless
	 * the call is connected OR tones feature is enabled.
	 */
#ifndef DEBUG_COREBRIDGE
	if (p_state!=PORT_STATE_CONNECT
	 && !p_m_mISDNport->tones)
		return;
#endif

#if 0
	/* the bearer capability must be audio in order to send and receive
	 * audio prior or after connect.
	 */
	if (!(p_bearerinfo.capability&CLASS_CAPABILITY_AUDIO) && p_state!=PORT_STATE_CONNECT)
		return;
#endif

	/* if rx is off, it may happen that fifos send us pending informations, we just ignore them */
	if (p_m_rxoff) {
		PDEBUG(DEBUG_BCHANNEL, "PmISDN(%s) ignoring data, because rx is turned off\n", p_name);
		return;
	}

	/* record data */
	if (p_record)
		record(data, len, 0); // from down

	/* randomize and listen to crypt message if enabled */
	if (p_m_crypt_listen) {
		/* the noisy randomizer */
		p = data;
		l = len;
		while(l--)
			mISDN_rand[mISDN_rand_count & 0xff] += *p++;

		cryptman_listen_bch(data, len);
	}
}


/*
 * set echotest
 */
void PmISDN::set_echotest(int echo)
{
	if (p_m_echo != echo) {
		p_m_echo = echo;
		PDEBUG(DEBUG_ISDN, "we set echo to echo=%d.\n", p_m_echo);
		if (p_m_b_channel)
			if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE)
				ph_control(p_m_mISDNport, this, p_m_mISDNport->b_sock[p_m_b_index].fd, p_m_echo?DSP_ECHO_ON:DSP_ECHO_OFF, 0, "DSP-ECHO", p_m_echo);
	}
}

/*
 * set tone
 */
void PmISDN::set_tone(const char *dir, const char *tone)
{
	int id = TONE_OFF;
	int dsp = DSP_NONE;

	/* if no directory is given (by extension), we use interface.conf or options.conf */
	if (!dir || !dir[0]) {
		if (p_m_mISDNport->ifport->tones_dir[0])
			dir = p_m_mISDNport->ifport->tones_dir;
		else if (options.tones_dir[0])
			dir = options.tones_dir;
	}

	if (!tone)
		tone = "";
	PDEBUG(DEBUG_ISDN, "isdn port now plays tone:'%s'.\n", tone);
	if (!tone[0]) {
		id = TONE_OFF;
		goto setdsp;
	}

	/* check for dsp tones */
	if (!strcmp(dir, "american"))
		dsp = DSP_AMERICAN;
	if (!strcmp(dir, "german"))
		dsp = DSP_GERMAN;
	if (!strcmp(dir, "oldgerman"))
		dsp = DSP_OLDGERMAN;

	/* check if we NOT really have to use a dsp-tone */
	if (dsp == DSP_NONE) {
		nodsp:
		if (p_m_tone)
		if (p_m_b_index > -1)
		if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE && p_m_mISDNport->b_mode[p_m_b_index] == B_MODE_TRANSPARENT) {
			PDEBUG(DEBUG_ISDN, "we reset tone from id=%d to OFF.\n", p_m_tone);
			ph_control(p_m_mISDNport, this, p_m_mISDNport->b_sock[p_m_b_index].fd, DSP_TONE_PATT_OFF, 0, "DSP-TONE", 0);
		}
		p_m_tone = 0;
		Port::set_tone(dir, tone);
		return;
	}

	/* now we USE dsp-tone, convert name */
	if (!strcmp(tone, "dialtone")) {
		switch(dsp) {
		case DSP_AMERICAN: id = TONE_AMERICAN_DIALTONE; break;
		case DSP_GERMAN: id = TONE_GERMAN_DIALTONE; break;
		case DSP_OLDGERMAN: id = TONE_GERMAN_OLDDIALTONE; break;
		}
	} else if (!strcmp(tone, "dialpbx")) {
		switch(dsp) {
		case DSP_AMERICAN: id = TONE_AMERICAN_DIALPBX; break;
		case DSP_GERMAN: id = TONE_GERMAN_DIALPBX; break;
		case DSP_OLDGERMAN: id = TONE_GERMAN_OLDDIALPBX; break;
		}
	} else if (!strcmp(tone, "ringing")) {
		switch(dsp) {
		case DSP_AMERICAN: id = TONE_AMERICAN_RINGING; break;
		case DSP_GERMAN: id = TONE_GERMAN_RINGING; break;
		case DSP_OLDGERMAN: id = TONE_GERMAN_OLDRINGING; break;
		}
	} else if (!strcmp(tone, "ringpbx")) {
		switch(dsp) {
		case DSP_AMERICAN: id = TONE_AMERICAN_RINGPBX; break;
		case DSP_GERMAN: id = TONE_GERMAN_RINGPBX; break;
		case DSP_OLDGERMAN: id = TONE_GERMAN_OLDRINGPBX; break;
		}
	} else if (!strcmp(tone, "busy")) {
		busy:
		switch(dsp) {
		case DSP_AMERICAN: id = TONE_AMERICAN_BUSY; break;
		case DSP_GERMAN: id = TONE_GERMAN_BUSY; break;
		case DSP_OLDGERMAN: id = TONE_GERMAN_OLDBUSY; break;
		}
	} else if (!strcmp(tone, "release")) {
		hangup:
		switch(dsp) {
		case DSP_AMERICAN: id = TONE_AMERICAN_HANGUP; break;
		case DSP_GERMAN: id = TONE_GERMAN_HANGUP; break;
		case DSP_OLDGERMAN: id = TONE_GERMAN_OLDHANGUP; break;
		}
	} else if (!strcmp(tone, "cause_10"))
		goto hangup;
	else if (!strcmp(tone, "cause_11"))
		goto busy;
	else if (!strcmp(tone, "cause_22")) {
		switch(dsp) {
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
	if (p_m_tone != id) {
		/* set new tone */
		p_m_tone = id;
		PDEBUG(DEBUG_ISDN, "we set tone to id=%d.\n", p_m_tone);
		if (p_m_b_index > -1)
		if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE && p_m_mISDNport->b_mode[p_m_b_index] == B_MODE_TRANSPARENT)
			ph_control(p_m_mISDNport, this, p_m_mISDNport->b_sock[p_m_b_index].fd, p_m_tone?DSP_TONE_PATT_ON:DSP_TONE_PATT_OFF, p_m_tone, "DSP-TONE", p_m_tone);
	}
	/* turn user-space tones off in cases of no tone OR dsp tone */
	Port::set_tone("",NULL);
}


/* MESSAGE_mISDNSIGNAL */
//extern struct lcr_msg *dddebug;
void PmISDN::message_mISDNsignal(unsigned int epoint_id, int message_id, union parameter *param)
{
	int oldconf, newconf;
	switch(param->mISDNsignal.message) {
		case mISDNSIGNAL_VOLUME:
		if (p_m_tx_gain != param->mISDNsignal.tx_gain) {
			p_m_tx_gain = param->mISDNsignal.tx_gain;
			PDEBUG(DEBUG_BCHANNEL, "we change tx-volume to shift=%d.\n", p_m_tx_gain);
			if (p_m_b_index > -1)
			if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE && p_m_mISDNport->b_mode[p_m_b_index] == B_MODE_TRANSPARENT)
				ph_control(p_m_mISDNport, this, p_m_mISDNport->b_sock[p_m_b_index].fd, DSP_VOL_CHANGE_TX, p_m_tx_gain, "DSP-TX_GAIN", p_m_tx_gain);
		} else
			PDEBUG(DEBUG_BCHANNEL, "we already have tx-volume shift=%d.\n", p_m_rx_gain);
		if (p_m_rx_gain != param->mISDNsignal.rx_gain) {
			p_m_rx_gain = param->mISDNsignal.rx_gain;
			PDEBUG(DEBUG_BCHANNEL, "we change rx-volume to shift=%d.\n", p_m_rx_gain);
			if (p_m_b_index > -1)
			if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE && p_m_mISDNport->b_mode[p_m_b_index] == B_MODE_TRANSPARENT)
				ph_control(p_m_mISDNport, this, p_m_mISDNport->b_sock[p_m_b_index].fd, DSP_VOL_CHANGE_RX, p_m_rx_gain, "DSP-RX_GAIN", p_m_rx_gain);
		} else
			PDEBUG(DEBUG_BCHANNEL, "we already have rx-volume shift=%d.\n", p_m_rx_gain);
		break;

		case mISDNSIGNAL_CONF:
		oldconf = p_m_mute?0:p_m_conf;
		p_m_conf = param->mISDNsignal.conf;
		newconf = p_m_mute?0:p_m_conf;
		set_conf(oldconf, newconf);
		break;

		case mISDNSIGNAL_DELAY:
		if (p_m_delay != param->mISDNsignal.delay) {
			p_m_delay = param->mISDNsignal.delay;
			PDEBUG(DEBUG_BCHANNEL, "we change delay mode to delay=%d.\n", p_m_delay);
			if (p_m_b_index > -1)
			if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE && p_m_mISDNport->b_mode[p_m_b_index] == B_MODE_TRANSPARENT)
				ph_control(p_m_mISDNport, this, p_m_mISDNport->b_sock[p_m_b_index].fd, p_m_delay?DSP_DELAY:DSP_JITTER, p_m_delay, "DSP-DELAY", p_m_delay);
		} else
			PDEBUG(DEBUG_BCHANNEL, "we already have delay=%d.\n", p_m_delay);
		break;

		default:
		PERROR("PmISDN(%s) unsupported signal message %d.\n", p_name, param->mISDNsignal.message);
	}
}

/* MESSAGE_CRYPT */
void PmISDN::message_crypt(unsigned int epoint_id, int message_id, union parameter *param)
{
	struct lcr_msg *message;

	switch(param->crypt.type) {
		case CC_ACTBF_REQ:           /* activate blowfish */
		p_m_crypt = 1;
		p_m_crypt_key_len = param->crypt.len;
		if (p_m_crypt_key_len > (int)sizeof(p_m_crypt_key)) {
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
		if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE && p_m_mISDNport->b_mode[p_m_b_index] == B_MODE_TRANSPARENT)
			ph_control_block(p_m_mISDNport, this, p_m_mISDNport->b_sock[p_m_b_index].fd, p_m_crypt?DSP_BF_ENABLE_KEY:DSP_BF_DISABLE, p_m_crypt_key, p_m_crypt_key_len, "DSP-CRYPT", p_m_crypt_key_len);
		break;

		case CC_DACT_REQ:            /* deactivate session encryption */
		p_m_crypt = 0;
		goto crypt_off;
		break;

		case CR_LISTEN_REQ:          /* start listening to messages */
		p_m_crypt_listen = 1;
		update_rxoff();
		p_m_crypt_listen_state = 0;
		break;

		case CR_UNLISTEN_REQ:        /* stop listening to messages */
		p_m_crypt_listen = 0;
		update_rxoff();
		break;

		case CR_MESSAGE_REQ:         /* send message */
		p_m_crypt_msg_len = cryptman_encode_bch(param->crypt.data, param->crypt.len, p_m_crypt_msg, sizeof(p_m_crypt_msg));
		if (!p_m_crypt_msg_len) {
			PERROR("PmISDN(%s) message too long %d > %d\n", p_name, param->crypt.len-1, sizeof(p_m_crypt_msg));
			break;
		}
		p_m_crypt_msg_current = 0; /* reset */
		p_m_crypt_msg_loops = 6; /* enable */
		update_rxoff();
		update_load();
#if 0
		/* disable txmix, or we get corrupt data due to audio process */
		if (p_m_txmix && p_m_b_index>=0 && p_m_mISDNport->b_mode[p_m_b_index] == B_MODE_TRANSPARENT) {
			PDEBUG(DEBUG_BCHANNEL, "for sending CR_MESSAGE_REQ, we reset txmix from txmix=%d.\n", p_m_txmix);
			ph_control(p_m_mISDNport, this, p_mISDNport->b_sock[p_m_b_index].fd, DSP_MIX_OFF, 0, "DSP-TXMIX", 0);
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
int PmISDN::message_epoint(unsigned int epoint_id, int message_id, union parameter *param)
{
	if (Port::message_epoint(epoint_id, message_id, param)) {
		if (message_id == MESSAGE_BRIDGE)
			update_rxoff();
		return 1;
	}

	switch(message_id) {
		case MESSAGE_mISDNSIGNAL: /* user command */
		PDEBUG(DEBUG_ISDN, "PmISDN(%s) received special ISDN SIGNAL %d.\n", p_name, param->mISDNsignal.message);
		message_mISDNsignal(epoint_id, message_id, param);
		return 1;

		case MESSAGE_CRYPT: /* crypt control command */
		PDEBUG(DEBUG_ISDN, "PmISDN(%s) received encryption command '%d'.\n", p_name, param->crypt.type);
		message_crypt(epoint_id, message_id, param);
		return 1;

		case MESSAGE_DISABLE_DEJITTER:
		PDEBUG(DEBUG_ISDN, "PmISDN(%s) received de-jitter disable order.\n", p_name);
		p_m_disable_dejitter = 1;
		p_m_preload = param->queue;
		update_rxoff();
		return 1;
	}

	return 0;
}

void PmISDN::update_rxoff(void)
{
	int tx_dejitter = 0;

	/* call bridges in user space OR crypto OR recording */
	if (p_bridge || p_m_crypt_msg_loops || p_m_crypt_listen || p_record || p_m_inband_receive_on) {
		/* rx IS required */
		if (p_m_rxoff) {
			/* turn on RX */
			p_m_rxoff = 0;
			PDEBUG(DEBUG_BCHANNEL, "%s: receive data is required, so we turn them on\n", __FUNCTION__);
			if (p_m_b_index > -1)
				if (p_m_mISDNport->b_port[p_m_b_index] && p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE)
					ph_control(p_m_mISDNport, this, p_m_mISDNport->b_sock[p_m_b_index].fd, DSP_RECEIVE_ON, 0, "DSP-RXOFF", 0);
		}
	} else {
		/* rx NOT required */
		if (!p_m_rxoff) {
			/* turn off RX */
			p_m_rxoff = 1;
			PDEBUG(DEBUG_BCHANNEL, "%s: receive data is not required, so we turn them off\n", __FUNCTION__);
			if (p_m_b_index > -1)
				if (p_m_mISDNport->b_port[p_m_b_index] && p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE)
					ph_control(p_m_mISDNport, this, p_m_mISDNport->b_sock[p_m_b_index].fd, DSP_RECEIVE_OFF, 0, "DSP-RXOFF", 1);
		}
	}
	/* recording */
	if (p_record) {
		/* txdata IS required */
		if (!p_m_txdata) {
			/* turn on RX */
			p_m_txdata = 1;
			PDEBUG(DEBUG_BCHANNEL, "%s: transmit data is required, so we turn them on\n", __FUNCTION__);
			if (p_m_b_index > -1)
				if (p_m_mISDNport->b_port[p_m_b_index] && p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE)
					ph_control(p_m_mISDNport, this, p_m_mISDNport->b_sock[p_m_b_index].fd, DSP_TXDATA_ON, 0, "DSP-TXDATA", 1);
		}
	} else {
		/* txdata NOT required */
		if (p_m_txdata) {
			/* turn off RX */
			p_m_txdata = 0;
			PDEBUG(DEBUG_BCHANNEL, "%s: transmit data is not required, so we turn them off\n", __FUNCTION__);
			if (p_m_b_index > -1)
				if (p_m_mISDNport->b_port[p_m_b_index] && p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE)
					ph_control(p_m_mISDNport, this, p_m_mISDNport->b_sock[p_m_b_index].fd, DSP_TXDATA_OFF, 0, "DSP-TXDATA", 0);
		}
	}
	/* dejitter on bridge */
	if (p_bridge && !p_m_disable_dejitter)
		tx_dejitter = 1;
	if (p_m_tx_dejitter != tx_dejitter) {
		p_m_tx_dejitter = tx_dejitter;
		PDEBUG(DEBUG_BCHANNEL, "we change dejitter mode to delay=%d.\n", p_m_tx_dejitter);
		if (p_m_b_index > -1)
			if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE && p_m_mISDNport->b_mode[p_m_b_index] == B_MODE_TRANSPARENT)
				ph_control(p_m_mISDNport, this, p_m_mISDNport->b_sock[p_m_b_index].fd, DSP_TX_DEJITTER, p_m_tx_dejitter, "DSP-TX_DEJITTER", p_m_tx_dejitter);
	}
}

static int mISDN_upqueue(struct lcr_fd *fd, unsigned int what, void *instance, int i)
{
	struct mISDNport *mISDNport;
	struct mbuffer *mb;
	struct l3_msg *l3m;
	char byte;
	int ret;

	/* unset global semaphore */
	upqueue_avail = 0;
	// with a very small incident, upqueue_avail may be set by mISDN thread and
	// another byte may be sent to the pipe, which causes a call to this function
	// again with nothing in the upqueue. this is no problem.
	ret = read(fd->fd, &byte, 1);

	/* process all ports */
	mISDNport = mISDNport_first;
	while(mISDNport) {
		/* handle queued up-messages (d-channel) */
		while ((mb = mdequeue(&mISDNport->upqueue))) {
			l3m = &mb->l3;
			switch(l3m->type) {
				case MPH_ACTIVATE_IND:
				if (mISDNport->l1link != 1) {
					l1l2l3_trace_header(mISDNport, NULL, L1_ACTIVATE_IND, DIRECTION_IN);
					end_trace();
					mISDNport->l1link = 1;
				}
				break;
	
				case MPH_DEACTIVATE_IND:
				if (mISDNport->l1link != 0) {
					l1l2l3_trace_header(mISDNport, NULL, L1_DEACTIVATE_IND, DIRECTION_IN);
					end_trace();
					mISDNport->l1link = 0;
				}
				break;

				case MPH_INFORMATION_IND:
				PDEBUG(DEBUG_ISDN, "Received MPH_INFORMATION_IND for port %d (%s).\n", mISDNport->portnum, mISDNport->ifport->interface->name);
				switch (l3m->pid) {
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
				mISDNport->l2link = 1;
				if (l3m->pid < 128)
					mISDNport->l2mask[l3m->pid >> 3] |= (1 << (l3m->pid & 7));
				if ((!mISDNport->ntmode || mISDNport->ptp) && l3m->pid < 127) {
					if (mISDNport->l2establish.active) {
						unsched_timer(&mISDNport->l2establish);
						PDEBUG(DEBUG_ISDN, "the link became active before l2establish timer expiry.\n");
					}
				}
				break;

				case MT_L2RELEASE:
				if (l3m->pid < 128)
					mISDNport->l2mask[l3m->pid >> 3] &= ~(1 << (l3m->pid & 7));
				if (!mISDNport->l2establish.active) {
					l1l2l3_trace_header(mISDNport, NULL, L2_RELEASE_IND, DIRECTION_IN);
					add_trace("tei", NULL, "%d", l3m->pid);
					end_trace();
					/* down if not nt-ptmp */ 
					if (!mISDNport->ntmode || mISDNport->ptp)
						mISDNport->l2link = 0;
				}
				if ((!mISDNport->ntmode || mISDNport->ptp) && l3m->pid < 127) {
					if (!mISDNport->l2establish.active && mISDNport->l2hold) {
						PDEBUG(DEBUG_ISDN, "set timer and establish.\n");
						schedule_timer(&mISDNport->l2establish, 5, 0);
						mISDNport->ml3->to_layer3(mISDNport->ml3, MT_L2ESTABLISH, 0, NULL);
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
		mISDNport = mISDNport->next;
	}
	return 0;
}

/* l2 establish timer fires */
static int l2establish_timeout(struct lcr_timer *timer, void *instance, int i)
{
	struct mISDNport *mISDNport = (struct mISDNport *)instance;

	if (mISDNport->l2hold && (mISDNport->ptp || !mISDNport->ntmode)) {
		PDEBUG(DEBUG_ISDN, "the L2 establish timer expired, we try to establish the link portnum=%d.\n", mISDNport->portnum);
		mISDNport->ml3->to_layer3(mISDNport->ml3, MT_L2ESTABLISH, 0, NULL);
		schedule_timer(&mISDNport->l2establish, 5, 0); /* 5 seconds */
	}

	return 0;
}

/* handle frames from bchannel */
static int b_sock_callback(struct lcr_fd *fd, unsigned int what, void *instance, int i)
{
	struct mISDNport *mISDNport = (struct mISDNport *)instance;
	unsigned char buffer[2048+MISDN_HEADER_LEN];
	struct mISDNhead *hh = (struct mISDNhead *)buffer;
	int ret;

	ret = recv(fd->fd, buffer, sizeof(buffer), 0);
	if (ret < 0) {
		PERROR("read error frame, errno %d\n", errno);
		return 0;
	}
	if (ret < (int)MISDN_HEADER_LEN) {
		PERROR("read short frame, got %d, expected %d\n", ret, (int)MISDN_HEADER_LEN);
		return 0;
	}
	switch(hh->prim) {
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
			PDEBUG(DEBUG_BCHANNEL, "b-channel is not associated to an ISDNPort (socket %d), ignoring.\n", fd->fd);
		break;

		case PH_ACTIVATE_IND:
		case DL_ESTABLISH_IND:
		case PH_ACTIVATE_CNF:
		case DL_ESTABLISH_CNF:
		PDEBUG(DEBUG_BCHANNEL, "DL_ESTABLISH confirm: bchannel is now activated (socket %d).\n", fd->fd);
		bchannel_event(mISDNport, i, B_EVENT_ACTIVATED);
		break;

		case PH_DEACTIVATE_IND:
		case DL_RELEASE_IND:
		case PH_DEACTIVATE_CNF:
		case DL_RELEASE_CNF:
		PDEBUG(DEBUG_BCHANNEL, "DL_RELEASE confirm: bchannel is now de-activated (socket %d).\n", fd->fd);
		bchannel_event(mISDNport, i, B_EVENT_DEACTIVATED);
		break;

		default:
		PERROR("child message not handled: prim(0x%x) socket(%d) msg->len(%d)\n", hh->prim, fd->fd, ret-MISDN_HEADER_LEN);
	}

	return 0;
}

/* process timer events for bchannel handling */
static int b_timer_timeout(struct lcr_timer *timer, void *instance, int i)
{
	struct mISDNport *mISDNport = (struct mISDNport *)instance;

	bchannel_event(mISDNport, i, B_EVENT_TIMEOUT);

	return 0;
}


int do_layer3(struct mlayer3 *ml3, unsigned int cmd, unsigned int pid, struct l3_msg *l3m)
{
	/* IMPORTAINT:
	 *
	 * l3m must be queued, except for MT_ASSIGN
	 *
	 */
	struct mISDNport *mISDNport = (struct mISDNport *)ml3->priv;
	struct mbuffer *mb;

#ifdef OLD_MT_ASSIGN
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
	if (cmd==MT_ASSIGN && (pid&MISDN_PID_CR_FLAG) && (pid>>16)==MISDN_CES_MASTER) {
		/* let's do some checking if someone changes stack behaviour */
		if (mt_assign_pid != 0)
			FATAL("someone played with the mISDNuser stack. MT_ASSIGN not currently expected.\n");
		mt_assign_pid = pid;
		return(0);
	}
#endif
	/* queue message, create, if required */
	if (!l3m) {
		l3m = alloc_l3_msg();
		if (!l3m)
			FATAL("No memory for layer 3 message\n");
	}
	mb = container_of(l3m, struct mbuffer, l3);
	l3m->type = cmd;
	l3m->pid = pid;
	mqueue_tail(&mISDNport->upqueue, mb);
	if (!upqueue_avail) {
		// multiple threads may cause multiple calls of this section, but this
		// results only in multiple processing of the upqueue read.
		// this is no problem.
		upqueue_avail = 1;
		char byte = 0;
		int ret;
		ret = write(upqueue_pipe[1], &byte, 1);
	}
	return 0;
}

int mISDN_getportbyname(int sock, int cnt, char *portname)
{
	struct mISDN_devinfo devinfo;
	int port = 0, ret;

	/* resolve name */
	while (port < cnt) {
		devinfo.id = port;
		ret = ioctl(sock, IMGETDEVINFO, &devinfo);
		if (ret < 0)
			return ret;
		if (!strcasecmp(devinfo.name, portname))
			break;
		port++;
	}
	if (port == cnt)
		return -EIO;

	return (port);
}

/*
 * global function to add a new card (port)
 */
struct mISDNport *mISDNport_open(struct interface_port *ifport)
{
	int ret;
	struct mISDNport *mISDNport, **mISDNportp;
	int port = ifport->portnum;
	int ptp = ifport->ptp;
	int force_nt = ifport->nt;
	int l1hold = ifport->l1hold;
	int l2hold = ifport->l2hold;
	int ss5 = ifport->ss5;
	int i, cnt;
	int pri, bri, pots;
	int nt, te;
//	struct mlayer3 *ml3;
	struct mISDN_devinfo devinfo;
	unsigned int protocol, prop;

	/* check port counts */
	ret = ioctl(mISDNsocket, IMGETCOUNT, &cnt);
	if (ret < 0) {
		fprintf(stderr, "Cannot get number of mISDN devices. (ioctl IMGETCOUNT failed ret=%d)\n", ret);
		return(NULL);
	}

	if (cnt <= 0) {
		PERROR_RUNTIME("Found no card. Please be sure to load card drivers.\n");
		return(NULL);
	}
	if (port < 0) {
		port = mISDN_getportbyname(mISDNsocket, cnt, ifport->portname);
		if (port < 0) {
			PERROR_RUNTIME("Port name '%s' not found, use 'misdn_info' tool to list all existing ports.\n", ifport->portname);
			return(NULL);
		}
		// note: 'port' has still the port number
	}
	if (port>cnt || port<0) {
		PERROR_RUNTIME("Port (%d) given at 'ports' (options.conf) is out of existing port range (%d-%d)\n", port, 0, cnt);
		return(NULL);
	}

	/* get port attributes */
	pri = bri = pots = nt = te = 0;
	devinfo.id = port;
	ret = ioctl(mISDNsocket, IMGETDEVINFO, &devinfo);
	if (ret < 0) {
		PERROR_RUNTIME("Cannot get device information for port %d. (ioctl IMGETDEVINFO failed ret=%d)\n", port, ret);
		return(NULL);
	}
	if (devinfo.Dprotocols & (1 << ISDN_P_TE_S0)) {
		bri = 1;
		te = 1;
	}
	if (devinfo.Dprotocols & (1 << ISDN_P_NT_S0)) {
		bri = 1;
		nt = 1;
	}
	if (devinfo.Dprotocols & (1 << ISDN_P_TE_E1)) {
		pri = 1;
		te = 1;
	}
	if (devinfo.Dprotocols & (1 << ISDN_P_NT_E1)) {
		pri = 1;
		nt = 1;
	}
#ifdef ISDN_P_FXS
	if (devinfo.Dprotocols & (1 << ISDN_P_FXS)) {
		pots = 1;
		te = 1;
	}
#endif
#ifdef ISDN_P_FXO
	if (devinfo.Dprotocols & (1 << ISDN_P_FXO)) {
		pots = 1;
		nt = 1;
	}
#endif
	if (force_nt && !nt) {
		PERROR_RUNTIME("Port %d does not support NT-mode\n", port);
		return(NULL);
	}
	if (bri && pri) {
		PERROR_RUNTIME("Port %d supports BRI and PRI?? What kind of controller is that?. (Can't use this!)\n", port);
		return(NULL);
	}
	if (pots && !bri && !pri) {
		PERROR_RUNTIME("Port %d supports POTS, LCR does not!\n", port);
		return(NULL);
	}
	if (!bri && !pri) {
		PERROR_RUNTIME("Port %d does not support BRI nor PRI!\n", port);
		return(NULL);
	}
	if (!nt && !te) {
		PERROR_RUNTIME("Port %d does not support NT-mode nor TE-mode!\n", port);
		return(NULL);
	}
	/* set NT by turning off TE */
	if (force_nt && nt)
		te = 0;
	/* if TE an NT is supported (and not forced to NT), turn off NT */
	if (te && nt)
		nt = 0;

	/* check for double use of port */
	if (nt) {
		mISDNport = mISDNport_first;
		while(mISDNport) {
			if (mISDNport->portnum == port)
				break;
			mISDNport = mISDNport->next;
		}
		if (mISDNport) {
			PERROR_RUNTIME("Port %d already in use by LCR. You can't use a NT port multiple times.\n", port);
			return(NULL);
		}
	}

	/* check for continous channelmap with no bchannel on slot 16 */
	if (test_channelmap(0, devinfo.channelmap)) {
		PERROR_RUNTIME("Port %d provides channel 0, but we cannot access it!\n", port);
		return(NULL);
	}
	i = 1;
	while(i < (int)devinfo.nrbchan + 1) {
		if (i == 16) {
			if (test_channelmap(i, devinfo.channelmap)) {
				PERROR("Port %d provides bchannel 16. Pleas upgrade mISDN, if this port is mISDN loopback interface.\n", port);
				return(NULL);
			}
		} else {
			if (!test_channelmap(i, devinfo.channelmap)) {
				PERROR_RUNTIME("Port %d has no channel on slot %d!\n", port, i);
				return(NULL);
			}
		}
		i++;
	}

	/* add mISDNport structure */
	mISDNportp = &mISDNport_first;
	while(*mISDNportp)
		mISDNportp = &((*mISDNportp)->next);
	mISDNport = (struct mISDNport *)MALLOC(sizeof(struct mISDNport));
	add_timer(&mISDNport->l2establish, l2establish_timeout, mISDNport, 0);
	if (ss5) {
		/* ss5 link is always active */
		mISDNport->l1link = 1;
		mISDNport->l2link = 1;
	} else {
		mISDNport->l1link = -1;
		mISDNport->l2link = -1;
	}
	pmemuse++;
	*mISDNportp = mISDNport;

	/* if pri, must set PTP */
	if (pri)
		ptp = 1;

	/* set ss5 params */
	if (ss5) {
		/* try to keep interface enabled */
		l1hold = 1;
		l2hold = 0;
	}
	/* set l2hold */
	switch (l2hold) {
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
	protocol = (nt)?L3_PROTOCOL_DSS1_NET:L3_PROTOCOL_DSS1_USER;
	prop = (1 << MISDN_FLG_L2_CLEAN);
	if (ptp) // ptp forced
	       prop |= (1 << MISDN_FLG_PTP);
	if (nt) // supports hold/retrieve on nt-mode
	       prop |= (1 << MISDN_FLG_NET_HOLD);
	if (l1hold) // supports layer 1 hold
	       prop |= (1 << MISDN_FLG_L1_HOLD);
	if (l2hold) // supports layer 2 hold
	       prop |= (1 << MISDN_FLG_L2_HOLD);
	/* open layer 3 and init upqueue */
	/* queue must be initializes, because l3-thread may send messages during open_layer3() */
	mqueue_init(&mISDNport->upqueue);
	mISDNport->ml3 = open_layer3(port, protocol, prop , do_layer3, mISDNport);
	if (!mISDNport->ml3) {
		mqueue_purge(&mISDNport->upqueue);
		PERROR_RUNTIME("open_layer3() failed for port %d\n", port);
		start_trace(port,
			ifport->interface,
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

	SCPY(mISDNport->name, devinfo.name);
	mISDNport->b_num = devinfo.nrbchan;
	mISDNport->portnum = port;
	mISDNport->ntmode = nt;
	mISDNport->tespecial = ifport->tespecial;
	mISDNport->pri = pri;
	mISDNport->ptp = ptp;
	mISDNport->l1hold = l1hold;
	mISDNport->l2hold = l2hold;
	mISDNport->ss5 = ss5;
	PDEBUG(DEBUG_ISDN, "Port has %d b-channels.\n", mISDNport->b_num);
	i = 0;
	while(i < mISDNport->b_num) {
		mISDNport->b_state[i] = B_STATE_IDLE;
		add_timer(&mISDNport->b_timer[i], b_timer_timeout, mISDNport, i);
		i++;
	}

	/* if ptp, pull up the link */
	if (mISDNport->l2hold && (mISDNport->ptp || !mISDNport->ntmode)) {
		mISDNport->ml3->to_layer3(mISDNport->ml3, MT_L2ESTABLISH, 0, NULL);
		l1l2l3_trace_header(mISDNport, NULL, L2_ESTABLISH_REQ, DIRECTION_OUT);
		add_trace("tei", NULL, "%d", 0);
		end_trace();
		schedule_timer(&mISDNport->l2establish, 5, 0); /* 5 seconds */
	}

	/* for nt-mode ptmp the link is always up */
	if (mISDNport->ntmode && !mISDNport->ptp)
		mISDNport->l2link = 1;

	PDEBUG(DEBUG_BCHANNEL, "using 'mISDN_dsp.o' module\n");

	start_trace(mISDNport->portnum,
		    ifport->interface,
		    NULL,
		    NULL,
		    DIRECTION_NONE,
		    CATEGORY_CH,
		    0,
		    "PORT (open)");
	add_trace("mode", NULL, (mISDNport->ntmode)?"network":"terminal");
	add_trace("channels", NULL, "%d", mISDNport->b_num);
	if (mISDNport->ss5)
		add_trace("ccitt#5", NULL, "enabled");
	end_trace();

	return(mISDNport);
}


/*
 * load static port instances, if required by mISDNport
 */
void mISDNport_static(struct mISDNport *mISDNport)
{
	int i;

	i = 0;
	while(i < mISDNport->b_num) {
#ifdef WITH_SS5
		if (mISDNport->ss5)
			ss5_create_channel(mISDNport, i);
#endif
		i++;
	}
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
	int i;

	/* remove all port instance that are linked to this mISDNport */
	again:
	port = port_first;
	while(port) {
		if ((port->p_type&PORT_CLASS_MASK) == PORT_CLASS_mISDN) {
			isdnport = (class PmISDN *)port;
			if (isdnport->p_m_mISDNport && isdnport->p_m_mISDNport == mISDNport) {
				PDEBUG(DEBUG_ISDN, "port %s uses mISDNport %d, destroying it.\n", isdnport->p_name, mISDNport->portnum);
				delete isdnport;
				goto again;
			}
		}
		port = port->next;
	}

	/* only if we are already part of interface */
	if (mISDNport->ifport) {
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
	while(i < mISDNport->b_num) {
		if (mISDNport->b_sock[i].inuse) {
			_bchannel_destroy(mISDNport, i);
			PDEBUG(DEBUG_BCHANNEL, "freeing %s port %d bchannel (index %d).\n", (mISDNport->ntmode)?"NT":"TE", mISDNport->portnum, i);
		}
		if (mISDNport->b_timer[i].inuse) {
			del_timer(&mISDNport->b_timer[i]);
		}
		i++;
	}
	del_timer(&mISDNport->l2establish);

	/* close layer 3, if open */
	if (mISDNport->ml3) {
		close_layer3(mISDNport->ml3);
	}

	/* purge upqueue */
	mqueue_purge(&mISDNport->upqueue);

	/* remove from list */
	mISDNportp = &mISDNport_first;
	while(*mISDNportp) {
		if (*mISDNportp == mISDNport) {
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
 * enque data from remote port
 */
int PmISDN::bridge_rx(unsigned char *data, int length)
{
	unsigned char buf[MISDN_HEADER_LEN+((length>p_m_preload)?length:p_m_preload)];
	struct mISDNhead *hh = (struct mISDNhead *)buf;
	int ret;

	if (p_m_b_index < 0)
		return -EIO;
	if (p_m_mISDNport->b_state[p_m_b_index] != B_STATE_ACTIVE)
		return -EINVAL;

	/* check if high priority tones exist
	 * ignore data in this case
	 */
	if (p_tone_name[0] || p_m_crypt_msg_loops || p_m_inband_send_on)
		return -EBUSY;

	/* preload procedure
	 * if transmit buffer in DSP module is empty,
	 * preload it to DSP_LOAD to prevent jitter gaps.
	 */
	if ((!p_bridge || p_m_disable_dejitter) && p_m_load == 0 && p_m_preload > 0) {
		hh->prim = PH_DATA_REQ; 
		hh->id = 0;
		memset(buf+MISDN_HEADER_LEN, silence, p_m_preload);
		ret = sendto(p_m_mISDNport->b_sock[p_m_b_index].fd, buf, MISDN_HEADER_LEN+p_m_preload, 0, NULL, 0);
		if (ret <= 0)
			PERROR("Failed to send to socket %d\n", p_m_mISDNport->b_sock[p_m_b_index].fd);
		p_m_load += p_m_preload;
		schedule_timer(&p_m_loadtimer, 0, PORT_TRANSMIT * 125);
	}

	/* drop if load would exceed ISDN_MAXLOAD
	 * this keeps the delay not too high
	 */
	if (p_m_load+length > (p_m_preload << 1))
		return -EINVAL;

	/* make and send frame */
	hh->prim = PH_DATA_REQ;
	hh->id = 0;
	memcpy(buf+MISDN_HEADER_LEN, data, length);
	ret = sendto(p_m_mISDNport->b_sock[p_m_b_index].fd, buf, MISDN_HEADER_LEN+length, 0, NULL, 0);
	if (ret <= 0)
		PERROR("Failed to send to socket %d\n", p_m_mISDNport->b_sock[p_m_b_index].fd);
	p_m_load += length;

	return 0;
}

int PmISDN::inband_send(unsigned char *buffer, int len)
{
	PERROR("this function must be derived to function!\n");
	return 0;
}

void PmISDN::inband_send_on(void)
{
	PDEBUG(DEBUG_PORT, "turning inband signalling send on.\n");
	p_m_inband_send_on = 1;
	/* trigger inband transmit */
	update_load();
}

void PmISDN::inband_send_off(void)
{
	PDEBUG(DEBUG_PORT, "turning inband signalling send off.\n");
	p_m_inband_send_on = 0;
}

void PmISDN::inband_receive(unsigned char *buffer, int len)
{
//
//	if (len >= SS5_DECODER_NPOINTS)
//		ss5_decode(buffer, SS5_DECODER_NPOINTS);
	PERROR("this function must be derived to function!\n");
}

void PmISDN::inband_receive_on(void)
{
	/* this must work during constructor, see ss5.cpp */
	PDEBUG(DEBUG_PORT, "turning inband signalling receive on.\n");
	p_m_inband_receive_on = 1;
	update_rxoff();
}

void PmISDN::inband_receive_off(void)
{
	PDEBUG(DEBUG_PORT, "turning inband signalling receive off.\n");
	p_m_inband_receive_on = 0;
	update_rxoff();
}

void PmISDN::mute_on(void)
{
	if (p_m_mute)
		return;
	PDEBUG(DEBUG_PORT, "turning mute on.\n");
	p_m_mute = 1;
	set_conf(p_m_conf, 0);
}

void PmISDN::mute_off(void)
{
	if (!p_m_mute)
		return;
	PDEBUG(DEBUG_PORT, "turning mute off.\n");
	p_m_mute = 0;
	set_conf(0, p_m_conf);
}


