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
#include "config.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
extern "C" {
#include <getopt.h>

#include <openbsc/db.h>
#include <osmocore/select.h>
#include <openbsc/debug.h>
#include <openbsc/e1_input.h>
#include <osmocore/talloc.h>
#include <openbsc/mncc.h>
#include <openbsc/trau_frame.h>
#include <openbsc/osmo_msc.h>
//#include <osmocom/vty/command.h>
struct gsm_network *bsc_gsmnet = 0;
extern int ipacc_rtp_direct;
extern int bsc_bootstrap_network(int (*mmc_rev)(struct gsm_network *, int, void *),
				 const char *cfg_file);
extern int bsc_shutdown_net(struct gsm_network *net);
void talloc_ctx_init(void);
void on_dso_load_token(void);
void on_dso_load_rrlp(void);
void on_dso_load_ho_dec(void);
int bts_model_unknown_init(void);
int bts_model_bs11_init(void);
int bts_model_nanobts_init(void);
static struct log_target *stderr_target;
extern const char *openbsc_copyright;

/* timer to store statistics */
#define DB_SYNC_INTERVAL	60, 0
static struct timer_list db_sync_timer;

/* FIXME: copied from the include file, because it will con compile with C++ */
struct vty_app_info {
	const char *name;
	const char *version;
	const char *copyright;
	void *tall_ctx;
	int (*go_parent_cb)(struct vty *vty);
	int (*is_config_node)(struct vty *vty, int node);
};

extern int bsc_vty_go_parent(struct vty *vty);
extern int bsc_vty_is_config_node(struct vty *vty, int node);
static struct vty_app_info vty_info = {
	"OpenBSC",
	PACKAGE_VERSION,
	NULL,
	NULL,
	bsc_vty_go_parent,
	bsc_vty_is_config_node,
};

void vty_init(struct vty_app_info *app_info);
int bsc_vty_init(void);

}

/* timer handling */
static int _db_store_counter(struct counter *counter, void *data)
{
	return db_store_counter(counter);
}

static void db_sync_timer_cb(void *data)
{
	/* store counters to database and re-schedule */
	counters_for_each(_db_store_counter, NULL);
	bsc_schedule_timer(&db_sync_timer, DB_SYNC_INTERVAL);
}

/*
 * constructor
 */
Pgsm_bs::Pgsm_bs(int type, struct mISDNport *mISDNport, char *portname, struct port_settings *settings, int channel, int exclusive, int mode) : Pgsm(type, mISDNport, portname, settings, channel, exclusive, mode)
{
	p_m_g_instance = gsm->network;
	PDEBUG(DEBUG_GSM, "Created new GSMBSPort(%s).\n", portname);
}

/*
 * destructor
 */
Pgsm_bs::~Pgsm_bs()
{
	PDEBUG(DEBUG_GSM, "Destroyed GSM BS process(%s).\n", p_name);
}

/* DTMF INDICATION */
void Pgsm_bs::start_dtmf_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
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
	send_and_free_mncc(p_m_g_instance, resp->msg_type, resp);

	/* send dialing information */
	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_INFORMATION);
	memcpy(&message->param.information, &p_dialinginfo, sizeof(struct dialing_info));
	message_put(message);
}
void Pgsm_bs::stop_dtmf_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
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
	send_and_free_mncc(p_m_g_instance, resp->msg_type, resp);
}

/* HOLD INDICATION */
void Pgsm_bs::hold_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
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
	send_and_free_mncc(p_m_g_instance, resp->msg_type, resp);

	/* disable audio */
	if (p_m_g_tch_connected) { /* it should be true */
		gsm_trace_header(p_m_mISDNport, this, MNCC_FRAME_DROP, DIRECTION_OUT);
		end_trace();
		frame = create_mncc(MNCC_FRAME_DROP, p_m_g_callref);
		send_and_free_mncc(p_m_g_instance, frame->msg_type, frame);
		p_m_g_tch_connected = 0;
	}
}


/* RETRIEVE INDICATION */
void Pgsm_bs::retr_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
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
	send_and_free_mncc(p_m_g_instance, resp->msg_type, resp);

	/* enable audio */
	if (!p_m_g_tch_connected) { /* it should be true */
		gsm_trace_header(p_m_mISDNport, this, MNCC_FRAME_RECV, DIRECTION_OUT);
		end_trace();
		frame = create_mncc(MNCC_FRAME_RECV, p_m_g_callref);
		send_and_free_mncc(p_m_g_instance, frame->msg_type, frame);
		p_m_g_tch_connected = 1;
	}
}

/*
 * handles all indications
 */
/* SETUP INDICATION */
void Pgsm_bs::setup_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc)
{
	int ret;
	class Endpoint *epoint;
	struct lcr_msg *message;
	int channel;
	struct gsm_mncc *mode, *proceeding, *frame;

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
		send_and_free_mncc(p_m_g_instance, mncc->msg_type, mncc);
		new_state(PORT_STATE_RELEASE);
		trigger_work(&p_m_g_delete);
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
		send_and_free_mncc(p_m_g_instance, mncc->msg_type, mncc);
		new_state(PORT_STATE_RELEASE);
		trigger_work(&p_m_g_delete);
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
		send_and_free_mncc(p_m_g_instance, mncc->msg_type, mncc);
		new_state(PORT_STATE_RELEASE);
		trigger_work(&p_m_g_delete);
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
	send_and_free_mncc(p_m_g_instance, mode->msg_type, mode);

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
	send_and_free_mncc(p_m_g_instance, proceeding->msg_type, proceeding);

	new_state(PORT_STATE_IN_PROCEEDING);

	if (p_m_mISDNport->tones && !p_m_g_tch_connected) { /* only if ... */
		gsm_trace_header(p_m_mISDNport, this, MNCC_FRAME_RECV, DIRECTION_OUT);
		end_trace();
		frame = create_mncc(MNCC_FRAME_RECV, p_m_g_callref);
		send_and_free_mncc(p_m_g_instance, frame->msg_type, frame);
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

/*
 * BSC sends message to port
 */
static int message_bsc(struct gsm_network *net, int msg_type, void *arg)
{
	struct gsm_mncc *mncc = (struct gsm_mncc *)arg;
	unsigned int callref = mncc->callref;
	class Port *port;
	class Pgsm_bs *pgsm_bs = NULL;
	char name[64];
	struct mISDNport *mISDNport;

	/* Special messages */
	switch(msg_type) {
	}

	/* find callref */
	callref = mncc->callref;
	port = port_first;
	while(port) {
		if ((port->p_type & PORT_CLASS_GSM_MASK) == PORT_CLASS_GSM_BS) {
			pgsm_bs = (class Pgsm_bs *)port;
			if (pgsm_bs->p_m_g_callref == callref) {
				break;
			}
		}
		port = port->next;
	}

	if (msg_type == GSM_TCHF_FRAME) {
		if (port)
			pgsm_bs->frame_receive((struct gsm_trau_frame *)arg);
		return 0;
	}

	if (!port) {
		if (msg_type != MNCC_SETUP_IND)
			return(0);
		/* find gsm port */
		mISDNport = mISDNport_first;
		while(mISDNport) {
			if (mISDNport->gsm_bs)
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
			send_and_free_mncc(gsm->network, rej->msg_type, rej);
			return 0;
		}
		/* creating port object, transparent until setup with hdlc */
		SPRINT(name, "%s-%d-in", mISDNport->ifport->interface->name, mISDNport->portnum);
		if (!(pgsm_bs = new Pgsm_bs(PORT_TYPE_GSM_BS_IN, mISDNport, name, NULL, 0, 0, B_MODE_TRANSPARENT)))

			FATAL("Cannot create Port instance.\n");
	}

	switch(msg_type) {
		case MNCC_SETUP_IND:
		pgsm_bs->setup_ind(msg_type, callref, mncc);
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
		trigger_work(&p_m_g_delete);
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
		trigger_work(&p_m_g_delete);
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
		trigger_work(&p_m_g_delete);
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
	send_and_free_mncc(p_m_g_instance, mncc->msg_type, mncc);

	new_state(PORT_STATE_OUT_SETUP);

	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_PROCEEDING);
	message_put(message);

	new_state(PORT_STATE_OUT_PROCEEDING);
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
	if (gsm) {
		/* shutdown network */
		if (gsm->network)
			bsc_shutdown_net((struct gsm_network *)gsm->network);
		/* free network */
//		if (gsm->network) {
//			free((struct gsm_network *)gsm->network); /* TBD */
//		}
	}

	return(rc);
}

int gsm_bs_init(void)
{
	char hlr[128], cfg[128], filename[128];
        mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	int pcapfd, rc;

	vty_info.copyright = openbsc_copyright;

	log_init(&log_info);
	tall_bsc_ctx = talloc_named_const(NULL, 1, "openbsc");
	talloc_ctx_init();
	on_dso_load_token();
	on_dso_load_rrlp();
	on_dso_load_ho_dec();
	stderr_target = log_target_create_stderr();
	log_add_target(stderr_target);

	bts_model_unknown_init();
	bts_model_bs11_init();
	bts_model_nanobts_init();

	/* enable filters */
	log_set_all_filter(stderr_target, 1);

	/* Init VTY (need to preceed options) */
	vty_init(&vty_info);
	bsc_vty_init();

	/* set debug */
	if (gsm->conf.debug[0])
		log_parse_category_mask(stderr_target, gsm->conf.debug);

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
	bsc_api_init(bsc_gsmnet, msc_bsc_api());
	gsm->network = bsc_gsmnet;

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

	/* setup the timer */
	db_sync_timer.cb = db_sync_timer_cb;
	db_sync_timer.data = NULL;
	bsc_schedule_timer(&db_sync_timer, DB_SYNC_INTERVAL);

	return 0;
}

/*
 * handles bsc select function within LCR's main loop
 */
int handle_gsm_bs(void)
{
	int ret1, ret2;

	ret1 = bsc_upqueue((struct gsm_network *)gsm->network);
	log_reset_context();
	ret2 = bsc_select_main(1); /* polling */
	if (ret1 || ret2)
		return 1;
	return 0;
}

