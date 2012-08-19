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
Premote::Premote(int type, char *portname, struct port_settings *settings, struct interface *interface, int remote_id) : Port(type, portname, settings, interface)
{
	union parameter param;

	p_callerinfo.itype = (interface->extension)?INFO_ITYPE_ISDN_EXTENSION:INFO_ITYPE_ISDN;
	p_r_ref = new_remote++;
	SCPY(p_r_remote_app, interface->remote_app);
	p_r_tones = (interface->is_tones == IS_YES);

	/* send new ref to remote socket */
	memset(&param, 0, sizeof(union parameter));
	if (type == PORT_TYPE_REMOTE_OUT)
		param.newref.direction = 1; /* new ref from lcr */
	p_r_remote_id = remote_id;
	if (admin_message_from_lcr(p_r_remote_id, p_r_ref, MESSAGE_NEWREF, &param) < 0)
		FATAL("No socket with remote application '%s' found, this shall not happen. because we already created one.\n", p_r_remote_app);

	PDEBUG(DEBUG_PORT, "Created new RemotePort(%s).\n", portname);

}

/*
 * destructor
 */
Premote::~Premote()
{
	PDEBUG(DEBUG_PORT, "Destroyed Remote process(%s).\n", p_name);
}

/*
 * endpoint sends messages to the port
 */
int Premote::message_epoint(unsigned int epoint_id, int message_type, union parameter *param)
{
	struct epoint_list *epointlist;

	if (Port::message_epoint(epoint_id, message_type, param))
		return 1;

	switch (message_type) {
	case MESSAGE_SETUP:
		struct interface *interface;
		interface = getinterfacebyname(p_interface_name);
		if (!interface) {
			PERROR("Cannot find interface %s.\n", p_interface_name);
			return 0;
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

		/* set context to pbx */
		if (!param->setup.dialinginfo.context[0]) {
			if (interface->remote_context[0])
				SCPY(param->setup.dialinginfo.context, interface->remote_context);
			else
				SCPY(param->setup.dialinginfo.context, "lcr");
		}
		/* screen */
		memcpy(&p_callerinfo, &param->setup.callerinfo, sizeof(p_callerinfo));
		memcpy(&p_redirinfo, &param->setup.redirinfo, sizeof(p_redirinfo));
		do_screen(1, p_callerinfo.id, sizeof(p_callerinfo.id), &p_callerinfo.ntype, &p_callerinfo.present, p_interface_name);
		do_screen(1, p_callerinfo.id2, sizeof(p_callerinfo.id2), &p_callerinfo.ntype2, &p_callerinfo.present2, p_interface_name);
		do_screen(1, p_redirinfo.id, sizeof(p_redirinfo.id), &p_redirinfo.ntype, &p_redirinfo.present, p_interface_name);
		memcpy(&param->setup.callerinfo, &p_callerinfo, sizeof(p_callerinfo));
		memcpy(&param->setup.redirinfo, &p_redirinfo, sizeof(p_redirinfo));

		new_state(PORT_STATE_OUT_SETUP);
		break;

	case MESSAGE_PROCEEDING:
		new_state(PORT_STATE_IN_PROCEEDING);
		break;

	case MESSAGE_ALERTING:
		new_state(PORT_STATE_IN_ALERTING);
		break;

	case MESSAGE_CONNECT:
		new_state(PORT_STATE_CONNECT);
		break;

	case MESSAGE_DISCONNECT:
		new_state(PORT_STATE_OUT_DISCONNECT);
		break;

	case MESSAGE_RELEASE:
		new_state(PORT_STATE_RELEASE);
		break;
	}

	/* look for Remote's interface */
	if (admin_message_from_lcr(p_r_remote_id, p_r_ref, message_type, param)<0) {
		PERROR("No socket with remote application '%s' found, this shall not happen. Closing socket shall cause release of all remote ports.\n", p_r_remote_app);
		return 0;		
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
	struct interface *interface;

	switch (message_type) {
	case MESSAGE_TRAFFIC:
		bridge_tx(param->traffic.data, param->traffic.len);
		if (p_tone_name[0]) {
			read_audio(param->traffic.data, param->traffic.len);
			admin_message_from_lcr(p_r_remote_id, p_r_ref, MESSAGE_TRAFFIC, param);
		}
		break;

	case MESSAGE_SETUP:
		interface = getinterfacebyname(p_interface_name);
		if (!interface) {
			PERROR("Cannot find interface %s.\n", p_interface_name);
			return;
		}

		/* enable audio path */
		if (interface->is_tones == IS_YES) {
			union parameter newparam;
			memset(&newparam, 0, sizeof(union parameter));
			admin_message_from_lcr(p_r_remote_id, p_r_ref, MESSAGE_PATTERN, &newparam);
			newparam.audiopath = 1;
			admin_message_from_lcr(p_r_remote_id, p_r_ref, MESSAGE_AUDIOPATH, &newparam);
		}

		/* set source interface */
		param->setup.callerinfo.itype = p_callerinfo.itype;
		SCPY(param->setup.callerinfo.interface, interface->name);
		
		/* create endpoint */
		if (p_epointlist)
			FATAL("Incoming call but already got an endpoint.\n");
		if (!(epoint = new Endpoint(p_serial, 0)))
			FATAL("No memory for Endpoint instance\n");
		epoint->ep_app = new_endpointapp(epoint, 0, interface->app); //incoming

		epointlist_new(epoint->ep_serial);

		new_state(PORT_STATE_IN_SETUP);
		break;

	case MESSAGE_PROCEEDING:
		new_state(PORT_STATE_OUT_PROCEEDING);
		break;

	case MESSAGE_ALERTING:
		new_state(PORT_STATE_OUT_ALERTING);
		break;

	case MESSAGE_CONNECT:
		new_state(PORT_STATE_CONNECT);
		break;

	case MESSAGE_DISCONNECT:
		new_state(PORT_STATE_IN_DISCONNECT);
		break;

	case MESSAGE_RELEASE:
		new_state(PORT_STATE_RELEASE);
		break;
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

/* receive from remote Port instance */
int Premote::bridge_rx(unsigned char *data, int len)
{
	union parameter newparam;
	int l;

	/* don't send tones, if not enabled or not connected */
	if (!p_r_tones
	 && p_state != PORT_STATE_CONNECT)
	 	return 0;

	if (p_tone_name[0])
		return 0;

	memset(&newparam, 0, sizeof(union parameter));
	/* split, if exeeds data size */
	while(len) {
		l = (len > (int)sizeof(newparam.traffic.data)) ? sizeof(newparam.traffic.data) : len;
		newparam.traffic.len = l;
		len -= l;
		memcpy(newparam.traffic.data, data, l);
		data += l;
		admin_message_from_lcr(p_r_remote_id, p_r_ref, MESSAGE_TRAFFIC, &newparam);
	}

	return 0;
}


