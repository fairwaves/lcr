/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** The EndpointApp represents the application for the Endpoint.              **
**                                                                           **
\*****************************************************************************/ 

#include "main.h"

/*
 * EndpointApp constructor
 */
EndpointApp::EndpointApp(class Endpoint *epoint, int origin, int type)
{
	ea_endpoint = epoint;
	ea_type = type;
	classuse++;
}

/*
 * endpoint destructor
 */
EndpointApp::~EndpointApp(void)
{
	classuse--;
}

/* mini application for test purpose only */

void EndpointApp::ea_message_port(unsigned int port_id, int message_type, union parameter *param)
{
	PDEBUG(DEBUG_EPOINT, "%s: Spare function.\n", __FUNCTION__);
}

void EndpointApp::ea_message_join(unsigned int join_id, int message_type, union parameter *param)
{
	PDEBUG(DEBUG_EPOINT, "%s: Spare function.\n", __FUNCTION__);
}


/* create endpoint app */
class EndpointApp *new_endpointapp(class Endpoint *epoint, int origin, int type)
{
	class EndpointApp *app = NULL;

	switch (type) {
	case EAPP_TYPE_PBX:
		app = new EndpointAppPBX(epoint, origin);
		break;
	case EAPP_TYPE_BRIDGE:
		app = new EndpointAppBridge(epoint, origin);
		break;
	}

	if (!app)
		FATAL("Failed to create endpoint APP (type %d)\n", type);

	epoint->ep_app_type = type;
	epoint->ep_app = app;

	return app;
}

#ifdef WITH_MISDN
/*
 * hunts an mISDNport that is available for an outgoing call
 * if no ifname was given, any interface that is not an extension
 * will be searched.
 */
struct mISDNport *EndpointApp::hunt_port(char *ifname, int *channel)
{
	struct interface *interface;
	struct interface_port *ifport, *ifport_start;
	struct select_channel *selchannel; 
	struct mISDNport *mISDNport;
	int index, i;
	int there_is_an_external = 0;

	interface = interface_first;

	/* first find the given interface or, if not given, one with no extension */
	checknext:
	if (!interface) {
		if (!there_is_an_external && !(ifname && ifname[0])) {
			trace_header("CHANNEL SELECTION (no external interface specified)", DIRECTION_NONE);
			add_trace("info", NULL, "Add 'extern' parameter to interface.conf.");
			end_trace();
		}
		return(NULL);
	}

	/* check for given interface */
	if (ifname && ifname[0]) {
		if (!strcasecmp(interface->name, ifname)) {
			/* found explicit interface */
			trace_header("CHANNEL SELECTION (found given interface)", DIRECTION_NONE);
			add_trace("interface", NULL, "%s", ifname);
			end_trace();
			goto foundif;
		}

	} else {
		if (interface->external) {
			there_is_an_external = 1;
			/* found non extension */
			trace_header("CHANNEL SELECTION (found external interface)", DIRECTION_NONE);
			add_trace("interface", NULL, "%s", interface->name);
			end_trace();
			goto foundif;
		}
	}

	interface = interface->next;
	goto checknext;
foundif:

	/* see if interface has ports */
	if (!interface->ifport) {
		/* no ports */
		trace_header("CHANNEL SELECTION (active ports, skipping)", DIRECTION_NONE);
		add_trace("interface", NULL, "%s", interface->name);
		end_trace();
		interface = interface->next;
		goto checknext;
	}

	/* select port by algorithm */
	ifport_start = interface->ifport;
	index = 0;
	if (interface->hunt == HUNT_ROUNDROBIN) {
		while(ifport_start->next && index<interface->hunt_next) {
			ifport_start = ifport_start->next;
			index++;
		}
		trace_header("CHANNEL SELECTION (starting round-robin)", DIRECTION_NONE);
		add_trace("port", NULL, "%d", ifport_start->portnum);
		add_trace("position", NULL, "%d", index);
		end_trace();
	}

	/* loop ports */
	ifport = ifport_start;
	nextport:

	/* see if port is available */
	if (!ifport->mISDNport) {
		trace_header("CHANNEL SELECTION (port not available, skipping)", DIRECTION_NONE);
		add_trace("port", NULL, "%d", ifport->portnum);
		add_trace("position", NULL, "%d", index);
		end_trace();
		goto portbusy;
	}
	mISDNport = ifport->mISDNport;

	/* see if port is administratively blocked */
	if (ifport->block) {
		trace_header("CHANNEL SELECTION (port blocked by admin, skipping)", DIRECTION_NONE);
		add_trace("port", NULL, "%d", ifport->portnum);
		add_trace("position", NULL, "%d", index);
		end_trace();
		goto portbusy;
	}

	/* see if link is up on PTP*/
	if (mISDNport->l2hold && mISDNport->l2link<1) {
		trace_header("CHANNEL SELECTION (port's layer 2 is down, skipping)", DIRECTION_NONE);
		add_trace("port", NULL, "%d", ifport->portnum);
		add_trace("position", NULL, "%d", index);
		end_trace();
		goto portbusy;
	}

	/* check for channel form selection list */
	*channel = 0;
#ifdef WITH_SS5
	if (mISDNport->ss5) {
		class Pss5 *port;
		port = ss5_hunt_line(mISDNport);
		if (port) {
			*channel = port->p_m_b_channel;
			trace_header("CHANNEL SELECTION (selecting SS5 channel)", DIRECTION_NONE);
			add_trace("port", NULL, "%d", ifport->portnum);
			add_trace("position", NULL, "%d", index);
			add_trace("channel", NULL, "%d", *channel);
			end_trace();
		}
	} else
#endif
	{
		selchannel = ifport->out_channel;
		while(selchannel) {
			switch(selchannel->channel) {
				case CHANNEL_FREE: /* free channel */
				if (mISDNport->b_reserved >= mISDNport->b_num)
					break; /* all channel in use or reserverd */
				/* find channel */
				i = 0;
				while(i < mISDNport->b_num) {
					if (mISDNport->b_port[i] == NULL) {
						*channel = i+1+(i>=15);
						trace_header("CHANNEL SELECTION (selecting free channel)", DIRECTION_NONE);
						add_trace("port", NULL, "%d", ifport->portnum);
						add_trace("position", NULL, "%d", index);
						add_trace("channel", NULL, "%d", *channel);
						end_trace();
						break;
					}
					i++;
				}
				if (*channel)
					break;
				trace_header("CHANNEL SELECTION (no channel is 'free')", DIRECTION_NONE);
				add_trace("port", NULL, "%d", ifport->portnum);
				add_trace("position", NULL, "%d", index);
				end_trace();
				break;

				case CHANNEL_ANY: /* don't ask for channel */
				if (mISDNport->b_reserved >= mISDNport->b_num) {
					trace_header("CHANNEL SELECTION (cannot ask for 'any' channel, all reserved)", DIRECTION_NONE);
					add_trace("port", NULL, "%d", ifport->portnum);
					add_trace("position", NULL, "%d", index);
					add_trace("total", NULL, "%d", mISDNport->b_num);
					add_trace("reserved", NULL, "%d", mISDNport->b_reserved);
					end_trace();
					break; /* all channel in use or reserverd */
				}
				trace_header("CHANNEL SELECTION (using 'any' channel)", DIRECTION_NONE);
				add_trace("port", NULL, "%d", ifport->portnum);
				add_trace("position", NULL, "%d", index);
				end_trace();
				*channel = CHANNEL_ANY;
				break;

				case CHANNEL_NO: /* call waiting */
				trace_header("CHANNEL SELECTION (using 'no' channel, call-waiting)", DIRECTION_NONE);
				add_trace("port", NULL, "%d", ifport->portnum);
				add_trace("position", NULL, "%d", index);
				end_trace();
				*channel = CHANNEL_NO;
				break;

				default:
				if (selchannel->channel<1 || selchannel->channel==16) {
					trace_header("CHANNEL SELECTION (channel out of range)", DIRECTION_NONE);
					add_trace("port", NULL, "%d", ifport->portnum);
					add_trace("position", NULL, "%d", index);
					add_trace("channel", NULL, "%d", selchannel->channel);
					end_trace();
					break; /* invalid channels */
				}
				i = selchannel->channel-1-(selchannel->channel>=17);
				if (i >= mISDNport->b_num) {
					trace_header("CHANNEL SELECTION (channel out of range)", DIRECTION_NONE);
					add_trace("port", NULL, "%d", ifport->portnum);
					add_trace("position", NULL, "%d", index);
					add_trace("channel", NULL, "%d", selchannel->channel);
					add_trace("channels", NULL, "%d", mISDNport->b_num);
					end_trace();
					break; /* channel not in port */
				}
				if (mISDNport->b_port[i] == NULL) {
					*channel = selchannel->channel;
					trace_header("CHANNEL SELECTION (selecting given channel)", DIRECTION_NONE);
					add_trace("port", NULL, "%d", ifport->portnum);
					add_trace("position", NULL, "%d", index);
					add_trace("channel", NULL, "%d", *channel);
					end_trace();
					break;
				}
				break;
			}
			if (*channel)
				break; /* found channel */
			selchannel = selchannel->next;
		}
	}

	/* if channel was found, return mISDNport and channel */
	if (*channel) {
		/* setting next port to start next time */
		if (interface->hunt == HUNT_ROUNDROBIN) {
			index++;
			if (!ifport->next)
				index = 0;
			interface->hunt_next = index;
		}
		
		return(mISDNport);
	}

	trace_header("CHANNEL SELECTION (skipping, no channel found)", DIRECTION_NONE);
	add_trace("port", NULL, "%d", ifport->portnum);
	add_trace("position", NULL, "%d", index);
	end_trace();

	portbusy:
	/* go next port, until all ports are checked */
	index++;
	ifport = ifport->next;
	if (!ifport) {
		index = 0;
		ifport = interface->ifport;
	}
	if (ifport != ifport_start)
		goto nextport;

	if (!ifname) {
		interface = interface->next;
		goto checknext;
	}

	return(NULL); /* no port found */
}
#endif

/* hunts for the given interface
 * it does not need to have an mISDNport instance */
struct interface *EndpointApp::hunt_interface(char *ifname)
{
	struct interface *interface;
	int there_is_an_external = 0;

	interface = interface_first;

	/* first find the given interface or, if not given, one with no extension */
	checknext:
	if (!interface) {
		if (!there_is_an_external && !(ifname && ifname[0])) {
			trace_header("CHANNEL SELECTION (no external interface specified)", DIRECTION_NONE);
			add_trace("info", NULL, "Add 'extern' parameter to interface.conf.");
			end_trace();
		}
		return(NULL);
	}

	/* check for given interface */
	if (ifname && ifname[0]) {
		if (!strcasecmp(interface->name, ifname)) {
			/* found explicit interface */
			trace_header("CHANNEL SELECTION (found given interface)", DIRECTION_NONE);
			add_trace("interface", NULL, "%s", ifname);
			end_trace();
			goto foundif;
		}
	} else {
		if (interface->external) {
			there_is_an_external = 1;
			/* found non extension */
			trace_header("CHANNEL SELECTION (found external interface)", DIRECTION_NONE);
			add_trace("interface", NULL, "%s", interface->name);
			end_trace();
			goto foundif;
		}
	}

	interface = interface->next;
	goto checknext;
foundif:

	return interface;
}

/* must be overloaded by specific app */
void EndpointApp::trace_header(const char *name, int direction)
{
}
