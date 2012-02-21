/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** The EndpointAppBridge implements direct bridge between interfaces         **
**                                                                           **
\*****************************************************************************/ 


#include "main.h"

class EndpointAppBridge *appbridge_first = NULL;

/*
 * EndpointAppBridge constructor
 */
EndpointAppBridge::EndpointAppBridge(class Endpoint *epoint, int origin) : EndpointApp(epoint, origin, EAPP_TYPE_BRIDGE)
{
	class EndpointAppBridge **apppointer;

	/* add application to chain */
	next = NULL;
	apppointer = &appbridge_first;
	while(*apppointer)
		apppointer = &((*apppointer)->next);
	*apppointer = this;

	PDEBUG(DEBUG_EPOINT, "Bridge endpoint created\n");
}

/*
 * EpointAppBridge destructor
 */
EndpointAppBridge::~EndpointAppBridge(void)
{
	class EndpointAppBridge *temp, **tempp;

	/* detach */
	temp =appbridge_first;
	tempp = &appbridge_first;
	while(temp) {
		if (temp == this)
			break;

		tempp = &temp->next;
		temp = temp->next;
	}
	if (temp == 0)
		FATAL("Endpoint not in endpoint's list.\n");
	*tempp = next;

	PDEBUG(DEBUG_EPOINT, "Bridge endpoint destroyed\n");
}


/*
 * trace header for application
 */
void EndpointAppBridge::trace_header(const char *name, int direction)
{
	struct trace _trace;

	char msgtext[sizeof(_trace.name)];

	SCPY(msgtext, name);

	/* init trace with given values */
	start_trace(-1,
		    NULL,
		    "", //numberrize_callerinfo(e_callerinfo.id, e_callerinfo.ntype, options.national, options.international),
		    "", // e_dialinginfo.id,
		    direction,
		    CATEGORY_EP,
		    ea_endpoint->ep_serial,
		    msgtext);
}

/* port MESSAGE_SETUP */
void EndpointAppBridge::port_setup(struct port_list *portlist, int message_type, union parameter *param)
{
	struct interface *interface_in = interface_first;
	struct interface *interface_out = interface_first;
	struct port_settings	port_settings;
	class Port		*port = NULL;
	struct lcr_msg *message;
	unsigned int bridge_id;
	unsigned int source_port_id = portlist->port_id;
	char portname[64];
	int cause = 47;

	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) epoint received setup from='%s' to='%s'\n", ea_endpoint->ep_serial, param->setup.callerinfo.id, param->setup.dialinginfo.id);

	if (!ea_endpoint->ep_portlist) {
		PERROR("Endpoint has no port in portlist\n");
		return;
	}
	if (ea_endpoint->ep_portlist->next) {
		PDEBUG(DEBUG_EPOINT, "Endpoint already received setup, ignoring.\n");
		return;
	}

	while (interface_in) {
		if (!strcmp(interface_in->name, param->setup.callerinfo.interface))
			break;
		interface_in = interface_in->next;
	}
	if (!interface_in) {
		PERROR("Cannot find source interface %s.\n", param->setup.callerinfo.interface);
fail:
		message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = cause;
		message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		message_put(message);
		ea_endpoint->free_portlist(portlist);

		/* destroy endpoint */
		ea_endpoint->ep_use = 0;
		trigger_work(&ea_endpoint->ep_delete);
		return;
	}

	while (interface_out) {
		if (!strcmp(interface_out->name, interface_in->bridge_if))
			break;
		interface_out = interface_out->next;
	}
	if (!interface_out) {
		PERROR("Cannot find destination interface %s.\n", interface_in->bridge_if);
		goto fail;
		return;
	}

	/* create port for interface */
	SPRINT(portname, "%s-%d-out", interface_out->name, 0);
	memset(&port_settings, 0, sizeof(port_settings));
#ifdef WITH_SIP
	if (interface_out->sip) {
		port = new Psip(PORT_TYPE_SIP_OUT, portname, &port_settings, interface_out);
	} else
#endif
#ifdef WITH_GSM_BS
	if (interface_out->gsm_bs) {
		port = new Pgsm_bs(PORT_TYPE_GSM_BS_OUT, portname, &port_settings, interface_out);
	} else
#endif
#ifdef WITH_GSM_MS
	if (interface_out->gsm_ms) {
		port = new Pgsm_bs(PORT_TYPE_GSM_MS_OUT, portname, &port_settings, interface_out);
	} else
#endif
	{
#ifdef WITH_MISDN
		struct mISDNport *mISDNport;
		char *ifname = interface_out->name;
		int channel = 0;
		struct admin_list *admin;
		int earlyb;
		int mode = B_MODE_TRANSPARENT;

		/* hunt for mISDNport and create Port */
		mISDNport = hunt_port(ifname, &channel);
		if (!mISDNport) {
			trace_header("INTERFACE (busy)", DIRECTION_NONE);
			add_trace("interface", NULL, "%s", ifname);
			end_trace();
			cause = 33;
			goto fail;
		}

		SPRINT(portname, "%s-%d-out", mISDNport->ifport->interface->name, mISDNport->portnum);
#ifdef WITH_SS5
		if (mISDNport->ss5)
			port = ss5_hunt_line(mISDNport);
		else
#endif
		if (mISDNport->ifport->remote) {
			admin = admin_first;
			while(admin) {
				if (admin->remote_name[0] && !strcmp(admin->remote_name, mISDNport->ifport->remote_app))
					break;
				admin = admin->next;
			}
			if (!admin) {
				trace_header("INTERFACE (remote not connected)", DIRECTION_NONE);
				add_trace("application", NULL, "%s", mISDNport->ifport->remote_app);
				end_trace();
				cause = 27;
				goto fail;
			}
			port = new Premote(PORT_TYPE_REMOTE_OUT, mISDNport, portname, &port_settings, channel, mISDNport->ifport->channel_force, mode, admin->sock);
		} else
			port = new Pdss1((mISDNport->ntmode)?PORT_TYPE_DSS1_NT_OUT:PORT_TYPE_DSS1_TE_OUT, mISDNport, portname, &port_settings, channel, mISDNport->ifport->channel_force, mode);
		earlyb = mISDNport->earlyb;
#else
		trace_header("INTERFACE (has no function)", DIRECTION_NONE);
		add_trace("interface", NULL, "%s", ifname);
		end_trace();
		cause = 31;
		goto fail
#endif
	}
	if (!port)
		FATAL("Remote interface, but not supported???\n");
	portlist = ea_endpoint->portlist_new(port->p_serial, port->p_type, interface_out->is_earlyb == IS_YES);
	if (!portlist)
		FATAL("EPOINT(%d) cannot allocate port_list relation\n", ea_endpoint->ep_serial);
	/* forward setup */
	message_forward(ea_endpoint->ep_serial, port->p_serial, EPOINT_TO_PORT, param);  

	/* apply bridge to interfaces */
	/* FIXME: use mISDN bridge for mISDN ports */
	bridge_id = join_serial++;
	message = message_create(ea_endpoint->ep_serial, source_port_id, EPOINT_TO_PORT, MESSAGE_BRIDGE);
	message->param.bridge_id = bridge_id;
	message_put(message);
	message = message_create(ea_endpoint->ep_serial, port->p_serial, EPOINT_TO_PORT, MESSAGE_BRIDGE);
	message->param.bridge_id = bridge_id;
	message_put(message);
}

/* port MESSAGE_RELEASE */
void EndpointAppBridge::port_release(struct port_list *portlist, int message_type, union parameter *param)
{
	struct port_list *remote;

	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) epoint received release from port\n");

	if (!ea_endpoint->ep_portlist || !ea_endpoint->ep_portlist->next)
		goto out;
	if (ea_endpoint->ep_portlist->port_id == portlist->port_id)
		remote = ea_endpoint->ep_portlist->next;
	else
		remote = ea_endpoint->ep_portlist;
	/* forward release */
	message_forward(ea_endpoint->ep_serial, remote->port_id, EPOINT_TO_PORT, param);  

	/* remove relations to in and out port */
	ea_endpoint->free_portlist(portlist);
	ea_endpoint->free_portlist(remote);

out:
	/* destroy endpoint */
	ea_endpoint->ep_use = 0;
	trigger_work(&ea_endpoint->ep_delete);
}

/* port other messages */
void EndpointAppBridge::port_other(struct port_list *portlist, int message_type, union parameter *param)
{
	unsigned int remote;

	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) epoint received message %d from port\n", message_type);

	if (!ea_endpoint->ep_portlist || !ea_endpoint->ep_portlist->next)
		return;
	if (ea_endpoint->ep_portlist->port_id == portlist->port_id)
		remote = ea_endpoint->ep_portlist->next->port_id;
	else
		remote = ea_endpoint->ep_portlist->port_id;
	/* forward release */
	message_forward(ea_endpoint->ep_serial, remote, EPOINT_TO_PORT, param);  
}

/* port sends message to the endpoint
 */
void EndpointAppBridge::ea_message_port(unsigned int port_id, int message_type, union parameter *param)
{
	struct port_list *portlist;

	portlist = ea_endpoint->ep_portlist;
	while(portlist) {
		if (port_id == portlist->port_id)
			break;
		portlist = portlist->next;
	}
	if (!portlist) {
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) warning: port is not related to this endpoint. This may happen, if port has been released after the message was created.\n", ea_endpoint->ep_serial);
		return;
	}

//	PDEBUG(DEBUG_EPOINT, "received message %d (terminal %s, caller id %s)\n", message, e_ext.number, e_callerinfo.id);
	switch(message_type) {
		/* PORT sends SETUP message */
		case MESSAGE_SETUP:
		port_setup(portlist, message_type, param);
		break;

		/* PORT sends RELEASE message */
		case MESSAGE_RELEASE:
		port_release(portlist, message_type, param);
		break;

		default:
		port_other(portlist, message_type, param);
	}

	/* Note: this endpoint may be destroyed, so we MUST return */
}

