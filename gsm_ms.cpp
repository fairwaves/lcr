/*****************************************************************************\
**                                                                           **
** LCR                                                                       **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** mISDN gsm (MS mode)                                                       **
**                                                                           **
\*****************************************************************************/ 

#include "main.h"
#include "mncc.h"


struct lcr_gsm *gsm_ms_first = NULL;

static int dtmf_timeout(struct lcr_timer *timer, void *instance, int index);

#define DTMF_ST_IDLE		0	/* no DTMF active */
#define DTMF_ST_START		1	/* DTMF started, waiting for resp. */
#define DTMF_ST_MARK		2	/* wait tone duration */
#define DTMF_ST_STOP		3	/* DTMF stopped, waiting for resp. */
#define DTMF_ST_SPACE		4	/* wait space between tones */

#define RTP_PT_GSM_FULL		3

/*
 * constructor
 */
Pgsm_ms::Pgsm_ms(int type, char *portname, struct port_settings *settings, struct interface *interface) : Pgsm(type, portname, settings, interface)
{
	struct lcr_gsm *gsm_ms = gsm_ms_first;

	p_g_lcr_gsm = NULL;
	SCPY(p_g_ms_name, interface->gsm_ms_name);

	while (gsm_ms) {
		if (gsm_ms->type == LCR_GSM_TYPE_MS && !strcmp(gsm_ms->name, p_g_ms_name)) {
			p_g_lcr_gsm = gsm_ms;
			break;
		}
		gsm_ms = gsm_ms->gsm_ms_next;
	}

	p_g_dtmf_state = DTMF_ST_IDLE;
	p_g_dtmf_index = 0;
	p_g_dtmf[0] = '\0';
	memset(&p_g_dtmf_timer, 0, sizeof(p_g_dtmf_timer));
	add_timer(&p_g_dtmf_timer, dtmf_timeout, this, 0);

	PDEBUG(DEBUG_GSM, "Created new GSMMSPort(%s %s).\n", portname, p_g_ms_name);
}

/*
 * destructor
 */
Pgsm_ms::~Pgsm_ms()
{
	PDEBUG(DEBUG_GSM, "Destroyed GSM MS process(%s).\n", p_name);
	del_timer(&p_g_dtmf_timer);
}

/*
 * handles all indications
 */
/* SETUP INDICATION */
void Pgsm_ms::setup_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	class Endpoint *epoint;
	struct lcr_msg *message;
	struct gsm_mncc *proceeding, *frame;
	struct interface *interface;

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

	gsm_trace_header(p_interface_name, this, MNCC_SETUP_IND, DIRECTION_IN);
	/* caller information */
	p_callerinfo.ntype = INFO_NTYPE_NOTPRESENT;
	if (mncc->fields & MNCC_F_CALLING) {
		switch (mncc->calling.present) {
			case 1:
			p_callerinfo.present = INFO_PRESENT_RESTRICTED;
			break;
			case 2:
			p_callerinfo.present = INFO_PRESENT_NOTAVAIL;
			break;
			default:
			p_callerinfo.present = INFO_PRESENT_ALLOWED;
			break;
		}
		switch (mncc->calling.screen) {
			case 0:
			p_callerinfo.screen = INFO_SCREEN_USER;
			break;
			case 1:
			p_callerinfo.screen = INFO_SCREEN_USER_VERIFIED_PASSED;
			break;
			case 2:
			p_callerinfo.screen = INFO_SCREEN_USER_VERIFIED_FAILED;
			break;
			default:
			p_callerinfo.screen = INFO_SCREEN_NETWORK;
			break;
		}
		switch (mncc->calling.type) {
			case 0x0:
			p_callerinfo.ntype = INFO_NTYPE_UNKNOWN;
			break;
			case 0x1:
			p_callerinfo.ntype = INFO_NTYPE_INTERNATIONAL;
			break;
			case 0x2:
			p_callerinfo.ntype = INFO_NTYPE_NATIONAL;
			break;
			case 0x4:
			p_callerinfo.ntype = INFO_NTYPE_SUBSCRIBER;
			break;
			default:
			p_callerinfo.ntype = INFO_NTYPE_UNKNOWN;
			break;
		}
		SCPY(p_callerinfo.id, mncc->calling.number);
		add_trace("calling", "type", "%d", mncc->calling.type);
		add_trace("calling", "plan", "%d", mncc->calling.plan);
		add_trace("calling", "present", "%d", mncc->calling.present);
		add_trace("calling", "screen", "%d", mncc->calling.screen);
		add_trace("calling", "number", "%s", mncc->calling.number);
	}
	SCPY(p_callerinfo.interface, p_interface_name);
	/* dialing information */
	if (mncc->fields & MNCC_F_CALLED) {
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
		add_trace("dialing", "type", "%d", mncc->called.type);
		add_trace("dialing", "plan", "%d", mncc->called.plan);
		add_trace("dialing", "number", "%s", mncc->called.number);
	}
	p_dialinginfo.sending_complete = 1;
	/* redir info */
	p_redirinfo.ntype = INFO_NTYPE_NOTPRESENT;
	if (mncc->fields & MNCC_F_REDIRECTING) {
		switch (mncc->redirecting.present) {
			case 1:
			p_redirinfo.present = INFO_PRESENT_RESTRICTED;
			break;
			case 2:
			p_redirinfo.present = INFO_PRESENT_NOTAVAIL;
			break;
			default:
			p_redirinfo.present = INFO_PRESENT_ALLOWED;
			break;
		}
		switch (mncc->redirecting.screen) {
			case 0:
			p_redirinfo.screen = INFO_SCREEN_USER;
			break;
			case 1:
			p_redirinfo.screen = INFO_SCREEN_USER_VERIFIED_PASSED;
			break;
			case 2:
			p_redirinfo.screen = INFO_SCREEN_USER_VERIFIED_FAILED;
			break;
			default:
			p_redirinfo.screen = INFO_SCREEN_NETWORK;
			break;
		}
		switch (mncc->redirecting.type) {
			case 0x0:
			p_redirinfo.ntype = INFO_NTYPE_UNKNOWN;
			break;
			case 0x1:
			p_redirinfo.ntype = INFO_NTYPE_INTERNATIONAL;
			break;
			case 0x2:
			p_redirinfo.ntype = INFO_NTYPE_NATIONAL;
			break;
			case 0x4:
			p_redirinfo.ntype = INFO_NTYPE_SUBSCRIBER;
			break;
			default:
			p_redirinfo.ntype = INFO_NTYPE_UNKNOWN;
			break;
		}
		SCPY(p_redirinfo.id, mncc->redirecting.number);
		add_trace("redir", "type", "%d", mncc->redirecting.type);
		add_trace("redir", "plan", "%d", mncc->redirecting.plan);
		add_trace("redir", "present", "%d", mncc->redirecting.present);
		add_trace("redir", "screen", "%d", mncc->redirecting.screen);
		add_trace("redir", "number", "%s", mncc->redirecting.number);
	}
	/* bearer capability */
	if (mncc->fields & MNCC_F_BEARER_CAP) {
		switch (mncc->bearer_cap.transfer) {
			case 1:
			p_capainfo.bearer_capa = INFO_BC_DATAUNRESTRICTED;
			break;
			case 2:
			case 3:
			p_capainfo.bearer_capa = INFO_BC_AUDIO;
			p_capainfo.bearer_info1 = (options.law=='a')?3:2;
			break;
			default:
			p_capainfo.bearer_capa = INFO_BC_SPEECH;
			p_capainfo.bearer_info1 = (options.law=='a')?3:2;
			break;
		}
		switch (mncc->bearer_cap.mode) {
			case 1:
			p_capainfo.bearer_mode = INFO_BMODE_PACKET;
			break;
			default:
			p_capainfo.bearer_mode = INFO_BMODE_CIRCUIT;
			break;
		}
		add_trace("bearer", "transfer", "%d", mncc->bearer_cap.transfer);
		add_trace("bearer", "mode", "%d", mncc->bearer_cap.mode);
	} else {
		p_capainfo.bearer_capa = INFO_BC_SPEECH;
		p_capainfo.bearer_info1 = (options.law=='a')?3:2;
		p_capainfo.bearer_mode = INFO_BMODE_CIRCUIT;
	}
	/* if packet mode works some day, see dss1.cpp for conditions */
	p_capainfo.source_mode = B_MODE_TRANSPARENT;

	end_trace();

	/* create endpoint */
	if (p_epointlist)
		FATAL("Incoming call but already got an endpoint.\n");
	if (!(epoint = new Endpoint(p_serial, 0)))
		FATAL("No memory for Endpoint instance\n");
	epoint->ep_app = new_endpointapp(epoint, 0, interface->app); //incoming
	epointlist_new(epoint->ep_serial);

	/* modify lchan to GSM codec V1 */
	modify_lchan(RTP_PT_GSM_FULL);

	/* send call proceeding */
	gsm_trace_header(p_interface_name, this, MNCC_CALL_CONF_REQ, DIRECTION_OUT);
	proceeding = create_mncc(MNCC_CALL_CONF_REQ, p_g_callref);
	/* bearer capability (mandatory, if not present in setup message) */
	if (!(mncc->fields & MNCC_F_BEARER_CAP)) {
		proceeding->fields |= MNCC_F_BEARER_CAP;
		proceeding->bearer_cap.coding = 0;
		proceeding->bearer_cap.radio = 1;
		proceeding->bearer_cap.speech_ctm = 0;
		proceeding->bearer_cap.speech_ver[0] = 0;
		proceeding->bearer_cap.speech_ver[1] = -1; /* end of list */
		proceeding->bearer_cap.transfer = 0;
		proceeding->bearer_cap.mode = 0;
	}
	/* DTMF supported */
	proceeding->fields |= MNCC_F_CCCAP;
	proceeding->cccap.dtmf = 1;
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
	message_put(message);
}

/*
 * MS sends message to port
 */
int message_ms(struct lcr_gsm *gsm_ms, int msg_type, void *arg)
{
	struct gsm_mncc *mncc = (struct gsm_mncc *)arg;
	unsigned int callref = mncc->callref;
	class Port *port;
	class Pgsm_ms *pgsm_ms = NULL;
	char name[64];
//	struct mISDNport *mISDNport;

	/* Special messages */
	switch (msg_type) {
	}

	/* find callref */
	callref = mncc->callref;
	port = port_first;
	while(port) {
		if ((port->p_type & PORT_CLASS_GSM_MASK) == PORT_CLASS_GSM_MS) {
			pgsm_ms = (class Pgsm_ms *)port;
			if (pgsm_ms->p_g_callref == callref) {
				break;
			}
		}
		port = port->next;
	}

	if (msg_type == GSM_TCHF_FRAME
	 || msg_type == GSM_BAD_FRAME) {
		if (port)
			pgsm_ms->frame_receive(arg);
		return 0;
	}

	if (!port) {
		struct interface *interface;

		if (msg_type != MNCC_SETUP_IND)
			return(0);

		interface = getinterfacebyname(gsm_ms->interface_name);
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
			add_trace("reason", NULL, "interface %s not found", gsm_ms->interface_name);
			end_trace();
			send_and_free_mncc(gsm_ms, rej->msg_type, rej);
			return 0;
		}
		/* creating port object, transparent until setup with hdlc */
		SPRINT(name, "%s-%d-in", interface->name, 0);
		if (!(pgsm_ms = new Pgsm_ms(PORT_TYPE_GSM_MS_IN, name, NULL, interface)))
			FATAL("Cannot create Port instance.\n");
	}

	switch(msg_type) {
		case MNCC_SETUP_IND:
		pgsm_ms->setup_ind(msg_type, callref, mncc);
		break;

		case MNCC_CALL_PROC_IND:
		pgsm_ms->call_proc_ind(msg_type, callref, mncc);
		break;

		case MNCC_ALERT_IND:
		pgsm_ms->alert_ind(msg_type, callref, mncc);
		break;

		case MNCC_SETUP_CNF:
		pgsm_ms->setup_cnf(msg_type, callref, mncc);
		break;

		case MNCC_SETUP_COMPL_IND:
		pgsm_ms->setup_compl_ind(msg_type, callref, mncc);
		break;

		case MNCC_DISC_IND:
		pgsm_ms->disc_ind(msg_type, callref, mncc);
		break;

		case MNCC_REL_IND:
		case MNCC_REL_CNF:
		case MNCC_REJ_IND:
		pgsm_ms->rel_ind(msg_type, callref, mncc);
		break;

		case MNCC_NOTIFY_IND:
		pgsm_ms->notify_ind(msg_type, callref, mncc);
		break;

		case MNCC_START_DTMF_RSP:
		case MNCC_START_DTMF_REJ:
		case MNCC_STOP_DTMF_RSP:
		pgsm_ms->dtmf_statemachine(mncc);
		break;

		default:
		;
	}
	return(0);
}

/* MESSAGE_SETUP */
void Pgsm_ms::message_setup(unsigned int epoint_id, int message_id, union parameter *param)
{
	struct lcr_msg *message;
	struct epoint_list *epointlist;
	struct gsm_mncc *mncc;

	/* copy setup infos to port */
	memcpy(&p_callerinfo, &param->setup.callerinfo, sizeof(p_callerinfo));
	memcpy(&p_dialinginfo, &param->setup.dialinginfo, sizeof(p_dialinginfo));
	memcpy(&p_capainfo, &param->setup.capainfo, sizeof(p_capainfo));
	memcpy(&p_redirinfo, &param->setup.redirinfo, sizeof(p_redirinfo));

	/* no instance */
	if (!p_g_lcr_gsm || p_g_lcr_gsm->mncc_lfd.fd < 0) {
		gsm_trace_header(p_interface_name, this, MNCC_SETUP_REQ, DIRECTION_OUT);
		add_trace("failure", NULL, "MS %s instance is unavailable", p_g_ms_name);
		end_trace();
		message = message_create(p_serial, epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = 41;
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
	if (!strncasecmp(p_dialinginfo.id, "emerg", 5)) {
		mncc->emergency = 1;
	} else {
		/* caller info (only clir) */
		switch (p_callerinfo.present) {
			case INFO_PRESENT_ALLOWED:
			mncc->clir.inv = 1;
			break;
			default:
			mncc->clir.sup = 1;
		}
		/* dialing information (mandatory) */
		mncc->fields |= MNCC_F_CALLED;
		mncc->called.type = 0; /* unknown */
		mncc->called.plan = 1; /* isdn */
		SCPY(mncc->called.number, p_dialinginfo.id);
		add_trace("dialing", "number", "%s", mncc->called.number);
		
		/* bearer capability (mandatory) */
		mncc->fields |= MNCC_F_BEARER_CAP;
		mncc->bearer_cap.coding = 0;
		mncc->bearer_cap.radio = 1;
		mncc->bearer_cap.speech_ctm = 0;
		mncc->bearer_cap.speech_ver[0] = 0;
		mncc->bearer_cap.speech_ver[1] = -1; /* end of list */
		switch (p_capainfo.bearer_capa) {
			case INFO_BC_DATAUNRESTRICTED:
			case INFO_BC_DATARESTRICTED:
			mncc->bearer_cap.transfer = 1;
			break;
			case INFO_BC_SPEECH:
			mncc->bearer_cap.transfer = 0;
			break;
			default:
			mncc->bearer_cap.transfer = 2;
			break;
		}
		switch (p_capainfo.bearer_mode) {
			case INFO_BMODE_PACKET:
			mncc->bearer_cap.mode = 1;
			break;
			default:
			mncc->bearer_cap.mode = 0;
			break;
		}
		/* DTMF supported */
		mncc->fields |= MNCC_F_CCCAP;
		mncc->cccap.dtmf = 1;

		/* request keypad from remote */
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_ENABLEKEYPAD);
		message_put(message);
	}

	end_trace();
	send_and_free_mncc(p_g_lcr_gsm, mncc->msg_type, mncc);

	new_state(PORT_STATE_OUT_SETUP);

	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_PROCEEDING);
	message_put(message);

	new_state(PORT_STATE_OUT_PROCEEDING);

	p_g_rtp_bridge = 0;
}

void Pgsm_ms::dtmf_statemachine(struct gsm_mncc *mncc)
{
	struct gsm_mncc *dtmf;

	switch (p_g_dtmf_state) {
	case DTMF_ST_SPACE:
	case DTMF_ST_IDLE:
		/* end of string */
		if (!p_g_dtmf[p_g_dtmf_index]) {
			PDEBUG(DEBUG_GSM, "done with DTMF\n");
			p_g_dtmf_state = DTMF_ST_IDLE;
			return;
		}
		gsm_trace_header(p_interface_name, this, MNCC_START_DTMF_REQ, DIRECTION_OUT);
		dtmf = create_mncc(MNCC_START_DTMF_REQ, p_g_callref);
		dtmf->keypad = p_g_dtmf[p_g_dtmf_index++];
		p_g_dtmf_state = DTMF_ST_START;
		PDEBUG(DEBUG_GSM, "start DTMF (keypad %c)\n",
			dtmf->keypad);
		end_trace();
		send_and_free_mncc(p_g_lcr_gsm, dtmf->msg_type, dtmf);
		return;
	case DTMF_ST_START:
		if (mncc->msg_type != MNCC_START_DTMF_RSP) {
			PDEBUG(DEBUG_GSM, "DTMF was rejected\n");
			return;
		}
		schedule_timer(&p_g_dtmf_timer, 0, 70 * 1000);
		p_g_dtmf_state = DTMF_ST_MARK;
		PDEBUG(DEBUG_GSM, "DTMF is on\n");
		break;
	case DTMF_ST_MARK:
		gsm_trace_header(p_interface_name, this, MNCC_STOP_DTMF_REQ, DIRECTION_OUT);
		dtmf = create_mncc(MNCC_STOP_DTMF_REQ, p_g_callref);
		p_g_dtmf_state = DTMF_ST_STOP;
		end_trace();
		send_and_free_mncc(p_g_lcr_gsm, dtmf->msg_type, dtmf);
		return;
	case DTMF_ST_STOP:
		schedule_timer(&p_g_dtmf_timer, 0, 120 * 1000);
		p_g_dtmf_state = DTMF_ST_SPACE;
		PDEBUG(DEBUG_GSM, "DTMF is off\n");
		break;
	}
}

static int dtmf_timeout(struct lcr_timer *timer, void *instance, int index)
{
	class Pgsm_ms *pgsm_ms = (class Pgsm_ms *)instance;

	PDEBUG(DEBUG_GSM, "DTMF timer has fired\n");
	pgsm_ms->dtmf_statemachine(NULL);

	return 0;
}

/* MESSAGE_DTMF */
void Pgsm_ms::message_dtmf(unsigned int epoint_id, int message_id, union parameter *param)
{
	char digit = param->dtmf;

	if (digit >= 'a' && digit <= 'c')
		digit = digit - 'a' + 'A';
	if (!strchr("01234567890*#ABC", digit))
		return;

	/* schedule */
	if (p_g_dtmf_state == DTMF_ST_IDLE) {
		p_g_dtmf_index = 0;
		p_g_dtmf[0] = '\0';
	}
	SCCAT(p_g_dtmf, digit);
	if (p_g_dtmf_state == DTMF_ST_IDLE)
		dtmf_statemachine(NULL);
}

/* MESSAGE_INFORMATION */
void Pgsm_ms::message_information(unsigned int epoint_id, int message_id, union parameter *param)
{
	char digit;
	int i;

	for (i = 0; i < (int)strlen(param->information.id); i++) {
		digit = param->information.id[i];
		if (digit >= 'a' && digit <= 'c')
			digit = digit - 'a' + 'A';
		if (!strchr("01234567890*#ABC", digit))
			continue;

		/* schedule */
		if (p_g_dtmf_state == DTMF_ST_IDLE) {
			p_g_dtmf_index = 0;
			p_g_dtmf[0] = '\0';
		}
		SCCAT(p_g_dtmf, digit);
		if (p_g_dtmf_state == DTMF_ST_IDLE)
			dtmf_statemachine(NULL);
	}
}

/*
 * endpoint sends messages to the port
 */
int Pgsm_ms::message_epoint(unsigned int epoint_id, int message_id, union parameter *param)
{
	struct lcr_msg *message;

	if (message_id == MESSAGE_CONNECT) {
		/* request keypad from remote */
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_ENABLEKEYPAD);
		message_put(message);
	}

	if (Pgsm::message_epoint(epoint_id, message_id, param))
		return(1);

	switch(message_id) {
		case MESSAGE_SETUP: /* dial-out command received from epoint */
		if (p_state!=PORT_STATE_IDLE)
			break;
		message_setup(epoint_id, message_id, param);
		break;

		case MESSAGE_DTMF:
		message_dtmf(epoint_id, message_id, param);
		break;

		case MESSAGE_INFORMATION:
		message_information(epoint_id, message_id, param);
		break;

		default:
		PDEBUG(DEBUG_GSM, "Pgsm_ms(%s) gsm port with (caller id %s) received unhandled nessage: %d\n", p_name, p_callerinfo.id, message_id);
	}

	return(1);
}

int gsm_ms_exit(int rc)
{
	/* destroy all instances */
	while (gsm_ms_first)
		gsm_ms_delete(gsm_ms_first->name);

	return rc;
}

int gsm_ms_init(void)
{
	return 0;
}

/* add a new GSM mobile instance */
int gsm_ms_new(struct interface *interface)
{
	struct lcr_gsm *gsm_ms = gsm_ms_first, **gsm_ms_p = &gsm_ms_first;

	while (gsm_ms) {
		gsm_ms_p = &gsm_ms->gsm_ms_next;
		gsm_ms = gsm_ms->gsm_ms_next;
	}

	PDEBUG(DEBUG_GSM, "GSM: interface for MS '%s' is created\n", interface->gsm_ms_name);

	/* create gsm instance */
	gsm_ms = (struct lcr_gsm *)MALLOC(sizeof(struct lcr_gsm));

	SCPY(gsm_ms->interface_name, interface->name);
	gsm_ms->type = LCR_GSM_TYPE_MS;
	SCPY(gsm_ms->name, interface->gsm_ms_name);
	gsm_ms->sun.sun_family = AF_UNIX;
	SPRINT(gsm_ms->sun.sun_path, "/tmp/ms_mncc_%s", gsm_ms->name);

	memset(&gsm_ms->socket_retry, 0, sizeof(gsm_ms->socket_retry));
	add_timer(&gsm_ms->socket_retry, mncc_socket_retry_cb, gsm_ms, 0);

	/* do the initial connect */
	mncc_socket_retry_cb(&gsm_ms->socket_retry, gsm_ms, 0);

	*gsm_ms_p = gsm_ms;

	return 0;
}

int gsm_ms_delete(const char *name)
{
	struct lcr_gsm *gsm_ms = gsm_ms_first, **gsm_ms_p = &gsm_ms_first;
//	class Port *port;
//	class Pgsm_ms *pgsm_ms = NULL;

	PDEBUG(DEBUG_GSM, "GSM: interface for MS '%s' is deleted\n", name);

	while (gsm_ms) {
		if (gsm_ms->type == LCR_GSM_TYPE_MS && !strcmp(gsm_ms->name, name))
			break;
		gsm_ms_p = &gsm_ms->gsm_ms_next;
		gsm_ms = gsm_ms->gsm_ms_next;
	}

	if (!gsm_ms)
		return 0;

/* not needed, because:
 * - shutdown of interface will destry port instances locally
 * - closing of socket will make remote socket destroy calls locally
 */
#if 0
	port = port_first;
	while(port) {
		if ((port->p_type & PORT_CLASS_GSM_MASK) == PORT_CLASS_GSM_MS) {
			pgsm_ms = (class Pgsm_ms *)port;
			if (pgsm_ms->p_g_lcr_gsm == gsm_ms && pgsm_ms->p_g_callref) {
				struct gsm_mncc *rej;

				rej = create_mncc(MNCC_REL_REQ, pgsm_ms->p_g_callref);
				rej->fields |= MNCC_F_CAUSE;
				rej->cause.coding = 3;
				rej->cause.location = 1;
				rej->cause.value = 27;
				gsm_trace_header(NULL, NULL, MNCC_REJ_REQ, DIRECTION_OUT);
				add_trace("cause", "coding", "%d", rej->cause.coding);
				add_trace("cause", "location", "%d", rej->cause.location);
				add_trace("cause", "value", "%d", rej->cause.value);
				end_trace();
				send_and_free_mncc(gsm_ms, rej->msg_type, rej);
				pgsm_ms->new_state(PORT_STATE_RELEASE);
				trigger_work(&pgsm_ms->p_g_delete);
			}
		}
	}
#endif

	if (gsm_ms->mncc_lfd.fd > -1) {
		close(gsm_ms->mncc_lfd.fd);
		unregister_fd(&gsm_ms->mncc_lfd);
	}
	del_timer(&gsm_ms->socket_retry);

	/* remove instance from list */
	*gsm_ms_p = gsm_ms->gsm_ms_next;
	FREE(gsm_ms, sizeof(struct lcr_gsm));

	return 0;
}


