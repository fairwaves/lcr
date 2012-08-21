/*****************************************************************************\
**                                                                           **
** LCR                                                                       **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**            MNCC-Interface: Harald Welte				     **
**                                                                           **
** mISDN gsm                                                                 **
**                                                                           **
\*****************************************************************************/ 

#include "main.h"
#include "mncc.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
extern "C" {
#include "gsm_audio.h"
}

#include <assert.h>

#define SOCKET_RETRY_TIMER	5

//struct lcr_gsm *gsm = NULL;

int new_callref = 1;

/* names of MNCC-SAP */
static const struct _value_string {
	int msg_type;
	const char *name;
} mncc_names[] = {
	{ 0,			"New call ref" },
	{ 1,			"Codec negotiation" },
	{ MNCC_SETUP_REQ,	"MNCC_SETUP_REQ" },
	{ MNCC_SETUP_IND,	"MNCC_SETUP_IND" },
	{ MNCC_SETUP_RSP,	"MNCC_SETUP_RSP" },
	{ MNCC_SETUP_CNF,	"MNCC_SETUP_CNF" },
	{ MNCC_SETUP_COMPL_REQ,	"MNCC_SETUP_COMPL_REQ" },
	{ MNCC_SETUP_COMPL_IND,	"MNCC_SETUP_COMPL_IND" },
	{ MNCC_CALL_CONF_IND,	"MNCC_CALL_CONF_IND" },
	{ MNCC_CALL_PROC_REQ,	"MNCC_CALL_PROC_REQ" },
	{ MNCC_PROGRESS_REQ,	"MNCC_PROGRESS_REQ" },
	{ MNCC_ALERT_REQ,	"MNCC_ALERT_REQ" },
	{ MNCC_ALERT_IND,	"MNCC_ALERT_IND" },
	{ MNCC_NOTIFY_REQ,	"MNCC_NOTIFY_REQ" },
	{ MNCC_NOTIFY_IND,	"MNCC_NOTIFY_IND" },
	{ MNCC_DISC_REQ,	"MNCC_DISC_REQ" },
	{ MNCC_DISC_IND,	"MNCC_DISC_IND" },
	{ MNCC_REL_REQ,		"MNCC_REL_REQ" },
	{ MNCC_REL_IND,		"MNCC_REL_IND" },
	{ MNCC_REL_CNF,		"MNCC_REL_CNF" },
	{ MNCC_FACILITY_REQ,	"MNCC_FACILITY_REQ" },
	{ MNCC_FACILITY_IND,	"MNCC_FACILITY_IND" },
	{ MNCC_START_DTMF_IND,	"MNCC_START_DTMF_IND" },
	{ MNCC_START_DTMF_RSP,	"MNCC_START_DTMF_RSP" },
	{ MNCC_START_DTMF_REJ,	"MNCC_START_DTMF_REJ" },
	{ MNCC_STOP_DTMF_IND,	"MNCC_STOP_DTMF_IND" },
	{ MNCC_STOP_DTMF_RSP,	"MNCC_STOP_DTMF_RSP" },
	{ MNCC_MODIFY_REQ,	"MNCC_MODIFY_REQ" },
	{ MNCC_MODIFY_IND,	"MNCC_MODIFY_IND" },
	{ MNCC_MODIFY_RSP,	"MNCC_MODIFY_RSP" },
	{ MNCC_MODIFY_CNF,	"MNCC_MODIFY_CNF" },
	{ MNCC_MODIFY_REJ,	"MNCC_MODIFY_REJ" },
	{ MNCC_HOLD_IND,	"MNCC_HOLD_IND" },
	{ MNCC_HOLD_CNF,	"MNCC_HOLD_CNF" },
	{ MNCC_HOLD_REJ,	"MNCC_HOLD_REJ" },
	{ MNCC_RETRIEVE_IND,	"MNCC_RETRIEVE_IND" },
	{ MNCC_RETRIEVE_CNF,	"MNCC_RETRIEVE_CNF" },
	{ MNCC_RETRIEVE_REJ,	"MNCC_RETRIEVE_REJ" },
	{ MNCC_USERINFO_REQ,	"MNCC_USERINFO_REQ" },
	{ MNCC_USERINFO_IND,	"MNCC_USERINFO_IND" },
	{ MNCC_REJ_REQ,		"MNCC_REJ_REQ" },
	{ MNCC_REJ_IND,		"MNCC_REJ_IND" },
	{ MNCC_PROGRESS_IND,	"MNCC_PROGRESS_IND" },
	{ MNCC_CALL_PROC_IND,	"MNCC_CALL_PROC_IND" },
	{ MNCC_CALL_CONF_REQ,	"MNCC_CALL_CONF_REQ" },
	{ MNCC_START_DTMF_REQ,	"MNCC_START_DTMF_REQ" },
	{ MNCC_STOP_DTMF_REQ,	"MNCC_STOP_DTMF_REQ" },
	{ MNCC_HOLD_REQ,	"MNCC_HOLD_REQ " },
	{ MNCC_RETRIEVE_REQ,	"MNCC_RETRIEVE_REQ" },
	{ MNCC_LCHAN_MODIFY,	"MNCC_LCHAN_MODIFY" },
	{ 0,			NULL }
};

const char *mncc_name(int value)
{
	int i = 0;

	while (mncc_names[i].name) {
		if (mncc_names[i].msg_type == value)
			return mncc_names[i].name;
		i++;
	}
	return "unknown";
}

static int mncc_send(struct lcr_gsm *lcr_gsm, int msg_type, void *data);

/*
 * create and send mncc message
 */
struct gsm_mncc *create_mncc(int msg_type, unsigned int callref)
{
	struct gsm_mncc *mncc;

	mncc = (struct gsm_mncc *)MALLOC(sizeof(struct gsm_mncc));
	mncc->msg_type = msg_type;
	mncc->callref = callref;
	return (mncc);
}
int send_and_free_mncc(struct lcr_gsm *lcr_gsm, unsigned int msg_type, void *data)
{
	int ret = 0;

	if (lcr_gsm) {
		ret = mncc_send(lcr_gsm, msg_type, data);
	}
	free(data);

	return ret;
}

void Pgsm::send_mncc_rtp_connect(void)
{
	struct gsm_mncc_rtp *nrtp;

	nrtp = (struct gsm_mncc_rtp *) create_mncc(MNCC_RTP_CONNECT, p_g_callref);
	nrtp->ip = p_g_rtp_ip_remote;
	nrtp->port = p_g_rtp_port_remote;
	switch (p_g_media_type) {
	case MEDIA_TYPE_GSM:
		nrtp->payload_msg_type = GSM_TCHF_FRAME;
		break;
	case MEDIA_TYPE_GSM_EFR:
		nrtp->payload_msg_type = GSM_TCHF_FRAME_EFR;
		break;
	case MEDIA_TYPE_AMR:
		nrtp->payload_msg_type = GSM_TCH_FRAME_AMR;
		break;
	case MEDIA_TYPE_GSM_HR:
		nrtp->payload_msg_type = GSM_TCHH_FRAME;
		break;
	}
	nrtp->payload_type = p_g_payload_type;
	PDEBUG(DEBUG_GSM, "sending MNCC RTP connect with payload_msg_type=%x, payload_type=%d\n", nrtp->payload_msg_type, nrtp->payload_type);
	send_and_free_mncc(p_g_lcr_gsm, nrtp->msg_type, nrtp);
}

static int delete_event(struct lcr_work *work, void *instance, int index);

/*
 * constructor
 */
Pgsm::Pgsm(int type, char *portname, struct port_settings *settings, struct interface *interface) : Port(type, portname, settings, interface)
{
	p_g_tones = 0;
	if (interface->is_tones == IS_YES)
		p_g_tones = 1;
	p_g_earlyb = 0;
	if (interface->is_earlyb == IS_YES)
		p_g_earlyb = 1;
	p_g_rtp_bridge = 0;
	if (interface->rtp_bridge)
		p_g_rtp_bridge = 1;
	p_g_rtp_payloads = 0;
	memset(&p_g_samples, 0, sizeof(p_g_samples));
	p_callerinfo.itype = (interface->extension)?INFO_ITYPE_ISDN_EXTENSION:INFO_ITYPE_ISDN;
	memset(&p_g_delete, 0, sizeof(p_g_delete));
	add_work(&p_g_delete, delete_event, this, 0);
	p_g_lcr_gsm = NULL;
	p_g_callref = 0;
	p_g_mode = 0;
	p_g_gsm_b_sock = -1;
	p_g_gsm_b_index = -1;
	p_g_gsm_b_active = 0;
	p_g_notify_pending = NULL;
	p_g_setup_pending = NULL;
	p_g_connect_pending = NULL;
	p_g_decoder = gsm_audio_create();
	p_g_encoder = gsm_audio_create();
	if (!p_g_encoder || !p_g_decoder) {
		PERROR("Failed to create GSM audio codec instance\n");
		trigger_work(&p_g_delete);
	}
	p_g_rxpos = 0;
	p_g_tch_connected = 0;
	p_g_media_type = 0;

	PDEBUG(DEBUG_GSM, "Created new GSMPort(%s).\n", portname);
}

/*
 * destructor
 */
Pgsm::~Pgsm()
{
	PDEBUG(DEBUG_GSM, "Destroyed GSM process(%s).\n", p_name);

	del_work(&p_g_delete);

	/* remove queued message */
	if (p_g_notify_pending)
		message_free(p_g_notify_pending);
	if (p_g_setup_pending)
		message_free(p_g_setup_pending);
	if (p_g_connect_pending)
		message_free(p_g_connect_pending);

	/* close codec */
	if (p_g_encoder)
		gsm_audio_destroy(p_g_encoder);
	if (p_g_decoder)
		gsm_audio_destroy(p_g_decoder);
}


/* receive encoded frame from gsm */
void Pgsm::frame_receive(void *arg)
{
	struct gsm_data_frame *frame = (struct gsm_data_frame *)arg;
	unsigned char data[160];
	int i;

	if (!p_g_decoder)
		return;

	if (frame->msg_type != GSM_BAD_FRAME) {
		if ((frame->data[0]>>4) != 0xd)
			PERROR("received GSM frame with wrong magig 0x%x\n", frame->data[0]>>4);
	
		/* decode */
		gsm_audio_decode(p_g_decoder, frame->data, p_g_samples);
		for (i = 0; i < 160; i++) {
			data[i] = audio_s16_to_law[p_g_samples[i] & 0xffff];
		}
	} else if (p_echotest) {
		/* beep on bad frame */
		for (i = 0; i < 160; i++) {
			if ((i & 3) > 2)
				p_g_samples[i] = 15000;
			else
				p_g_samples[i] = -15000;
			data[i] = audio_s16_to_law[p_g_samples[i] & 0xffff];
		}
	} else {
		/* repeat on bad frame */
		for (i = 0; i < 160; i++) {
			p_g_samples[i] = (p_g_samples[i] * 14) >> 4;
			data[i] = audio_s16_to_law[p_g_samples[i] & 0xffff];
		}
	}

	/* record data */
	if (p_record)
		record(data, 160, 0); // from down
	if (p_tap)
		tap(data, 160, 0); // from down

	/* local echo */
	if (p_echotest)
		bridge_rx(data, 160);

	/* send to remote*/
	bridge_tx(data, 160);
}

/* send traffic to gsm */
int Pgsm::bridge_rx(unsigned char *data, int len)
{
	if (p_tone_name[0])
		return -EINVAL;

	return audio_send(data, len);
}

int Pgsm::audio_send(unsigned char *data, int len)
{
	unsigned char frame[33];

	/* record data */
	if (p_record)
		record(data, len, 1); // from up
	if (p_tap)
		tap(data, len, 1); // from up

	/* encoder init failed */
	if (!p_g_encoder)
		return -EINVAL;

	/* (currently) not connected, so don't flood tch! */
	if (!p_g_tch_connected)
		return -EINVAL;

	/* write to rx buffer */
	while(len--) {
		p_g_rxdata[p_g_rxpos++] = audio_law_to_s32[*data++];
		if (p_g_rxpos == 160) {
			p_g_rxpos = 0;

			/* encode data */
			gsm_audio_encode(p_g_encoder, p_g_rxdata, frame);
			frame_send(frame);
		}
	}

	return 0;
}

void Pgsm::frame_send(void *_frame)
{
	unsigned char buffer[sizeof(struct gsm_data_frame) + 33];
	struct gsm_data_frame *frame = (struct gsm_data_frame *)buffer;
	
	frame->msg_type = GSM_TCHF_FRAME;
	frame->callref = p_g_callref;
	memcpy(frame->data, _frame, 33);

	if (p_g_lcr_gsm) {
		mncc_send(p_g_lcr_gsm, frame->msg_type, frame);
	}
}

/*
 * create trace
 */
void gsm_trace_header(const char *interface_name, class Pgsm *port, unsigned int msg_type, int direction)
{
	char msgtext[64];
	struct interface *interface = interface_first;

	interface = getinterfacebyname(interface_name);
	if (!interface)
		return;

	/* select message and primitive text */
	SCPY(msgtext, mncc_name(msg_type));

	/* add direction */
	if (port) {
		switch(port->p_type) {
		case PORT_TYPE_GSM_BS_OUT:
		case PORT_TYPE_GSM_BS_IN:
			SCAT(msgtext, " LCR<->BSC");
			break;
		case PORT_TYPE_GSM_MS_OUT:
		case PORT_TYPE_GSM_MS_IN:
			SCAT(msgtext, " LCR<->MS");
			break;
		}
	} else
		SCAT(msgtext, " ----");

	/* init trace with given values */
	start_trace(-1,
		    interface,
		    port?numberrize_callerinfo(port->p_callerinfo.id, port->p_callerinfo.ntype, options.national, options.international):NULL,
		    port?port->p_dialinginfo.id:NULL,
		    direction,
		    CATEGORY_CH,
		    port?port->p_serial:0,
		    msgtext);
}

/* modify lchan to given payload type */
void Pgsm::modify_lchan(int media_type)
{
	struct gsm_mncc *mode;

	/* already modified to that payload type */
	if (p_g_media_type == media_type)
		return;

	p_g_media_type = media_type;
	gsm_trace_header(p_interface_name, this, MNCC_LCHAN_MODIFY, DIRECTION_OUT);
	mode = create_mncc(MNCC_LCHAN_MODIFY, p_g_callref);
	switch (media_type) {
	case MEDIA_TYPE_GSM_EFR:
		add_trace("speech", "version", "EFR given");
		mode->lchan_mode = 0x21; /* GSM V2 */
		break;
	case MEDIA_TYPE_AMR:
		add_trace("speech", "version", "AMR given");
		mode->lchan_mode = 0x41; /* GSM V3 */
		break;
	default:
		add_trace("speech", "version", "Full/Half Rate given");
		mode->lchan_mode = 0x01; /* GSM V1 */
	}
	mode->lchan_type = 0x02; /* FIXME: unused */
	add_trace("mode", NULL, "0x%02x", mode->lchan_mode);
	end_trace();
	send_and_free_mncc(p_g_lcr_gsm, mode->msg_type, mode);
}

/* CALL PROCEEDING INDICATION (from network) */
void Pgsm::call_proc_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	struct lcr_msg *message;
	struct gsm_mncc *frame;

	gsm_trace_header(p_interface_name, this, msg_type, DIRECTION_IN);
	end_trace();

	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_PROCEEDING);
	message_put(message);

	new_state(PORT_STATE_OUT_PROCEEDING);

	if (p_g_earlyb && !p_g_tch_connected) { /* only if ... */
		gsm_trace_header(p_interface_name, this, MNCC_FRAME_RECV, DIRECTION_OUT);
		end_trace();
		frame = create_mncc(MNCC_FRAME_RECV, p_g_callref);
		send_and_free_mncc(p_g_lcr_gsm, frame->msg_type, frame);
		p_g_tch_connected = 1;
	}
}

/* ALERTING INDICATION */
void Pgsm::alert_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	struct lcr_msg *message;
	struct gsm_mncc *frame;

	gsm_trace_header(p_interface_name, this, msg_type, DIRECTION_IN);
	end_trace();

	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_ALERTING);
	message_put(message);

	new_state(PORT_STATE_OUT_ALERTING);

	if (p_g_earlyb && !p_g_tch_connected) { /* only if ... */
		gsm_trace_header(p_interface_name, this, MNCC_FRAME_RECV, DIRECTION_OUT);
		end_trace();
		frame = create_mncc(MNCC_FRAME_RECV, p_g_callref);
		send_and_free_mncc(p_g_lcr_gsm, frame->msg_type, frame);
		p_g_tch_connected = 1;
	}
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
	SCPY(p_connectinfo.interface, p_interface_name);

	gsm_trace_header(p_interface_name, this, msg_type, DIRECTION_IN);
	if (p_connectinfo.id[0])
		add_trace("connect", "number", "%s", p_connectinfo.id);
	else if (mncc->imsi[0])
		SPRINT(p_connectinfo.id, "imsi-%s", p_connectinfo.imsi);
	if (mncc->imsi[0])
		add_trace("connect", "imsi", "%s", p_connectinfo.imsi);
	end_trace();

	/* send resp */
	gsm_trace_header(p_interface_name, this, MNCC_SETUP_COMPL_REQ, DIRECTION_OUT);
	resp = create_mncc(MNCC_SETUP_COMPL_REQ, p_g_callref);
	end_trace();
	send_and_free_mncc(p_g_lcr_gsm, resp->msg_type, resp);

	new_state(PORT_STATE_CONNECT);

	if (!p_g_tch_connected) { /* only if ... */
		gsm_trace_header(p_interface_name, this, MNCC_FRAME_RECV, DIRECTION_OUT);
		end_trace();
		frame = create_mncc(MNCC_FRAME_RECV, p_g_callref);
		send_and_free_mncc(p_g_lcr_gsm, frame->msg_type, frame);
		p_g_tch_connected = 1;
	}

	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_CONNECT);
	memcpy(&message->param.connectinfo, &p_connectinfo, sizeof(struct connect_info));

	/* if we have a bridge, but not yet modified, the phone accepts out requested payload.
	 * we force the first in list */
	if (p_g_rtp_bridge) {
		if (!p_g_media_type) {
			/* modify to first given type */
			modify_lchan(p_g_rtp_media_types[0]);
			/* also set payload type */
			p_g_payload_type = p_g_rtp_payload_types[0];
		}
		message->param.connectinfo.rtpinfo.media_types[0] = p_g_media_type;
		message->param.connectinfo.rtpinfo.payload_types[0] = p_g_payload_type;
		message->param.connectinfo.rtpinfo.payloads = 1;
	}

	if (p_g_rtp_bridge) {
		struct gsm_mncc_rtp *rtp;

		PDEBUG(DEBUG_GSM, "Request RTP peer info, before forwarding connect msg\n");
		p_g_connect_pending = message;
		rtp = (struct gsm_mncc_rtp *) create_mncc(MNCC_RTP_CREATE, p_g_callref);
		send_and_free_mncc(p_g_lcr_gsm, rtp->msg_type, rtp);
	} else
		message_put(message);
}

/* CONNECT ACK INDICATION */
void Pgsm::setup_compl_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	struct gsm_mncc *frame;

	gsm_trace_header(p_interface_name, this, msg_type, DIRECTION_IN);
	end_trace();

	new_state(PORT_STATE_CONNECT);

	if (!p_g_tch_connected) { /* only if ... */
		gsm_trace_header(p_interface_name, this, MNCC_FRAME_RECV, DIRECTION_OUT);
		end_trace();
		frame = create_mncc(MNCC_FRAME_RECV, p_g_callref);
		send_and_free_mncc(p_g_lcr_gsm, frame->msg_type, frame);
		p_g_tch_connected = 1;
	}
}

/* DISCONNECT INDICATION */
void Pgsm::disc_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	struct lcr_msg *message;
	int cause = 16, location = 0;
	struct gsm_mncc *resp;

	gsm_trace_header(p_interface_name, this, msg_type, DIRECTION_IN);
	if (mncc->fields & MNCC_F_CAUSE) {
		location = mncc->cause.location;
		cause = mncc->cause.value;
		add_trace("cause", "coding", "%d", mncc->cause.coding);
		add_trace("cause", "location", "%d", location);
		add_trace("cause", "value", "%d", cause);
	}
	end_trace();

	/* send release */
	resp = create_mncc(MNCC_REL_REQ, p_g_callref);
	gsm_trace_header(p_interface_name, this, MNCC_REL_REQ, DIRECTION_OUT);
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
	send_and_free_mncc(p_g_lcr_gsm, resp->msg_type, resp);

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
	trigger_work(&p_g_delete);
}

/* CC_RELEASE INDICATION */
void Pgsm::rel_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	int location = 0, cause = 16;
	struct lcr_msg *message;

	gsm_trace_header(p_interface_name, this, msg_type, DIRECTION_IN);
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
	trigger_work(&p_g_delete);
}

/* NOTIFY INDICATION */
void Pgsm::notify_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	struct lcr_msg *message;

	gsm_trace_header(p_interface_name, this, msg_type, DIRECTION_IN);
	add_trace("notify", NULL, "%d", mncc->notify);
	end_trace();

	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_NOTIFY);
	message->param.notifyinfo.notify = mncc->notify;
	message_put(message);
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
			if (p_g_notify_pending)
				message_free(p_g_notify_pending);
			p_g_notify_pending = message_create(ACTIVE_EPOINT(p_epointlist), p_serial, EPOINT_TO_PORT, message_id);
			memcpy(&p_g_notify_pending->param, param, sizeof(union parameter));
		} else {
			/* sending notification */
			gsm_trace_header(p_interface_name, this, MNCC_NOTIFY_REQ, DIRECTION_OUT);
			add_trace("notify", NULL, "%d", notify);
			end_trace();
			mncc = create_mncc(MNCC_NOTIFY_REQ, p_g_callref);
			mncc->notify = notify;
			send_and_free_mncc(p_g_lcr_gsm, mncc->msg_type, mncc);
		}
	}
}

/* RTP create indication */
void Pgsm::rtp_create_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	struct gsm_mncc_rtp *rtp = (struct gsm_mncc_rtp *) mncc;

	/* send queued setup, as we received remote RTP info */
	if (p_g_setup_pending) {
		struct lcr_msg *message;

		message = p_g_setup_pending;
		PDEBUG(DEBUG_GSM, "Got RTP peer info (%08x,%d) forwarding setup\n", rtp->ip, rtp->port);
		message->param.setup.rtpinfo.ip = rtp->ip;
		message->param.setup.rtpinfo.port = rtp->port;
		message_put(message);
		p_g_setup_pending = NULL;
	}
	if (p_g_connect_pending) {
		PDEBUG(DEBUG_GSM, "Got RTP peer info (%08x,%d) connecting RTP... \n", rtp->ip, rtp->port);
		send_mncc_rtp_connect();
	}
}

/* RTP connect indication */
void Pgsm::rtp_connect_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	struct lcr_msg *message;
	struct gsm_mncc_rtp *rtp = (struct gsm_mncc_rtp *) mncc;

	if (p_g_connect_pending) {
		message = p_g_connect_pending;
		PDEBUG(DEBUG_GSM, "Got RTP peer info (%08x,%d) forwarding connect\n", rtp->ip, rtp->port);
		message->param.connectinfo.rtpinfo.ip = rtp->ip;
		message->param.connectinfo.rtpinfo.port = rtp->port;
		message_put(message);
		p_g_connect_pending = NULL;
	}
}

/* MESSAGE_PROGRESS */
void Pgsm::message_progress(unsigned int epoint_id, int message_id, union parameter *param)
{
	if (param->progressinfo.progress == 8) {
		PDEBUG(DEBUG_GSM, "Remote provides tones for us\n");
		p_g_tones = 1;
	}

	if (param->progressinfo.rtpinfo.port) {
		PDEBUG(DEBUG_GSM, "PROGRESS with RTP peer info, sent to BSC (%08x,%d) with media %d, pt %d\n", param->progressinfo.rtpinfo.ip, param->progressinfo.rtpinfo.port, param->progressinfo.rtpinfo.media_types[0], param->progressinfo.rtpinfo.payload_types[0]);

		/* modify channel to givne type, also sets media type */
		modify_lchan(param->progressinfo.rtpinfo.media_types[0]);

		/* connect RTP */
		p_g_rtp_ip_remote = param->progressinfo.rtpinfo.ip;
		p_g_rtp_port_remote = param->progressinfo.rtpinfo.port;
		/* p_g_media_type is already set by modify_lchan() */
		p_g_payload_type = param->progressinfo.rtpinfo.payload_types[0];
		send_mncc_rtp_connect();
	}
}

/* MESSAGE_ALERTING */
void Pgsm::message_alerting(unsigned int epoint_id, int message_id, union parameter *param)
{
	struct gsm_mncc *mncc;

	/* send alert */
	gsm_trace_header(p_interface_name, this, MNCC_ALERT_REQ, DIRECTION_OUT);
	mncc = create_mncc(MNCC_ALERT_REQ, p_g_callref);
	if (p_g_tones) {
		mncc->fields |= MNCC_F_PROGRESS;
		mncc->progress.coding = 3; /* GSM */
		mncc->progress.location = 1;
		mncc->progress.descr = 8;
		add_trace("progress", "coding", "%d", mncc->progress.coding);
		add_trace("progress", "location", "%d", mncc->progress.location);
		add_trace("progress", "descr", "%d", mncc->progress.descr);
	}
	end_trace();
	send_and_free_mncc(p_g_lcr_gsm, mncc->msg_type, mncc);

	new_state(PORT_STATE_IN_ALERTING);

	if (p_g_tones && !p_g_tch_connected) { /* only if ... */
		gsm_trace_header(p_interface_name, this, MNCC_FRAME_RECV, DIRECTION_OUT);
		end_trace();
		mncc = create_mncc(MNCC_FRAME_RECV, p_g_callref);
		send_and_free_mncc(p_g_lcr_gsm, mncc->msg_type, mncc);
		p_g_tch_connected = 1;
	}
}

/* MESSAGE_CONNECT */
void Pgsm::message_connect(unsigned int epoint_id, int message_id, union parameter *param)
{
	struct gsm_mncc *mncc;

	/* copy connected information */
	memcpy(&p_connectinfo, &param->connectinfo, sizeof(p_connectinfo));
	/* screen outgoing caller id */
	do_screen(1, p_connectinfo.id, sizeof(p_connectinfo.id), &p_connectinfo.ntype, &p_connectinfo.present, p_interface_name);

	/* send connect */
	mncc = create_mncc(MNCC_SETUP_RSP, p_g_callref);
	gsm_trace_header(p_interface_name, this, MNCC_SETUP_RSP, DIRECTION_OUT);
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
	send_and_free_mncc(p_g_lcr_gsm, mncc->msg_type, mncc);

	new_state(PORT_STATE_CONNECT_WAITING);

	if (param->connectinfo.rtpinfo.port) {
		PDEBUG(DEBUG_GSM, "CONNECT with RTP peer info, sent to BSC (%08x,%d)\n", param->connectinfo.rtpinfo.ip, param->connectinfo.rtpinfo.port);

		/* modify channel to givne type, also sets media type */
		modify_lchan(param->connectinfo.rtpinfo.media_types[0]);

		/* connect RTP */
		p_g_rtp_ip_remote = param->connectinfo.rtpinfo.ip;
		p_g_rtp_port_remote = param->connectinfo.rtpinfo.port;
		/* p_g_media_type is already set by modify_lchan() */
		p_g_payload_type = param->connectinfo.rtpinfo.payload_types[0];
		send_mncc_rtp_connect();
	}
}

/* MESSAGE_DISCONNECT */
void Pgsm::message_disconnect(unsigned int epoint_id, int message_id, union parameter *param)
{
	struct gsm_mncc *mncc;

	/* send disconnect */
	mncc = create_mncc(MNCC_DISC_REQ, p_g_callref);
	gsm_trace_header(p_interface_name, this, MNCC_DISC_REQ, DIRECTION_OUT);
	if (p_g_tones) {
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
	send_and_free_mncc(p_g_lcr_gsm, mncc->msg_type, mncc);

	new_state(PORT_STATE_OUT_DISCONNECT);

	if (p_g_tones && !p_g_tch_connected) { /* only if ... */
		gsm_trace_header(p_interface_name, this, MNCC_FRAME_RECV, DIRECTION_OUT);
		end_trace();
		mncc = create_mncc(MNCC_FRAME_RECV, p_g_callref);
		send_and_free_mncc(p_g_lcr_gsm, mncc->msg_type, mncc);
		p_g_tch_connected = 1;
	}
}


/* MESSAGE_RELEASE */
void Pgsm::message_release(unsigned int epoint_id, int message_id, union parameter *param)
{
	struct gsm_mncc *mncc;

	/* send release */
	mncc = create_mncc(MNCC_REL_REQ, p_g_callref);
	gsm_trace_header(p_interface_name, this, MNCC_REL_REQ, DIRECTION_OUT);
	mncc->fields |= MNCC_F_CAUSE;
	mncc->cause.coding = 3;
	mncc->cause.location = param->disconnectinfo.location;
	mncc->cause.value = param->disconnectinfo.cause;
	add_trace("cause", "coding", "%d", mncc->cause.coding);
	add_trace("cause", "location", "%d", mncc->cause.location);
	add_trace("cause", "value", "%d", mncc->cause.value);
	end_trace();
	send_and_free_mncc(p_g_lcr_gsm, mncc->msg_type, mncc);

	new_state(PORT_STATE_RELEASE);
	trigger_work(&p_g_delete);
	return;
}

/*
 * endpoint sends messages to the port
 */
int Pgsm::message_epoint(unsigned int epoint_id, int message_id, union parameter *param)
{
	int ret = 0;

	if (Port::message_epoint(epoint_id, message_id, param))
		return 1;

	switch(message_id) {
		case MESSAGE_NOTIFY: /* display and notifications */
		ret = 1;
		message_notify(epoint_id, message_id, param);
		break;

//		case MESSAGE_FACILITY: /* facility message */
//		message_facility(epoint_id, message_id, param);
//		break;

		case MESSAGE_PROCEEDING: /* message not handles */
		ret = 1;
		break;

		case MESSAGE_PROGRESS:
		ret = 1;
		message_progress(epoint_id, message_id, param);
		break;

		case MESSAGE_ALERTING: /* call of endpoint is ringing */
		ret = 1;
		if (p_state!=PORT_STATE_IN_PROCEEDING)
			break;
		message_alerting(epoint_id, message_id, param);
		if (p_g_notify_pending) {
			/* send pending notify message during connect */
			message_notify(ACTIVE_EPOINT(p_epointlist), p_g_notify_pending->type, &p_g_notify_pending->param);
			message_free(p_g_notify_pending);
			p_g_notify_pending = NULL;
		}
		break;

		case MESSAGE_CONNECT: /* call of endpoint is connected */
		ret = 1;
		if (p_state!=PORT_STATE_IN_PROCEEDING
		 && p_state!=PORT_STATE_IN_ALERTING)
			break;
		message_connect(epoint_id, message_id, param);
		if (p_g_notify_pending) {
			/* send pending notify message during connect */
			message_notify(ACTIVE_EPOINT(p_epointlist), p_g_notify_pending->type, &p_g_notify_pending->param);
			message_free(p_g_notify_pending);
			p_g_notify_pending = NULL;
		}
		break;

		case MESSAGE_DISCONNECT: /* call has been disconnected */
		ret = 1;
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
		ret = 1;
		if (p_state==PORT_STATE_RELEASE)
			break;
		message_release(epoint_id, message_id, param);
		break;

	}

	return ret;
}

/* deletes only if l3id is release, otherwhise it will be triggered then */
static int delete_event(struct lcr_work *work, void *instance, int index)
{
	class Pgsm *gsmport = (class Pgsm *)instance;

	delete gsmport;

	return 0;
}

int gsm_exit(int rc)
{
	return(rc);
}

int gsm_init(void)
{
	/* seed the PRNG */
	srand(time(NULL));

	return 0;
}

/*
 * MNCC interface
 */

static int mncc_q_enqueue(struct lcr_gsm *lcr_gsm, struct gsm_mncc *mncc, unsigned int len)
{
	struct mncc_q_entry *qe;

	qe = (struct mncc_q_entry *) MALLOC(sizeof(*qe)+sizeof(*mncc)+len);
	if (!qe)
		return -ENOMEM;

	qe->next = NULL;
	qe->len = len;
	memcpy(qe->data, mncc, len);

	/* in case of empty list ... */
	if (!lcr_gsm->mncc_q_hd && !lcr_gsm->mncc_q_tail) {
		/* the list head and tail both point to the new qe */
		lcr_gsm->mncc_q_hd = lcr_gsm->mncc_q_tail = qe;
	} else {
		/* append to tail of list */
		lcr_gsm->mncc_q_tail->next = qe;
		lcr_gsm->mncc_q_tail = qe;
	}

	lcr_gsm->mncc_lfd.when |= LCR_FD_WRITE;

	return 0;
}

static struct mncc_q_entry *mncc_q_dequeue(struct lcr_gsm *lcr_gsm)
{
	struct mncc_q_entry *qe = lcr_gsm->mncc_q_hd;
	if (!qe)
		return NULL;

	/* dequeue the successfully sent message */
	lcr_gsm->mncc_q_hd = qe->next;
	if (!qe)
		return NULL;
	if (qe == lcr_gsm->mncc_q_tail)
		lcr_gsm->mncc_q_tail = NULL;

	return qe;
}

/* routine called by LCR code if it wants to send a message to OpenBSC */
static int mncc_send(struct lcr_gsm *lcr_gsm, int msg_type, void *data)
{
	int len = 0;

	/* FIXME: the caller should provide this */
	switch (msg_type) {
	case GSM_TCHF_FRAME:
		len = sizeof(struct gsm_data_frame) + 33;
		break;
	default:
		len = sizeof(struct gsm_mncc);
		break;
	}
		
	return mncc_q_enqueue(lcr_gsm, (struct gsm_mncc *)data, len);
}

/* close MNCC socket */
static int mncc_fd_close(struct lcr_gsm *lcr_gsm, struct lcr_fd *lfd)
{
	class Port *port;
	class Pgsm *pgsm = NULL;
	struct lcr_msg *message;

	PERROR("Lost MNCC socket, retrying in %u seconds\n", SOCKET_RETRY_TIMER);
	close(lfd->fd);
	unregister_fd(lfd);
	lfd->fd = -1;

	/* free all the calls that were running through the MNCC interface */
	port = port_first;
	while(port) {
		if ((port->p_type & PORT_CLASS_mISDN_MASK) == PORT_CLASS_GSM) {
			pgsm = (class Pgsm *)port;
			if (pgsm->p_g_lcr_gsm == lcr_gsm) {
				message = message_create(pgsm->p_serial, ACTIVE_EPOINT(pgsm->p_epointlist), PORT_TO_EPOINT, MESSAGE_RELEASE);
				message->param.disconnectinfo.cause = 27; // temp. unavail.
				message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
				message_put(message);
				pgsm->new_state(PORT_STATE_RELEASE);
				trigger_work(&pgsm->p_g_delete);
			}
		}
		port = port->next;
	}

	/* flush the queue */
	while (mncc_q_dequeue(lcr_gsm))
		;

	/* start the re-connect timer */
	schedule_timer(&lcr_gsm->socket_retry, SOCKET_RETRY_TIMER, 0);

	return 0;
}

/* write to OpenBSC via MNCC socket */
static int mncc_fd_write(struct lcr_fd *lfd, void *inst, int idx)
{
	struct lcr_gsm *lcr_gsm = (struct lcr_gsm *) inst;
	struct mncc_q_entry *qe, *qe2;
	int rc;

	while (1) {
		qe = lcr_gsm->mncc_q_hd;
		if (!qe) {
			lfd->when &= ~LCR_FD_WRITE;
			break;
		}
		rc = write(lfd->fd, qe->data, qe->len);
		if (rc == 0)
			return mncc_fd_close(lcr_gsm, lfd);
		if (rc < 0)
			return rc;
		if (rc < (int)qe->len)
			return -1;
		/* dequeue the successfully sent message */
		qe2 = mncc_q_dequeue(lcr_gsm);
		assert(qe == qe2);
		free(qe);
	}
	return 0;
}

/* read from OpenBSC via MNCC socket */
static int mncc_fd_read(struct lcr_fd *lfd, void *inst, int idx)
{
	struct lcr_gsm *lcr_gsm = (struct lcr_gsm *) inst;
	int rc;
	static char buf[sizeof(struct gsm_mncc)+1024];
	struct gsm_mncc *mncc_prim = (struct gsm_mncc *) buf;

	memset(buf, 0, sizeof(buf));
	rc = recv(lfd->fd, buf, sizeof(buf), 0);
	if (rc == 0)
		return mncc_fd_close(lcr_gsm, lfd);
	if (rc < 0)
		return rc;

	/* Hand the MNCC message into LCR */
	switch (lcr_gsm->type) {
#ifdef WITH_GSM_BS
	case LCR_GSM_TYPE_NETWORK:
		return message_bsc(lcr_gsm, mncc_prim->msg_type, mncc_prim);
#endif
#ifdef WITH_GSM_MS
	case LCR_GSM_TYPE_MS:
		return message_ms(lcr_gsm, mncc_prim->msg_type, mncc_prim);
#endif
	default:
		return 0;
	}
}

/* file descriptor callback if we can read or write form MNCC socket */
static int mncc_fd_cb(struct lcr_fd *lfd, unsigned int what, void *inst, int idx)
{
	int rc = 0;

	if (what & LCR_FD_READ)
		rc = mncc_fd_read(lfd, inst, idx);
	if (rc < 0)
		return rc;

	if (what & LCR_FD_WRITE)
		rc = mncc_fd_write(lfd, inst, idx);

	return rc;
}

int mncc_socket_retry_cb(struct lcr_timer *timer, void *inst, int index)
{
	struct lcr_gsm *lcr_gsm = (struct lcr_gsm *) inst;
	int fd, rc;

	lcr_gsm->mncc_lfd.fd = -1;

	fd = socket(PF_UNIX, SOCK_SEQPACKET, 0);
	if (fd < 0) {
		PERROR("Cannot create SEQPACKET socket, giving up!\n");
		return fd;
	}

	rc = connect(fd, (struct sockaddr *) &lcr_gsm->sun,
		     sizeof(lcr_gsm->sun));
	if (rc < 0) {
		PERROR("Could not connect to MNCC socket %s, "
			"retrying in %u seconds\n", lcr_gsm->sun.sun_path,
			SOCKET_RETRY_TIMER);
		close(fd);
		schedule_timer(&lcr_gsm->socket_retry, SOCKET_RETRY_TIMER, 0);
	} else {
		PDEBUG(DEBUG_GSM, "Connected to MNCC socket %s!\n", lcr_gsm->sun.sun_path);
		lcr_gsm->mncc_lfd.fd = fd;
		register_fd(&lcr_gsm->mncc_lfd, LCR_FD_READ, &mncc_fd_cb, lcr_gsm, 0);
	}

	return 0;
}

