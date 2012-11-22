/*****************************************************************************\
**                                                                           **
** LCR                                                                       **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** mISDN fxs                                                                 **
**                                                                           **
\*****************************************************************************/ 

/* Procedures:


*****TBD: THIS TEXT IS OLD ******

off-hook indication:
	no port instance:
		Create port/endpoint instance and send SETUP message to endpoint.
	port instance active:
		Send CONNECT message to endpoint.
	port instance inactive:
		Put inactive port instance active. Send RETRIEVE message to endpoint.
on-hook indication:
	Release active port instance. Send RELEASE message to endpoint if exists.
	inactive port instance:
		Send ring request. Use caller ID on incomming call or connected ID on outgoing call.
hookflash indication:
	active port instance not connected:
		Release active port instance. Send RELEASE message to endpoint if exists.
	active port instance connected:
		Put active port instance inactive. Send HOLD MESSAGE to endpoint.
	inactive port instance:
		Put inactive port instance active. Send RETRIEVE message to endpoint.
	no inactive port instance:
		Create port/endpoint instance and send SETUP message to endpoint.
keypulse indication:
	active port instance in incomming overlap state:
		Send INFORMATION message to endpoint.
	active port instance in other state:
		Send KEYPAD message to endpoint, if exists.
SETUP message:
	no instance:
		Create port instance and send ALERTING message to endpoint.
		Send ring request. Use caller ID.
	only one instance active:
		Create port instance and send ALERTING message to endpoint.
		Send knock sound. Send ALERTING message to endpoint.
	one instance on hold:
		Send RELEASE message (cause = BUSY) to endpoint.
PROCEEDING / ALERTING / CONNECT message:
	(change state only)
DISCONNECT message:
	is inactive port instance:
		Release port instance. Send RELEASE message to endpoint.
RELEASE message:
	is active port instance:
		Create hangup tone (release tone)
	is inactive port instance:
		Release port instance.
*/

#include "main.h"
#include "myisdn.h"
// socket mISDN
//#include <sys/socket.h>
extern "C" {
}

#ifdef ISDN_P_FXS_POTS

static int fxs_age = 0;

static int delete_event(struct lcr_work *work, void *instance, int index);

static int dtmf_timeout(struct lcr_timer *timer, void *instance, int index)
{
	class Pfxs *pfxs = (class Pfxs *)instance;

	/* allow DTMF dialing now */
	PDEBUG(DEBUG_ISDN, "%s: allow DTMF now\n", pfxs->p_name);
	pfxs->p_m_fxs_allow_dtmf = 1;

	return 0;
}

/*
 * constructor
 */
Pfxs::Pfxs(int type, struct mISDNport *mISDNport, char *portname, struct port_settings *settings, struct interface *interface, int mode) : PmISDN(type, mISDNport, portname, settings, interface, 0, 0, mode)
{
	p_callerinfo.itype = (mISDNport->ifport->interface->extension)?INFO_ITYPE_ISDN_EXTENSION:INFO_ITYPE_ISDN;

	memset(&p_m_fxs_delete, 0, sizeof(p_m_fxs_delete));
	add_work(&p_m_fxs_delete, delete_event, this, 0);
	p_m_fxs_allow_dtmf = 0;
	memset(&p_m_fxs_dtmf_timer, 0, sizeof(p_m_fxs_dtmf_timer));
	add_timer(&p_m_fxs_dtmf_timer, dtmf_timeout, this, 0);
	p_m_fxs_age = fxs_age++;
	p_m_fxs_knocking = 0;

	PDEBUG(DEBUG_ISDN, "Created new FXSPort(%s). Currently %d objects use, FXS port #%d\n", portname, mISDNport->use, p_m_portnum);
}


/*
 * destructor
 */
Pfxs::~Pfxs()
{
	del_timer(&p_m_fxs_dtmf_timer);
	del_work(&p_m_fxs_delete);
}

/* deletes only if l3id is release, otherwhise it will be triggered then */
static int delete_event(struct lcr_work *work, void *instance, int index)
{
	class Pfxs *pots = (class Pfxs *)instance;

	delete pots;

	return 0;
}


int Pfxs::hunt_bchannel(void)
{
	if (p_m_mISDNport->b_num < 1)
		return -47;
	if (p_m_mISDNport->b_port[0])
		return -17;
	return 1;
}

int Pfxs::ph_control_pots(unsigned int cont, unsigned char *data, int len)
{
	unsigned char buffer[MISDN_HEADER_LEN+sizeof(int)+len];
	struct mISDNhead *ctrl = (struct mISDNhead *)buffer;
	unsigned int *d = (unsigned int *)(buffer+MISDN_HEADER_LEN);
	int ret;

	ctrl->prim = PH_CONTROL_REQ;
	ctrl->id = 0;
	*d++ = cont;
	if (len)
		memcpy(d, data, len);
	ret = sendto(p_m_mISDNport->pots_sock.fd, buffer, MISDN_HEADER_LEN+sizeof(int)+len, 0, NULL, 0);
	if (ret <= 0)
		PERROR("Failed to send to socket %d\n", p_m_mISDNport->pots_sock.fd);

	return ret;
}

void Pfxs::pickup_ind(unsigned int cont)
{
	struct interface *interface = p_m_mISDNport->ifport->interface;
	class Endpoint *epoint;
	struct lcr_msg *message;
	int ret, channel;

	p_m_fxs_age = fxs_age++;

	if (p_m_fxs_knocking) {
		ph_control_pots(POTS_CW_OFF, NULL, 0);
		p_m_fxs_knocking = 0;
	}

	chan_trace_header(p_m_mISDNport, this, "PICKUP", DIRECTION_NONE);

	if (interface->ifmsn && interface->ifmsn->msn[0]) {
		SCPY(p_callerinfo.id, interface->ifmsn->msn);
		add_trace("caller", "ID", "%s", p_callerinfo.id);
	}
	p_callerinfo.present = INFO_PRESENT_ALLOWED;
	p_callerinfo.isdn_port = p_m_portnum;
	SCPY(p_callerinfo.interface, p_m_mISDNport->ifport->interface->name);
	p_capainfo.source_mode = B_MODE_TRANSPARENT;
	p_capainfo.bearer_capa = INFO_BC_AUDIO;
	p_capainfo.bearer_info1 = 0x80 + ((options.law=='a')?3:2);
	p_capainfo.bearer_mode = INFO_BMODE_CIRCUIT;

	if ((cont & (~POTS_KP_MASK)) == POTS_KP_VAL) {
		p_dialinginfo.ntype = INFO_NTYPE_UNKNOWN;
		p_dialinginfo.id[0] = cont & POTS_KP_MASK;
	}

	if (!p_m_b_channel) {
		PDEBUG(DEBUG_ISDN, "Pfxs(%s) alloc bchannel\n", p_name);
		/* hunt bchannel */
		ret = channel = hunt_bchannel();
		if (ret < 0)
			goto no_channel;

		/* open channel */
		ret = seize_bchannel(channel, 1);
		if (ret < 0) {
no_channel:
			/*
			 * NOTE: we send MT_RELEASE_COMPLETE to "REJECT" the channel
			 * in response to the setup
			 */
			add_trace("error", NULL, "no b-channel");
			end_trace();
			new_state(PORT_STATE_RELEASE);
			trigger_work(&p_m_fxs_delete);
			return;
		}
		end_trace();
		bchannel_event(p_m_mISDNport, p_m_b_index, B_EVENT_USE);
	} else {
		PDEBUG(DEBUG_ISDN, "Pfxs(%s) bchannel is already active\n", p_name);
	}

	/* create endpoint */
	if (p_epointlist)
		FATAL("Incoming call but already got an endpoint.\n");
	if (!(epoint = new Endpoint(p_serial, 0)))
		FATAL("No memory for Endpoint instance\n");
	epoint->ep_app = new_endpointapp(epoint, 0, p_m_mISDNport->ifport->interface->app); //incoming
	epointlist_new(epoint->ep_serial);

	/* indicate flash control */
	if (cont == POTS_HOOK_FLASH || cont == POTS_EARTH_KEY)
		p_dialinginfo.flash = 1;

	/* send setup message to endpoit */
	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_SETUP);
	message->param.setup.isdn_port = p_m_portnum;
	message->param.setup.port_type = p_type;
//	message->param.setup.dtmf = !p_m_mISDNport->ifport->nodtmf;
	memcpy(&message->param.setup.callerinfo, &p_callerinfo, sizeof(struct caller_info));
	memcpy(&message->param.setup.dialinginfo, &p_dialinginfo, sizeof(struct dialing_info));
	memcpy(&message->param.setup.capainfo, &p_capainfo, sizeof(struct capa_info));
	message_put(message);

	new_state(PORT_STATE_IN_OVERLAP);

	schedule_timer(&p_m_fxs_dtmf_timer, 0, 500000);
}

void Pfxs::hangup_ind(unsigned int cont)
{
	struct lcr_msg *message;

	/* deactivate bchannel */
	chan_trace_header(p_m_mISDNport, this, "HANGUP", DIRECTION_NONE);
	end_trace();
	drop_bchannel();
	PDEBUG(DEBUG_ISDN, "Pfxs(%s) drop bchannel\n", p_name);

	/* send release message, if not already */
	if (p_epointlist) {
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = 16;
		message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		message_put(message);
	}
	new_state(PORT_STATE_RELEASE);
	trigger_work(&p_m_fxs_delete);
}

void Pfxs::answer_ind(unsigned int cont)
{
	struct lcr_msg *message;
	int ret, channel;

	if (p_m_fxs_knocking) {
		ph_control_pots(POTS_CW_OFF, NULL, 0);
		p_m_fxs_knocking = 0;
	}

	chan_trace_header(p_m_mISDNport, this, "ANSWER", DIRECTION_NONE);
	if (!p_m_b_channel) {
		PDEBUG(DEBUG_ISDN, "Pfxs(%s) alloc bchannel\n", p_name);
		/* hunt bchannel */
		ret = channel = hunt_bchannel();
		if (ret < 0)
			goto no_channel;

		/* open channel */
		ret = seize_bchannel(channel, 1);
		if (ret < 0) {
no_channel:
			/*
			 * NOTE: we send MT_RELEASE_COMPLETE to "REJECT" the channel
			 * in response to the setup
			 */
			add_trace("error", NULL, "no b-channel");
			end_trace();
			new_state(PORT_STATE_RELEASE);
			trigger_work(&p_m_fxs_delete);
			return;
		}
		end_trace();
		bchannel_event(p_m_mISDNport, p_m_b_index, B_EVENT_USE);
	} else {
		PDEBUG(DEBUG_ISDN, "Pfxs(%s) bchannel is already active\n", p_name);
	}

	if (p_m_hold) {
		p_m_hold = 0;
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_NOTIFY);
		message->param.notifyinfo.notify = INFO_NOTIFY_REMOTE_RETRIEVAL;
		message->param.notifyinfo.local = 1; /* call is held by supplementary service */
		message_put(message);
	} else {
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_CONNECT);
		message_put(message);
	}

	new_state(PORT_STATE_CONNECT);
}

void Pfxs::hold_ind(unsigned int cont)
{
	struct lcr_msg *message;

	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_NOTIFY);
	message->param.notifyinfo.notify = INFO_NOTIFY_REMOTE_HOLD;
	message->param.notifyinfo.local = 1; /* call is held by supplementary service */
	message_put(message);

	/* deactivate bchannel */
	chan_trace_header(p_m_mISDNport, this, "HOLD", DIRECTION_NONE);
	end_trace();
	drop_bchannel();
	PDEBUG(DEBUG_ISDN, "Pfxs(%s) drop bchannel\n", p_name);

	p_m_hold = 1;
}

void Pfxs::retrieve_ind(unsigned int cont)
{
	struct lcr_msg *message;
	int ret, channel;

	p_m_fxs_age = fxs_age++;

	if (p_m_fxs_knocking) {
		ph_control_pots(POTS_CW_OFF, NULL, 0);
		p_m_fxs_knocking = 0;
	}

	if (cont == POTS_ON_HOOK) {
		const char *callerid;

		if (p_state == PORT_STATE_CONNECT) {
			new_state(PORT_STATE_OUT_ALERTING);
#if 0
			message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_ALERTING);
			message_put(message);
#endif
			chan_trace_header(p_m_mISDNport, this, "RING (retrieve)", DIRECTION_NONE);
		} else
			chan_trace_header(p_m_mISDNport, this, "RING (after knocking)", DIRECTION_NONE);
		if (p_type == PORT_TYPE_POTS_FXS_IN) {
			if (p_connectinfo.id[0]) {
				callerid = numberrize_callerinfo(p_connectinfo.id, p_connectinfo.ntype, options.national, options.international);
				add_trace("connect", "number", callerid);
			} else {
				callerid = p_dialinginfo.id;
				add_trace("dialing", "number", callerid);
			}
		} else {
			callerid = numberrize_callerinfo(p_callerinfo.id, p_callerinfo.ntype, options.national, options.international);
			add_trace("caller", "id", callerid);
		}
		ph_control_pots(POTS_RING_ON, (unsigned char *)callerid, strlen(callerid));
		end_trace();
		return;
	}

	chan_trace_header(p_m_mISDNport, this, "RETRIEVE", DIRECTION_NONE);
	if (!p_m_b_channel) {
		PDEBUG(DEBUG_ISDN, "Pfxs(%s) alloc bchannel\n", p_name);
		/* hunt bchannel */
		ret = channel = hunt_bchannel();
		if (ret < 0)
			goto no_channel;

		/* open channel */
		ret = seize_bchannel(channel, 1);
		if (ret < 0) {
no_channel:
			/*
			 * NOTE: we send MT_RELEASE_COMPLETE to "REJECT" the channel
			 * in response to the setup
			 */
			add_trace("error", NULL, "no b-channel");
			end_trace();
			new_state(PORT_STATE_RELEASE);
			trigger_work(&p_m_fxs_delete);
			return;
		}
		end_trace();
		bchannel_event(p_m_mISDNport, p_m_b_index, B_EVENT_USE);
	} else {
		PDEBUG(DEBUG_ISDN, "Pfxs(%s) bchannel is already active\n", p_name);
	}

	p_m_hold = 0;
	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_NOTIFY);
	message->param.notifyinfo.notify = INFO_NOTIFY_REMOTE_RETRIEVAL;
	message->param.notifyinfo.local = 1; /* call is held by supplementary service */
	message_put(message);
}

void Pfxs::keypulse_ind(unsigned int cont)
{
	struct lcr_msg *message;

	p_m_fxs_allow_dtmf = 0; /* disable DTMF from now on */
	chan_trace_header(p_m_mISDNport, this, "PULSE", DIRECTION_NONE);
	add_trace("KP", NULL, "%c", cont & DTMF_TONE_MASK);
	end_trace();
	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_INFORMATION);
	message->param.information.id[0] = cont & POTS_KP_MASK;
	PDEBUG(DEBUG_ISDN, "Pfxs(%s) PH_CONTROL INDICATION  KP digit '%c'\n", p_name, message->param.information.id[0]);
	message_put(message);
}

void Pfxs::reject_ind(unsigned int cont)
{
	struct lcr_msg *message;

	if (p_m_fxs_knocking) {
		ph_control_pots(POTS_CW_OFF, NULL, 0);
		p_m_fxs_knocking = 0;
	}

	/* deactivate bchannel */
	chan_trace_header(p_m_mISDNport, this, "REJECT", DIRECTION_NONE);
	end_trace();
	drop_bchannel();
	PDEBUG(DEBUG_ISDN, "Pfxs(%s) drop bchannel\n", p_name);

	/* send release message, if not already */
	if (p_epointlist) {
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = 16;
		message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		message_put(message);
	}
	new_state(PORT_STATE_RELEASE);
	trigger_work(&p_m_fxs_delete);
}


void Pfxs::message_setup(unsigned int epoint_id, int message_id, union parameter *param)
{
	struct lcr_msg *message;
	class Port *port;
	class Pfxs *pots;
	struct epoint_list *epointlist;
	const char *callerid;
	int any_call = 0;

	memcpy(&p_callerinfo, &param->setup.callerinfo, sizeof(p_callerinfo));
	memcpy(&p_redirinfo, &param->setup.redirinfo, sizeof(p_redirinfo));

	message = message_create(p_serial, epoint_id, PORT_TO_EPOINT, MESSAGE_ALERTING);
	message_put(message);

	new_state(PORT_STATE_OUT_ALERTING);

	/* attach only if not already */
	epointlist = p_epointlist;
	while(epointlist) {
		if (epointlist->epoint_id == epoint_id)
			break;
		epointlist = epointlist->next;
	}
	if (!epointlist)
		epointlist_new(epoint_id);

	/* find port in connected active state */
	port = port_first;
	while(port) {
		if ((port->p_type & PORT_CLASS_POTS_MASK) == PORT_CLASS_POTS_FXS) {
			pots = (class Pfxs *)port;
			if (pots->p_m_mISDNport == p_m_mISDNport) {
				if (pots != this)
					any_call = 1;
				if (pots->p_state == PORT_STATE_CONNECT && !pots->p_m_hold)
					break; // found
			}
		}
		port = port->next;
	}

	if (port) {
		PDEBUG(DEBUG_ISDN, "Pfxs(%s) knock because there is an ongoing active call\n", p_name);
		chan_trace_header(p_m_mISDNport, this, "KNOCK", DIRECTION_NONE);
		callerid = numberrize_callerinfo(p_callerinfo.id, p_callerinfo.ntype, options.national, options.international);
		add_trace("caller", "id", callerid);
		end_trace();
		ph_control_pots(POTS_CW_ON, (unsigned char *)callerid, strlen(callerid));
		p_m_fxs_knocking = 1;
		return;
	}
	if (any_call) {
		/* reject call, because we have a call, but we are not connected */
		PDEBUG(DEBUG_ISDN, "Pfxs(%s) reject because there is an ongoing and incomplete call\n", p_name);
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = 17; // busy
		message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		message_put(message);
		new_state(PORT_STATE_RELEASE);
		trigger_work(&p_m_fxs_delete);
		return;
	}
	PDEBUG(DEBUG_ISDN, "Pfxs(%s) ring because there is not calll\n", p_name);
	chan_trace_header(p_m_mISDNport, this, "RING", DIRECTION_NONE);
	callerid = numberrize_callerinfo(p_callerinfo.id, p_callerinfo.ntype, options.national, options.international);
	add_trace("caller", "id", callerid);
	end_trace();
	ph_control_pots(POTS_RING_ON, (unsigned char *)callerid, strlen(callerid));
}

void Pfxs::message_proceeding(unsigned int epoint_id, int message_id, union parameter *param)
{
	new_state(PORT_STATE_IN_PROCEEDING);
}

void Pfxs::message_alerting(unsigned int epoint_id, int message_id, union parameter *param)
{
	new_state(PORT_STATE_IN_ALERTING);
}

void Pfxs::message_connect(unsigned int epoint_id, int message_id, union parameter *param)
{
	new_state(PORT_STATE_CONNECT);

	memcpy(&p_connectinfo, &param->connectinfo, sizeof(struct connect_info));
}

void Pfxs::message_disconnect(unsigned int epoint_id, int message_id, union parameter *param)
{
	if (p_state == PORT_STATE_OUT_ALERTING) {
		if (p_m_fxs_knocking) {
			ph_control_pots(POTS_CW_OFF, NULL, 0);
			p_m_fxs_knocking = 0;
		} else {
			ph_control_pots(POTS_RING_OFF, NULL, 0);
		}
		if (p_epointlist) {
			struct lcr_msg *message;
			message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_RELEASE);
			message->param.disconnectinfo.cause = 16;
			message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
			message_put(message);
		}
		free_epointid(epoint_id);
		new_state(PORT_STATE_RELEASE);
		trigger_work(&p_m_fxs_delete);
		return;
	}

	new_state(PORT_STATE_OUT_DISCONNECT);
}

void Pfxs::message_release(unsigned int epoint_id, int message_id, union parameter *param)
{
	chan_trace_header(p_m_mISDNport, this, "CLEAR", DIRECTION_NONE);
	end_trace();

	if (p_state == PORT_STATE_OUT_ALERTING) {
		if (p_m_fxs_knocking) {
			ph_control_pots(POTS_CW_OFF, NULL, 0);
			p_m_fxs_knocking = 0;
		} else {
			ph_control_pots(POTS_RING_OFF, NULL, 0);
		}
		trigger_work(&p_m_fxs_delete);
	}
	if (p_state == PORT_STATE_CONNECT) {
		if (!p_m_hold)
			set_tone("", "release");
		else
			trigger_work(&p_m_fxs_delete);
	}

	new_state(PORT_STATE_RELEASE);

	free_epointid(epoint_id);
}

/*
 * endpoint sends messages to the port
 */
int Pfxs::message_epoint(unsigned int epoint_id, int message_id, union parameter *param)
{
	if (PmISDN::message_epoint(epoint_id, message_id, param))
		return(1);

	switch(message_id) {
		case MESSAGE_SETUP: /* dial-out command received from epoint */
		message_setup(epoint_id, message_id, param);
		break;

		case MESSAGE_PROCEEDING: /* call of endpoint is proceeding */
		message_proceeding(epoint_id, message_id, param);
		break;

		case MESSAGE_ALERTING: /* call of endpoint is ringing */
		message_alerting(epoint_id, message_id, param);
		break;

		case MESSAGE_CONNECT: /* call of endpoint is connected */
		message_connect(epoint_id, message_id, param);
		break;

		case MESSAGE_DISCONNECT: /* call has been disconnected */
		message_disconnect(epoint_id, message_id, param);
		break;

		case MESSAGE_RELEASE: /* release isdn port */
		if (p_state==PORT_STATE_RELEASE) {
			break;
		}
		message_release(epoint_id, message_id, param);
		break;
	}

	return(1);
}

/*
 * data from isdn-stack (layer-1) to pbx (port class)
 */
int stack2manager_fxs(struct mISDNport *mISDNport, unsigned int cont)
{
	class Port *port;
	class Pfxs *pots, *latest_pots = NULL, *alerting_pots = NULL;
	int latest = -1;
	char name[32];

	PDEBUG(DEBUG_ISDN, "cont(0x%x)\n", cont);

	if ((cont & (~POTS_KP_MASK)) == POTS_KP_VAL) {
		/* find port in dialing state */
		port = port_first;
		while(port) {
			if ((port->p_type & PORT_CLASS_POTS_MASK) == PORT_CLASS_POTS_FXS) {
				pots = (class Pfxs *)port;
				if (pots->p_m_mISDNport == mISDNport
				 && pots->p_state == PORT_STATE_IN_OVERLAP)
					break; // found
			}
			port = port->next;
		}
		if (port) {
			pots->keypulse_ind(cont);
			return 0;
		}
		goto flash;
	}

	switch (cont) {
	case POTS_OFF_HOOK:
		/* find ringing */
		port = port_first;
		while(port) {
			if ((port->p_type & PORT_CLASS_POTS_MASK) == PORT_CLASS_POTS_FXS) {
				pots = (class Pfxs *)port;
				if (pots->p_m_mISDNport == mISDNport
				 && pots->p_state == PORT_STATE_OUT_ALERTING)
					break; // found
			}
			port = port->next;
		}
		if (port) {
			pots->answer_ind(cont);
			break;
		}

setup:
		/* creating port object */
		SPRINT(name, "%s-%d-in", mISDNport->ifport->interface->name, mISDNport->portnum);
		pots = new Pfxs(PORT_TYPE_POTS_FXS_IN, mISDNport, name, NULL, mISDNport->ifport->interface, B_MODE_TRANSPARENT);
		if (!pots)
			FATAL("Failed to create Port instance\n");
		pots->pickup_ind(cont);
		break;

	case POTS_ON_HOOK:
		if (mISDNport->ifport->pots_transfer) {
			struct lcr_msg *message;
			class Pfxs *pots1 = NULL, *pots2 = NULL;
			int count = 0;
			port = port_first;
			while(port) {
				if ((port->p_type & PORT_CLASS_POTS_MASK) == PORT_CLASS_POTS_FXS) {
					pots = (class Pfxs *)port;
					if (pots->p_m_mISDNport == mISDNport) {
						if (pots->p_state == PORT_STATE_CONNECT
						 || pots->p_state == PORT_STATE_IN_PROCEEDING
						 || pots->p_state == PORT_STATE_IN_ALERTING) {
							if (count == 0)
						 		pots1 = pots;
							if (count == 1)
						 		pots2 = pots;
						 	count++;
						}
					}
				}
				port = port->next;
			}

			if (count == 2) {
				if (pots1->p_state == PORT_STATE_CONNECT) {
					message = message_create(pots1->p_serial, ACTIVE_EPOINT(pots1->p_epointlist), PORT_TO_EPOINT, MESSAGE_TRANSFER);
					message_put(message);
				}
				else if (pots2->p_state == PORT_STATE_CONNECT) {
					message = message_create(pots2->p_serial, ACTIVE_EPOINT(pots2->p_epointlist), PORT_TO_EPOINT, MESSAGE_TRANSFER);
					message_put(message);
				}
				pots1->hangup_ind(cont);
				pots2->hangup_ind(cont);
				break;
			}
		}
		if (mISDNport->ifport->pots_ring) {
			/* release all except calls on hold, let the latest call ring */
			port = port_first;
			while(port) {
				if ((port->p_type & PORT_CLASS_POTS_MASK) == PORT_CLASS_POTS_FXS) {
					pots = (class Pfxs *)port;
					if (pots->p_m_mISDNport == mISDNport) {
						if (pots->p_state == PORT_STATE_CONNECT && pots->p_m_hold) {
							if (pots->p_m_fxs_age > latest) {
								latest = pots->p_m_fxs_age;
								latest_pots = pots;
							}
						}
						if (pots->p_state == PORT_STATE_OUT_ALERTING)
							alerting_pots = pots;
					}
				}
				port = port->next;
			}
			port = port_first;
			while(port) {
				if ((port->p_type & PORT_CLASS_POTS_MASK) == PORT_CLASS_POTS_FXS) {
					pots = (class Pfxs *)port;
					if (pots->p_m_mISDNport == mISDNport) {
						if ((pots->p_state != PORT_STATE_CONNECT || !pots->p_m_hold) && pots != alerting_pots) {
							PDEBUG(DEBUG_ISDN, "Pfxs(%s) release because pots-ring-after-hangup set and call not on hold / alerting\n", pots->p_name);
							pots->hangup_ind(cont);
						}
					}
				}
				port = port->next;
			}
			if (alerting_pots) {
				PDEBUG(DEBUG_ISDN, "Pfxs(%s) answer because pots-ring-after-hangup set and call is alerting (knocking)\n", alerting_pots->p_name);
				alerting_pots->retrieve_ind(cont);
				break;
			}
			if (latest_pots) {
				PDEBUG(DEBUG_ISDN, "Pfxs(%s) retrieve because pots-ring-after-hangup set and call is latest on hold\n", latest_pots->p_name);
				latest_pots->retrieve_ind(cont);
				break;
			}
		} else {
			/* release all pots */
			port = port_first;
			while(port) {
				if ((port->p_type & PORT_CLASS_POTS_MASK) == PORT_CLASS_POTS_FXS) {
					pots = (class Pfxs *)port;
					if (pots->p_m_mISDNport == mISDNport) {
						PDEBUG(DEBUG_ISDN, "Pfxs(%s) release because pots-ring-after-hangup not set\n", pots->p_name);
						pots->hangup_ind(cont);
					}
				}
				port = port->next;
			}
		}
		break;
	case POTS_HOOK_FLASH:
	case POTS_EARTH_KEY:
flash:
		if (!mISDNport->ifport->pots_flash) {
			PDEBUG(DEBUG_ISDN, "Pfxs flash key is disabled\n");
			break;
		}
		/* hold active pots / release not active pots */
		port = port_first;
		while(port) {
			if ((port->p_type & PORT_CLASS_POTS_MASK) == PORT_CLASS_POTS_FXS) {
				pots = (class Pfxs *)port;
				if (pots->p_m_mISDNport == mISDNport) {
				 	if (pots->p_state == PORT_STATE_CONNECT) {
						if (pots->p_m_hold) {
							if (pots->p_m_fxs_age > latest) {
								latest = pots->p_m_fxs_age;
								latest_pots = pots;
							}
						} else {
							PDEBUG(DEBUG_ISDN, "Pfxs(%s) hold, because flash on active call\n", pots->p_name);
							pots->hold_ind(cont);
						}
					} else if (pots->p_state == PORT_STATE_OUT_ALERTING) {
						alerting_pots = pots;
					} else {
						PDEBUG(DEBUG_ISDN, "Pfxs(%s) hangup, because flash on incomplete/released call\n", pots->p_name);
						pots->hangup_ind(cont);
					}
				}
			}
			port = port->next;
		}
#if 0
		/* now we have our bchannel available, so we can look alerting port to answer */
		if (alerting_pots) {
			PDEBUG(DEBUG_ISDN, "Pfxs(%s) answer because call is alerting (knocking)\n", alerting_pots->p_name);
			alerting_pots->answer_ind(cont);
			break;
		}
		if (latest_pots) {
			PDEBUG(DEBUG_ISDN, "Pfxs(%s) retrieve because call is latest on hold\n", latest_pots->p_name);
			latest_pots->retrieve_ind(cont);
			break;
		}
#endif
		goto setup;

		default:
		PERROR("unhandled message: xontrol(0x%x)\n", cont);
		return(-EINVAL);
	}
	return(0);
}

#endif /* ISDN_P_FXS_POTS */
