/*****************************************************************************\
**                                                                           **
** LCR                                                                       **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** mISDN gsm (BS mode)                                                       **
**                                                                           **
\*****************************************************************************/ 

#include "main.h"
#include "mncc.h"

struct lcr_gsm *gsm_bs = NULL;

#define PAYLOAD_TYPE_GSM 3
#define PAYLOAD_TYPE_GSM_HALF 96
#define PAYLOAD_TYPE_GSM_EFR  97
#define PAYLOAD_TYPE_AMR      98

/*
 * DTMF stuff
 */
unsigned char dtmf_samples[16][8000];
static int dtmf_x[4] = { 1209, 1336, 1477, 1633 };
static int dtmf_y[4] = { 697, 770, 852, 941 };

void generate_dtmf(void)
{
	double fx, fy, sample;
	int i, x, y;
	unsigned char *law;

	for (y = 0; y < 4; y++) {
		fy = 2 * 3.1415927 * ((double)dtmf_y[y]) / 8000.0;
		for (x = 0; x < 4; x++) {
			fx = 2 * 3.1415927 * ((double)dtmf_x[x]) / 8000.0;
			law = dtmf_samples[y << 2 | x];
			for (i = 0; i < 8000; i++) {
				sample = sin(fy * ((double)i)) * 0.251 * 32767.0; /* -6 dB */
				sample += sin(fx * ((double)i)) * 0.158 * 32767.0; /* -8 dB */
				*law++ = audio_s16_to_law[(int)sample & 0xffff];
			}
		}
	}
}


/*
 * constructor
 */
Pgsm_bs::Pgsm_bs(int type, char *portname, struct port_settings *settings, struct interface *interface) : Pgsm(type, portname, settings, interface)
{
	p_g_lcr_gsm = gsm_bs;
	p_g_dtmf = NULL;
	p_g_dtmf_index = 0;

	PDEBUG(DEBUG_GSM, "Created new GSMBSPort(%s).\n", portname);
}

/*
 * destructor
 */
Pgsm_bs::~Pgsm_bs()
{
	PDEBUG(DEBUG_GSM, "Destroyed GSM BS process(%s).\n", p_name);
}

static const char *media_type2name(unsigned char media_type) {
	switch (media_type) {
	case MEDIA_TYPE_ULAW:
		return "PCMU";
	case MEDIA_TYPE_ALAW:
		return "PCMA";
	case MEDIA_TYPE_GSM:
		return "GSM";
	case MEDIA_TYPE_GSM_HR:
		return "GSM-HR";
	case MEDIA_TYPE_GSM_EFR:
		return "GSM-EFR";
	case MEDIA_TYPE_AMR:
		return "AMR";
	}

	return "UKN";
}

/* PROCEEDING INDICATION (from MS) */
void Pgsm_bs::call_conf_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	int media_types[8];
	unsigned char payload_types[8];
	int payloads = 0;

	gsm_trace_header(p_interface_name, this, msg_type, DIRECTION_IN);
	if (mncc->fields & MNCC_F_CAUSE) {
		add_trace("cause", "coding", "%d", mncc->cause.coding);
		add_trace("cause", "location", "%", mncc->cause.location);
		add_trace("cause", "value", "%", mncc->cause.value);
	}
	end_trace();

	new_state(PORT_STATE_OUT_PROCEEDING);

	/* get list of offered payload types
	 * if list ist empty, the FR V1 is selected */
	select_payload_type(mncc, payload_types, media_types, &payloads, sizeof(payload_types));
	/* if no given payload type is supported, we select from channel type */
	if (!payloads) {
		switch (mncc->lchan_type) {
		case GSM_LCHAN_TCH_F:
			media_types[0] = MEDIA_TYPE_GSM;
			payload_types[0] = PAYLOAD_TYPE_GSM;
			payloads = 1;
			break;
		case GSM_LCHAN_TCH_H:
			media_types[0] = MEDIA_TYPE_GSM_HR;
			payload_types[0] = 96; /* dynamic */
			payloads = 1;
			break;
		default:
			mncc = create_mncc(MNCC_REL_REQ, callref);
			gsm_trace_header(p_interface_name, this, MNCC_REL_REQ, DIRECTION_OUT);
			mncc->fields |= MNCC_F_CAUSE;
			mncc->cause.coding = 3;
			mncc->cause.location = 1;
			mncc->cause.value = 65;
			add_trace("cause", "coding", "%d", mncc->cause.coding);
			add_trace("cause", "location", "%d", mncc->cause.location);
			add_trace("cause", "value", "%d", mncc->cause.value);
			add_trace("reason", NULL, "Given lchan not supported");
			end_trace();
			send_and_free_mncc(p_g_lcr_gsm, mncc->msg_type, mncc);
			new_state(PORT_STATE_RELEASE);
			trigger_work(&p_g_delete);
			return;
		}
	}

	/* select first payload type that matches the rtp list */
	if (p_g_rtp_bridge) {
		int i, j;

		for (i = 0; i < p_g_rtp_payloads; i++) {
			for (j = 0; j < payloads; j++) {
				if (p_g_rtp_media_types[i] == media_types[j])
					break;
			}
			if (j < payloads)
				break;
		}
		if (i == p_g_rtp_payloads) {
			struct lcr_msg *message;

			/* payload offered by remote RTP is not supported */
			message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_RELEASE);
			message->param.disconnectinfo.cause = 65;
			message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
			message_put(message);
			/* send release */
			mncc = create_mncc(MNCC_REL_REQ, p_g_callref);
			gsm_trace_header(p_interface_name, this, MNCC_REL_REQ, DIRECTION_OUT);
			mncc->fields |= MNCC_F_CAUSE;
			mncc->cause.coding = 3;
			mncc->cause.location = LOCATION_PRIVATE_LOCAL;
			mncc->cause.value = 65;
			add_trace("cause", "coding", "%d", mncc->cause.coding);
			add_trace("cause", "location", "%d", mncc->cause.location);
			add_trace("cause", "value", "%d", mncc->cause.value);
			add_trace("reason", NULL, "None of the payload types are supported by MS");
			end_trace();
			send_and_free_mncc(p_g_lcr_gsm, mncc->msg_type, mncc);
			new_state(PORT_STATE_RELEASE);
			trigger_work(&p_g_delete);

			return;
		}
		modify_lchan(p_g_rtp_media_types[i]);
		/* use the payload type from received rtp list, not from locally generated payload types */
		p_g_payload_type = p_g_rtp_payload_types[i];
	} else {
		/* modify to first given payload */
		modify_lchan(media_types[0]);
		p_g_payload_type = payload_types[0];
	}
}

/* DTMF INDICATION */
void Pgsm_bs::start_dtmf_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	struct gsm_mncc *resp;

	gsm_trace_header(p_interface_name, this, msg_type, DIRECTION_IN);
	add_trace("keypad", NULL, "%c", mncc->keypad);
	end_trace();
	SPRINT(p_dialinginfo.id, "%c", mncc->keypad);
	p_dialinginfo.ntype = INFO_NTYPE_UNKNOWN;

	/* send resp */
	gsm_trace_header(p_interface_name, this, MNCC_START_DTMF_RSP, DIRECTION_OUT);
	add_trace("keypad", NULL, "%c", mncc->keypad);
	end_trace();
	resp = create_mncc(MNCC_START_DTMF_RSP, p_g_callref);
	resp->fields |= MNCC_F_KEYPAD;
	resp->keypad = mncc->keypad;
	send_and_free_mncc(p_g_lcr_gsm, resp->msg_type, resp);

	if (p_g_rtp_bridge) {
		/* if two members are bridged */
		if (p_bridge && p_bridge->first && p_bridge->first->next && !p_bridge->first->next->next) {
			class Port *remote = NULL;

			/* select other member */
			if (p_bridge->first->port == this)
				remote = p_bridge->first->next->port;
			if (p_bridge->first->next->port == this)
				remote = p_bridge->first->port;

			if (remote) {
				struct lcr_msg *message;

				/* send dtmf information, because we bridge RTP directly */
				message = message_create(0, remote->p_serial, EPOINT_TO_PORT, MESSAGE_DTMF);
				message->param.dtmf = mncc->keypad;
				message_put(message);
			}
		}
	} else {
		/* generate DTMF tones, since we do audio forwarding inside LCR */
		switch (mncc->keypad) {
			case '1': p_g_dtmf = dtmf_samples[0]; break;
			case '2': p_g_dtmf = dtmf_samples[1]; break;
			case '3': p_g_dtmf = dtmf_samples[2]; break;
			case 'a':
			case 'A': p_g_dtmf = dtmf_samples[3]; break;
			case '4': p_g_dtmf = dtmf_samples[4]; break;
			case '5': p_g_dtmf = dtmf_samples[5]; break;
			case '6': p_g_dtmf = dtmf_samples[6]; break;
			case 'b':
			case 'B': p_g_dtmf = dtmf_samples[7]; break;
			case '7': p_g_dtmf = dtmf_samples[8]; break;
			case '8': p_g_dtmf = dtmf_samples[9]; break;
			case '9': p_g_dtmf = dtmf_samples[10]; break;
			case 'c':
			case 'C': p_g_dtmf = dtmf_samples[11]; break;
			case '*': p_g_dtmf = dtmf_samples[12]; break;
			case '0': p_g_dtmf = dtmf_samples[13]; break;
			case '#': p_g_dtmf = dtmf_samples[14]; break;
			case 'd':
			case 'D': p_g_dtmf = dtmf_samples[15]; break;
		}
		p_g_dtmf_index = 0;
	}
}
void Pgsm_bs::stop_dtmf_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	struct gsm_mncc *resp;

	gsm_trace_header(p_interface_name, this, msg_type, DIRECTION_IN);
	add_trace("keypad", NULL, "%c", mncc->keypad);
	end_trace();

	/* send resp */
	gsm_trace_header(p_interface_name, this, MNCC_STOP_DTMF_RSP, DIRECTION_OUT);
	add_trace("keypad", NULL, "%c", mncc->keypad);
	end_trace();
	resp = create_mncc(MNCC_STOP_DTMF_RSP, p_g_callref);
	resp->keypad = mncc->keypad;
	send_and_free_mncc(p_g_lcr_gsm, resp->msg_type, resp);
	
	/* stop DTMF */
	p_g_dtmf = NULL;
}

void Pgsm_bs::rtp_modify(unsigned int msg_type, unsigned int callref, struct gsm_mncc_rtp *mncc)
{
	struct lcr_msg *message;
	gsm_trace_header(p_interface_name, this, msg_type, DIRECTION_IN);
	end_trace();

	if (p_g_tch_connected) {
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_RTP_MODIFY);
		message->param.rtpinfo.payloads = 1;
		message->param.rtpinfo.payload_types[0] = p_g_payload_type;
		message->param.rtpinfo.media_types[0] = p_g_media_type;
		message->param.rtpinfo.ip = mncc->ip;
		message->param.rtpinfo.port = mncc->port;
		message_put(message);
	}
}

/* HOLD INDICATION */
void Pgsm_bs::hold_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	struct lcr_msg *message;
	struct gsm_mncc *resp, *frame;

	gsm_trace_header(p_interface_name, this, msg_type, DIRECTION_IN);
	end_trace();

	/* notify the hold of call */
	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_NOTIFY);
	message->param.notifyinfo.notify = INFO_NOTIFY_REMOTE_HOLD;
	message->param.notifyinfo.local = 1; /* call is held by supplementary service */
	message->param.notifyinfo.media_type = p_g_media_type; /* current media type or 0 if not set */
	message->param.notifyinfo.payload_type = p_g_payload_type; /* current payload type */
	message_put(message);

	/* acknowledge hold */
	gsm_trace_header(p_interface_name, this, MNCC_HOLD_CNF, DIRECTION_OUT);
	end_trace();
	resp = create_mncc(MNCC_HOLD_CNF, p_g_callref);
	send_and_free_mncc(p_g_lcr_gsm, resp->msg_type, resp);

	/* disable audio */
	if (p_g_tch_connected) { /* it should be true */
		gsm_trace_header(p_interface_name, this, MNCC_FRAME_DROP, DIRECTION_OUT);
		end_trace();
		frame = create_mncc(MNCC_FRAME_DROP, p_g_callref);
		send_and_free_mncc(p_g_lcr_gsm, frame->msg_type, frame);
		p_g_tch_connected = 0;
	}
}


/* RETRIEVE INDICATION */
void Pgsm_bs::retr_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	struct lcr_msg *message;
	struct gsm_mncc *resp, *frame;

	gsm_trace_header(p_interface_name, this, msg_type, DIRECTION_IN);
	end_trace();

	/* notify the retrieve of call */
	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_NOTIFY);
	message->param.notifyinfo.notify = INFO_NOTIFY_REMOTE_RETRIEVAL;
	message->param.notifyinfo.local = 1; /* call is retrieved by supplementary service */
	message->param.notifyinfo.media_type = p_g_media_type; /* current media type or 0 if not set */
	message->param.notifyinfo.payload_type = p_g_payload_type; /* current payload type */
	message_put(message);

	/* acknowledge retr */
	gsm_trace_header(p_interface_name, this, MNCC_RETRIEVE_CNF, DIRECTION_OUT);
	end_trace();
	resp = create_mncc(MNCC_RETRIEVE_CNF, p_g_callref);
	send_and_free_mncc(p_g_lcr_gsm, resp->msg_type, resp);

	/* enable audio */
	if (!p_g_tch_connected) { /* it should be true */
		gsm_trace_header(p_interface_name, this, MNCC_FRAME_RECV, DIRECTION_OUT);
		end_trace();
		frame = create_mncc(MNCC_FRAME_RECV, p_g_callref);
		send_and_free_mncc(p_g_lcr_gsm, frame->msg_type, frame);
		p_g_tch_connected = 1;
	}
}

/*
 * select payload type by given list or GSM V1 FR 
 * return the payload type or 0 if not given 
 */

void Pgsm_bs::select_payload_type(struct gsm_mncc *mncc, unsigned char *payload_types, int *media_types, int *payloads, int max_payloads)
{
	int media_type;
	unsigned char payload_type;
	int half;
	void *encoder, *decoder;

	*payloads = 0;

	gsm_trace_header(p_interface_name, this, 1 /* codec negotioation */, DIRECTION_NONE);
	if ((mncc->fields & MNCC_F_BEARER_CAP)) {
		/* select preferred payload type from list */
		int i;

		add_trace("bearer", "capa", "given by MS");
		for (i = 0; mncc->bearer_cap.speech_ver[i] >= 0; i++) {
			half = 0;
			/* select payload type we support */
			switch (mncc->bearer_cap.speech_ver[i]) {
			case 0:
				add_trace("speech", "version", "Full Rate given");
				media_type = MEDIA_TYPE_GSM;
				payload_type = PAYLOAD_TYPE_GSM;
				encoder = p_g_fr_encoder;
				decoder = p_g_fr_decoder;
				break;
			case 2:
				add_trace("speech", "version", "EFR given");
				media_type = MEDIA_TYPE_GSM_EFR;
				payload_type = PAYLOAD_TYPE_GSM_EFR;
				encoder = p_g_amr_encoder;
				decoder = p_g_amr_decoder;
				break;
			case 4:
				add_trace("speech", "version", "AMR given");
				media_type = MEDIA_TYPE_AMR;
				payload_type = PAYLOAD_TYPE_AMR;
				encoder = p_g_amr_encoder;
				decoder = p_g_amr_decoder;
				break;
			case 1:
				add_trace("speech", "version", "Half Rate given");
				media_type = MEDIA_TYPE_GSM_HR;
				payload_type = PAYLOAD_TYPE_GSM_HALF;
				encoder = p_g_hr_encoder;
				decoder = p_g_hr_decoder;
				half = 1;
				break;
			case 5:
				add_trace("speech", "version", "AMR Half Rate given");
				media_type = MEDIA_TYPE_AMR;
				payload_type = PAYLOAD_TYPE_AMR;
				encoder = p_g_amr_encoder;
				decoder = p_g_amr_decoder;
				half = 1;
				break;
			default:
				add_trace("speech", "version", "%d given", mncc->bearer_cap.speech_ver[i]);
				media_type = 0;
				payload_type = 0;
			}
			/* wen don't support it, so we check the next */
			if (!media_type) {
				add_trace("speech", "ignored", "Not supported by LCR");
				continue;
			}
			if (!half && mncc->lchan_type != GSM_LCHAN_TCH_F) {
				add_trace("speech", "ignored", "Not TCH/F");
				continue;
			}
			if (half && mncc->lchan_type != GSM_LCHAN_TCH_H) {
				add_trace("speech", "ignored", "Not TCH/H");
				continue;
			}
			if (!p_g_rtp_bridge) {
				if (!encoder || !decoder) {
					add_trace("speech", "ignored", "Codec not supported");
					continue;
				}
			}
			if (*payloads <= max_payloads) {
				media_types[*payloads] = media_type;
				payload_types[*payloads] = payload_type;
				(*payloads)++;
			}
		}
	} else {
		add_trace("bearer", "capa", "not given by MS");
		add_trace("speech", "version", "Full Rate given");
		media_types[0] = MEDIA_TYPE_GSM;
		payload_types[0] = PAYLOAD_TYPE_GSM;
		*payloads = 1;
	}
	if (!(*payloads))
		add_trace("error", "", "All given payload types unsupported");
	end_trace();
}

/*
 * handles all indications
 */
/* SETUP INDICATION */
void Pgsm_bs::setup_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	class Endpoint *epoint;
	struct lcr_msg *message;
	struct gsm_mncc *proceeding, *frame;
	struct interface *interface;
	int media_types[8];
	unsigned char payload_types[8];
	int payloads = 0;

	interface = getinterfacebyname(p_interface_name);
	if (!interface) {
		PERROR("Cannot find interface %s.\n", p_interface_name);
		return;
	}

	/* process given callref */
	gsm_trace_header(p_interface_name, this, 0, DIRECTION_IN);
	add_trace("callref", "new", "0x%x", callref);
	if (p_g_callref) {
		/* release in case the ID is already in use */
		add_trace("error", NULL, "callref already in use");
		end_trace();
		mncc = create_mncc(MNCC_REJ_REQ, callref);
		gsm_trace_header(p_interface_name, this, MNCC_REJ_REQ, DIRECTION_OUT);
		mncc->fields |= MNCC_F_CAUSE;
		mncc->cause.coding = 3;
		mncc->cause.location = 1;
		mncc->cause.value = 47;
		add_trace("cause", "coding", "%d", mncc->cause.coding);
		add_trace("cause", "location", "%d", mncc->cause.location);
		add_trace("cause", "value", "%d", mncc->cause.value);
		add_trace("reason", NULL, "callref already in use");
		end_trace();
		send_and_free_mncc(p_g_lcr_gsm, mncc->msg_type, mncc);
		new_state(PORT_STATE_RELEASE);
		trigger_work(&p_g_delete);
		return;
	}
	p_g_callref = callref;
	end_trace();

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
	SCPY(p_callerinfo.interface, p_interface_name);

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
	p_capainfo.bearer_capa = INFO_BC_SPEECH;
	p_capainfo.bearer_info1 = (options.law=='a')?3:2;
	p_capainfo.bearer_mode = INFO_BMODE_CIRCUIT;
	p_capainfo.source_mode = B_MODE_TRANSPARENT;
	p_g_mode = p_capainfo.source_mode;

	/* get list of offered payload types
	 * if list ist empty, the FR V1 is selected */
	select_payload_type(mncc, payload_types, media_types, &payloads, sizeof(payload_types));
	/* if no given payload type is supported, we select from channel type */
	if (!payloads) {
		switch (mncc->lchan_type) {
		case GSM_LCHAN_TCH_F:
			media_types[0] = MEDIA_TYPE_GSM;
			payload_types[0] = PAYLOAD_TYPE_GSM;
			payloads = 1;
			break;
		case GSM_LCHAN_TCH_H:
			media_types[0] = MEDIA_TYPE_GSM_HR;
			payload_types[0] = 96; /* dynamic */
			payloads = 1;
			break;
		default:
			mncc = create_mncc(MNCC_REJ_REQ, callref);
			gsm_trace_header(p_interface_name, this, MNCC_REJ_REQ, DIRECTION_OUT);
			mncc->fields |= MNCC_F_CAUSE;
			mncc->cause.coding = 3;
			mncc->cause.location = 1;
			mncc->cause.value = 65;
			add_trace("cause", "coding", "%d", mncc->cause.coding);
			add_trace("cause", "location", "%d", mncc->cause.location);
			add_trace("cause", "value", "%d", mncc->cause.value);
			add_trace("reason", NULL, "Given lchan not supported");
			end_trace();
			send_and_free_mncc(p_g_lcr_gsm, mncc->msg_type, mncc);
			new_state(PORT_STATE_RELEASE);
			trigger_work(&p_g_delete);
			return;
		}
	}
#if 0
	/* if no given payload type is supported, we reject the call */
	if (!payloads) {
	}
#endif

	/* useruser */

	/* what infos did we got ... */
	gsm_trace_header(p_interface_name, this, msg_type, DIRECTION_IN);
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
	epoint->ep_app = new_endpointapp(epoint, 0, interface->app); //incoming
	epointlist_new(epoint->ep_serial);

	/* modify lchan in case of no rtp bridge */
	if (!p_g_rtp_bridge)
		modify_lchan(media_types[0]);

	/* send call proceeding */
	gsm_trace_header(p_interface_name, this, MNCC_CALL_PROC_REQ, DIRECTION_OUT);
	proceeding = create_mncc(MNCC_CALL_PROC_REQ, p_g_callref);
	if (p_g_tones) {
		proceeding->fields |= MNCC_F_PROGRESS;
		proceeding->progress.coding = 3; /* GSM */
		proceeding->progress.location = 1;
		proceeding->progress.descr = 8;
		add_trace("progress", "coding", "%d", proceeding->progress.coding);
		add_trace("progress", "location", "%d", proceeding->progress.location);
		add_trace("progress", "descr", "%d", proceeding->progress.descr);
	}
	end_trace();
	send_and_free_mncc(p_g_lcr_gsm, proceeding->msg_type, proceeding);

	new_state(PORT_STATE_IN_PROCEEDING);

	if (p_g_tones && !p_g_tch_connected) { /* only if ... */
		gsm_trace_header(p_interface_name, this, MNCC_FRAME_RECV, DIRECTION_OUT);
		end_trace();
		frame = create_mncc(MNCC_FRAME_RECV, p_g_callref);
		send_and_free_mncc(p_g_lcr_gsm, frame->msg_type, frame);
		p_g_tch_connected = 1;
	}

	/* send setup message to endpoit */
	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_SETUP);
	message->param.setup.port_type = p_type;
//	message->param.setup.dtmf = 0;
	memcpy(&message->param.setup.dialinginfo, &p_dialinginfo, sizeof(struct dialing_info));
	memcpy(&message->param.setup.callerinfo, &p_callerinfo, sizeof(struct caller_info));
	memcpy(&message->param.setup.capainfo, &p_capainfo, sizeof(struct capa_info));
	SCPY((char *)message->param.setup.useruser.data, (char *)mncc->useruser.info);
	message->param.setup.useruser.len = strlen(mncc->useruser.info);
	message->param.setup.useruser.protocol = mncc->useruser.proto;
	if (p_g_rtp_bridge) {
		struct gsm_mncc_rtp *rtp;
		int i;

		PDEBUG(DEBUG_GSM, "Request RTP peer info, before forwarding setup\n");
		p_g_setup_pending = message;
		rtp = (struct gsm_mncc_rtp *) create_mncc(MNCC_RTP_CREATE, p_g_callref);
		send_and_free_mncc(p_g_lcr_gsm, rtp->msg_type, rtp);

		for (i = 0; i < (int)sizeof(message->param.setup.rtpinfo.payload_types) && i < payloads; i++) {
			message->param.setup.rtpinfo.media_types[i] = media_types[i];
			message->param.setup.rtpinfo.payload_types[i] = payload_types[i];
			message->param.setup.rtpinfo.payloads++;
		}

	} else
		message_put(message);

}

/*
 * BSC sends message to port
 */
int message_bsc(struct lcr_gsm *lcr_gsm, int msg_type, void *arg)
{
	struct gsm_mncc *mncc = (struct gsm_mncc *)arg;
	unsigned int callref = mncc->callref;
	class Port *port;
	class Pgsm_bs *pgsm_bs = NULL;
	char name[64];
//	struct mISDNport *mISDNport;

	/* Special messages */
	switch(msg_type) {
	}

	/* find callref */
	callref = mncc->callref;
	port = port_first;
	while(port) {
		if ((port->p_type & PORT_CLASS_GSM_MASK) == PORT_CLASS_GSM_BS) {
			pgsm_bs = (class Pgsm_bs *)port;
			if (pgsm_bs->p_g_callref == callref) {
				break;
			}
		}
		port = port->next;
	}

	if (msg_type == MNCC_RTP_MODIFY) {
		pgsm_bs->rtp_modify(msg_type, callref, (struct gsm_mncc_rtp *)arg);
	}

	if (msg_type == GSM_TCHF_FRAME
	 || msg_type == GSM_TCHF_FRAME_EFR
	 || msg_type == GSM_TCHH_FRAME
	 || msg_type == GSM_TCH_FRAME_AMR
	 || msg_type == GSM_BAD_FRAME) {
		if (port) {
			/* inject DTMF, if enabled */
			if (pgsm_bs->p_g_dtmf) {
				unsigned char data[160];
				int i;

				for (i = 0; i < 160; i++) {
					data[i] = pgsm_bs->p_g_dtmf[pgsm_bs->p_g_dtmf_index++];
					if (pgsm_bs->p_g_dtmf_index == 8000)
						pgsm_bs->p_g_dtmf_index = 0;
				}
				/* send */
				pgsm_bs->bridge_tx(data, 160);
			} else
				pgsm_bs->frame_receive(arg);
			/* if we do not bridge we need to inject audio, if available */
			if (!pgsm_bs->p_bridge || pgsm_bs->p_tone_name[0]) {
				unsigned char data[160];
				int i;

				i = pgsm_bs->read_audio(data, 160);
				if (i)
					pgsm_bs->audio_send(data, i);
			}
		}
		return 0;
	}

	if (!port) {
		struct interface *interface;

		if (msg_type != MNCC_SETUP_IND)
			return(0);

		interface = getinterfacebyname(lcr_gsm->interface_name);
		if (!interface) {
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
			add_trace("reason", NULL, "interface %s not found", lcr_gsm->interface_name);
			end_trace();
			send_and_free_mncc(lcr_gsm, rej->msg_type, rej);
			return 0;
		}
		/* creating port object, transparent until setup with hdlc */
		SPRINT(name, "%s-%d-in", interface->name, 0);
		if (!(pgsm_bs = new Pgsm_bs(PORT_TYPE_GSM_BS_IN, name, NULL, interface)))
			FATAL("Cannot create Port instance.\n");
	}

	switch(msg_type) {
		case MNCC_SETUP_IND:
		pgsm_bs->setup_ind(msg_type, callref, mncc);
		break;

		case MNCC_RTP_CREATE:
		pgsm_bs->rtp_create_ind(msg_type, callref, mncc);
		break;

		case MNCC_RTP_CONNECT:
		pgsm_bs->rtp_connect_ind(msg_type, callref, mncc);
		break;

		case MNCC_START_DTMF_IND:
		pgsm_bs->start_dtmf_ind(msg_type, callref, mncc);
		break;

		case MNCC_STOP_DTMF_IND:
		pgsm_bs->stop_dtmf_ind(msg_type, callref, mncc);
		break;

		case MNCC_CALL_CONF_IND:
		pgsm_bs->call_conf_ind(msg_type, callref, mncc);
		break;

		case MNCC_ALERT_IND:
		pgsm_bs->alert_ind(msg_type, callref, mncc);
		break;

		case MNCC_SETUP_CNF:
		pgsm_bs->setup_cnf(msg_type, callref, mncc);
		break;

		case MNCC_SETUP_COMPL_IND:
		pgsm_bs->setup_compl_ind(msg_type, callref, mncc);
		break;

		case MNCC_DISC_IND:
		pgsm_bs->disc_ind(msg_type, callref, mncc);
		break;

		case MNCC_REL_IND:
		case MNCC_REL_CNF:
		case MNCC_REJ_IND:
		pgsm_bs->rel_ind(msg_type, callref, mncc);
		break;

		case MNCC_NOTIFY_IND:
		pgsm_bs->notify_ind(msg_type, callref, mncc);
		break;

		case MNCC_HOLD_IND:
		pgsm_bs->hold_ind(msg_type, callref, mncc);
		break;

		case MNCC_RETRIEVE_IND:
		pgsm_bs->retr_ind(msg_type, callref, mncc);
		break;

		default:
		PDEBUG(DEBUG_GSM, "Pgsm_bs(%s) gsm port with (caller id %s) received unhandled nessage: 0x%x\n", pgsm_bs->p_name, pgsm_bs->p_callerinfo.id, msg_type);
	}
	return(0);
}

/* MESSAGE_SETUP */
void Pgsm_bs::message_setup(unsigned int epoint_id, int message_id, union parameter *param)
{
	struct lcr_msg *message;
	struct epoint_list *epointlist;
	struct gsm_mncc *mncc;
	struct interface *interface;

	interface = getinterfacebyname(p_interface_name);
	if (!interface) {
		PERROR("Cannot find interface %s.\n", p_interface_name);
		return;
	}

	/* copy setup infos to port */
	memcpy(&p_callerinfo, &param->setup.callerinfo, sizeof(p_callerinfo));
	memcpy(&p_dialinginfo, &param->setup.dialinginfo, sizeof(p_dialinginfo));
	memcpy(&p_capainfo, &param->setup.capainfo, sizeof(p_capainfo));
	memcpy(&p_redirinfo, &param->setup.redirinfo, sizeof(p_redirinfo));

	/* no GSM MNCC connection */
	if (p_g_lcr_gsm->mncc_lfd.fd < 0) {
		gsm_trace_header(p_interface_name, this, MNCC_SETUP_REQ, DIRECTION_OUT);
		add_trace("failure", NULL, "No MNCC connection.");
		end_trace();
		message = message_create(p_serial, epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = 41; // temp. failure.
		message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		message_put(message);
		new_state(PORT_STATE_RELEASE);
		trigger_work(&p_g_delete);
		return;
	}

	/* no number */
	if (!p_dialinginfo.id[0]) {
		gsm_trace_header(p_interface_name, this, MNCC_SETUP_REQ, DIRECTION_OUT);
		add_trace("failure", NULL, "No dialed subscriber given.");
		end_trace();
		message = message_create(p_serial, epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = 28;
		message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		message_put(message);
		new_state(PORT_STATE_RELEASE);
		trigger_work(&p_g_delete);
		return;
	}

	/* unsupported codec for RTP bridge */
	if (param->setup.rtpinfo.port) {
		int i;

		p_g_rtp_payloads = 0;
		gsm_trace_header(p_interface_name, this, 1 /* codec negotioation */, DIRECTION_NONE);
		for (i = 0; i < param->setup.rtpinfo.payloads; i++) {
			switch (param->setup.rtpinfo.media_types[i]) {
			case MEDIA_TYPE_GSM:
			case MEDIA_TYPE_GSM_EFR:
			case MEDIA_TYPE_AMR:
			case MEDIA_TYPE_GSM_HR:
				add_trace("rtp", "payload", "%s:%d supported", media_type2name(param->setup.rtpinfo.media_types[i]), param->setup.rtpinfo.payload_types[i]);
				if (p_g_rtp_payloads < (int)sizeof(p_g_rtp_payload_types)) {
					p_g_rtp_media_types[p_g_rtp_payloads] = param->setup.rtpinfo.media_types[i];
					p_g_rtp_payload_types[p_g_rtp_payloads] = param->setup.rtpinfo.payload_types[i];
					p_g_rtp_payloads++;
				}
				break;
			default:
				add_trace("rtp", "payload", "%s:%d unsupported", media_type2name(param->setup.rtpinfo.media_types[i]), param->setup.rtpinfo.payload_types[i]);
			}
		}
		end_trace();
		if (!p_g_rtp_payloads) {
			gsm_trace_header(p_interface_name, this, MNCC_SETUP_REQ, DIRECTION_OUT);
			add_trace("failure", NULL, "No payload given that is supported by GSM");
			end_trace();
			message = message_create(p_serial, epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
			message->param.disconnectinfo.cause = 65;
			message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
			message_put(message);
			new_state(PORT_STATE_RELEASE);
			trigger_work(&p_g_delete);
			return;
		}
	}

//		SCPY(&p_m_tones_dir, param->setup.ext.tones_dir);
	/* screen outgoing caller id */
	do_screen(1, p_callerinfo.id, sizeof(p_callerinfo.id), &p_callerinfo.ntype, &p_callerinfo.present, p_interface_name);

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
	gsm_trace_header(p_interface_name, this, 0, DIRECTION_OUT);
	p_g_callref = new_callref++;
	add_trace("callref", "new", "0x%x", p_g_callref);
	end_trace();

	gsm_trace_header(p_interface_name, this, MNCC_SETUP_REQ, DIRECTION_OUT);
	mncc = create_mncc(MNCC_SETUP_REQ, p_g_callref);
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

	if (interface->gsm_bs_hr) {
		add_trace("lchan", "type", "TCH/H or TCH/F");
		mncc->lchan_type = GSM_LCHAN_TCH_H;
	} else {
		add_trace("lchan", "type", "TCH/F");
		mncc->lchan_type = GSM_LCHAN_TCH_F;
	}

	end_trace();
	send_and_free_mncc(p_g_lcr_gsm, mncc->msg_type, mncc);

	new_state(PORT_STATE_OUT_SETUP);

	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_PROCEEDING);
	message_put(message);

	/* RTP bridge */
	if (param->setup.rtpinfo.port) {
		p_g_rtp_bridge = 1;
		p_g_rtp_ip_remote = param->setup.rtpinfo.ip;
		p_g_rtp_port_remote = param->setup.rtpinfo.port;
	} else
		p_g_rtp_bridge = 0;
}

/*
 * endpoint sends messages to the port
 */
int Pgsm_bs::message_epoint(unsigned int epoint_id, int message_id, union parameter *param)
{
	if (Pgsm::message_epoint(epoint_id, message_id, param))
		return(1);

	switch(message_id) {
		case MESSAGE_SETUP: /* dial-out command received from epoint */
		if (p_state!=PORT_STATE_IDLE)
			break;
		message_setup(epoint_id, message_id, param);
		break;

		default:
		PDEBUG(DEBUG_GSM, "Pgsm_bs(%s) gsm port with (caller id %s) received unhandled nessage: %d\n", p_name, p_callerinfo.id, message_id);
	}

	return(1);
}

int gsm_bs_exit(int rc)
{
	/* free gsm instance */
	if (gsm_bs) {
		if (gsm_bs->mncc_lfd.fd > -1) {
			close(gsm_bs->mncc_lfd.fd);
			unregister_fd(&gsm_bs->mncc_lfd);
		}

		del_timer(&gsm_bs->socket_retry);
		free(gsm_bs);
		gsm_bs = NULL;
	}


	return(rc);
}

int gsm_bs_init(struct interface *interface)
{
	/* create gsm instance */
	gsm_bs = (struct lcr_gsm *)MALLOC(sizeof(struct lcr_gsm));

	SCPY(gsm_bs->interface_name, interface->name);
	gsm_bs->type = LCR_GSM_TYPE_NETWORK;
	gsm_bs->sun.sun_family = AF_UNIX;
	SCPY(gsm_bs->sun.sun_path, "/tmp/bsc_mncc");

	memset(&gsm_bs->socket_retry, 0, sizeof(gsm_bs->socket_retry));
	add_timer(&gsm_bs->socket_retry, mncc_socket_retry_cb, gsm_bs, 0);

	/* do the initial connect */
	mncc_socket_retry_cb(&gsm_bs->socket_retry, gsm_bs, 0);

	generate_dtmf();

	return 0;
}
