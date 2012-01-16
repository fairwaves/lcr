/*****************************************************************************\
**                                                                           **
** LCR                                                                       **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** mISDN remote                                                              **
**                                                                           **
\*****************************************************************************/ 

#include "main.h"

unsigned int new_remote = 1000;

/*
 * constructor
 */
Premote::Premote(int type, struct mISDNport *mISDNport, char *portname, struct port_settings *settings, int channel, int exclusive, int mode, int remote_id) : PmISDN(type, mISDNport, portname, settings, channel, exclusive, mode)
{
	union parameter param;

	p_callerinfo.itype = (mISDNport->ifport->interface->extension)?INFO_ITYPE_ISDN_EXTENSION:INFO_ITYPE_ISDN;
	p_m_r_ref = new_remote++;
	SCPY(p_m_r_remote_app, mISDNport->ifport->remote_app);
	p_m_r_handle = 0;

	/* send new ref to remote socket */
	memset(&param, 0, sizeof(union parameter));
	if (type == PORT_TYPE_REMOTE_OUT)
		param.newref.direction = 1; /* new ref from lcr */
	p_m_r_remote_id = remote_id;
	if (admin_message_from_lcr(p_m_r_remote_id, p_m_r_ref, MESSAGE_NEWREF, &param) < 0)
		FATAL("No socket with remote application '%s' found, this shall not happen. because we already created one.\n", mISDNport->ifport->remote_app);

	PDEBUG(DEBUG_GSM, "Created new RemotePort(%s).\n", portname);

}

/*
 * destructor
 */
Premote::~Premote()
{
	/* need to remote (import) external channel from remote application */
	if (p_m_r_handle) {
		message_bchannel_to_remote(p_m_r_remote_id, p_m_r_ref, BCHANNEL_REMOVE, p_m_r_handle, 0, 0, 0, 0, 0, 0, 1);
		chan_trace_header(p_m_mISDNport, this, "MESSAGE_BCHANNEL (to remote application)", DIRECTION_NONE);
		add_trace("type", NULL, "remove");
		add_trace("channel", NULL, "%d.%d", p_m_r_handle>>8, p_m_r_handle&0xff);
		end_trace();
	}

	PDEBUG(DEBUG_GSM, "Destroyed Remote process(%s).\n", p_name);

}

/*
 * endpoint sends messages to the port
 */
int Premote::message_epoint(unsigned int epoint_id, int message_type, union parameter *param)
{
	struct lcr_msg *message;
	int channel;
	int ret;
	struct epoint_list *epointlist;

	if (PmISDN::message_epoint(epoint_id, message_type, param))
		return 1;

	if (message_type == MESSAGE_SETUP) {
		ret = channel = hunt_bchannel();
		if (ret < 0)
			goto no_channel;
		/* open channel */
		ret = seize_bchannel(channel, 1);
		if (ret < 0) {
			no_channel:
			message = message_create(p_serial, epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
			message->param.disconnectinfo.cause = 34;
			message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
			message_put(message);
			new_state(PORT_STATE_RELEASE);
			delete this;
			return 0;
		}
		bchannel_event(p_m_mISDNport, p_m_b_index, B_EVENT_USE);

		/* attach only if not already */
		epointlist = p_epointlist;
		while(epointlist) {
			if (epointlist->epoint_id == epoint_id)
				break;
			epointlist = epointlist->next;
		}
		if (!epointlist)
			epointlist_new(epoint_id);

		/* set context to pbx */
		SCPY(param->setup.context, "pbx");
	}

	/* look for Remote's interface */
	if (admin_message_from_lcr(p_m_r_remote_id, p_m_r_ref, message_type, param)<0) {
		PERROR("No socket with remote application '%s' found, this shall not happen. Closing socket shall cause release of all remote ports.\n", p_m_mISDNport->ifport->remote_app);
		return 0;		
	}

	/* enable audio path */
	if (message_type == MESSAGE_SETUP) {
		union parameter newparam;
		memset(&newparam, 0, sizeof(union parameter));
		admin_message_from_lcr(p_m_r_remote_id, p_m_r_ref, MESSAGE_PATTERN, &newparam);
		newparam.audiopath = 1;
		admin_message_from_lcr(p_m_r_remote_id, p_m_r_ref, MESSAGE_AUDIOPATH, &newparam);
	}

	if (message_type == MESSAGE_RELEASE) {
		new_state(PORT_STATE_RELEASE);
		delete this;
		return 0;
	}

	return 0;
}

void Premote::message_remote(int message_type, union parameter *param)
{
	class Endpoint *epoint;
	struct lcr_msg *message;
	int channel;
	int ret;

	if (message_type == MESSAGE_SETUP) {
		/* enable audio path */
		union parameter newparam;
		memset(&newparam, 0, sizeof(union parameter));
		admin_message_from_lcr(p_m_r_remote_id, p_m_r_ref, MESSAGE_PATTERN, &newparam);
		newparam.audiopath = 1;
		admin_message_from_lcr(p_m_r_remote_id, p_m_r_ref, MESSAGE_AUDIOPATH, &newparam);

		/* set source interface */
		param->setup.callerinfo.itype = p_callerinfo.itype;
		param->setup.callerinfo.isdn_port = p_m_portnum;
		SCPY(param->setup.callerinfo.interface, p_m_mISDNport->ifport->interface->name);
		
		ret = channel = hunt_bchannel();
		if (ret < 0)
			goto no_channel;

		/* open channel */
		ret = seize_bchannel(channel, 1);
		if (ret < 0) {
			no_channel:
			message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_RELEASE);
			message->param.disconnectinfo.cause = 34;
			message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
			message_put(message);
			new_state(PORT_STATE_RELEASE);
			delete this;
			return;
		}
		bchannel_event(p_m_mISDNport, p_m_b_index, B_EVENT_USE);

		/* create endpoint */
		if (p_epointlist)
			FATAL("Incoming call but already got an endpoint.\n");
		if (!(epoint = new Endpoint(p_serial, 0)))
			FATAL("No memory for Endpoint instance\n");
		epoint->ep_app = new_endpointapp(epoint, 0, p_m_mISDNport->ifport->interface->app); //incoming

		epointlist_new(epoint->ep_serial);
	}

	/* set serial on bchannel message
	 * also ref is given, so we send message with ref */
	if (message_type == MESSAGE_BCHANNEL) {
		int i = p_m_b_index;
		unsigned int portid = (mISDNloop.port<<8) + i+1+(i>=15);
		switch (param->bchannel.type) {
		case BCHANNEL_REQUEST:
			p_m_r_handle = portid;
			message_bchannel_to_remote(p_m_r_remote_id, p_m_r_ref, BCHANNEL_ASSIGN, portid, 0, 0, 0, 0, 0, 0, 1);
			chan_trace_header(p_m_mISDNport, this, "MESSAGE_BCHANNEL (to remote application)", DIRECTION_NONE);
			add_trace("type", NULL, "assign");
			add_trace("channel", NULL, "%d.%d", portid>>8, portid&0xff);
			end_trace();
			break;
		case BCHANNEL_RELEASE:
			p_m_r_handle = 0;
			message_bchannel_to_remote(p_m_r_remote_id, p_m_r_ref, BCHANNEL_REMOVE, portid, 0, 0, 0, 0, 0, 0, 1);
			chan_trace_header(p_m_mISDNport, this, "MESSAGE_BCHANNEL (to remote application)", DIRECTION_NONE);
			add_trace("type", NULL, "remove");
			add_trace("channel", NULL, "%d.%d", portid>>8, portid&0xff);
			end_trace();
			break;
		}
		return;
	}
	
	/* cannot just forward, because param is not of container "struct lcr_msg" */
	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, message_type);
	memcpy(&message->param, param, sizeof(message->param));
	message_put(message);

	if (message_type == MESSAGE_RELEASE) {
		new_state(PORT_STATE_RELEASE);
		delete this;
		return;
	}
}

/* select free bchannel from loopback interface */
int Premote::hunt_bchannel(void)
{
	return loop_hunt_bchannel(this, p_m_mISDNport);
}



