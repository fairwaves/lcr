/*****************************************************************************\
**                                                                           **
** LCR                                                                       **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** mISDN gsm                                                                 **
**                                                                           **
\*****************************************************************************/ 

#include "main.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
extern "C" {
#include <getopt.h>

#include <openbsc/db.h>
#include <openbsc/select.h>
#include <openbsc/debug.h>
#include <openbsc/e1_input.h>
#include <openbsc/talloc.h>
#include <openbsc/mncc.h>
#include <openbsc/trau_frame.h>
struct gsm_network *bsc_gsmnet = 0;
extern int ipacc_rtp_direct;
extern int bsc_bootstrap_network(int (*mmc_rev)(struct gsm_network *, int, void *),
				 const char *cfg_file);
extern int bsc_shutdown_net(struct gsm_network *net);
void talloc_ctx_init(void);
void on_dso_load_token(void);
void on_dso_load_rrlp(void);

#include "gsm_audio.h"

#undef AF_ISDN
#undef PF_ISDN
extern  int     AF_ISDN;
#define PF_ISDN AF_ISDN
}


struct lcr_gsm *gsm = NULL;

static unsigned int new_callref = 1;


/*
 * create and send mncc message
 */
static struct gsm_mncc *create_mncc(int msg_type, unsigned int callref)
{
	struct gsm_mncc *mncc;

	mncc = (struct gsm_mncc *)MALLOC(sizeof(struct gsm_mncc));
	mncc->msg_type = msg_type;
	mncc->callref = callref;
	return (mncc);
}
static int send_and_free_mncc(struct gsm_network *net, unsigned int msg_type, void *data)
{
	int ret;

	ret = mncc_send(net, msg_type, data);
	free(data);

	return ret;
}


/*
 * constructor
 */
Pgsm::Pgsm(int type, struct mISDNport *mISDNport, char *portname, struct port_settings *settings, int channel, int exclusive, int mode) : PmISDN(type, mISDNport, portname, settings, channel, exclusive, mode)
{
	p_callerinfo.itype = (mISDNport->ifport->interface->extension)?INFO_ITYPE_ISDN_EXTENSION:INFO_ITYPE_ISDN;
	p_m_g_callref = 0;
	p_m_g_mode = 0;
	p_m_g_gsm_b_sock = -1;
	p_m_g_gsm_b_index = -1;
	p_m_g_gsm_b_active = 0;
	p_m_g_notify_pending = NULL;
	p_m_g_decoder = gsm_audio_create();
	p_m_g_encoder = gsm_audio_create();
	if (!p_m_g_encoder || !p_m_g_decoder) {
		PERROR("Failed to create GSM audio codec instance\n");
		p_m_delete = 1;
	}
	p_m_g_rxpos = 0;
	p_m_g_tch_connected = 0;

	PDEBUG(DEBUG_GSM, "Created new mISDNPort(%s).\n", portname);
}

/*
 * destructor
 */
Pgsm::~Pgsm()
{
	PDEBUG(DEBUG_GSM, "Destroyed GSM process(%s).\n", p_name);

	/* remove queued message */
	if (p_m_g_notify_pending)
		message_free(p_m_g_notify_pending);

	/* close audio transfer socket */
	if (p_m_g_gsm_b_sock > -1)
		bchannel_close();

	/* close codec */
	if (p_m_g_encoder)
		gsm_audio_destroy(p_m_g_encoder);
	if (p_m_g_decoder)
		gsm_audio_destroy(p_m_g_decoder);
}


/* close bsc side bchannel */
void Pgsm::bchannel_close(void)
{
	if (p_m_g_gsm_b_sock > -1)
		close(p_m_g_gsm_b_sock);
	p_m_g_gsm_b_sock = -1;
	p_m_g_gsm_b_index = -1;
	p_m_g_gsm_b_active = 0;
}

/* open bsc side bchannel */
int Pgsm::bchannel_open(int index)
{
	int ret;
	unsigned int on = 1;
	struct sockaddr_mISDN addr;
	struct mISDNhead act;

	if (p_m_g_gsm_b_sock > -1) {
		PERROR("Socket already created for index %d\n", index);
		return(-EIO);
	}

	/* open socket */
	ret = p_m_g_gsm_b_sock = socket(PF_ISDN, SOCK_DGRAM, ISDN_P_B_RAW);
	if (ret < 0) {
		PERROR("Failed to open bchannel-socket for index %d\n", index);
		bchannel_close();
		return(ret);
	}
	
	/* set nonblocking io */
	ret = ioctl(p_m_g_gsm_b_sock, FIONBIO, &on);
	if (ret < 0) {
		PERROR("Failed to set bchannel-socket index %d into nonblocking IO\n", index);
		bchannel_close();
		return(ret);
	}

	/* bind socket to bchannel */
	addr.family = AF_ISDN;
	addr.dev = gsm->gsm_port;
	addr.channel = index+1+(index>15);
	ret = bind(p_m_g_gsm_b_sock, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		PERROR("Failed to bind bchannel-socket for index %d\n", index);
		bchannel_close();
		return(ret);
	}
	/* activate bchannel */
	PDEBUG(DEBUG_GSM, "Activating GSM side channel index %i.\n", index);
	act.prim = PH_ACTIVATE_REQ; 
	act.id = 0;
	ret = sendto(p_m_g_gsm_b_sock, &act, MISDN_HEADER_LEN, 0, NULL, 0);
	if (ret < 0) {
		PERROR("Failed to activate index %d\n", index);
		bchannel_close();
		return(ret);
	}

	p_m_g_gsm_b_index = index;

	return(0);
}

/* receive from bchannel */
void Pgsm::bchannel_receive(struct mISDNhead *hh, unsigned char *data, int len)
{
	unsigned char frame[33];

	/* encoder init failed */
	if (!p_m_g_encoder)
		return;

	/* (currently) not connected, so don't flood tch! */
	if (!p_m_g_tch_connected)
		return;

	/* write to rx buffer */
	while(len--) {
		p_m_g_rxdata[p_m_g_rxpos++] = audio_law_to_s32[*data++];
		if (p_m_g_rxpos == 160) {
			p_m_g_rxpos = 0;

			/* encode data */
			gsm_audio_encode(p_m_g_encoder, p_m_g_rxdata, frame);
			frame_send(frame);
		}
	}
}

/* transmit to bchannel */
void Pgsm::bchannel_send(unsigned int prim, unsigned int id, unsigned char *data, int len)
{
	unsigned char buf[MISDN_HEADER_LEN+len];
	struct mISDNhead *hh = (struct mISDNhead *)buf;
	int ret;

	if (!p_m_g_gsm_b_active)
		return;

	/* make and send frame */
	hh->prim = PH_DATA_REQ;
	hh->id = 0;
	memcpy(buf+MISDN_HEADER_LEN, data, len);
	ret = sendto(p_m_g_gsm_b_sock, buf, MISDN_HEADER_LEN+len, 0, NULL, 0);
	if (ret <= 0)
		PERROR("Failed to send to socket index %d\n", index);
}

void Pgsm::frame_send(void *_frame)
{
	unsigned char buffer[sizeof(struct gsm_data_frame) + 33];
	struct gsm_data_frame *frame = (struct gsm_data_frame *)buffer;
	
	frame->msg_type = GSM_TCHF_FRAME;
	frame->callref = p_m_g_callref;
	memcpy(frame->data, _frame, 33);
	mncc_send((struct gsm_network *)gsm->network, frame->msg_type, frame);
}


void Pgsm::frame_receive(void *_frame)
{
	struct gsm_data_frame *frame = (struct gsm_data_frame *)_frame;
	signed short samples[160];
	unsigned char data[160];
	int i;

	if (!p_m_g_decoder)
		return;

	if ((frame->data[0]>>4) != 0xd)
		PERROR("received GSM frame with wrong magig 0x%x\n", frame->data[0]>>4);
	
	/* decode */
	gsm_audio_decode(p_m_g_decoder, frame->data, samples);
	for (i = 0; i < 160; i++) {
		data[i] = audio_s16_to_law[samples[i] & 0xffff];
	}

	/* send */
	bchannel_send(PH_DATA_REQ, 0, data, 160);
}


/*
 * create trace
 **/
static void gsm_trace_header(struct mISDNport *mISDNport, class PmISDN *port, unsigned int msg_type, int direction)
{
	char msgtext[64];

	/* select message and primitive text */
	SCPY(msgtext, get_mncc_name(msg_type));

	/* add direction */
	if (direction == DIRECTION_OUT)
		SCAT(msgtext, " MSC->BSC");
	else
		SCAT(msgtext, " MSC<-BSC");

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



/* select bchannel */
int Pgsm::hunt_bchannel(void)
{
	int channel;
	int i;

	chan_trace_header(p_m_mISDNport, this, "CHANNEL SELECTION (setup)", DIRECTION_NONE);
	add_trace("channel", "reserved", "%d", p_m_mISDNport->b_reserved);
	if (p_m_mISDNport->b_reserved >= p_m_mISDNport->b_num) { // of out chan..
		add_trace("conclusion", NULL, "all channels are reserved");
		end_trace();
		return(-34); // no channel
	}
	/* find channel */
	i = 0;
	channel = 0;
	while(i < p_m_mISDNport->b_num) {
		if (p_m_mISDNport->b_port[i] == NULL) {
			channel = i+1+(i>=15);
			break;
		}
		i++;
	}
	if (!channel) {
		add_trace("conclusion", NULL, "no channel available");
		end_trace();
		return(-6); // channel unacceptable
	}
	add_trace("conclusion", NULL, "channel available");
	add_trace("connect", "channel", "%d", channel);
	end_trace();
	return(channel);
}


/*
 * handles all indications
 */
/* SETUP INDICATION */
void Pgsm::setup_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	int ret;
	class Endpoint *epoint;
	struct lcr_msg *message;
	int channel;
	struct gsm_mncc *mode, *proceeding, *frame;

	/* emergency shutdown */
	printf("%d %d\n", mncc->emergency, !gsm->conf.noemergshut);
	if (mncc->emergency && !gsm->conf.noemergshut) {
		start_trace(p_m_mISDNport->portnum,
			p_m_mISDNport->ifport->interface,
			NULL,
			NULL,
			DIRECTION_NONE,
			CATEGORY_CH,
			0,
			"EMERGENCY SHUTDOWN (due to received emergency call)");
		end_trace();
		quit = 1;
	}
	/* process given callref */
	l1l2l3_trace_header(p_m_mISDNport, this, L3_NEW_L3ID_IND, DIRECTION_IN);
	add_trace("callref", "new", "0x%x", callref);
	if (p_m_g_callref) {
		/* release in case the ID is already in use */
		add_trace("error", NULL, "callref already in use");
		end_trace();
		mncc = create_mncc(MNCC_REJ_REQ, callref);
		gsm_trace_header(p_m_mISDNport, this, MNCC_REJ_REQ, DIRECTION_OUT);
		mncc->fields |= MNCC_F_CAUSE;
		mncc->cause.coding = 3;
		mncc->cause.location = 1;
		mncc->cause.value = 47;
		add_trace("cause", "coding", "%d", mncc->cause.coding);
		add_trace("cause", "location", "%d", mncc->cause.location);
		add_trace("cause", "value", "%d", mncc->cause.value);
		add_trace("reason", NULL, "callref already in use");
		end_trace();
		send_and_free_mncc((struct gsm_network *)gsm->network, mncc->msg_type, mncc);
		new_state(PORT_STATE_RELEASE);
		p_m_delete = 1;
		return;
	}
	p_m_g_callref = callref;
	end_trace();

	/* if blocked, release call with MT_RELEASE_COMPLETE */
	if (p_m_mISDNport->ifport->block) {
		mncc = create_mncc(MNCC_REJ_REQ, p_m_g_callref);
		gsm_trace_header(p_m_mISDNport, this, MNCC_REJ_REQ, DIRECTION_OUT);
		mncc->fields |= MNCC_F_CAUSE;
		mncc->cause.coding = 3;
		mncc->cause.location = 1;
		mncc->cause.value = 27;
		add_trace("cause", "coding", "%d", mncc->cause.coding);
		add_trace("cause", "location", "%d", mncc->cause.location);
		add_trace("cause", "value", "%d", mncc->cause.value);
		add_trace("reason", NULL, "port is blocked");
		end_trace();
		send_and_free_mncc((struct gsm_network *)gsm->network, mncc->msg_type, mncc);
		new_state(PORT_STATE_RELEASE);
		p_m_delete = 1;
		return;
	}

	/* caller info */
	if (mncc->clir.inv)
		p_callerinfo.present = INFO_PRESENT_RESTRICTED;
	else
		p_callerinfo.present = INFO_PRESENT_ALLOWED;
	if (mncc->calling.number[0])
		SCPY(p_callerinfo.id, mncc->calling.number);
	else
		p_callerinfo.present = INFO_PRESENT_NOTAVAIL;
	SCPY(p_callerinfo.imsi, mncc->imsi);
	p_callerinfo.screen = INFO_SCREEN_NETWORK;
	p_callerinfo.ntype = INFO_NTYPE_UNKNOWN;
	p_callerinfo.isdn_port = p_m_portnum;
	SCPY(p_callerinfo.interface, p_m_mISDNport->ifport->interface->name);

	/* dialing information */
	SCAT(p_dialinginfo.id, mncc->called.number);
	switch (mncc->called.type) {
		case 0x1:
		p_dialinginfo.ntype = INFO_NTYPE_INTERNATIONAL;
		break;
		case 0x2:
		p_dialinginfo.ntype = INFO_NTYPE_NATIONAL;
		break;
		case 0x4:
		p_dialinginfo.ntype = INFO_NTYPE_SUBSCRIBER;
		break;
		default:
		p_dialinginfo.ntype = INFO_NTYPE_UNKNOWN;
		break;
	}
	if (mncc->emergency) {
		SCPY(p_dialinginfo.id, "emergency");
	}
	p_dialinginfo.sending_complete = 1;

	/* bearer capability */
	// todo
	p_capainfo.bearer_capa = INFO_BC_SPEECH;
	p_capainfo.bearer_info1 = (options.law=='a')?3:2;
	p_capainfo.bearer_mode = INFO_BMODE_CIRCUIT;
	p_capainfo.source_mode = B_MODE_TRANSPARENT;
	p_m_g_mode = p_capainfo.source_mode;

	/* useruser */

	/* hunt channel */
	ret = channel = hunt_bchannel();
	if (ret < 0)
		goto no_channel;

	/* open channel */
	ret = seize_bchannel(channel, 1);
	if (ret < 0) {
		no_channel:
		mncc = create_mncc(MNCC_REJ_REQ, p_m_g_callref);
		gsm_trace_header(p_m_mISDNport, this, MNCC_REJ_REQ, DIRECTION_OUT);
		mncc->fields |= MNCC_F_CAUSE;
		mncc->cause.coding = 3;
		mncc->cause.location = 1;
		mncc->cause.value = 34;
		add_trace("cause", "coding", "%d", mncc->cause.coding);
		add_trace("cause", "location", "%d", mncc->cause.location);
		add_trace("cause", "value", "%d", mncc->cause.value);
		add_trace("reason", NULL, "no channel");
		end_trace();
		send_and_free_mncc((struct gsm_network *)gsm->network, mncc->msg_type, mncc);
		new_state(PORT_STATE_RELEASE);
		p_m_delete = 1;
		return;
	}
	bchannel_event(p_m_mISDNport, p_m_b_index, B_EVENT_USE);
	if (bchannel_open(p_m_b_index))
		goto no_channel;

	/* what infos did we got ... */
	gsm_trace_header(p_m_mISDNport, this, msg_type, DIRECTION_IN);
	if (p_callerinfo.id[0])
		add_trace("calling", "number", "%s", p_callerinfo.id);
	else
		SPRINT(p_callerinfo.id, "imsi-%s", p_callerinfo.imsi);
	add_trace("calling", "imsi", "%s", p_callerinfo.imsi);
	add_trace("dialing", "number", "%s", p_dialinginfo.id);
	end_trace();

	/* create endpoint */
	if (p_epointlist)
		FATAL("Incoming call but already got an endpoint.\n");
	if (!(epoint = new Endpoint(p_serial, 0)))
		FATAL("No memory for Endpoint instance\n");
	if (!(epoint->ep_app = new DEFAULT_ENDPOINT_APP(epoint, 0))) //incoming
		FATAL("No memory for Endpoint Application instance\n");
	epointlist_new(epoint->ep_serial);

	/* modify lchan to GSM codec V1 */
	gsm_trace_header(p_m_mISDNport, this, MNCC_LCHAN_MODIFY, DIRECTION_OUT);
	mode = create_mncc(MNCC_LCHAN_MODIFY, p_m_g_callref);
	mode->lchan_mode = 0x01; /* GSM V1 */
	add_trace("mode", NULL, "0x%02x", mode->lchan_mode);
	end_trace();
	send_and_free_mncc((struct gsm_network *)gsm->network, mode->msg_type, mode);

	/* send call proceeding */
	gsm_trace_header(p_m_mISDNport, this, MNCC_CALL_PROC_REQ, DIRECTION_OUT);
	proceeding = create_mncc(MNCC_CALL_PROC_REQ, p_m_g_callref);
	if (p_m_mISDNport->tones) {
		proceeding->fields |= MNCC_F_PROGRESS;
		proceeding->progress.coding = 3; /* GSM */
		proceeding->progress.location = 1;
		proceeding->progress.descr = 8;
		add_trace("progress", "coding", "%d", proceeding->progress.coding);
		add_trace("progress", "location", "%d", proceeding->progress.location);
		add_trace("progress", "descr", "%d", proceeding->progress.descr);
	}
	end_trace();
	send_and_free_mncc((struct gsm_network *)gsm->network, proceeding->msg_type, proceeding);

	new_state(PORT_STATE_IN_PROCEEDING);

	if (p_m_mISDNport->tones && !p_m_g_tch_connected) { /* only if ... */
		gsm_trace_header(p_m_mISDNport, this, MNCC_FRAME_RECV, DIRECTION_OUT);
		end_trace();
		frame = create_mncc(MNCC_FRAME_RECV, p_m_g_callref);
		send_and_free_mncc((struct gsm_network *)gsm->network, frame->msg_type, frame);
		p_m_g_tch_connected = 1;
	}

	/* send setup message to endpoit */
	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_SETUP);
	message->param.setup.isdn_port = p_m_portnum;
	message->param.setup.port_type = p_type;
//	message->param.setup.dtmf = 0;
	memcpy(&message->param.setup.dialinginfo, &p_dialinginfo, sizeof(struct dialing_info));
	memcpy(&message->param.setup.callerinfo, &p_callerinfo, sizeof(struct caller_info));
	memcpy(&message->param.setup.capainfo, &p_capainfo, sizeof(struct capa_info));
	SCPY((char *)message->param.setup.useruser.data, (char *)mncc->useruser.info);
	message->param.setup.useruser.len = strlen(mncc->useruser.info);
	message->param.setup.useruser.protocol = mncc->useruser.proto;
	message_put(message);
}

/* DTMF INDICATION */
void Pgsm::start_dtmf_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	struct lcr_msg *message;
	struct gsm_mncc *resp;

	gsm_trace_header(p_m_mISDNport, this, msg_type, DIRECTION_IN);
	add_trace("keypad", NULL, "%c", mncc->keypad);
	end_trace();
	SPRINT(p_dialinginfo.id, "%c", mncc->keypad);
	p_dialinginfo.ntype = INFO_NTYPE_UNKNOWN;

	/* send resp */
	gsm_trace_header(p_m_mISDNport, this, MNCC_START_DTMF_RSP, DIRECTION_OUT);
	add_trace("keypad", NULL, "%c", mncc->keypad);
	end_trace();
	resp = create_mncc(MNCC_START_DTMF_RSP, p_m_g_callref);
	resp->keypad = mncc->keypad;
	send_and_free_mncc((struct gsm_network *)gsm->network, resp->msg_type, resp);

	/* send dialing information */
	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_INFORMATION);
	memcpy(&message->param.information, &p_dialinginfo, sizeof(struct dialing_info));
	message_put(message);
}
void Pgsm::stop_dtmf_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	struct gsm_mncc *resp;

	gsm_trace_header(p_m_mISDNport, this, msg_type, DIRECTION_IN);
	add_trace("keypad", NULL, "%c", mncc->keypad);
	end_trace();

	/* send resp */
	gsm_trace_header(p_m_mISDNport, this, MNCC_STOP_DTMF_RSP, DIRECTION_OUT);
	add_trace("keypad", NULL, "%c", mncc->keypad);
	end_trace();
	resp = create_mncc(MNCC_STOP_DTMF_RSP, p_m_g_callref);
	resp->keypad = mncc->keypad;
	send_and_free_mncc((struct gsm_network *)gsm->network, resp->msg_type, resp);
}

/* PROCEEDING INDICATION */
void Pgsm::call_conf_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	struct gsm_mncc *mode;

	gsm_trace_header(p_m_mISDNport, this, msg_type, DIRECTION_IN);
	if (mncc->fields & MNCC_F_CAUSE) {
		add_trace("cause", "coding", "%d", mncc->cause.coding);
		add_trace("cause", "location", "%", mncc->cause.location);
		add_trace("cause", "value", "%", mncc->cause.value);
	}
	end_trace();

	/* modify lchan to GSM codec V1 */
	gsm_trace_header(p_m_mISDNport, this, MNCC_LCHAN_MODIFY, DIRECTION_OUT);
	mode = create_mncc(MNCC_LCHAN_MODIFY, p_m_g_callref);
	mode->lchan_mode = 0x01; /* GSM V1 */
	add_trace("mode", NULL, "0x%02x", mode->lchan_mode);
	end_trace();
	send_and_free_mncc((struct gsm_network *)gsm->network, mode->msg_type, mode);

}

/* ALERTING INDICATION */
void Pgsm::alert_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	struct lcr_msg *message;

	gsm_trace_header(p_m_mISDNport, this, msg_type, DIRECTION_IN);
	end_trace();

	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_ALERTING);
	message_put(message);

	new_state(PORT_STATE_OUT_ALERTING);

}

/* CONNECT INDICATION */
void Pgsm::setup_cnf(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	struct gsm_mncc *resp, *frame;
	struct lcr_msg *message;

	SCPY(p_connectinfo.id, mncc->connected.number);
	SCPY(p_connectinfo.imsi, mncc->imsi);
	p_connectinfo.present = INFO_PRESENT_ALLOWED;
	p_connectinfo.screen = INFO_SCREEN_NETWORK;
	p_connectinfo.ntype = INFO_NTYPE_UNKNOWN;
	p_connectinfo.isdn_port = p_m_portnum;
	SCPY(p_connectinfo.interface, p_m_mISDNport->ifport->interface->name);

	gsm_trace_header(p_m_mISDNport, this, msg_type, DIRECTION_IN);
	if (p_connectinfo.id[0])
		add_trace("connect", "number", "%s", p_connectinfo.id);
	else if (mncc->imsi[0])
		SPRINT(p_connectinfo.id, "imsi-%s", p_connectinfo.imsi);
	if (mncc->imsi[0])
		add_trace("connect", "imsi", "%s", p_connectinfo.imsi);
	end_trace();

	/* send resp */
	gsm_trace_header(p_m_mISDNport, this, MNCC_SETUP_COMPL_REQ, DIRECTION_OUT);
	resp = create_mncc(MNCC_SETUP_COMPL_REQ, p_m_g_callref);
	end_trace();
	send_and_free_mncc((struct gsm_network *)gsm->network, resp->msg_type, resp);

	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_CONNECT);
	memcpy(&message->param.connectinfo, &p_connectinfo, sizeof(struct connect_info));
	message_put(message);

	new_state(PORT_STATE_CONNECT);

	if (!p_m_g_tch_connected) { /* only if ... */
		gsm_trace_header(p_m_mISDNport, this, MNCC_FRAME_RECV, DIRECTION_OUT);
		end_trace();
		frame = create_mncc(MNCC_FRAME_RECV, p_m_g_callref);
		send_and_free_mncc((struct gsm_network *)gsm->network, frame->msg_type, frame);
		p_m_g_tch_connected = 1;
	}
}

/* CONNECT ACK INDICATION */
void Pgsm::setup_compl_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	struct gsm_mncc *frame;

	gsm_trace_header(p_m_mISDNport, this, msg_type, DIRECTION_IN);
	end_trace();

	new_state(PORT_STATE_CONNECT);

	if (!p_m_g_tch_connected) { /* only if ... */
		gsm_trace_header(p_m_mISDNport, this, MNCC_FRAME_RECV, DIRECTION_OUT);
		end_trace();
		frame = create_mncc(MNCC_FRAME_RECV, p_m_g_callref);
		send_and_free_mncc((struct gsm_network *)gsm->network, frame->msg_type, frame);
		p_m_g_tch_connected = 1;
	}
}

/* DISCONNECT INDICATION */
void Pgsm::disc_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	struct lcr_msg *message;
	int cause = 16, location = 0;
	struct gsm_mncc *resp;

	gsm_trace_header(p_m_mISDNport, this, msg_type, DIRECTION_IN);
	if (mncc->fields & MNCC_F_CAUSE) {
		location = mncc->cause.location;
		cause = mncc->cause.value;
		add_trace("cause", "coding", "%d", mncc->cause.coding);
		add_trace("cause", "location", "%d", location);
		add_trace("cause", "value", "%d", cause);
	}
	end_trace();

	/* send release */
	resp = create_mncc(MNCC_REL_REQ, p_m_g_callref);
	gsm_trace_header(p_m_mISDNport, this, MNCC_REL_REQ, DIRECTION_OUT);
#if 0
	resp->fields |= MNCC_F_CAUSE;
	resp->cause.coding = 3;
	resp->cause.location = 1;
	resp->cause.value = cause;
	add_trace("cause", "coding", "%d", resp->cause.coding);
	add_trace("cause", "location", "%d", resp->cause.location);
	add_trace("cause", "value", "%d", resp->cause.value);
#endif
	end_trace();
	send_and_free_mncc((struct gsm_network *)gsm->network, resp->msg_type, resp);

	/* sending release to endpoint */
	while(p_epointlist) {
		message = message_create(p_serial, p_epointlist->epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = cause;
		message->param.disconnectinfo.location = location;
		message_put(message);
		/* remove epoint */
		free_epointlist(p_epointlist);
	}
	new_state(PORT_STATE_RELEASE);
	p_m_delete = 1;
}

/* CC_RELEASE INDICATION */
void Pgsm::rel_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	int location = 0, cause = 16;
	struct lcr_msg *message;

	gsm_trace_header(p_m_mISDNport, this, msg_type, DIRECTION_IN);
	if (mncc->fields & MNCC_F_CAUSE) {
		location = mncc->cause.location;
		cause = mncc->cause.value;
		add_trace("cause", "coding", "%d", mncc->cause.coding);
		add_trace("cause", "location", "%d", mncc->cause.location);
		add_trace("cause", "value", "%d", mncc->cause.value);
	}
	end_trace();

	/* sending release to endpoint */
	while(p_epointlist) {
		message = message_create(p_serial, p_epointlist->epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = cause;
		message->param.disconnectinfo.location = location;
		message_put(message);
		/* remove epoint */
		free_epointlist(p_epointlist);
	}
	new_state(PORT_STATE_RELEASE);
	p_m_delete = 1;
}

/* NOTIFY INDICATION */
void Pgsm::notify_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	gsm_trace_header(p_m_mISDNport, this, msg_type, DIRECTION_IN);
	end_trace();
}


/* HOLD INDICATION */
void Pgsm::hold_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	struct lcr_msg *message;
	struct gsm_mncc *resp, *frame;

	gsm_trace_header(p_m_mISDNport, this, msg_type, DIRECTION_IN);
	end_trace();

	/* notify the hold of call */
	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_NOTIFY);
	message->param.notifyinfo.notify = INFO_NOTIFY_REMOTE_HOLD;
	message->param.notifyinfo.local = 1; /* call is held by supplementary service */
	message_put(message);

	/* acknowledge hold */
	gsm_trace_header(p_m_mISDNport, this, MNCC_HOLD_CNF, DIRECTION_OUT);
	end_trace();
	resp = create_mncc(MNCC_HOLD_CNF, p_m_g_callref);
	send_and_free_mncc((struct gsm_network *)gsm->network, resp->msg_type, resp);

	/* disable audio */
	if (p_m_g_tch_connected) { /* it should be true */
		gsm_trace_header(p_m_mISDNport, this, MNCC_FRAME_DROP, DIRECTION_OUT);
		end_trace();
		frame = create_mncc(MNCC_FRAME_DROP, p_m_g_callref);
		send_and_free_mncc((struct gsm_network *)gsm->network, frame->msg_type, frame);
		p_m_g_tch_connected = 0;
	}
}


/* RETRIEVE INDICATION */
void Pgsm::retr_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	struct lcr_msg *message;
	struct gsm_mncc *resp, *frame;

	gsm_trace_header(p_m_mISDNport, this, msg_type, DIRECTION_IN);
	end_trace();

	/* notify the retrieve of call */
	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_NOTIFY);
	message->param.notifyinfo.notify = INFO_NOTIFY_REMOTE_RETRIEVAL;
	message->param.notifyinfo.local = 1; /* call is retrieved by supplementary service */
	message_put(message);

	/* acknowledge retr */
	gsm_trace_header(p_m_mISDNport, this, MNCC_RETRIEVE_CNF, DIRECTION_OUT);
	end_trace();
	resp = create_mncc(MNCC_RETRIEVE_CNF, p_m_g_callref);
	send_and_free_mncc((struct gsm_network *)gsm->network, resp->msg_type, resp);

	/* enable audio */
	if (!p_m_g_tch_connected) { /* it should be true */
		gsm_trace_header(p_m_mISDNport, this, MNCC_FRAME_RECV, DIRECTION_OUT);
		end_trace();
		frame = create_mncc(MNCC_FRAME_RECV, p_m_g_callref);
		send_and_free_mncc((struct gsm_network *)gsm->network, frame->msg_type, frame);
		p_m_g_tch_connected = 1;
	}
}

/*
 * BSC sends message to port
 */
static int message_bsc(struct gsm_network *net, int msg_type, void *arg)
{
	struct gsm_mncc *mncc = (struct gsm_mncc *)arg;
	unsigned int callref = mncc->callref;
	class Port *port;
	class Pgsm *pgsm = NULL;
	char name[64];
	struct mISDNport *mISDNport;

	/* Special messages */
	switch(msg_type) {
	}

	/* find callref */
	callref = mncc->callref;
	port = port_first;
	while(port) {
		if ((port->p_type & PORT_CLASS_mISDN_MASK) == PORT_CLASS_mISDN_GSM) {
			pgsm = (class Pgsm *)port;
			if (pgsm->p_m_g_callref == callref) {
				break;
			}
		}
		port = port->next;
	}

	if (msg_type == GSM_TCHF_FRAME) {
		if (port)
			pgsm->frame_receive((struct gsm_trau_frame *)arg);
		return 0;
	}

	if (!port) {
		if (msg_type != MNCC_SETUP_IND)
			return(0);
		/* find gsm port */
		mISDNport = mISDNport_first;
		while(mISDNport) {
			if (mISDNport->gsm)
				break;
			mISDNport = mISDNport->next;
		}
		if (!mISDNport) {
			struct gsm_mncc *rej;

			rej = create_mncc(MNCC_REJ_REQ, callref);
			rej->fields |= MNCC_F_CAUSE;
			rej->cause.coding = 3;
			rej->cause.location = 1;
			rej->cause.value = 27;
			gsm_trace_header(NULL, NULL, MNCC_REJ_REQ, DIRECTION_OUT);
			add_trace("cause", "coding", "%d", rej->cause.coding);
			add_trace("cause", "location", "%d", rej->cause.location);
			add_trace("cause", "value", "%d", rej->cause.value);
			end_trace();
			send_and_free_mncc((struct gsm_network *)gsm->network, rej->msg_type, rej);
			return 0;
		}
		/* creating port object, transparent until setup with hdlc */
		SPRINT(name, "%s-%d-in", mISDNport->ifport->interface->name, mISDNport->portnum);
		if (!(pgsm = new Pgsm(PORT_TYPE_GSM_IN, mISDNport, name, NULL, 0, 0, B_MODE_TRANSPARENT)))

			FATAL("Cannot create Port instance.\n");
	}

	switch(msg_type) {
		case MNCC_SETUP_IND:
		pgsm->setup_ind(msg_type, callref, mncc);
		break;

		case MNCC_START_DTMF_IND:
		pgsm->start_dtmf_ind(msg_type, callref, mncc);
		break;

		case MNCC_STOP_DTMF_IND:
		pgsm->stop_dtmf_ind(msg_type, callref, mncc);
		break;

		case MNCC_CALL_CONF_IND:
		pgsm->call_conf_ind(msg_type, callref, mncc);
		break;

		case MNCC_ALERT_IND:
		pgsm->alert_ind(msg_type, callref, mncc);
		break;

		case MNCC_SETUP_CNF:
		pgsm->setup_cnf(msg_type, callref, mncc);
		break;

		case MNCC_SETUP_COMPL_IND:
		pgsm->setup_compl_ind(msg_type, callref, mncc);
		break;

		case MNCC_DISC_IND:
		pgsm->disc_ind(msg_type, callref, mncc);
		break;

		case MNCC_REL_IND:
		case MNCC_REL_CNF:
		case MNCC_REJ_IND:
		pgsm->rel_ind(msg_type, callref, mncc);
		break;

		case MNCC_NOTIFY_IND:
		pgsm->notify_ind(msg_type, callref, mncc);
		break;

		case MNCC_HOLD_IND:
		pgsm->hold_ind(msg_type, callref, mncc);
		break;

		case MNCC_RETRIEVE_IND:
		pgsm->retr_ind(msg_type, callref, mncc);
		break;

		default:
		PDEBUG(DEBUG_GSM, "Pgsm(%s) gsm port with (caller id %s) received unhandled nessage: 0x%x\n", pgsm->p_name, pgsm->p_callerinfo.id, msg_type);
	}
	return(0);
}

/* MESSAGE_SETUP */
void Pgsm::message_setup(unsigned int epoint_id, int message_id, union parameter *param)
{
	struct lcr_msg *message;
	int ret;
	struct epoint_list *epointlist;
	struct gsm_mncc *mncc;
	int channel;

	/* copy setup infos to port */
	memcpy(&p_callerinfo, &param->setup.callerinfo, sizeof(p_callerinfo));
	memcpy(&p_dialinginfo, &param->setup.dialinginfo, sizeof(p_dialinginfo));
	memcpy(&p_capainfo, &param->setup.capainfo, sizeof(p_capainfo));
	memcpy(&p_redirinfo, &param->setup.redirinfo, sizeof(p_redirinfo));

	/* no number */
	if (!p_dialinginfo.id[0]) {
		gsm_trace_header(p_m_mISDNport, this, MNCC_SETUP_REQ, DIRECTION_OUT);
		add_trace("failure", NULL, "No dialed subscriber given.");
		end_trace();
		message = message_create(p_serial, epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = 28;
		message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		message_put(message);
		new_state(PORT_STATE_RELEASE);
		p_m_delete = 1;
		return;
	}
	
	/* release if port is blocked */
	if (p_m_mISDNport->ifport->block) {
		gsm_trace_header(p_m_mISDNport, this, MNCC_SETUP_REQ, DIRECTION_OUT);
		add_trace("failure", NULL, "Port blocked.");
		end_trace();
		message = message_create(p_serial, epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = 27; // temp. unavail.
		message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		message_put(message);
		new_state(PORT_STATE_RELEASE);
		p_m_delete = 1;
		return;
	}

	/* hunt channel */
	ret = channel = hunt_bchannel();
	if (ret < 0)
		goto no_channel;
	/* open channel */
	ret = seize_bchannel(channel, 1);
	if (ret < 0) {
		no_channel:
		gsm_trace_header(p_m_mISDNport, this, MNCC_SETUP_REQ, DIRECTION_OUT);
		add_trace("failure", NULL, "No internal audio channel available.");
		end_trace();
		message = message_create(p_serial, epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = 34;
		message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		message_put(message);
		new_state(PORT_STATE_RELEASE);
		p_m_delete = 1;
		return;
	}
	bchannel_event(p_m_mISDNport, p_m_b_index, B_EVENT_USE);
	if (bchannel_open(p_m_b_index))
		goto no_channel;

//		SCPY(&p_m_tones_dir, param->setup.ext.tones_dir);
	/* screen outgoing caller id */
	do_screen(1, p_callerinfo.id, sizeof(p_callerinfo.id), &p_callerinfo.ntype, &p_callerinfo.present, p_m_mISDNport->ifport->interface);

	/* attach only if not already */
	epointlist = p_epointlist;
	while(epointlist) {
		if (epointlist->epoint_id == epoint_id)
			break;
		epointlist = epointlist->next;
	}
	if (!epointlist)
		epointlist_new(epoint_id);

	/* creating l3id */
	l1l2l3_trace_header(p_m_mISDNport, this, L3_NEW_L3ID_REQ, DIRECTION_OUT);
	p_m_g_callref = new_callref++;
	add_trace("callref", "new", "0x%x", p_m_g_callref);
	end_trace();

	gsm_trace_header(p_m_mISDNport, this, MNCC_SETUP_REQ, DIRECTION_OUT);
	mncc = create_mncc(MNCC_SETUP_REQ, p_m_g_callref);
	/* caller information */
	mncc->fields |= MNCC_F_CALLING;
	mncc->calling.plan = 1;
	switch (p_callerinfo.ntype) {
		case INFO_NTYPE_UNKNOWN:
		mncc->calling.type = 0x0;
		break;
		case INFO_NTYPE_INTERNATIONAL:
		mncc->calling.type = 0x1;
		break;
		case INFO_NTYPE_NATIONAL:
		mncc->calling.type = 0x2;
		break;
		case INFO_NTYPE_SUBSCRIBER:
		mncc->calling.type = 0x4;
		break;
		default: /* INFO_NTYPE_NOTPRESENT */
		mncc->fields &= ~MNCC_F_CALLING;
		break;
	}
	switch (p_callerinfo.screen) {
		case INFO_SCREEN_USER:
		mncc->calling.screen = 0;
		break;
		default: /* INFO_SCREEN_NETWORK */
		mncc->calling.screen = 3;
		break;
	}
	switch (p_callerinfo.present) {
		case INFO_PRESENT_ALLOWED:
		mncc->calling.present = 0;
		break;
		case INFO_PRESENT_RESTRICTED:
		mncc->calling.present = 1;
		break;
		default: /* INFO_PRESENT_NOTAVAIL */
		mncc->calling.present = 2;
		break;
	}
	if (mncc->fields & MNCC_F_CALLING) {
		SCPY(mncc->calling.number, p_callerinfo.id);
		add_trace("calling", "type", "%d", mncc->calling.type);
		add_trace("calling", "plan", "%d", mncc->calling.plan);
		add_trace("calling", "present", "%d", mncc->calling.present);
		add_trace("calling", "screen", "%d", mncc->calling.screen);
		add_trace("calling", "number", "%s", mncc->calling.number);
	}
	/* dialing information */
	mncc->fields |= MNCC_F_CALLED;
	if (!strncmp(p_dialinginfo.id, "imsi-", 5)) {
		SCPY(mncc->imsi, p_dialinginfo.id+5);
		add_trace("dialing", "imsi", "%s", mncc->imsi);
	} else {
		SCPY(mncc->called.number, p_dialinginfo.id);
		add_trace("dialing", "number", "%s", mncc->called.number);
	}
	
	/* sending user-user */

	/* redirecting number */
	mncc->fields |= MNCC_F_REDIRECTING;
	mncc->redirecting.plan = 1;
	switch (p_redirinfo.ntype) {
		case INFO_NTYPE_UNKNOWN:
		mncc->redirecting.type = 0x0;
		break;
		case INFO_NTYPE_INTERNATIONAL:
		mncc->redirecting.type = 0x1;
		break;
		case INFO_NTYPE_NATIONAL:
		mncc->redirecting.type = 0x2;
		break;
		case INFO_NTYPE_SUBSCRIBER:
		mncc->redirecting.type = 0x4;
		break;
		default: /* INFO_NTYPE_NOTPRESENT */
		mncc->fields &= ~MNCC_F_REDIRECTING;
		break;
	}
	switch (p_redirinfo.screen) {
		case INFO_SCREEN_USER:
		mncc->redirecting.screen = 0;
		break;
		default: /* INFO_SCREE_NETWORK */
		mncc->redirecting.screen = 3;
		break;
	}
	switch (p_redirinfo.present) {
		case INFO_PRESENT_ALLOWED:
		mncc->redirecting.present = 0;
		break;
		case INFO_PRESENT_RESTRICTED:
		mncc->redirecting.present = 1;
		break;
		default: /* INFO_PRESENT_NOTAVAIL */
		mncc->redirecting.present = 2;
		break;
	}
	/* sending redirecting number only in ntmode */
	if (mncc->fields & MNCC_F_REDIRECTING) {
		SCPY(mncc->redirecting.number, p_redirinfo.id);
		add_trace("redir", "type", "%d", mncc->redirecting.type);
		add_trace("redir", "plan", "%d", mncc->redirecting.plan);
		add_trace("redir", "present", "%d", mncc->redirecting.present);
		add_trace("redir", "screen", "%d", mncc->redirecting.screen);
		add_trace("redir", "number", "%s", mncc->redirecting.number);
	}
	/* bearer capability */
	//todo

	end_trace();
	send_and_free_mncc((struct gsm_network *)gsm->network, mncc->msg_type, mncc);

	new_state(PORT_STATE_OUT_SETUP);

	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_PROCEEDING);
	message_put(message);

	new_state(PORT_STATE_OUT_PROCEEDING);
}

/* MESSAGE_NOTIFY */
void Pgsm::message_notify(unsigned int epoint_id, int message_id, union parameter *param)
{
	struct gsm_mncc *mncc;
	int notify;

//	printf("if = %d\n", param->notifyinfo.notify);
	if (param->notifyinfo.notify>INFO_NOTIFY_NONE) {
		notify = param->notifyinfo.notify & 0x7f;
		if (p_state!=PORT_STATE_CONNECT /*&& p_state!=PORT_STATE_IN_PROCEEDING*/ && p_state!=PORT_STATE_IN_ALERTING) {
			/* queue notification */
			if (p_m_g_notify_pending)
				message_free(p_m_g_notify_pending);
			p_m_g_notify_pending = message_create(ACTIVE_EPOINT(p_epointlist), p_serial, EPOINT_TO_PORT, message_id);
			memcpy(&p_m_g_notify_pending->param, param, sizeof(union parameter));
		} else {
			/* sending notification */
			gsm_trace_header(p_m_mISDNport, this, MNCC_NOTIFY_REQ, DIRECTION_OUT);
			add_trace("notify", NULL, "%d", notify);
			end_trace();
			mncc = create_mncc(MNCC_NOTIFY_REQ, p_m_g_callref);
			mncc->notify = notify;
			send_and_free_mncc((struct gsm_network *)gsm->network, mncc->msg_type, mncc);
		}
	}
}

/* MESSAGE_ALERTING */
void Pgsm::message_alerting(unsigned int epoint_id, int message_id, union parameter *param)
{
	struct gsm_mncc *mncc;

	/* send alert */
	gsm_trace_header(p_m_mISDNport, this, MNCC_ALERT_REQ, DIRECTION_OUT);
	mncc = create_mncc(MNCC_ALERT_REQ, p_m_g_callref);
	if (p_m_mISDNport->tones) {
		mncc->fields |= MNCC_F_PROGRESS;
		mncc->progress.coding = 3; /* GSM */
		mncc->progress.location = 1;
		mncc->progress.descr = 8;
		add_trace("progress", "coding", "%d", mncc->progress.coding);
		add_trace("progress", "location", "%d", mncc->progress.location);
		add_trace("progress", "descr", "%d", mncc->progress.descr);
	}
	end_trace();
	send_and_free_mncc((struct gsm_network *)gsm->network, mncc->msg_type, mncc);

	new_state(PORT_STATE_IN_ALERTING);

	if (p_m_mISDNport->tones && !p_m_g_tch_connected) { /* only if ... */
		gsm_trace_header(p_m_mISDNport, this, MNCC_FRAME_RECV, DIRECTION_OUT);
		end_trace();
		mncc = create_mncc(MNCC_FRAME_RECV, p_m_g_callref);
		send_and_free_mncc((struct gsm_network *)gsm->network, mncc->msg_type, mncc);
		p_m_g_tch_connected = 1;
	}
}

/* MESSAGE_CONNECT */
void Pgsm::message_connect(unsigned int epoint_id, int message_id, union parameter *param)
{
	struct gsm_mncc *mncc;

	/* copy connected information */
	memcpy(&p_connectinfo, &param->connectinfo, sizeof(p_connectinfo));
	/* screen outgoing caller id */
	do_screen(1, p_connectinfo.id, sizeof(p_connectinfo.id), &p_connectinfo.ntype, &p_connectinfo.present, p_m_mISDNport->ifport->interface);

	/* send connect */
	mncc = create_mncc(MNCC_SETUP_RSP, p_m_g_callref);
	gsm_trace_header(p_m_mISDNport, this, MNCC_SETUP_RSP, DIRECTION_OUT);
	/* caller information */
	mncc->fields |= MNCC_F_CONNECTED;
	mncc->connected.plan = 1;
	switch (p_callerinfo.ntype) {
		case INFO_NTYPE_UNKNOWN:
		mncc->connected.type = 0x0;
		break;
		case INFO_NTYPE_INTERNATIONAL:
		mncc->connected.type = 0x1;
		break;
		case INFO_NTYPE_NATIONAL:
		mncc->connected.type = 0x2;
		break;
		case INFO_NTYPE_SUBSCRIBER:
		mncc->connected.type = 0x4;
		break;
		default: /* INFO_NTYPE_NOTPRESENT */
		mncc->fields &= ~MNCC_F_CONNECTED;
		break;
	}
	switch (p_callerinfo.screen) {
		case INFO_SCREEN_USER:
		mncc->connected.screen = 0;
		break;
		default: /* INFO_SCREEN_NETWORK */
		mncc->connected.screen = 3;
		break;
	}
	switch (p_callerinfo.present) {
		case INFO_PRESENT_ALLOWED:
		mncc->connected.present = 0;
		break;
		case INFO_PRESENT_RESTRICTED:
		mncc->connected.present = 1;
		break;
		default: /* INFO_PRESENT_NOTAVAIL */
		mncc->connected.present = 2;
		break;
	}
	if (mncc->fields & MNCC_F_CONNECTED) {
		SCPY(mncc->connected.number, p_connectinfo.id);
		add_trace("connected", "type", "%d", mncc->connected.type);
		add_trace("connected", "plan", "%d", mncc->connected.plan);
		add_trace("connected", "present", "%d", mncc->connected.present);
		add_trace("connected", "screen", "%d", mncc->connected.screen);
		add_trace("connected", "number", "%s", mncc->connected.number);
	}
	end_trace();
	send_and_free_mncc((struct gsm_network *)gsm->network, mncc->msg_type, mncc);

	new_state(PORT_STATE_CONNECT_WAITING);
}

/* MESSAGE_DISCONNECT */
void Pgsm::message_disconnect(unsigned int epoint_id, int message_id, union parameter *param)
{
	struct gsm_mncc *mncc;

	/* send disconnect */
	mncc = create_mncc(MNCC_DISC_REQ, p_m_g_callref);
	gsm_trace_header(p_m_mISDNport, this, MNCC_DISC_REQ, DIRECTION_OUT);
	if (p_m_mISDNport->tones) {
		mncc->fields |= MNCC_F_PROGRESS;
		mncc->progress.coding = 3; /* GSM */
		mncc->progress.location = 1;
		mncc->progress.descr = 8;
		add_trace("progress", "coding", "%d", mncc->progress.coding);
		add_trace("progress", "location", "%d", mncc->progress.location);
		add_trace("progress", "descr", "%d", mncc->progress.descr);
	}
	mncc->fields |= MNCC_F_CAUSE;
	mncc->cause.coding = 3;
	mncc->cause.location = param->disconnectinfo.location;
	mncc->cause.value = param->disconnectinfo.cause;
	add_trace("cause", "coding", "%d", mncc->cause.coding);
	add_trace("cause", "location", "%d", mncc->cause.location);
	add_trace("cause", "value", "%d", mncc->cause.value);
	end_trace();
	send_and_free_mncc((struct gsm_network *)gsm->network, mncc->msg_type, mncc);

	new_state(PORT_STATE_OUT_DISCONNECT);

	if (p_m_mISDNport->tones && !p_m_g_tch_connected) { /* only if ... */
		gsm_trace_header(p_m_mISDNport, this, MNCC_FRAME_RECV, DIRECTION_OUT);
		end_trace();
		mncc = create_mncc(MNCC_FRAME_RECV, p_m_g_callref);
		send_and_free_mncc((struct gsm_network *)gsm->network, mncc->msg_type, mncc);
		p_m_g_tch_connected = 1;
	}
}


/* MESSAGE_RELEASE */
void Pgsm::message_release(unsigned int epoint_id, int message_id, union parameter *param)
{
	struct gsm_mncc *mncc;

	/* send release */
	mncc = create_mncc(MNCC_REL_REQ, p_m_g_callref);
	gsm_trace_header(p_m_mISDNport, this, MNCC_REL_REQ, DIRECTION_OUT);
	mncc->fields |= MNCC_F_CAUSE;
	mncc->cause.coding = 3;
	mncc->cause.location = param->disconnectinfo.location;
	mncc->cause.value = param->disconnectinfo.cause;
	add_trace("cause", "coding", "%d", mncc->cause.coding);
	add_trace("cause", "location", "%d", mncc->cause.location);
	add_trace("cause", "value", "%d", mncc->cause.value);
	end_trace();
	send_and_free_mncc((struct gsm_network *)gsm->network, mncc->msg_type, mncc);

	new_state(PORT_STATE_RELEASE);
	p_m_delete = 1;
	return;
}


/*
 * endpoint sends messages to the port
 */
int Pgsm::message_epoint(unsigned int epoint_id, int message_id, union parameter *param)
{
	if (PmISDN::message_epoint(epoint_id, message_id, param))
		return(1);

	switch(message_id) {
		case MESSAGE_SETUP: /* dial-out command received from epoint */
		if (p_state!=PORT_STATE_IDLE)
			break;
		message_setup(epoint_id, message_id, param);
		break;

		case MESSAGE_NOTIFY: /* display and notifications */
		message_notify(epoint_id, message_id, param);
		break;

//		case MESSAGE_FACILITY: /* facility message */
//		message_facility(epoint_id, message_id, param);
//		break;

		case MESSAGE_PROCEEDING: /* message not handles */
		break;

		case MESSAGE_ALERTING: /* call of endpoint is ringing */
		if (p_state!=PORT_STATE_IN_PROCEEDING)
			break;
		message_alerting(epoint_id, message_id, param);
		if (p_m_g_notify_pending) {
			/* send pending notify message during connect */
			message_notify(ACTIVE_EPOINT(p_epointlist), p_m_g_notify_pending->type, &p_m_g_notify_pending->param);
			message_free(p_m_g_notify_pending);
			p_m_g_notify_pending = NULL;
		}
		break;

		case MESSAGE_CONNECT: /* call of endpoint is connected */
		if (p_state!=PORT_STATE_IN_PROCEEDING
		 && p_state!=PORT_STATE_IN_ALERTING)
			break;
		message_connect(epoint_id, message_id, param);
		if (p_m_g_notify_pending) {
			/* send pending notify message during connect */
			message_notify(ACTIVE_EPOINT(p_epointlist), p_m_g_notify_pending->type, &p_m_g_notify_pending->param);
			message_free(p_m_g_notify_pending);
			p_m_g_notify_pending = NULL;
		}
		break;

		case MESSAGE_DISCONNECT: /* call has been disconnected */
		if (p_state!=PORT_STATE_IN_PROCEEDING
		 && p_state!=PORT_STATE_IN_ALERTING
		 && p_state!=PORT_STATE_OUT_SETUP
		 && p_state!=PORT_STATE_OUT_OVERLAP
		 && p_state!=PORT_STATE_OUT_PROCEEDING
		 && p_state!=PORT_STATE_OUT_ALERTING
		 && p_state!=PORT_STATE_CONNECT
		 && p_state!=PORT_STATE_CONNECT_WAITING)
			break;
		message_disconnect(epoint_id, message_id, param);
		break;

		case MESSAGE_RELEASE: /* release isdn port */
		if (p_state==PORT_STATE_RELEASE)
			break;
		message_release(epoint_id, message_id, param);
		break;

		default:
		PDEBUG(DEBUG_GSM, "Pgsm(%s) gsm port with (caller id %s) received unhandled nessage: %d\n", p_name, p_callerinfo.id, message_id);
	}

	return(1);
}


/*
 * handler
 */
int Pgsm::handler(void)
{
	int ret;
	int work = 0;
	unsigned char buffer[2048+MISDN_HEADER_LEN];
	struct mISDNhead *hh = (struct mISDNhead *)buffer;

	if ((ret = PmISDN::handler()))
		return(ret);

	/* handle destruction */
	if (p_m_delete) {
		delete this;
		return(-1);
	}

	/* handle message from bchannel */
	if (p_m_g_gsm_b_sock > -1) {
		ret = recv(p_m_g_gsm_b_sock, buffer, sizeof(buffer), 0);
		if (ret >= (int)MISDN_HEADER_LEN) {
			switch(hh->prim) {
				/* we don't care about confirms, we use rx data to sync tx */
				case PH_DATA_CNF:
				break;
				/* we receive audio data, we respond to it AND we send tones */
				case PH_DATA_IND:
				bchannel_receive(hh, buffer+MISDN_HEADER_LEN, ret-MISDN_HEADER_LEN);
				break;
				case PH_ACTIVATE_IND:
				p_m_g_gsm_b_active = 1;
				break;
				case PH_DEACTIVATE_IND:
				p_m_g_gsm_b_active = 0;
				break;
			}
			work = 1;
		} else {
			if (ret < 0 && errno != EWOULDBLOCK)
				PERROR("Read from GSM port, index %d failed with return code %d\n", ret);
		}
	}

	return(work);
}


/*
 * handles bsc select function within LCR's main loop
 */
int handle_gsm(void)
{
	int ret1, ret2;

	ret1 = bsc_upqueue((struct gsm_network *)gsm->network);
	ret2 = bsc_select_main(1); /* polling */
	if (ret1 || ret2)
		return 1;
	return 0;
}

static void gsm_sock_close(void)
{
	if (gsm->gsm_sock > -1)
		close(gsm->gsm_sock);
	gsm->gsm_sock = -1;
}

static int gsm_sock_open(char *portname)
{
	int ret;
	int cnt;
	unsigned long on = 1;
	struct sockaddr_mISDN addr;
	struct mISDN_devinfo devinfo;
	int pri, bri;

	/* check port counts */
	ret = ioctl(mISDNsocket, IMGETCOUNT, &cnt);
	if (ret < 0) {
		fprintf(stderr, "Cannot get number of mISDN devices. (ioctl IMGETCOUNT failed ret=%d)\n", ret);
		return(ret);
	}

	if (cnt <= 0) {
		PERROR_RUNTIME("Found no card. Please be sure to load card drivers.\n");
		return -EIO;
	}
	gsm->gsm_port = mISDN_getportbyname(mISDNsocket, cnt, portname);
	if (gsm->gsm_port < 0) {
		PERROR_RUNTIME("Port name '%s' not found, did you load loopback interface for GSM?.\n", portname);
		return gsm->gsm_port;
	}
	/* get protocol */
	bri = pri = 0;
	devinfo.id = gsm->gsm_port;
	ret = ioctl(mISDNsocket, IMGETDEVINFO, &devinfo);
	if (ret < 0) {
		PERROR_RUNTIME("Cannot get device information for port %d. (ioctl IMGETDEVINFO failed ret=%d)\n", gsm->gsm_port, ret);
		return ret;
	}
	if (devinfo.Dprotocols & (1 << ISDN_P_TE_S0)) {
		bri = 1;
	}
	if (devinfo.Dprotocols & (1 << ISDN_P_TE_E1)) {
		pri = 1;
	}
	if (!pri && !pri) {
		PERROR_RUNTIME("GSM port %d does not support TE PRI or TE BRI.\n", gsm->gsm_port);
	}
	/* open socket */
	if ((gsm->gsm_sock = socket(PF_ISDN, SOCK_DGRAM, (pri)?ISDN_P_TE_E1:ISDN_P_TE_S0)) < 0) {
		PERROR_RUNTIME("GSM port %d failed to open socket.\n", gsm->gsm_port);
		gsm_sock_close();
		return gsm->gsm_sock;
	}
	/* set nonblocking io */
	if ((ret = ioctl(gsm->gsm_sock, FIONBIO, &on)) < 0) {
		PERROR_RUNTIME("GSM port %d failed to set socket into nonblocking io.\n", gsm->gsm_port);
		gsm_sock_close();
		return ret;
	}
	/* bind socket to dchannel */
	memset(&addr, 0, sizeof(addr));
	addr.family = AF_ISDN;
	addr.dev = gsm->gsm_port;
	addr.channel = 0;
	if ((ret = bind(gsm->gsm_sock, (struct sockaddr *)&addr, sizeof(addr))) < 0) {
		PERROR_RUNTIME("GSM port %d failed to bind socket. (name = %s errno=%d)\n", gsm->gsm_port, portname, errno);
		gsm_sock_close();
		return (ret);
	}

	return 0;
}


int gsm_exit(int rc)
{
	/* free gsm instance */
	if (gsm) {
		if (gsm->gsm_sock > -1)
			gsm_sock_close();
		/* shutdown network */
		if (gsm->network)
			bsc_shutdown_net((struct gsm_network *)gsm->network);
		/* free network */
//		if (gsm->network) {
//			free((struct gsm_network *)gsm->network); /* TBD */
//		}
		free(gsm);
		gsm = NULL;
	}

	return(rc);
}

int gsm_init(void)
{
	char hlr[128], cfg[128], filename[128];
        mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	int pcapfd, rc;

	tall_bsc_ctx = talloc_named_const(NULL, 1, "openbsc");
	talloc_ctx_init();
	on_dso_load_token();
	on_dso_load_rrlp();

	/* seed the PRNG */
	srand(time(NULL));

	/* create gsm instance */
	gsm = (struct lcr_gsm *)MALLOC(sizeof(struct lcr_gsm));
	gsm->gsm_sock = -1;

	/* parse options */
	if (!gsm_conf(&gsm->conf)) {
		PERROR("%s", gsm_conf_error);
		return gsm_exit(-EINVAL);
	}

	/* set debug */
	if (gsm->conf.debug[0])
		debug_parse_category_mask(gsm->conf.debug);

	/* open pcap file */
	if (gsm->conf.pcapfile[0]) {
		if (gsm->conf.pcapfile[0] == '/')
			SCPY(filename, gsm->conf.pcapfile);
		else
			SPRINT(filename, "%s/%s", CONFIG_DATA, gsm->conf.pcapfile);
		pcapfd = open(filename, O_WRONLY|O_TRUNC|O_CREAT, mode);
		if (pcapfd < 0) {
			PERROR("Failed to open file for pcap\n");
			return gsm_exit(-1);
		}
		e1_set_pcap_fd(pcapfd);
	}

	/* use RTP proxy for audio streaming */
	ipacc_rtp_direct = 0;

	/* init database */
	if (gsm->conf.hlr[0] == '/')
		SCPY(hlr, gsm->conf.hlr);
	else
		SPRINT(hlr, "%s/%s", CONFIG_DATA, gsm->conf.hlr);
	if (db_init(hlr)) {
		PERROR("GSM DB: Failed to init database '%s'. Please check the option settings.\n", hlr);
		return gsm_exit(-1);
	}
	printf("DB: Database initialized.\n");
	if (db_prepare()) {
		PERROR("GSM DB: Failed to prepare database.\n");
		return gsm_exit(-1);
	}
	printf("DB: Database prepared.\n");

	/* bootstrap network */
	if (gsm->conf.openbsc_cfg[0] == '/')
		SCPY(cfg, gsm->conf.openbsc_cfg);
	else
		SPRINT(cfg, "%s/%s", CONFIG_DATA, gsm->conf.openbsc_cfg);
	rc = bsc_bootstrap_network(&message_bsc, cfg);
	if (rc < 0) {
		PERROR("Failed to bootstrap GSM network.\n");
		return gsm_exit(-1);
	}
	gsm->network = bsc_gsmnet;

	/* open gsm loop interface */
	if (gsm_sock_open(gsm->conf.interface_bsc)) {
		return gsm_exit(-1);
	}

	return 0;
}

