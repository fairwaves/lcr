/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** The EndpointAppPBX implements PBX4Linux                                   **
**                                                                           **
\*****************************************************************************/ 


#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
#include "main.h"


class EndpointAppPBX *apppbx_first = NULL;

/*
 * EndpointAppPBX constructor
 */
EndpointAppPBX::EndpointAppPBX(class Endpoint *epoint) : EndpointApp(epoint)
{
	class EndpointAppPBX **apppointer;

	/* add application to chain */
	next = NULL;
	apppointer = &apppbx_first;
	while(*apppointer)
		apppointer = &((*apppointer)->next);
	*apppointer = this;

	/* initialize */
        memset(&e_ext, 0, sizeof(struct extension));
	e_ext.rights = 4; /* international */
	e_ext.rxvol = e_ext.txvol = 256;
        e_state = EPOINT_STATE_IDLE;
        e_terminal[0] = '\0';
	e_terminal_interface[0] = '\0';
        memset(&e_callerinfo, 0, sizeof(struct caller_info));
        memset(&e_dialinginfo, 0, sizeof(struct dialing_info));
        memset(&e_connectinfo, 0, sizeof(struct connect_info));
        memset(&e_redirinfo, 0, sizeof(struct redir_info));
        memset(&e_capainfo, 0, sizeof(struct capa_info));
        e_start = e_stop = 0;
//      e_origin = 0;
	e_ruleset = ruleset_main;
	if (e_ruleset)
        	e_rule = e_ruleset->rule_first;
	e_rule_nesting = 0;
        e_action = NULL;
	e_action_timeout = 0;
	e_match_timeout = 0;
	e_match_to_action = NULL;
        e_select = 0;
        e_extdialing = e_dialinginfo.number;
//        e_knocking = 0;
//        e_knocktime = 0;
	e_hold = 0;
//        e_call_tone[0] = e_hold_tone[0] = '\0';
        e_call_pattern /*= e_hold_pattern*/ = 0;
        e_redial = 0;
	e_tone[0] = '\0';
	e_adminid = 0; // will be set, if call was initiated via admin socket
        e_powerdialing = 0;
        e_powerdelay = 0;
        e_powerlimit = 0;
        e_callback = 0;
        e_cbdialing[0] = '\0';
        e_cbcaller[0] = '\0';
	e_cbto[0] = '\0';
        memset(&e_callbackinfo, 0, sizeof(struct caller_info));
        e_connectedmode = 0;
        e_dtmf = 0;
        e_dtmf_time = 0;
        e_dtmf_last = 0;
	e_cfnr_release = 0;
	e_cfnr_call = 0;
	e_password_timeout = 0;
	e_multipoint_cause = CAUSE_NOUSER;
	e_multipoint_location = LOCATION_PRIVATE_LOCAL;
	e_dialing_queue[0] = '\0';
	e_crypt = CRYPT_OFF;
	e_crypt_state = CM_ST_NULL;
	e_crypt_keyengine_busy = 0;
	e_crypt_info[0] = '\0'; 
	e_overlap = 0;
	e_vbox[0] = '\0';
	e_tx_state = NOTIFY_STATE_ACTIVE;
	e_rx_state = NOTIFY_STATE_ACTIVE;
	e_call_cause = e_call_location = 0;
/*********************************
 *********************************
 ********* ATTENTION *************
 *********************************
 *********************************/
/* if you add new values, that must be initialized, also check if they must
 * be initialized when doing callback
 */

}

/*
 * EpointAppPBX destructor
 */
EndpointAppPBX::~EndpointAppPBX(void)
{
	class EndpointAppPBX *temp, **tempp;

	/* detach */
	temp =apppbx_first;
	tempp = &apppbx_first;
	while(temp)
	{
		if (temp == this)
			break;

		tempp = &temp->next;
		temp = temp->next;
	}
	if (temp == 0)
	{
		PERROR("error: endpoint not in endpoint's list, exitting.\n");
		exit(-1);
	}
	*tempp = next;

}


EPOINT_STATE_NAMES

/* set new endpoint state
 */
void EndpointAppPBX::new_state(int state)
{
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) new state %s --> %s\n", ea_endpoint->ep_serial, state_name[e_state], state_name[state]);
	e_state = state;
}


/* screen caller id
 * out==0: incomming caller id, out==1: outgoing caller id
 */
void EndpointAppPBX::screen(int out, char *id, int idsize, int *type, int *present)
{
	struct interface	*interface;

	interface = interface_first;
	while(interface)
	{
		if (!strcmp(e_callerinfo.interface, interface->name))
		{
			break;
		}
		interface = interface->next;
	}
add logging
	if (interface)
	{
		/* screen incoming caller id */
		if (!out)
		{
			/* check for MSN numbers, use first MSN if no match */
			msn1 = NULL;
			ifmsn = interface->ifmsn;
			while(ifmns)
			{
				if (!msn1)
					msn1 = ifmns->msn;
				if (!strcmp(ifmns->mns, id))
				{
					break;
				}
				ifmsn = ifmsn->next;
			}
			if (!ifmns && mns1) // not in list, first msn given
				UNCPY(id, msn1, idsize);
			id[idsize-1] = '\0';
		}
	
		/* check screen list */
		if (out)
			iscreen = interface->ifscreen_out;
		else
			iscreen = interface->ifscreen_in;
		while (ifscreen)
		{
			if (ifcreen->match_type==-1 || ifscreen->match_type==*type)
			if (ifcreen->match_present==-1 || ifscreen->match_present==*present)
			{
				if (strchr(ifcreen->match_id,'%'))
				{
					if (!strncmp(ifscreen->match_id, id, strchr(ifscreen->match_id,'%')-ifscreen->match_id))
						break;
				} else
				{
					if (!strcmp(ifscreen->match_id, id))
						break;
				}
			}
			ifscreen = ifscreen->next;
		}
		if (ifscreen) // match
		{
			if (ifscren->result_type != -1)
				*type = ifscreen->result_type;
			if (ifscren->result_present != -1)
				*present = ifscreen->result_present;
			if (strchr(ifscreen->match_id,'%'))
			{
				SCPY(suffix, strchr(ifscreen->match_id,'%') - ifscreen->match_id + id);
				UNCPY(id, ifscreen->result_id);
				id[idsize-1] = '\0';
				if (strchr(ifscreen->result_id,'%'))
				{
					*strchr(ifscreen->result_id,'%') = '\0';
					UNCAT(id, suffix, idsize);
					id[idsize-1] = '\0';
				}
			} else
			{
				UNCPY(id, ifscreen->result_id, idsize);
				id[idsize-1] = '\0';
			}
		}
	}
}

/* release call and port (as specified)
 */
void EndpointAppPBX::release(int release, int calllocation, int callcause, int portlocation, int portcause)
{
	struct port_list *portlist;
	struct message *message;
	char cause[16];
	class Call *call;

	/* message to test call */
	admin_call_response(e_adminid, ADMIN_CALL_RELEASE, "", callcause, calllocation, 0);

	/* if a release is pending */
	if (release==RELEASE_CALL || release==RELEASE_ALL || release==RELEASE_PORT_CALLONLY)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): do pending release (callcause %d location %d)\n", ea_endpoint->ep_serial, callcause, calllocation);
		if (ea_endpoint->ep_call_id)
		{
			call = find_call_id(ea_endpoint->ep_call_id);
			if (call)
				call->release(ea_endpoint->ep_serial, 0, calllocation, callcause);
		}
		ea_endpoint->ep_call_id = 0;
		e_call_pattern = 0;
#if 0
		if (release != RELEASE_PORT_CALLONLY)
		{
			if (e_hold_id)
				call_release(e_hold_id, ea_endpoint->ep_serial, 1, calllocation, callcause);
			e_hold_id = 0;
		}
#endif
	}
	if (release==RELEASE_ALL || release==RELEASE_PORT_CALLONLY)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) do pending release (portcause %d portlocation)\n", ea_endpoint->ep_serial, portcause, portlocation);
		while((portlist = ea_endpoint->ep_portlist))
		{
			if (portlist->port_id)
			{
				SPRINT(cause, "cause_%02x", portcause);
				set_tone(portlist, cause);
				message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_RELEASE);
				message->param.disconnectinfo.cause = portcause;
				message->param.disconnectinfo.location = portlocation;
				message_put(message);
				logmessage(message);
			}
			ea_endpoint->free_portlist(portlist);
		}

		/* if callback is enabled, call back with the given caller id */
		if (e_callback)
		{
			/* reset some stuff */
		        new_state(EPOINT_STATE_IDLE);
			memset(&e_connectinfo, 0, sizeof(struct connect_info));
			memset(&e_redirinfo, 0, sizeof(struct redir_info));
			e_start = e_stop = 0;
			e_ruleset = ruleset_main;
			if (e_ruleset)
        			e_rule = e_ruleset->rule_first;
			e_action = NULL;
			e_action_timeout = 0;
			e_match_timeout = 0;
			e_match_to_action = NULL;
			//e_select = 0;
        		e_extdialing = e_dialinginfo.number;
			e_connectedmode = 0;
			e_dtmf = 0;
			e_dtmf_time = 0;
			e_dtmf_last = 0;
			e_cfnr_release = 0;
			e_cfnr_call = 0;
			e_multipoint_cause = CAUSE_NOUSER;
			e_multipoint_location = LOCATION_PRIVATE_LOCAL;
			e_dialing_queue[0] = '\0';
			e_crypt = 0;
			e_crypt_state = CM_ST_NULL;
			e_crypt_keyengine_busy = 0;
			e_crypt_info[0] = '\0'; 
			e_tone[0] = '\0';
			e_overlap = 0;
			e_vbox[0] = '\0';
			e_tx_state = NOTIFY_STATE_ACTIVE;
			e_rx_state = NOTIFY_STATE_ACTIVE;
			e_call_cause = e_call_location = 0;
			e_rule_nesting = 0;
			/* the caller info of the callback user */
			memcpy(&e_callbackinfo, &e_callerinfo, sizeof(e_callbackinfo));
			memset(&e_dialinginfo, 0, sizeof(e_dialinginfo));
			/* create dialing by callerinfo */
			if (e_terminal[0] && e_terminal_interface[0])
			{
				PDEBUG(DEBUG_EPOINT, "EPOINT(%d) preparing callback to internal: %s interface %s\n", ea_endpoint->ep_serial, e_terminal, e_terminal_interface);
				/* create callback to the current terminal */
				SCPY(e_dialinginfo.number, e_terminal);
				SCPY(e_dialinginfo.interfaces, e_terminal_interface);
				e_dialinginfo.itype = INFO_ITYPE_ISDN_EXTENSION;
				e_dialinginfo.ntype = INFO_NTYPE_UNKNOWN;
			} else
			{
				if (e_cbto[0])
				{
					SCPY(e_dialinginfo.number, e_cbto);
				} else
				{
					/* numberrize caller id and use it to dial to the callback */
					SCPY(e_dialinginfo.number, numberrize_callerinfo(e_callerinfo.id,e_callerinfo.ntype));
				}
				e_dialinginfo.itype = INFO_ITYPE_ISDN;
				e_dialinginfo.ntype = INFO_NTYPE_UNKNOWN;
				PDEBUG(DEBUG_EPOINT, "EPOINT(%d) preparing callback to external: %s\n", ea_endpoint->ep_serial, e_dialinginfo.number);
			}
			return;
		}

		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) do pending release of epoint itself.\n", ea_endpoint->ep_serial);
		ea_endpoint->ep_use--; /* when e_lock is 0, the endpoint will be deleted */
		return;
	}
}


/* cancel callerid if restricted, unless anon-ignore is enabled at extension or port is of type external (so called police gets caller id :)*/
void apply_callerid_restriction(int anon_ignore, int port_type, char *id, int *ntype, int *present, int *screen, char *voip, char *intern, char *name)
{
	PDEBUG(DEBUG_EPOINT, "id='%s' ntype=%d present=%d screen=%d voip='%s' intern='%s' name='%s'\n", (id)?id:"NULL", (ntype)?*ntype:-1, (present)?*present:-1, (screen)?*screen:-1, (voip)?voip:"NULL", (intern)?intern:"NULL", (name)?name:"NULL");

	/* caller id is not restricted, so we do nothing */
	if (*present != INFO_PRESENT_RESTRICTED)
		return;

	/* only extensions are restricted */
	if (!intern)
		return;
	if (!intern[0])
		return;

	/* if we enabled anonymouse ignore */
	if (anon_ignore)
		return;

	/* else we remove the caller id */
	if (id)
		id[0] = '\0';
	if (ntype)
		*ntype = INFO_NTYPE_UNKNOWN;
//	if (screen)
//		*screen = INFO_SCREEN_USER;
// maybe we should not make voip address anonymous
//	if (voip)
//		voip[0] = '\0';
// maybe it's no fraud to present internal id
//	if (intern)
//		intern[0] = '\0';
	if (name)
		name[0] = '\0';
}

/* used display message to display callerid as available */
char *EndpointAppPBX::apply_callerid_display(char *id, int itype, int ntype, int present, int screen, char *voip, char *intern, char *name)
{
	static char display[81];

	display[0] = '\0';
	char *cid = numberrize_callerinfo(id, ntype);

	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) id='%s' itype=%d ntype=%d present=%d screen=%d voip='%s' intern='%s' name='%s'\n", ea_endpoint->ep_serial, (id)?id:"NULL", itype, ntype, present, screen, (voip)?voip:"NULL", (intern)?intern:"NULL", (name)?name:"NULL");

	if (!id)
		id = "";
	if (!voip)
		voip = "";
	if (!intern)
		intern = "";
	if (!name)
		name = "";

	/* NOTE: is caller is is not available for this extesion, it has been removed by apply_callerid_restriction already */

	/* internal extension's caller id */
	if (intern[0] && e_ext.display_int)
	{
		if (!display[0])
			SCAT(display, intern);
		if (display[0])
			SCAT(display, " ");
		if (itype == INFO_ITYPE_VBOX)
			SCAT(display, "(vbox)");
		else
			SCAT(display, "(int)");
	}

	/* external caller id */
	if (!intern[0] && !voip[0] && e_ext.display_ext)
	{
		if (!display[0])
		{
			if (!cid[0])
			{
				if (present == INFO_PRESENT_RESTRICTED)
					SCAT(display, "anonymous");
				else
					SCAT(display, "unknown");
			}
			else
				SCAT(display, cid);
		}
	}

	/* voip caller id */
	if (voip[0] && e_ext.display_voip)
	{
		if (!display[0] && cid[0])
				SCAT(display, cid);
		if (display[0])
				SCAT(display, " ");
		SCAT(display, voip);
	}

	/* display if callerid is anonymouse but available due anon-ignore */
	if (e_ext.display_anon && present==INFO_PRESENT_RESTRICTED)
	{
		if (!cid[0])
			SCAT(display, "unknown");
		else 
			SCAT(display, cid);
		SCAT(display, " anon");
	}

	/* display if callerid is anonymouse but available due anon-ignore */
	if (e_ext.display_fake && screen==INFO_SCREEN_USER && present!=INFO_PRESENT_NULL)
	{
		if (!display[0])
		{
			if (!id[0])
			{
				if (present == INFO_PRESENT_RESTRICTED)
					SCAT(display, "anonymous");
				else
					SCAT(display, "unknown");
			}
			else
				SCAT(display, cid);
		}
		SCAT(display, " fake");
	}

	/* caller name */
	if (name[0] && e_ext.display_name)
	{
		if (!display[0] && cid[0])
				SCAT(display, cid);
		if (display[0])
				SCAT(display, " ");
		SCAT(display, name);
	}

	return(display);
}

/*
 * uses the current state to notify activity
 */
void EndpointAppPBX::notify_active(void)
{
	struct port_list *portlist = ea_endpoint->ep_portlist;
	struct message *message;
	int notify = 0;

	switch(e_tx_state)
	{
		case NOTIFY_STATE_ACTIVE:
		/* we are already active, so we don't do anything */
		break;

		case NOTIFY_STATE_SUSPEND:
		notify = INFO_NOTIFY_USER_RESUMED;
		while(portlist)
		{
			set_tone(portlist, NULL);
			portlist = portlist->next;
		}
		portlist = ea_endpoint->ep_portlist;
		break;

		case NOTIFY_STATE_HOLD:
		notify = INFO_NOTIFY_REMOTE_RETRIEVAL;
		while(portlist)
		{
			set_tone(portlist, NULL);
			portlist = portlist->next;
		}
		portlist = ea_endpoint->ep_portlist;
		break;

		case NOTIFY_STATE_CONFERENCE:
		notify = INFO_NOTIFY_CONFERENCE_DISCONNECTED;
		while(portlist)
		{
			set_tone(portlist, NULL);
			portlist = portlist->next;
		}
		portlist = ea_endpoint->ep_portlist;
		break;

		default:
		PERROR("unknown e_tx_state = %d\n", e_tx_state);
	}

	if (notify)
	while(portlist)
	{
		message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_NOTIFY);
		message->param.notifyinfo.notify = notify;
		message_put(message);
		logmessage(message);
		portlist = portlist->next;
	}
}


/*
 * keypad functions during call. one example to use this is to put a call on hold or start a conference
 */
void EndpointAppPBX::keypad_function(char digit)
{

	/* we must be in a call, in order to send messages to the call */
	if (e_terminal[0] == '\0')
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) IGNORING keypad received not from extension.\n", ea_endpoint->ep_serial);
		return;
	}

	switch(digit)
	{
		/* join conference */
		case '3':
		if (ea_endpoint->ep_call_id == 0)
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) keypad received during connect but not during a call.\n", ea_endpoint->ep_serial);
			break;
		}
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) join call with call on hold\n", ea_endpoint->ep_serial);
		join_call();
		break;

		/* crypt shared */
		case '7':
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) shared key encryption selected.\n", ea_endpoint->ep_serial);
		encrypt_shared();
		break;

		/* crypt key-exchange */
		case '8':
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) key exchange encryption selected.\n", ea_endpoint->ep_serial);
		encrypt_keyex();
		break;

		/* crypt off */
		case '9':
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) encryption off selected.\n", ea_endpoint->ep_serial);
		encrypt_off();
		break;

		default:	
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) unsupported keypad digit '%c'.\n", ea_endpoint->ep_serial, digit);
	}
}


/* set tone pattern for port */
void EndpointAppPBX::set_tone(struct port_list *portlist, char *tone)
{
	struct message *message;

	if (!tone)
		tone = "";

	/* store for suspended processes */
	SCPY(e_tone, tone);

	if (!portlist)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) no endpoint to notify tone.\n", ea_endpoint->ep_serial);
		return;
	}

	if (e_call_pattern /* pattern are provided */
	 && !(e_ext.own_setup && e_state == EPOINT_STATE_IN_SETUP)
	 && !(e_ext.own_setup && e_state == EPOINT_STATE_IN_OVERLAP)
	 && !(e_ext.own_proceeding && e_state == EPOINT_STATE_IN_PROCEEDING)
	 && !(e_ext.own_alerting && e_state == EPOINT_STATE_IN_ALERTING)
	 && !(e_ext.own_cause && e_state == EPOINT_STATE_IN_DISCONNECT)
	 && !(e_ext.own_setup && e_state == EPOINT_STATE_OUT_SETUP)
	 && !(e_ext.own_setup && e_state == EPOINT_STATE_OUT_OVERLAP)
	 && !(e_ext.own_proceeding && e_state == EPOINT_STATE_OUT_PROCEEDING)
	 && !(e_ext.own_alerting && e_state == EPOINT_STATE_OUT_ALERTING)
	 && !(e_ext.own_cause && e_state == EPOINT_STATE_OUT_DISCONNECT)
	 && tone[0] && !!strncmp(tone,"crypt_*",6))
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) tone not provided since patterns are available\n", ea_endpoint->ep_serial);
		tone = "";
	}

	if (portlist)
	{
		message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_TONE);
		SCPY(message->param.tone.dir, e_ext.tones_dir[0]?e_ext.tones_dir:options.tones_dir);
		SCPY(message->param.tone.name, tone);
		message_put(message);
		logmessage(message);
	}
}


/*
 * hunts an mISDNport that is available for an outgoing call
 * if no ifname was given, any interface that is not an extension
 * will be searched.
 */
static struct mISDNport *EndpointAppPBX::hunt_port(char *ifname, int *channel)
{
	struct interface *interface;
	struct mISDNport *mISDNport;

	interface = interface_first;

	/* first find the given interface or, if not given, one with no extension */
	checknext:
	if (!interface)
		return(null);

	/* check for given interface */
	if (ifname)
	{
		if (!strcasecmp(interface->name, ifname))
		{
			/* found explicit interface */
			printlog("%3d  interface %s found as given\n", ea_endpoint->ep_serial, ifname);
			goto found;
		}

	} else
	{
		if (!interface->extension)
		{
			/* found non extension */
			printlog("%3d  interface %s found, that is not an extension\n", ea_endpoint->ep_serial, interface->name);
			goto found;
		}
	}

	interface = interface->next;
	goto checknext;

	/* see if interface has ports */
	if (!interface->ifport)
	{
		/* no ports */
		printlog("%3d  interface %s has no active ports, skipping.\n", ea_endpoint->ep_serial, interface->name);
		interface = interface->next;
		goto checknext;
	}

	/* select port by algorithm */
	ifport_start = interface->port;
	index = 0;
	if (interface->hunt == HUNT_ROUNDROBIN)
	{
		while(ifport_start->next && index<interface->hunt_next)
		{
			ifport_start = ifport_start->next;
			i++;
		}
		printlog("%3d  starting with port#%d position %d (round-robin)\n", ea_endpoint->ep_serial, ifport_start->portnum, index);
	}

	/* loop ports */
	ifport = ifport_start;
	nextport:

	/* see if port is available */
	if (!ifport->mISDNport)
	{
		printlog("%3d  port#%d position %d is not available, skipping.\n", ea_endpoint->ep_serial, ifport->portnum, index);
		goto portbusy;
	}
	mISDNport = ifport->mISDNport;

#warning admin block auch bei incomming calls
#warning calls releasen, wenn port entfernt wird, geblockt wird
	/* see if port is administratively blocked */
	if (ifport->block)
	{
		printlog("%3d  port#%d position %d is administratively blocked, skipping.\n", ea_endpoint->ep_serial, ifport->portnum, index);
		goto portbusy;
	}

	/* see if link is up */
	if (mISDNport->ptp && !mISDNport->l2link)
	{
		printlog("%3d  port#%d position %d is ptp but layer 2 is down.\n", ea_endpoint->ep_serial, ifport->portnum, index);
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) skipping because it is PTP with L2 down\n", ea_endpoint->ep_serial);
		goto portbusy;
	}

	/* check for channel form selection list */
	*channel = 0;
	selchannel = ifport->selchannel;
	while(selchannel)
	{
		switch(selchannel->channel)
		{
			case CHANNEL_FREE: /* free channel */
			if (mISDNport->b_inuse >= mISDNport->b_num)
				break; /* all channel in use or reserverd */
			/* find channel */
			i = 0;
			while(i < mISDNport->b_num)
			{
				if (mISDNport->b_port[i] == NULL)
				{
					*channel = i+1+(i>=15);
					printlog("%3d  port#%d position %d selecting free channel %d\n", ea_endpoint->ep_serial, ifport->portnum, index, *channel);
					break;
				}
				i++;
			}
			break;

			case CHANNEL_ANY: /* don't ask for channel */
			if (mISDNport->b_inuse >= mISDNport->b_num)
			{
				break; /* all channel in use or reserverd */
			}
			printlog("%3d  port#%d position %d using with 'any channel'\n", ea_endpoint->ep_serial, ifport->portnum, index);
			*channel = SEL_CHANNEL_ANY;
			break;

			case CHANNEL_NO: /* call waiting */
			printlog("%3d  port#%d position %d using with 'no channel'\n", ea_endpoint->ep_serial, ifport->portnum, index);
			*channel = SEL_CHANNEL_NO;
			break;

			default:
			if (selchannel->channel<1 || selchannel->channel==16)
				break; /* invalid channels */
			i = selchannel->channel-1-(selchannel->channel>=17);
			if (i >= mISDNport->b_num)
				break; /* channel not in port */
			if (mISDNport->b_port[i] == NULL)
			{
				*channel = selchannel->channel;
				printlog("%3d  port#%d position %d selecting given channel %d\n", ea_endpoint->ep_serial, ifport->portnum, index, *channel);
				break;
			}
			break;
		}
		if (*channel)
			break; /* found channel */
		selchannel = selchannel->next;
	}

	/* if channel was found, return mISDNport and channel */
	if (*channel)
	{
		/* setting next port to start next time */
		if (interface->hunt == HUNT_ROUNDROBIN)
		{
			index++;
			if (!ifport->next)
				index = 0;
			interface->hunt_next = index;
		}
		
		return(mISDNport);
	}

	printlog("%3d  port#%d position %d skipping, because no channel found.\n", ea_endpoint->ep_serial, ifport->portnum, index);

	portbusy:
	/* go next port, until all ports are checked */
	index++;
	ifport = ifport->next;
	if (!ifport)
	{
		index = 0;
		ifport = interface->ifport;
	}
	if (ifport != ifport_start)
		goto nextport;

	return(NULL); /* no port found */
}

/* outgoing setup to port(s)
 * ports will be created and a setup is sent if everything is ok. otherwhise
 * the endpoint is destroyed.
 */
void EndpointAppPBX::out_setup(void)
{
	struct dialing_info	dialinginfo;
	class Port		*port;
//	class pdss1		*pdss1;
	struct port_list	*portlist;
	struct message		*message;
	int			anycall = 0;
	int			cause = CAUSE_RESSOURCEUNAVAIL;
	char			*p;
	char			cfp[64];
	struct mISDNport	*mISDNport;
	char			portname[32];
	char			*dirname;
	class EndpointAppPBX	*atemp;
//	char			allowed_ports[256];
//	char			exten[256];
	int			i, ii;
	int			use;
	char			ifname[sizeof(e_ext.interfaces)],
				number[256];
	struct port_settings	port_settings;
	int			channel = 0;

	/* create settings for creating port */
	memset(&port_settings, 0, sizeof(port_settings));
	if (e_ext.tones_dir)
		SCPY(port_settings.tones_dir, e_ext.tones_dir);
	else
		SCPY(port_settings.tones_dir, options.tones_dir);
	port_settings.tout_setup = e_ext.tout_setup;
	port_settings.tout_dialing = e_ext.tout_dialing;
	port_settings.tout_proceeding = e_ext.tout_proceeding;
	port_settings.tout_alerting = e_ext.tout_alerting;
	port_settings.tout_disconnect = e_ext.tout_disconnect;
//	port_settings.tout_hold = e_ext.tout_hold;
//	port_settings.tout_park = e_ext.tout_park;
	port_settings.no_seconds = e_ext.no_seconds;
	
	/* NOTE: currently the try_card feature is not supported. it should be used later to try another card, if the outgoing call fails on one port */

	/* check what dialinginfo.itype we got */
	switch(e_dialinginfo.itype)
	{
		/* *********************** call to extension or vbox */
		case INFO_ITYPE_ISDN_EXTENSION:
		/* check if we deny incoming calls when we use an extension */
		if (e_ext.noknocking)
		{
			atemp = apppbx_first;
			while(atemp)
			{
				if (atemp != this)
				if (!strcmp(atemp->e_terminal, e_terminal))
					break;
				atemp = atemp->next;
			}
			if (atemp)
			{
				PERROR("EPOINT(%d) noknocking and currently a call\n", ea_endpoint->ep_serial);
				release(RELEASE_ALL, LOCATION_PRIVATE_LOCAL, CAUSE_BUSY, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL); /* RELEASE_TYSPE_ call, port */
				return; /* must exit here */
			}
		}
		/* FALL THROUGH !!!! */
		case INFO_ITYPE_VBOX:
		/* get dialed extension's info */
//		SCPY(exten, e_dialinginfo.number);
//		if (strchr(exten, ','))
//			*strchr(exten, ',') = '\0';
//		if (!read_extension(&e_ext, exten))
		if (!read_extension(&e_ext, e_dialinginfo.number))
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) extension %s not configured\n", ea_endpoint->ep_serial, e_dialinginfo.number);
			release(RELEASE_ALL, LOCATION_PRIVATE_LOCAL, CAUSE_OUTOFORDER, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL); /* RELEASE_TYPE, call, port */
			return; /* must exit here */
		}

		if (e_dialinginfo.itype == INFO_ITYPE_VBOX)
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) dialing directly to VBOX\n", ea_endpoint->ep_serial);
			p = "vbox";
			goto vbox_only;
		}

		/* string from unconditional call forward (cfu) */
		p = e_ext.cfu;
		if (*p)
		{
			/* present to forwarded party */
			if (e_ext.anon_ignore && e_callerinfo.id[0])
			{
				e_callerinfo.present = INFO_PRESENT_ALLOWED;
			}
			if (!!strcmp(p, "vbox") || (e_capainfo.bearer_capa==INFO_BC_AUDIO) || (e_capainfo.bearer_capa==INFO_BC_SPEECH))
				goto cfu_only;
		}

		/* string from busy call forward (cfb) */
		p = e_ext.cfb;
		if (*p)
		{
			class EndpointAppPBX *checkapp = apppbx_first;
			while(checkapp)
			{
				if (checkapp != this) /* any other endpoint except our own */
				{
					if (!strcmp(checkapp->e_terminal, e_terminal))
					{
						/* present to forwarded party */
						if (e_ext.anon_ignore && e_callerinfo.id[0])
						{
							e_callerinfo.present = INFO_PRESENT_ALLOWED;
						}
						if (!!strcmp(p, "vbox") || (e_capainfo.bearer_capa==INFO_BC_AUDIO) || (e_capainfo.bearer_capa==INFO_BC_SPEECH))
							goto cfb_only;
					}
				}
				checkapp = checkapp->next;
			}
		}

		/* string from no-response call forward (cfnr) */
		p = e_ext.cfnr;
		if (*p)
		{
			/* when cfnr is done, out_setup() will setup the call */
			if (e_cfnr_call)
			{
				/* present to forwarded party */
				if (e_ext.anon_ignore && e_callerinfo.id[0])
				{
					e_callerinfo.present = INFO_PRESENT_ALLOWED;
				}
				goto cfnr_only;
			}
			if (!!strcmp(p, "vbox") || (e_capainfo.bearer_capa==INFO_BC_AUDIO) || (e_capainfo.bearer_capa==INFO_BC_SPEECH))
			{
				e_cfnr_release = now + e_ext.cfnr_delay;
				e_cfnr_call = now + e_ext.cfnr_delay + 1; /* call one second after release */
				PDEBUG(DEBUG_EPOINT, "EPOINT(%d) setting time for call-forward-busy to %s with delay %ld.\n", ea_endpoint->ep_serial, e_ext.cfnr, e_ext.cfnr_delay);
			}
		}

		/* call to all internal interfaces */
		p = e_ext.interfaces;
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) generating multiple calls for extension %s to interfaces %s\n", ea_endpoint->ep_serial, e_dialinginfo.number, p);
		while(*p)
		{
			ifname[0] = '\0';
			while(*p!=',' && *p!='\0')
				if (*p > ' ')
					SCCAT(ifname, *p++);
			if (*p == ',')
				p++;
			/* found interface */
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) calling to interface %s\n", ea_endpoint->ep_serial, ifname);
			/* hunt for mISDNport and create Port */
			mISDNport = hunt_port(ifname, &channel);
			if (!mISDNport)
			{
				printlog("%3d  endpoint INTERFACE '%s' not found or busy\n", ea_endpoint->ep_serial, ifname);
				PDEBUG(DEBUG_EPOINT, "EPOINT(%d) given interface: '%s' not found or too busy to accept calls.\n", ea_endpoint->ep_serial, e_ext.interfaces);
				continue;
			}
			/* creating INTERNAL port */
			SPRINT(portname, "%s-%d-out", mISDNport->interface_name, mISDNport->portnum);
			port = new Pdss1((mISDNport->ntmode)?PORT_TYPE_DSS1_NT_OUT:PORT_TYPE_DSS1_TE_OUT, mISDNport, portname, &port_settings, channel, mISDNport->ifport.channel_force);
			if (!port)
			{
				PDEBUG(DEBUG_EPOINT, "EPOINT(%d) port '%s' failed to create\n", ea_endpoint->ep_serial, mISDNport->interface_name);
				goto check_anycall_intern;
			}
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) got port %s\n", ea_endpoint->ep_serial, port->p_name);
			memset(&dialinginfo, 0, sizeof(dialinginfo));
			SCPY(dialinginfo.number, e_dialinginfo.number);
			dialinginfo.itype = INFO_ITYPE_ISDN_EXTENSION;
			dialinginfo.ntype = e_dialinginfo.ntype;
			/* create port_list relation */
			portlist = ea_endpoint->portlist_new(port->p_serial, port->p_type, mISDNport->is_earlyb);
			if (!portlist)
			{
				PERROR("EPOINT(%d) cannot allocate port_list relation\n", ea_endpoint->ep_serial);
				delete port;
				goto check_anycall_intern;
			}
			/* directory.list */
			if (e_callerinfo.id[0] && (e_ext.centrex || e_ext.display_name))
			{
				dirname = parse_directory(e_callerinfo.id, e_callerinfo.ntype);
				if (dirname)
					SCPY(e_callerinfo.name, dirname);
			}
//			dss1 = (class Pdss1 *)port;
			/* message */
//printf("INTERNAL caller=%s,id=%s,dial=%s\n", param.setup.networkid, param.setup.callerinfo.id, param.setup.dialinginfo.number);
			message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_SETUP);
			memcpy(&message->param.setup.dialinginfo, &dialinginfo, sizeof(struct dialing_info));
			memcpy(&message->param.setup.redirinfo, &e_redirinfo, sizeof(struct redir_info));
			memcpy(&message->param.setup.callerinfo, &e_callerinfo, sizeof(struct caller_info));
			memcpy(&message->param.setup.capainfo, &e_capainfo, sizeof(struct capa_info));
//terminal			SCPY(message->param.setup.from_terminal, e_terminal);
//terminal			if (e_dialinginfo.number)
//terminal				SCPY(message->param.setup.to_terminal, e_dialinginfo.number);
			/* handle restricted caller ids */
			apply_callerid_restriction(e_ext.anon_ignore, port->p_type, message->param.setup.callerinfo.id, &message->param.setup.callerinfo.ntype, &message->param.setup.callerinfo.present, &message->param.setup.callerinfo.screen, message->param.setup.callerinfo.voip, message->param.setup.callerinfo.intern, message->param.setup.callerinfo.name);
			apply_callerid_restriction(e_ext.anon_ignore, port->p_type, message->param.setup.redirinfo.id, &message->param.setup.redirinfo.ntype, &message->param.setup.redirinfo.present, NULL, message->param.setup.redirinfo.voip, message->param.setup.redirinfo.intern, 0);
			/* display callerid if desired for extension */
			SCPY(message->param.setup.callerinfo.display, apply_callerid_display(message->param.setup.callerinfo.id, message->param.setup.callerinfo.itype, message->param.setup.callerinfo.ntype, message->param.setup.callerinfo.present, message->param.setup.callerinfo.screen, message->param.setup.callerinfo.voip, message->param.setup.callerinfo.intern, message->param.setup.callerinfo.name));
//printf("\n\ndisplay = %s\n\n\n",message->param.setup.callerinfo.display);
			/* use cnip, if enabld */
			if (!e_ext.centrex)
				message->param.setup.callerinfo.name[0] = '\0';
			/* screen clip if prefix is required */
			if (message->param.setup.callerinfo.id[0] && e_ext.clip_prefix[0])
			{
				SCPY(message->param.setup.callerinfo.id, e_ext.clip_prefix);
				SCAT(message->param.setup.callerinfo.id, numberrize_callerinfo(e_callerinfo.id,e_callerinfo.ntype));
				message->param.setup.callerinfo.ntype = INFO_NTYPE_UNKNOWN;
			}
			/* use internal caller id */
			if (e_callerinfo.intern[0] && (message->param.setup.callerinfo.present!=INFO_PRESENT_RESTRICTED || e_ext.anon_ignore))
			{
				SCPY(message->param.setup.callerinfo.id, e_callerinfo.intern);
				message->param.setup.callerinfo.ntype = INFO_NTYPE_UNKNOWN;
			}
			message_put(message);
			logmessage(message);
			anycall = 1;
		}

		/* string from parallel call forward (cfp) */
		p = e_ext.cfp;
		if (*p)
		{
			if (e_ext.anon_ignore && e_callerinfo.id[0])
			{
				e_callerinfo.present = INFO_PRESENT_ALLOWED;
			}
		}

		vbox_only: /* entry point for answering machine only */
		cfu_only: /* entry point for cfu */
		cfb_only: /* entry point for cfb */
		cfnr_only: /* entry point for cfnr */
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) call extension %s for external destiantion(s) '%s'\n", ea_endpoint->ep_serial, e_dialinginfo.number, p);
//		i=0;
		while(*p)
		{
			/* only if vbox should be dialed, and terminal is given */
			earlyb = 0;
			if (!strcmp(p, "vbox") && e_terminal[0])
			{
				/* go to the end of p */
				p += strlen(p);

				/* answering vbox call */
				PDEBUG(DEBUG_EPOINT, "EPOINT(%d) answering machine\n", ea_endpoint->ep_serial);
				/* alloc port */
				if (!(port = new VBoxPort(PORT_TYPE_VBOX_OUT, &port_settings)))
				{
					PERROR("EPOINT(%d) no mem for port\n", ea_endpoint->ep_serial);
					break;
				}
				PDEBUG(DEBUG_EPOINT, "EPOINT(%d) allocated port %s\n", ea_endpoint->ep_serial, port->p_name);
				UCPY(cfp, e_terminal); /* cfp or any other direct forward/vbox */
			} else
			{
				cfp[0] = '\0';
				while(*p!=',' && *p!='\0')
					SCCAT(cfp, *p++);
				if (*p == ',')
					p++;
		hier auch wie oben
				/* external call */
				PDEBUG(DEBUG_EPOINT, "EPOINT(%d) cfp external %s\n", ea_endpoint->ep_serial, cfp);
				/* hunt for mISDNport and create Port */
				mISDNport = mISDNport_first;
				port = NULL;
				while(mISDNport)
				{
					/* check for external or given interface */
					if (((!e_dialinginfo.interfaces[0])&&mISDNport->iftype==IF_EXTERN) || !strcmp(mISDNport->interface_name, e_dialinginfo.interfaces))
					{
						PDEBUG(DEBUG_EPOINT, "EPOINT(%d) interface '%s' found\n", ea_endpoint->ep_serial, mISDNport->interface_name);
						cause = CAUSE_NOCHANNEL; /* when failing: out of channels */
						/* if PTP, skip all down links */
						if (mISDNport->ptp && !mISDNport->l2link)
						{
							printlog("%3d  endpoint INTERFACE Layer 2 of interface '%s' is down.\n", ea_endpoint->ep_serial, mISDNport->interface_name);
							PDEBUG(DEBUG_EPOINT, "EPOINT(%d) skipping because it is PTP with L2 down\n", ea_endpoint->ep_serial);
							mISDNport = mISDNport->next;
							continue;
						}
						/* if no channel is available */
						if (mISDNport->multilink || !mISDNport->ntmode || mISDNport->ptp)
						{
							use = 0;
							i = 0;
							ii = mISDNport->b_num;
							while(i < ii)
							{
								if (mISDNport->b_state[i])
									use++;
								i++;
							}
							if (use >= mISDNport->b_num)
							{
								printlog("%3d  endpoint INTERFACE Interface '%s' has no free channel.\n", ea_endpoint->ep_serial, mISDNport->interface_name);
								PDEBUG(DEBUG_EPOINT, "EPOINT(%d) skipping because it is not single link port_list NT-modem OR no channel available.\n", ea_endpoint->ep_serial);
								mISDNport = mISDNport->next;
								continue;
							}
						}
						/* found interface */
						printlog("%3d  endpoint INTERFACE Interface '%s' found for external call.\n", ea_endpoint->ep_serial, mISDNport->interface_name);
						break;
					}
					mISDNport = mISDNport->next;
				}
				if (mISDNport)
				{
					/* creating EXTERNAL port*/
					SPRINT(portname, "%s-%d-out", mISDNport->interface_name, mISDNport->portnum);
					port = new Pdss1((mISDNport->ntmode)?PORT_TYPE_DSS1_NT_OUT:PORT_TYPE_DSS1_TE_OUT, mISDNport, portname, &port_settings);
					earlyb = mISDNport->is_earlyb;
				} else
				{
					port = NULL;
					printlog("%3d  endpoint INTERFACE External interface not found or busy.\n", ea_endpoint->ep_serial);
					PDEBUG(DEBUG_EPOINT, "EPOINT(%d) given interface: '%s' too busy to accept calls.\n", ea_endpoint->ep_serial, e_dialinginfo.interfaces[0]?e_dialinginfo.interfaces:"any interface");
				}
			}
			if (!port)
			{
				PDEBUG(DEBUG_EPOINT, "EPOINT(%d) no port found or created, which is idle.\n", ea_endpoint->ep_serial);
				goto check_anycall_intern;
			}
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) found or created port %s\n", ea_endpoint->ep_serial, port->p_name);
			memset(&dialinginfo, 0, sizeof(dialinginfo));
			SCPY(dialinginfo.number, cfp);
			dialinginfo.itype = INFO_ITYPE_EXTERN;
			dialinginfo.ntype = e_dialinginfo.ntype;
			portlist = ea_endpoint->portlist_new(port->p_serial, port->p_type, earlyb);
			if (!portlist)
			{
				PERROR("EPOINT(%d) cannot allocate port_list relation\n", ea_endpoint->ep_serial);
				delete port;
				goto check_anycall_intern;
			}
//printf("EXTERNAL caller=%s,id=%s,dial=%s\n", param.setup.networkid, param.setup.callerinfo.id, param.setup.dialinginfo.number);
			message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_SETUP);
			memcpy(&message->param.setup.dialinginfo, &dialinginfo, sizeof(struct dialing_info));
			memcpy(&message->param.setup.redirinfo, &e_redirinfo, sizeof(struct redir_info));
			memcpy(&message->param.setup.callerinfo, &e_callerinfo, sizeof(struct caller_info));
			/* if clip is hidden */
			if (e_ext.clip==CLIP_HIDE && port->p_type!=PORT_TYPE_VBOX_OUT)
			{
				SCPY(message->param.setup.callerinfo.id, e_ext.callerid);
				SCPY(message->param.setup.callerinfo.intern, e_terminal);
				message->param.setup.callerinfo.ntype = e_ext.callerid_type;
				message->param.setup.callerinfo.present = e_ext.callerid_present;
			}
			memcpy(&message->param.setup.capainfo, &e_capainfo, sizeof(struct capa_info));
//terminal			SCPY(message->param.setup.from_terminal, e_terminal);
//terminal			if (e_dialinginfo.number)
//terminal				SCPY(message->param.setup.to_terminal, e_dialinginfo.number);
				/* handle restricted caller ids */
			apply_callerid_restriction(e_ext.anon_ignore, port->p_type, message->param.setup.callerinfo.id, &message->param.setup.callerinfo.ntype, &message->param.setup.callerinfo.present, &message->param.setup.callerinfo.screen, message->param.setup.callerinfo.voip, message->param.setup.callerinfo.intern, message->param.setup.callerinfo.name);
			apply_callerid_restriction(e_ext.anon_ignore, port->p_type, message->param.setup.redirinfo.id, &message->param.setup.redirinfo.ntype, &message->param.setup.redirinfo.present, NULL, message->param.setup.redirinfo.voip, message->param.setup.redirinfo.intern, 0);
			/* display callerid if desired for extension */
			SCPY(message->param.setup.callerinfo.display, apply_callerid_display(message->param.setup.callerinfo.id, message->param.setup.callerinfo.itype, message->param.setup.callerinfo.ntype, message->param.setup.callerinfo.present, message->param.setup.callerinfo.screen, message->param.setup.callerinfo.voip, message->param.setup.callerinfo.intern, message->param.setup.callerinfo.name));
			message_put(message);
			logmessage(message);
			anycall = 1;
		}

		check_anycall_intern:
		/* now we have all ports created */
		if (!anycall)
		{
			printlog("%3d  endpoint INTERFACE No port or no parallel forwarding defined. Nothing to call to.\n", ea_endpoint->ep_serial);
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) no port or no cfp defined for extension, nothing to dial.\n", ea_endpoint->ep_serial);
			if (!ea_endpoint->ep_call_id)
				break;
			release(RELEASE_ALL, LOCATION_PRIVATE_LOCAL, cause, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL); /* RELEASE_TYPE, call, port */
			return; /* must exit here */
		}
		break;

		/* *********************** external call */
		default:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) dialing external: '%s'\n", ea_endpoint->ep_serial, e_dialinginfo.number);
		/* call to extenal interfaces */
		p = e_dialinginfo.number;
		do
		{
			number[0] = '\0';
			while(*p!=',' && *p!='\0')
				SCCAT(number, *p++);
			if (*p == ',')
				p++;
			/* found number */
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) calling to number '%s' interface '%s'\n", ea_endpoint->ep_serial, number, e_dialinginfo.interfaces[0]?e_dialinginfo.interfaces:"any interface");
			/* hunt for mISDNport and create Port */
			mISDNport = mISDNport_first;
			port = NULL;
			while(mISDNport)
			{
				/* check for external or given interface */
				if ((!e_dialinginfo.interfaces[0]&&mISDNport->iftype==IF_EXTERN) || !strcmp(mISDNport->interface_name, e_dialinginfo.interfaces))
				{
					PDEBUG(DEBUG_EPOINT, "EPOINT(%d) interface '%s' found\n", ea_endpoint->ep_serial, mISDNport->interface_name);
					cause = CAUSE_NOCHANNEL; /* when failing: out of channels */
					/* if PTP, skip all down links */
					if (mISDNport->ptp && !mISDNport->l2link)
					{
						printlog("%3d  endpoint INTERFACE Layer 2 of interface '%s' is down.\n", ea_endpoint->ep_serial, mISDNport->interface_name);
						PDEBUG(DEBUG_EPOINT, "EPOINT(%d) skipping because it is PTP with L2 down\n", ea_endpoint->ep_serial);
						mISDNport = mISDNport->next;
						continue;
					}
					/* if no channel is available */
					if (mISDNport->multilink || !mISDNport->ntmode || mISDNport->ptp)
					{
						use = 0;
						i = 0;
						ii = mISDNport->b_num;
						while(i < ii)
						{
							if (mISDNport->b_state[i])
								use++;
							i++;
						}
						if (use >= mISDNport->b_num)
						{
							printlog("%3d  endpoint INTERFACE Interface '%s' has no free channel.\n", ea_endpoint->ep_serial, mISDNport->interface_name);
							PDEBUG(DEBUG_EPOINT, "EPOINT(%d) skipping because it is not single link port_list NT-modem OR no channel available.\n", ea_endpoint->ep_serial);
							mISDNport = mISDNport->next;
							continue;
						}
					}
					/* found interface */
					printlog("%3d  endpoint INTERFACE Interface '%s' found for external call.\n", ea_endpoint->ep_serial, mISDNport->interface_name);
					break;
				}
				mISDNport = mISDNport->next;
			}
			if (!mISDNport)
			{
				printlog("%3d  endpoint INTERFACE External interface not found or busy.\n", ea_endpoint->ep_serial);
				PDEBUG(DEBUG_EPOINT, "EPOINT(%d) given interface: '%s' too busy to accept calls.\n", ea_endpoint->ep_serial, e_dialinginfo.interfaces[0]?e_dialinginfo.interfaces:"any interface");
				goto check_anycall_extern;
			}
			/* creating EXTERNAL port*/
			SPRINT(portname, "%s-%d-out", mISDNport->interface_name, mISDNport->portnum);
			port = new Pdss1((mISDNport->ntmode)?PORT_TYPE_DSS1_NT_OUT:PORT_TYPE_DSS1_TE_OUT, mISDNport, portname, &port_settings);
			if (!port)	
			{
				PDEBUG(DEBUG_EPOINT, "EPOINT(%d) no external port available\n", ea_endpoint->ep_serial);
				goto check_anycall_extern;
			}
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) created port %s\n", ea_endpoint->ep_serial, port->p_name);
			memset(&dialinginfo, 0, sizeof(dialinginfo));
			SCPY(dialinginfo.number, number);
			dialinginfo.itype = INFO_ITYPE_EXTERN;
			dialinginfo.ntype = e_dialinginfo.ntype;
			portlist = ea_endpoint->portlist_new(port->p_serial, port->p_type, mISDNport->is_earlyb);
			if (!portlist)
			{
				PERROR("EPOINT(%d) cannot allocate port_list relation\n", ea_endpoint->ep_serial);
				delete port;
				goto check_anycall_extern;
			}
//			dss1 = (class Pdss1 *)port;
//printf("EXTERNAL caller=%s,id=%s,dial=%s\n", param.setup.networkid, param.setup.callerinfo.id, param.setup.dialinginfo.number);
			message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_SETUP);
			memcpy(&message->param.setup.dialinginfo, &dialinginfo, sizeof(struct dialing_info));
			SCPY(message->param.setup.dialinginfo.number, number);
			memcpy(&message->param.setup.redirinfo, &e_redirinfo, sizeof(struct redir_info));
			memcpy(&message->param.setup.callerinfo, &e_callerinfo, sizeof(struct caller_info));
			memcpy(&message->param.setup.capainfo, &e_capainfo, sizeof(struct capa_info));
//terminal			SCPY(message->param.setup.from_terminal, e_terminal);
//terminal			if (e_dialinginfo.number)
//terminal				SCPY(message->param.setup.to_terminal, e_dialinginfo.number);
				/* handle restricted caller ids */
			apply_callerid_restriction(e_ext.anon_ignore, port->p_type, message->param.setup.callerinfo.id, &message->param.setup.callerinfo.ntype, &message->param.setup.callerinfo.present, &message->param.setup.callerinfo.screen, message->param.setup.callerinfo.voip, message->param.setup.callerinfo.intern, message->param.setup.callerinfo.name);
			apply_callerid_restriction(e_ext.anon_ignore, port->p_type, message->param.setup.redirinfo.id, &message->param.setup.redirinfo.ntype, &message->param.setup.redirinfo.present, NULL, message->param.setup.redirinfo.voip, message->param.setup.redirinfo.intern, 0);
			/* display callerid if desired for extension */
			SCPY(message->param.setup.callerinfo.display, apply_callerid_display(message->param.setup.callerinfo.id, message->param.setup.callerinfo.itype, message->param.setup.callerinfo.ntype, message->param.setup.callerinfo.present, message->param.setup.callerinfo.screen, message->param.setup.callerinfo.voip, message->param.setup.callerinfo.intern, message->param.setup.callerinfo.name));
			message_put(message);
			logmessage(message);
			anycall = 1;
		} while(*p);

		check_anycall_extern:
		/* now we have all ports created */
		if (!anycall)
		{
			printlog("%3d  endpoint INTERFACE No free port found for making any call.\n", ea_endpoint->ep_serial);
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) no port found which is idle for at least one number\n", ea_endpoint->ep_serial);
			if (!ea_endpoint->ep_call_id)
				break;
			release(RELEASE_ALL, LOCATION_PRIVATE_LOCAL, CAUSE_NOCHANNEL, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL); /* RELEASE_TYPE, call, port */
			return; /* must exit here */
		}
		break;
	}

}


/* handler for endpoint
 */

extern int quit;
int EndpointAppPBX::handler(void)
{
	if (e_crypt_state!=CM_ST_NULL)
	{
		cryptman_handler();
	}

	/* process answering machine (play) handling */
	if (e_action)
	{
		if (e_action->index == ACTION_VBOX_PLAY)
			vbox_handler();

		/* process action timeout */
		if (e_action_timeout)
		if (now_d >= e_action_timeout)
		{
			if (e_state!=EPOINT_STATE_CONNECT)
			{
				e_redial = 0;
				PDEBUG(DEBUG_EPOINT, "EPOINT(%d) current action timed out.\n", ea_endpoint->ep_serial);
				e_multipoint_cause = CAUSE_NOUSER;
				e_multipoint_location = LOCATION_PRIVATE_LOCAL;
				new_state(EPOINT_STATE_IN_OVERLAP);
				e_call_pattern = 0;
				process_dialing();
				return(1); /* we must exit, because our endpoint might be gone */
			} else
				e_action_timeout = 0;
		}
	} else {
		/* process action timeout */
		if (e_match_timeout)
		if (now_d >= e_match_timeout)
		{
			e_redial = 0;
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) we got a match timeout.\n", ea_endpoint->ep_serial);
			process_dialing();
			return(1); /* we must exit, because our endpoint might be gone */
		}
	}


	/* process redialing (epoint redials to port) */
	if (e_redial)
	{
		if (now_d >= e_redial)
		{
			e_redial = 0;
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) starting redial.\n", ea_endpoint->ep_serial);

			new_state(EPOINT_STATE_OUT_SETUP);
			/* call special setup routine */
			out_setup();

			return(1);
		}
	}

	/* process powerdialing (epoint redials to epoint) */
	if (e_powerdialing > 0)
	{
		if (now_d >= e_powerdialing)
		{
			e_powerdialing = -1; /* leave power dialing on */
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) starting redial of powerdial.\n", ea_endpoint->ep_serial);

			/* redial */
			e_ruleset = ruleset_main;
			if (e_ruleset)
	        		e_rule = e_ruleset->rule_first;
			e_action = NULL;
			new_state(EPOINT_STATE_IN_OVERLAP);
			process_dialing();
			return(1);
		}
	}

	/* process call forward no response */
	if (e_cfnr_release)
	{
		struct port_list *portlist;
		struct message *message;

		if (now >= e_cfnr_release)
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) call-forward-no-response time has expired, hanging up.\n", ea_endpoint->ep_serial);
			e_cfnr_release = 0;

			/* release all ports */
			while((portlist = ea_endpoint->ep_portlist))
			{
				message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_RELEASE);
				message->param.disconnectinfo.cause = CAUSE_NORMAL; /* normal clearing */
				message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
				message_put(message);
				logmessage(message);
				ea_endpoint->free_portlist(portlist);
			}
			/* put on hold */
			message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_CHANNEL);
			message->param.channel = CHANNEL_STATE_HOLD;
			message_put(message);
			/* indicate no patterns */
			message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_NOPATTERN);
			message_put(message);
			/* set setup state, since we have no response from the new call */
			new_state(EPOINT_STATE_OUT_SETUP);
		}
	} else
	if (e_cfnr_call)
	{
		if (now >= e_cfnr_call)
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) call-forward-busy time has expired, calling the forwarded number: %s.\n", ea_endpoint->ep_serial, e_ext.cfnr);
			out_setup();
			e_cfnr_call = 0;
		}
	}

	/* handle connection to user */
	if (e_state == EPOINT_STATE_IDLE)
	{
		/* epoint is idle, check callback */
		if (e_callback)
		if (now_d >= e_callback)
		{
			e_callback = 0; /* done with callback */
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) starting callback.\n", ea_endpoint->ep_serial);
			new_state(EPOINT_STATE_OUT_SETUP);
			out_setup();
			return(1);
		}
	}

	/* check for password timeout */
	if (e_action)
	if (e_action->index==ACTION_PASSWORD || e_action->index==ACTION_PASSWORD_WRITE)
	{
		struct port_list *portlist;

		if (now >= e_password_timeout)
		{
			e_ruleset = ruleset_main;
			if (e_ruleset)
	        		e_rule = e_ruleset->rule_first;
			e_action = NULL;
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) password timeout %s\n", ea_endpoint->ep_serial, e_extdialing);
			printlog("%3d  endpoint PASSWORD timeout\n", ea_endpoint->ep_serial);
			e_connectedmode = 0;
			e_dtmf = 0;
			new_state(EPOINT_STATE_OUT_DISCONNECT);
			portlist = ea_endpoint->ep_portlist;
			if (portlist)
			{
				message_disconnect_port(portlist, CAUSE_NORMAL, LOCATION_PRIVATE_LOCAL, "");
				set_tone(portlist, "cause_10");
			}
			return(1);
		}
	}
 
	return(0);
}


/* doing a hookflash */
void EndpointAppPBX::hookflash(void)
{
	class Port *port;

	/* be sure that we are active */
	notify_active();
	e_tx_state = NOTIFY_STATE_ACTIVE;

	printlog("%3d  endpoint HOOKFLASH\n", ea_endpoint->ep_serial);
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) dtmf hookflash detected.\n", ea_endpoint->ep_serial);
	if (ea_endpoint->ep_use > 1)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) cannot hooflash while child process is running.\n", ea_endpoint->ep_serial);
		return;
	}
	/* dialtone after pressing the hash key */
	process_hangup(e_call_cause, e_call_location);
	e_multipoint_cause = CAUSE_NOUSER;
	e_multipoint_location = LOCATION_PRIVATE_LOCAL;
	port = find_port_id(ea_endpoint->ep_portlist->port_id);
	if (port)
	{
		port->set_echotest(0);
	}
	if (ea_endpoint->ep_call_id)
	{
		release(RELEASE_CALL, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL); /* RELEASE_TYPE, call, port */
	}
	e_ruleset = ruleset_main;
	if (e_ruleset)
	       	e_rule = e_ruleset->rule_first;
	e_action = NULL;
	new_state(EPOINT_STATE_IN_OVERLAP);
	e_connectedmode = 1;
	SCPY(e_dialinginfo.number, e_ext.prefix);
        e_extdialing = e_dialinginfo.number;
	e_call_pattern = 0;
	if (e_dialinginfo.number[0])
	{
		set_tone(ea_endpoint->ep_portlist, "dialing");
		process_dialing();
	} else
	{
		set_tone(ea_endpoint->ep_portlist, "dialpbx");
	}
	e_dtmf_time = now;
	e_dtmf_last = '\0';
}


/* messages from port
 */
/* port MESSAGE_SETUP */
void EndpointAppPBX::port_setup(struct port_list *portlist, int message_type, union parameter *param)
{
	struct message		*message;
	char			buffer[256];
	char			extension[32];
	char			extension1[32];
	char			*p;
	int			writeext;		/* flags need to write extension after modification */
	class Port		*port;

	portlist->port_type = param->setup.port_type;
	memcpy(&e_callerinfo, &param->setup.callerinfo, sizeof(e_callerinfo));
	memcpy(&e_dialinginfo, &param->setup.dialinginfo, sizeof(e_dialinginfo));
	memcpy(&e_redirinfo, &param->setup.redirinfo, sizeof(e_redirinfo));
	memcpy(&e_capainfo, &param->setup.capainfo, sizeof(e_capainfo));
	e_dtmf = param->setup.dtmf;

	/* screen by interface */
	if (e_callerinfo.interface[0])
	{
		/* screen incoming caller id */
		screen(0, e_callerinfo.id, sizeof(e_callerinfo.id), &e_callerinfo.ntype, &e_callerinfo.present);
	}
colp, outclip, outcolp

	/* process extension */
	if (e_callerinfo.itype == INFO_ITYPE_INTERN)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) incoming call is extension\n", ea_endpoint->ep_serial);
		/* port makes call from extension */
		SCPY(e_callerinfo.intern, e_callerinfo.id);
		SCPY(e_terminal, e_callerinfo.intern);
		SCPY(e_terminal_interface, e_callerinfo.interface);
	} else
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) incoming call is external or voip\n", ea_endpoint->ep_serial);
	}
	printlog("%3d  incoming %s='%s'%s%s%s%s dialing='%s'\n",
		ea_endpoint->ep_serial,
		(e_callerinfo.intern[0])?"SETUP from extension":"SETUP from extern",
		(e_callerinfo.intern[0])?e_callerinfo.intern:e_callerinfo.id,
		(e_callerinfo.present==INFO_PRESENT_RESTRICTED)?" anonymous":"",
		(e_redirinfo.id[0])?"redirected='":"",
		e_redirinfo.id,
		(e_redirinfo.id[0])?"'":"",
		e_dialinginfo.number
		);

	if (e_callerinfo.itype == INFO_ITYPE_INTERN)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) call from extension '%s'\n", ea_endpoint->ep_serial, e_terminal);

		/* get extension's info about caller */
		if (!read_extension(&e_ext, e_terminal))
		{
			/* extension doesn't exist */
			printlog("%3d  endpoint EXTENSION '%s' doesn't exist, please check or create.\n", ea_endpoint->ep_serial, e_callerinfo.id);
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) rejecting call from not existing extension: '%s'\n", ea_endpoint->ep_serial, e_terminal);
			message_disconnect_port(portlist, CAUSE_REJECTED, LOCATION_PRIVATE_LOCAL, "");
			new_state(EPOINT_STATE_OUT_DISCONNECT);
			set_tone(portlist, "cause_80"); /* pbx cause: extension not authorized */
			e_terminal[0] = '\0'; /* no terminal */
			return;
		}
		writeext = 0;

		/* put prefix (next) in front of e_dialinginfo.number */
		if (e_ext.next[0])
		{
			SPRINT(buffer, "%s%s", e_ext.next, e_dialinginfo.number);
			SCPY(e_dialinginfo.number, buffer);
			e_ext.next[0] = '\0';
			writeext = 1;
		} else if (e_ext.prefix[0])
		{
			SPRINT(buffer, "%s%s", e_ext.prefix, e_dialinginfo.number);
			SCPY(e_dialinginfo.number, buffer);
		}

		/* screen caller id */
		e_callerinfo.screen = INFO_SCREEN_NETWORK;
		if (e_ext.name[0])
			SCPY(e_callerinfo.name, e_ext.name);
		/* use caller id (or if exist: id_next_call) for this call */
		if (e_ext.id_next_call_present >= 0)
		{
			SCPY(e_callerinfo.id, e_ext.id_next_call);
			/* if we restrict the pesentation */
			if (e_ext.id_next_call_present==INFO_PRESENT_ALLOWED && e_callerinfo.present==INFO_PRESENT_RESTRICTED)
				e_callerinfo.present = INFO_PRESENT_RESTRICTED;
			else	e_callerinfo.present = e_ext.id_next_call_present;
			e_callerinfo.ntype = e_ext.id_next_call_type;
			e_ext.id_next_call_present = -1;
			writeext = 1;
		} else
		{
			SCPY(e_callerinfo.id, e_ext.callerid);
			/* if we restrict the pesentation */
			if (e_ext.callerid_present==INFO_PRESENT_ALLOWED && e_callerinfo.present==INFO_PRESENT_RESTRICTED)
				e_callerinfo.present = INFO_PRESENT_RESTRICTED;
			else	e_callerinfo.present = e_ext.callerid_present;
			e_callerinfo.ntype = e_ext.callerid_type;
		}

		/* extension is written */
		if (writeext)
			write_extension(&e_ext, e_terminal);

		/* set volume of rx and tx */
		if (param->setup.callerinfo.itype == INFO_ITYPE_INTERN)
		if (e_ext.txvol!=256 || e_ext.rxvol!=256)
		{
			message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_mISDNSIGNAL);
			message->param.mISDNsignal.message = mISDNSIGNAL_VOLUME;
			message->param.mISDNsignal.rxvol = e_ext.txvol;
			message->param.mISDNsignal.txvol = e_ext.rxvol;
			message_put(message);
		}

		/* start recording if enabled */
		if (e_ext.record!=CODEC_OFF && (e_capainfo.bearer_capa==INFO_BC_SPEECH || e_capainfo.bearer_capa==INFO_BC_AUDIO))
		{
			/* check if we are a terminal */
			if (e_terminal[0] == '\0')
				PERROR("Port(%d) cannot record because we are not a terminal\n", ea_endpoint->ep_serial);
			else
			{
				port = find_port_id(portlist->port_id);
				if (port)
					port->open_record(e_ext.record, 0, 0, e_terminal, e_ext.anon_ignore, "", 0);
			}
		}
	} else
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) call from external port\n", ea_endpoint->ep_serial);
		/* no terminal identification */
		e_terminal[0] = '\0';
		e_terminal_interface[0] = '\0';
		memset(&e_ext, 0, sizeof(e_ext));
		e_ext.rights = 4; /* right to dial internat */
	}

	/* incoming call */
	e_ruleset = ruleset_main;
	if (e_ruleset)
       		e_rule = e_ruleset->rule_first;
	e_action = NULL;
        e_extdialing = e_dialinginfo.number;
	new_state(EPOINT_STATE_IN_SETUP);
	if (e_dialinginfo.number[0])
	{
		set_tone(portlist, "dialing");
	} else
	{
		if (e_terminal[0])
			set_tone(portlist, "dialpbx");
		else
			set_tone(portlist, "dialtone");
	}
	process_dialing();
	if (e_state == EPOINT_STATE_IN_SETUP)
	{
		/* request MORE info, if not already at higher state */
		new_state(EPOINT_STATE_IN_OVERLAP);
		message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_OVERLAP);
		message_put(message);
		logmessage(message);
	}
}

/* port MESSAGE_INFORMATION */
void EndpointAppPBX::port_information(struct port_list *portlist, int message_type, union parameter *param)
{
	printlog("%3d  incoming INFORMATION information='%s'\n",
		ea_endpoint->ep_serial,
		param->information.number
		);
	e_overlap = 1;

	/* turn off dtmf detection, in case dtmf is sent with keypad information */
	if (e_dtmf)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) received dialing information, so dtmf is now disabled, to prevent double detection by keypad+dtmf.\n", ea_endpoint->ep_serial, param->information.number, e_terminal, e_callerinfo.id);
		e_dtmf = 0;
	}

	/* if vbox_play is done, the information are just used as they come */
	if (e_action)
	if (e_action->index == ACTION_VBOX_PLAY)
	{
		/* concat dialing string */
		SCAT(e_dialinginfo.number, param->information.number);
		process_dialing();
		return;
	}

	/* keypad when disconnect but in connected mode */
	if (e_state==EPOINT_STATE_OUT_DISCONNECT && e_connectedmode)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) keypad information received after disconnect: %s.\n", ea_endpoint->ep_serial, param->information.number);
		/* processing keypad function */
		if (param->information.number[0] == '0')
		{
			hookflash();
		}
		return;
	}

	/* keypad when connected */
	if (e_state == EPOINT_STATE_CONNECT && e_ext.keypad)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) keypad information received during connect: %s.\n", ea_endpoint->ep_serial, param->information.number);
		/* processing keypad function */
		if (param->information.number[0] == '0')
		{
			hookflash();
		}
		if (param->information.number[0])
			keypad_function(param->information.number[0]);
		return;
	}
	if (e_state != EPOINT_STATE_IN_OVERLAP)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) ignored because we are not in overlap, or connect state.\n", ea_endpoint->ep_serial);
		return;
	}
	if (!param->information.number[0])
		return;
	if (e_dialinginfo.number[0]=='\0' && !e_action)
	{
		set_tone(portlist, "dialing");
	}
	if (e_action)
	if (e_action->index==ACTION_OUTDIAL
	 || e_action->index==ACTION_EXTERNAL)
	{
		if (!e_extdialing)
			set_tone(portlist, "dialing");
		else if (!e_extdialing[0])
			set_tone(portlist, "dialing");
	}
	/* concat dialing string */
	SCAT(e_dialinginfo.number, param->information.number);
	process_dialing();
}

/* port MESSAGE_DTMF */
void EndpointAppPBX::port_dtmf(struct port_list *portlist, int message_type, union parameter *param)
{
	printlog("%3d  incoming DTMF digit='%c'\n",
		ea_endpoint->ep_serial,
		param->dtmf
		);
	/* only if dtmf detection is enabled */
	if (!e_dtmf)
	{
		PDEBUG(DEBUG_EPOINT, "dtmf detection is disabled\n");
		return;
	}

#if 0
NOTE: vbox is now handled due to overlap state
	/* if vbox_play is done, the dtmf digits are just used as they come */
	if (e_action)
	if (e_action->index == ACTION_VBOX_PLAY)
	{
		/* concat dialing string */
		if (strlen(e_dialinginfo.number)+1 < sizeof(e_dialinginfo.number))
		{
			e_dialinginfo.number[strlen(e_dialinginfo.number)+1] = '\0';
			e_dialinginfo.number[strlen(e_dialinginfo.number)] = param->dtmf;
			process_dialing();
		}
		/* continue to process *X# sequences */
	}
#endif

	/* check for *X# sequence */
	if (e_state == EPOINT_STATE_CONNECT)
	{
		if (e_dtmf_time+3 < now)
		{
			/* the last digit was too far in the past to be a sequence */
			if (param->dtmf == '*')
				/* only start is allowed in the sequence */
				e_dtmf_last = '*';
			else
				e_dtmf_last = '\0';
		} else
		{
			/* we have a sequence of digits, see what we got */
			if (param->dtmf == '*')
				e_dtmf_last = '*';
			else if (param->dtmf>='0' && param->dtmf<='9')
			{
				/* we need to have a star before we receive the digit of the sequence */
				if (e_dtmf_last == '*')
					e_dtmf_last = param->dtmf;
			} else if (param->dtmf == '#')
			{
				/* the hash key */
				if (e_dtmf_last>='0' && e_dtmf_last<='9')
				{
					PDEBUG(DEBUG_EPOINT, "EPOINT(%d) dtmf sequence *%c# detected.\n", ea_endpoint->ep_serial, e_dtmf_last);
					if (e_dtmf_last == '0')
					{
						hookflash();
						return;
					}
					/* processing keypad function */
					if (param->dtmf)
						keypad_function(e_dtmf_last);
					e_dtmf_last = '\0';
				}
			}
		}

		/* set last time of dtmf */
		e_dtmf_time = now;
		return;
	}

	/* check for ## hookflash during dialing */
	if (e_action)
	if (e_action->index==ACTION_PASSWORD
	 || e_action->index==ACTION_PASSWORD_WRITE)
		goto password;
	if (param->dtmf=='#') /* current digit is '#' */
	{
		if (e_state==EPOINT_STATE_IN_DISCONNECT
		 || (e_state!=EPOINT_STATE_CONNECT && e_dtmf_time+3>=now && e_dtmf_last=='#')) /* when disconnected, just #. when dialing, ##. */
		{
			hookflash();
			return;
		} else
		{
			e_dtmf_time = now;
			e_dtmf_last = '#';
		}
	} else
	{
		password:
		e_dtmf_time = now;
		e_dtmf_last = '\0';
	}
	

	/* dialing using dtmf digit */
	if (e_state==EPOINT_STATE_IN_OVERLAP)// && e_state==e_connectedmode)
	{
		if (e_dialinginfo.number[0]=='\0' && !e_action)
		{
			set_tone(portlist, "dialing");
		}
		/* concat dialing string */
		if (strlen(e_dialinginfo.number)+1 < sizeof(e_dialinginfo.number))
		{
			e_dialinginfo.number[strlen(e_dialinginfo.number)+1] = '\0';
			e_dialinginfo.number[strlen(e_dialinginfo.number)] = param->dtmf;
			process_dialing();
		}
	}
}

/* port MESSAGE_CRYPT */
void EndpointAppPBX::port_crypt(struct port_list *portlist, int message_type, union parameter *param)
{
	/* send crypt response to cryptman */
	if (param->crypt.type == CR_MESSAGE_IND)
		cryptman_msg2man(param->crypt.data, param->crypt.len);
	else
		cryptman_message(param->crypt.type, param->crypt.data, param->crypt.len);
}

/* port MESSAGE_OVERLAP */
void EndpointAppPBX::port_overlap(struct port_list *portlist, int message_type, union parameter *param)
{
	struct message *message;

	/* signal to call tool */
	admin_call_response(e_adminid, ADMIN_CALL_SETUP_ACK, "", 0, 0, 0);

	printlog("%3d  incoming SETUP ACKNOWLEDGE\n",
		ea_endpoint->ep_serial
		);
	if (e_dialing_queue[0] && portlist)
	{
		/* send what we have not dialed yet, because we had no setup complete */
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) dialing pending digits: '%s'\n", ea_endpoint->ep_serial, e_dialing_queue);
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_portlist->port_id, EPOINT_TO_PORT, MESSAGE_INFORMATION);
		SCPY(message->param.information.number, e_dialing_queue);
		message->param.information.ntype = INFO_NTYPE_UNKNOWN;
		message_put(message);
		logmessage(message);
		e_dialing_queue[0] = '\0';
	}
	/* check if pattern is available */
	if (!ea_endpoint->ep_portlist->next && portlist->earlyb) /* one port_list relation and tones available */
	{
		/* indicate patterns */
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_PATTERN);
		message_put(message);

		/* connect audio, if not already */
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_CHANNEL);
		message->param.channel = CHANNEL_STATE_CONNECT;
		message_put(message);
	} else
	{
		/* indicate no patterns */
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_NOPATTERN);
		message_put(message);

		/* disconnect audio, if not already */
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_CHANNEL);
		message->param.channel = CHANNEL_STATE_HOLD;
		message_put(message);
	}
	new_state(EPOINT_STATE_OUT_OVERLAP);
	/* if we are in a call */
	if (ea_endpoint->ep_call_id)
	{ 
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, message_type);
		memcpy(&message->param, param, sizeof(union parameter));
		message_put(message);
	}
}

/* port MESSAGE_PROCEEDING */
void EndpointAppPBX::port_proceeding(struct port_list *portlist, int message_type, union parameter *param)
{
	struct message *message;

	/* signal to call tool */
	admin_call_response(e_adminid, ADMIN_CALL_PROCEEDING, "", 0, 0, 0);

	printlog("%3d  incoming PROCEEDING\n",
		ea_endpoint->ep_serial
		);
	e_state = EPOINT_STATE_OUT_PROCEEDING;
	/* check if pattern is availatle */
	if (!ea_endpoint->ep_portlist->next && (portlist->earlyb || portlist->port_type==PORT_TYPE_VBOX_OUT)) /* one port_list relation and tones available */
	{
		/* indicate patterns */
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_PATTERN);
		message_put(message);

		/* connect audio, if not already */
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_CHANNEL);
		message->param.channel = CHANNEL_STATE_CONNECT;
		message_put(message);
	} else
	{
		/* indicate no patterns */
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_NOPATTERN);
		message_put(message);

		/* disconnect audio, if not already */
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_CHANNEL);
		message->param.channel = CHANNEL_STATE_HOLD;
		message_put(message);
	}
	/* if we are in a call */
	if (ea_endpoint->ep_call_id)
	{ 
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, message_type);
		memcpy(&message->param, param, sizeof(union parameter));
		message_put(message);
	}
}

/* port MESSAGE_ALERTING */
void EndpointAppPBX::port_alerting(struct port_list *portlist, int message_type, union parameter *param)
{
	struct message *message;

	/* signal to call tool */
	admin_call_response(e_adminid, ADMIN_CALL_ALERTING, "", 0, 0, 0);

	printlog("%3d  incoming ALERTING\n",
		ea_endpoint->ep_serial
		);
	new_state(EPOINT_STATE_OUT_ALERTING);
	/* check if pattern is available */
	if (!ea_endpoint->ep_portlist->next && (portlist->earlyb || portlist->port_type==PORT_TYPE_VBOX_OUT)) /* one port_list relation and tones available */
	{
		/* indicate patterns */
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_PATTERN);
		message_put(message);

		/* connect audio, if not already */
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_CHANNEL);
		message->param.channel = CHANNEL_STATE_CONNECT;
		message_put(message);
	} else
	{
		/* indicate no patterns */
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_NOPATTERN);
		message_put(message);

		/* disconnect audio, if not already */
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_CHANNEL);
		message->param.channel = CHANNEL_STATE_HOLD;
		message_put(message);
	}
	/* if we are in a call */
	if (ea_endpoint->ep_call_id)
	{ 
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, message_type);
		memcpy(&message->param, param, sizeof(union parameter));
		message_put(message);
	}
}

/* port MESSAGE_CONNECT */
void EndpointAppPBX::port_connect(struct port_list *portlist, int message_type, union parameter *param)
{
	struct message *message;
	char buffer[256];
	unsigned long port_id = portlist->port_id;
	struct port_list *tportlist;
	class Port *port;

	/* signal to call tool */
	admin_call_response(e_adminid, ADMIN_CALL_CONNECT, numberrize_callerinfo(param->connectinfo.id,param->connectinfo.ntype), 0, 0, 0);

	memcpy(&e_connectinfo, &param->connectinfo, sizeof(e_connectinfo));
	printlog("%3d  incoming CONNECT id='%s'%s\n",
		ea_endpoint->ep_serial,
		(e_connectinfo.intern[0])?e_connectinfo.intern:e_connectinfo.id,
		(e_connectinfo.present==INFO_PRESENT_RESTRICTED)?" anonymous":""
		);
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) removing all other ports (start)\n", ea_endpoint->ep_serial);
	while(ea_endpoint->ep_portlist->next) /* as long as we have at least two ports */
	{
		tportlist = ea_endpoint->ep_portlist;
		if (tportlist->port_id == port_id) /* if the first portlist is the calling one, the second must be a different one */
			tportlist = tportlist->next;
		if (tportlist->port_id == port_id)
		{
			PERROR("EPOINT(%d) software error: this should not happen since the portlist list must not have two links to the same port - exitting.\n");
			exit(-1);
		}
		message = message_create(ea_endpoint->ep_serial, tportlist->port_id, EPOINT_TO_PORT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = 26; /* non selected user clearing */
		message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		message_put(message);
		logmessage(message);
		ea_endpoint->free_portlist(tportlist);
	}
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) removing all other ports (end)\n", ea_endpoint->ep_serial);

	e_start = now;

	/* screen by interface */
	if (e_callerinfo.interface[0])
	{
		/* screen incoming caller id */
		screen(0, e_connectinfo.id, sizeof(e_connectinfo.id), &e_connectinfo.ntype, &e_connectinfo.present);
	}

	/* screen connected name */
	if (e_ext.name[0])
		SCPY(e_connectinfo.name, e_ext.name);

	/* add internal id to colp */
	SCPY(e_connectinfo.intern, e_terminal);

	/* we store the connected port number */
	SCPY(e_terminal_interface, e_connectinfo.interfaces);

	/* for internal and am calls, we get the extension's id */
	if (portlist->port_type==PORT_TYPE_VBOX_OUT || e_ext.colp==COLP_HIDE)
	{
		SCPY(e_connectinfo.id, e_ext.callerid);
		SCPY(e_connectinfo.intern, e_terminal);
		e_connectinfo.itype = INFO_ITYPE_INTERN;
		e_connectinfo.ntype = e_ext.callerid_type;
		e_connectinfo.present = e_ext.callerid_present;
	}
	if (portlist->port_type==PORT_TYPE_VBOX_OUT)
	{
		e_connectinfo.itype = INFO_ITYPE_VBOX;
		e_connectinfo.ntype = INFO_NTYPE_UNKNOWN;
	}

	new_state(EPOINT_STATE_CONNECT);

	/* set volume of rx and tx */
	if (e_ext.txvol!=256 || e_ext.rxvol!=256)
	{
		message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_mISDNSIGNAL);
		message->param.mISDNsignal.message = mISDNSIGNAL_VOLUME;
		message->param.mISDNsignal.rxvol = e_ext.txvol;
		message->param.mISDNsignal.txvol = e_ext.rxvol;
		message_put(message);
	}

	e_cfnr_call = e_cfnr_release = 0;
	if (e_terminal[0])
		e_dtmf = 1; /* allow dtmf */
//		if (call_countrelations(ea_endpoint->ep_call_id) == 2)
	{
		/* modify colp */
		/* other calls with no caller id (or not available for the extension) and force colp */
		if ((e_connectinfo.id[0]=='\0' || (e_connectinfo.present==INFO_PRESENT_RESTRICTED && !e_ext.anon_ignore))&& e_ext.colp==COLP_FORCE)
		{
			e_connectinfo.present = INFO_PRESENT_NOTAVAIL;
			if (portlist->port_type==PORT_TYPE_DSS1_TE_OUT || portlist->port_type==PORT_TYPE_DSS1_NT_OUT) /* external extension answered */
			{
				port = find_port_id(portlist->port_id);
				if (port)
				{
					SCPY(e_connectinfo.id, nationalize_callerinfo(port->p_dialinginfo.number, &e_connectinfo.ntype));
					e_connectinfo.present = INFO_PRESENT_ALLOWED;
				}
			}
		}
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, message_type);
		memcpy(&message->param.connectinfo, &e_connectinfo, sizeof(struct connect_info));
		message_put(message);
	}
	if (ea_endpoint->ep_call_id)
	{
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_CHANNEL);
		message->param.channel = CHANNEL_STATE_CONNECT;
		message_put(message);
	} else if (!e_adminid)
	{
		/* callback */
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) we have a callback, so we create a call with cbcaller: \"%s\".\n", ea_endpoint->ep_serial, e_cbcaller);
		SCPY(e_terminal, e_cbcaller);
		new_state(EPOINT_STATE_IN_OVERLAP);
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) callback from extension '%s'\n", ea_endpoint->ep_serial, e_terminal);

		/* get extension's info about terminal */
		if (!read_extension(&e_ext, e_terminal))
		{
			/* extension doesn't exist */
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) rejecting callback from not existing extension: '%s'\n", ea_endpoint->ep_serial, e_terminal);
			message_disconnect_port(portlist, CAUSE_REJECTED, LOCATION_PRIVATE_LOCAL, "");
			new_state(EPOINT_STATE_OUT_DISCONNECT);
			set_tone(portlist, "cause_80"); /* pbx cause: extension not authorized */
			return;
		}

		/* put prefix in front of e_cbdialing */
		SPRINT(buffer, "%s%s", e_ext.prefix, e_cbdialing);
		SCPY(e_dialinginfo.number, buffer);
		e_dialinginfo.itype = INFO_ITYPE_EXTERN;
		e_dialinginfo.ntype = INFO_NTYPE_UNKNOWN;

		/* use caller id (or if exist: id_next_call) for this call */
		e_callerinfo.screen = INFO_SCREEN_NETWORK;
		SCPY(e_callerinfo.intern, e_terminal);
		if (e_ext.id_next_call_present >= 0)
		{
			SCPY(e_callerinfo.id, e_ext.id_next_call);
			e_callerinfo.present = e_ext.id_next_call_present;
			e_callerinfo.ntype = e_ext.id_next_call_type;
			e_ext.id_next_call_present = -1;
			/* extension is written */
			write_extension(&e_ext, e_terminal);
		} else
		{
			SCPY(e_callerinfo.id, e_ext.callerid);
			e_callerinfo.present = e_ext.callerid_present;
			e_callerinfo.ntype = e_ext.callerid_type;
		}

		e_connectedmode = 1; /* dtmf-hangup & disconnect prevention */
		e_dtmf = 1;

		/* check if caller id is NOT authenticated */
		if (!parse_callbackauth(e_terminal, &e_callbackinfo))
		{
			/* make call state to enter password */
			new_state(EPOINT_STATE_IN_OVERLAP);
			e_action = &action_password_write;
			e_match_timeout = 0;
			e_match_to_action = NULL;
			e_dialinginfo.number[0] = '\0';
			e_extdialing = strchr(e_dialinginfo.number, '\0');
			e_password_timeout = now+20;
			process_dialing();
		} else
		{
			/* incoming call (callback) */
			e_ruleset = ruleset_main;
			if (e_ruleset)
				e_rule = e_ruleset->rule_first;
			e_action = NULL;
			e_extdialing = e_dialinginfo.number;
			if (e_dialinginfo.number[0])
			{
				set_tone(portlist, "dialing");
				process_dialing();
			} else
			{
				set_tone(portlist, "dialpbx");
			}
		}
	} else /* testcall */
	{
		set_tone(portlist, "hold");
	}

	/* start recording if enabled, not when answering machine answers */
	if (param->connectinfo.itype!=INFO_ITYPE_VBOX && e_terminal[0] && e_ext.record!=CODEC_OFF && (e_capainfo.bearer_capa==INFO_BC_SPEECH || e_capainfo.bearer_capa==INFO_BC_AUDIO))
	{
		/* check if we are a terminal */
		if (e_terminal[0] == '\0')
			PERROR("Port(%d) cannot record because we are not a terminal\n", ea_endpoint->ep_serial);
		else
		{
			port = find_port_id(portlist->port_id);
			if (port)
				port->open_record(e_ext.record, 0, 0, e_terminal, e_ext.anon_ignore, "", 0);
		}
	}
}

/* port MESSAGE_DISCONNECT MESSAGE_RELEASE */
void EndpointAppPBX::port_disconnect_release(struct port_list *portlist, int message_type, union parameter *param)
{
	struct message	*message;
	char		buffer[256];
	unsigned long	port_id = portlist->port_id;
	int		cause,
			location;

	/* signal to call tool */
	admin_call_response(e_adminid, (message_type==MESSAGE_DISCONNECT)?ADMIN_CALL_DISCONNECT:ADMIN_CALL_RELEASE, "", param->disconnectinfo.cause, param->disconnectinfo.location, 0);

	printlog("%3d  incoming %s cause='%d' (%s) location='%d' (%s)\n",
		ea_endpoint->ep_serial,
		(message_type==MESSAGE_DISCONNECT)?"DISCONNECT":"RELEASE",
		param->disconnectinfo.cause,
		(param->disconnectinfo.cause>0 && param->disconnectinfo.cause<128)?isdn_cause[param->disconnectinfo.cause].english:"-",
		param->disconnectinfo.location,
		(param->disconnectinfo.location>=0 && param->disconnectinfo.location<16)?isdn_location[param->disconnectinfo.location].english:"-"
		);

//#warning does this work? only disconnect when incoming port hat not already disconnected yet?
	if (e_state==EPOINT_STATE_IN_DISCONNECT && message_type!=MESSAGE_RELEASE)// || e_state==EPOINT_STATE_OUT_DISCONNECT || e_state==EPOINT_STATE_IDLE)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) ignored because we are already disconnected.\n", ea_endpoint->ep_serial);
		return;
	}

	/* collect cause */
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) current multipoint cause %d location %d, received cause %d location %d.\n", ea_endpoint->ep_serial, e_multipoint_cause, e_multipoint_location, param->disconnectinfo.cause, param->disconnectinfo.location);
	if (param->disconnectinfo.cause == CAUSE_REJECTED) /* call rejected */
	{
		e_multipoint_cause = CAUSE_REJECTED;
		e_multipoint_location = param->disconnectinfo.location;
	} else
	if (param->disconnectinfo.cause==CAUSE_NORMAL && e_multipoint_cause!=CAUSE_REJECTED) /* reject via hangup */
	{
		e_multipoint_cause = CAUSE_NORMAL;
		e_multipoint_location = param->disconnectinfo.location;
	} else
	if (param->disconnectinfo.cause==CAUSE_BUSY && e_multipoint_cause!=CAUSE_REJECTED && e_multipoint_cause!=CAUSE_NORMAL) /* busy */
	{
		e_multipoint_cause = CAUSE_BUSY;
		e_multipoint_location = param->disconnectinfo.location;
	} else
	if (param->disconnectinfo.cause==CAUSE_OUTOFORDER && e_multipoint_cause!=CAUSE_BUSY && e_multipoint_cause!=CAUSE_REJECTED && e_multipoint_cause!=CAUSE_NORMAL) /* no L1 */
	{
		e_multipoint_cause = CAUSE_OUTOFORDER;
		e_multipoint_location = param->disconnectinfo.location;
	} else
	if (param->disconnectinfo.cause!=CAUSE_NOUSER && e_multipoint_cause!=CAUSE_OUTOFORDER && e_multipoint_cause!=CAUSE_BUSY && e_multipoint_cause!=CAUSE_REJECTED && e_multipoint_cause!=CAUSE_NORMAL) /* anything but not 18 */
	{
		e_multipoint_cause = param->disconnectinfo.cause;
		e_multipoint_location = param->disconnectinfo.location;
	}
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) new multipoint cause %d location %d.\n", ea_endpoint->ep_serial, e_multipoint_cause, e_multipoint_location);

	/* check if we have more than one portlist relation and we just ignore the disconnect */
	if (ea_endpoint->ep_portlist) if (ea_endpoint->ep_portlist->next)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) the disconnect was from a multipoint call. we just release that relation.\n", ea_endpoint->ep_serial);
		portlist = ea_endpoint->ep_portlist;
		while(portlist)
		{
			if (portlist->port_id == port_id)
				break;
			portlist = portlist->next;
		}
		if (!portlist)
		{
			PERROR("EPOINT(%d) software error: no portlist related to the calling port.\n", ea_endpoint->ep_serial);
			exit(-1);
		}
		if (message_type != MESSAGE_RELEASE)
		{
			message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_RELEASE);
			message->param.disconnectinfo.cause = CAUSE_NORMAL; /* normal clearing */
			message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
			message_put(message);
			logmessage(message);
		}
		ea_endpoint->free_portlist(portlist);
		return; /* one relation removed */ 
	}
	if (e_multipoint_cause)
	{
		cause = e_multipoint_cause;
		location = e_multipoint_location;
	} else
	{
		cause = param->disconnectinfo.cause;
		location = param->disconnectinfo.location;
	}

	e_cfnr_call = e_cfnr_release = 0;

	/* process hangup */
	process_hangup(e_call_cause, e_call_location);
	e_multipoint_cause = 0;
	e_multipoint_location = LOCATION_PRIVATE_LOCAL;

	/* tone to disconnected end */
	SPRINT(buffer, "cause_%02x", cause);
	if (ea_endpoint->ep_portlist)
		set_tone(ea_endpoint->ep_portlist, buffer);

	new_state(EPOINT_STATE_IN_DISCONNECT);
	if (ea_endpoint->ep_call_id)
	{
		int haspatterns = 0;
		/* check if pattern is available */
		if (ea_endpoint->ep_portlist)
		if (!ea_endpoint->ep_portlist->next && ea_endpoint->ep_portlist->earlyb)
#warning wie ist das bei einem asterisk, gibts auch tones?
		if (callpbx_countrelations(ea_endpoint->ep_call_id)==2 // we must count relations, in order not to disturb the conference ; NOTE: 
		 && message_type != MESSAGE_RELEASE) // if we release, we are done
			haspatterns = 1;
		if (haspatterns)
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) the port has patterns.\n", ea_endpoint->ep_serial);
			/* indicate patterns */
			message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_PATTERN);
			message_put(message);
			/* connect audio, if not already */
			message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_CHANNEL);
			message->param.channel = CHANNEL_STATE_CONNECT;
			message_put(message);
			/* send disconnect */
			message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, message_type);
			memcpy(&message->param, param, sizeof(union parameter));
			message_put(message);
			/* disable encryption if disconnected */
//PERROR("REMOVE ME: state =%d, %d\n", e_crypt_state, e_crypt);
			if (e_crypt_state)
				cryptman_message(CI_DISCONNECT_IND, NULL, 0);
			return;
		} else
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) the port has no patterns.\n", ea_endpoint->ep_serial);
		}
	}
	release(RELEASE_ALL, location, cause, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL); /* RELEASE_TYPE, callcause, portcause */
	return; /* must exit here */
}

/* port MESSAGE_TIMEOUT */
void EndpointAppPBX::port_timeout(struct port_list *portlist, int message_type, union parameter *param)
{
	char cause[16];

	printlog("%3d  incoming TIMEOUT\n",
		ea_endpoint->ep_serial
		);
	message_type = MESSAGE_DISCONNECT;
	switch (param->state)
	{
		case PORT_STATE_OUT_SETUP:
		case PORT_STATE_OUT_OVERLAP:
		/* no user responding */
		release(RELEASE_ALL, LOCATION_PRIVATE_LOCAL, CAUSE_NOUSER, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL);
		return; /* must exit here */

		case PORT_STATE_IN_SETUP:
		case PORT_STATE_IN_OVERLAP:
		param->disconnectinfo.cause = CAUSE_INVALID; /* number incomplete */
		param->disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		break;

		case PORT_STATE_OUT_PROCEEDING:
		param->disconnectinfo.cause = CAUSE_NOUSER; /* no user responding */
		param->disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		release(RELEASE_ALL, LOCATION_PRIVATE_LOCAL, CAUSE_NOUSER, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL);
		return; /* must exit here */

		case PORT_STATE_IN_PROCEEDING:
		param->disconnectinfo.cause = CAUSE_NOUSER;
		param->disconnectinfo.location = LOCATION_PRIVATE_LOCAL; /* no user responding */
		break;

		case PORT_STATE_OUT_ALERTING:
		param->disconnectinfo.cause = CAUSE_NOANSWER; /* no answer */
		param->disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		release(RELEASE_ALL, LOCATION_PRIVATE_LOCAL, CAUSE_NOANSWER, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL);
		return; /* must exit here */

		case PORT_STATE_IN_ALERTING:
		param->disconnectinfo.cause = CAUSE_NOANSWER;
		param->disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		break;

		case PORT_STATE_IN_DISCONNECT:
		case PORT_STATE_OUT_DISCONNECT:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) in this special case, we release due to disconnect timeout.\n", ea_endpoint->ep_serial);
		release(RELEASE_ALL, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL);
		return; /* must exit here */

		default:
		param->disconnectinfo.cause = 31; /* normal unspecified */
		param->disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
	}
	/* release call, disconnect isdn */
	e_call_pattern = 0;
	new_state(EPOINT_STATE_OUT_DISCONNECT);
	SPRINT(cause, "cause_%02x", param->disconnectinfo.cause);
	SCPY(e_tone, cause);
	while(portlist)
	{
		set_tone(portlist, cause);
		message_disconnect_port(portlist, param->disconnectinfo.cause, param->disconnectinfo.location, "");
		portlist = portlist->next;
	}
	release(RELEASE_CALL, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL); /* RELEASE_TYPE, call, port */
}

/* port MESSAGE_NOTIFY */
void EndpointAppPBX::port_notify(struct port_list *portlist, int message_type, union parameter *param)
{
	struct message *message;
	char *logtext = "";
	char buffer[64];

	/* signal to call tool */
	admin_call_response(e_adminid, ADMIN_CALL_NOTIFY, numberrize_callerinfo(param->notifyinfo.id,param->notifyinfo.ntype), 0, 0, param->notifyinfo.notify);
	if (param->notifyinfo.notify)
	{
		e_rx_state = track_notify(e_rx_state, param->notifyinfo.notify);
	}

	/* if we get notification from stack, local shows if we disabled/enabled audio stream */
	if (param->notifyinfo.local) switch(param->notifyinfo.notify)
	{
		case INFO_NOTIFY_REMOTE_HOLD:
		case INFO_NOTIFY_USER_SUSPENDED:
		/* tell call about it */
		if (ea_endpoint->ep_call_id)
		{
			message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_CHANNEL);
			message->param.channel = CHANNEL_STATE_HOLD;
			message_put(message);
		}
		break;

		case INFO_NOTIFY_REMOTE_RETRIEVAL:
		case INFO_NOTIFY_USER_RESUMED:
		/* set volume of rx and tx */
		if (param->setup.callerinfo.itype == INFO_ITYPE_INTERN)
		if (e_ext.txvol!=256 || e_ext.rxvol!=256)
		if (portlist)
		{
			message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_mISDNSIGNAL);
			message->param.mISDNsignal.message = mISDNSIGNAL_VOLUME;
			message->param.mISDNsignal.rxvol = e_ext.txvol;
			message->param.mISDNsignal.txvol = e_ext.rxvol;
			message_put(message);
		}
		/* set current tone */
		if (portlist)
			set_tone(portlist, e_tone);
		/* tell call about it */
		if (ea_endpoint->ep_call_id)
		{
			message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_CHANNEL);
			message->param.channel = CHANNEL_STATE_CONNECT;
			message_put(message);
		}
		break;
	}

	/* get name of notify */
	switch(param->notifyinfo.notify)
	{
		case 0x00:
		logtext = "NULL";
		break;
		case 0x80:
		logtext = "USER_SUSPENDED";
		break;
		case 0x82:
		logtext = "BEARER_SERVICE_CHANGED";
		break;
		case 0x81:
		logtext = "USER_RESUMED";
		break;
		case 0xc2:
		logtext = "CONFERENCE_ESTABLISHED";
		break;
		case 0xc3:
		logtext = "CONFERENCE_DISCONNECTED";
		break;
		case 0xc4:
		logtext = "OTHER_PARTY_ADDED";
		break;
		case 0xc5:
		logtext = "ISOLATED";
		break;
		case 0xc6:
		logtext = "REATTACHED";
		break;
		case 0xc7:
		logtext = "OTHER_PARTY_ISOLATED";
		break;
		case 0xc8:
		logtext = "OTHER_PARTY_REATTACHED";
		break;
		case 0xc9:
		logtext = "OTHER_PARTY_SPLIT";
		break;
		case 0xca:
		logtext = "OTHER_PARTY_DISCONNECTED";
		break;
		case 0xcb:
		logtext = "CONFERENCE_FLOATING";
		break;
		case 0xcc:
		logtext = "CONFERENCE_DISCONNECTED_PREEMTED";
		break;
		case 0xcf:
		logtext = "CONFERENCE_FLOATING_SERVED_USER_PREEMTED";
		break;
		case 0xe0:
		logtext = "CALL_IS_A_WAITING_CALL";
		break;
		case 0xe8:
		logtext = "DIVERSION_ACTIVATED";
		break;
		case 0xe9:
		logtext = "RESERVED_CT_1";
		break;
		case 0xea:
		logtext = "RESERVED_CT_2";
		break;
		case 0xee:
		logtext = "REVERSE_CHARGING";
		break;
		case 0xf9:
		logtext = "REMOTE_HOLD";
		break;
		case 0xfa:
		logtext = "REMOTE_RETRIEVAL";
		break;
		case 0xfb:
		logtext = "CALL_IS_DIVERTING";
		break;
		default:
		SPRINT(buffer, "indicator=%d", param->notifyinfo.notify - 0x80);
		logtext = buffer;

	}
	printlog("%3d  incoming NOTIFY notify='%s' id='%s'%s\n",
		ea_endpoint->ep_serial,
		logtext,
		param->notifyinfo.id,
		(param->notifyinfo.present==INFO_PRESENT_RESTRICTED)?" anonymous":""
	);

	/* notify call if available */
	if (ea_endpoint->ep_call_id)
	{
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_NOTIFY);
		memcpy(&message->param.notifyinfo, &param->notifyinfo, sizeof(struct notify_info));
		message_put(message);
	}

}

/* port MESSAGE_FACILITY */
void EndpointAppPBX::port_facility(struct port_list *portlist, int message_type, union parameter *param)
{
	struct message *message;

	printlog("%3d  incoming FACILITY len='%d'\n",
		ea_endpoint->ep_serial,
		param->facilityinfo.len
		);

	message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_FACILITY);
	memcpy(&message->param.facilityinfo, &param->facilityinfo, sizeof(struct facility_info));
	message_put(message);
}

/* port MESSAGE_SUSPEND */
/* NOTE: before supending, the inactive-notification must be done in order to set call mixer */
void EndpointAppPBX::port_suspend(struct port_list *portlist, int message_type, union parameter *param)
{
	printlog("%3d  incoming SUSPEND\n",
		ea_endpoint->ep_serial
		);
	/* epoint is now parked */
	ea_endpoint->ep_park = 1;
	memcpy(ea_endpoint->ep_park_callid, param->parkinfo.callid, sizeof(ea_endpoint->ep_park_callid));
	ea_endpoint->ep_park_len = (param->parkinfo.len>8)?8:param->parkinfo.len;

	/* remove port relation */
	ea_endpoint->free_portlist(portlist);
}

/* port MESSAGE_RESUME */
/* NOTE: before resume, the active-notification must be done in order to set call mixer */
void EndpointAppPBX::port_resume(struct port_list *portlist, int message_type, union parameter *param)
{
	printlog("%3d  incoming RESUME\n",
		ea_endpoint->ep_serial
		);
	/* epoint is now resumed */
	ea_endpoint->ep_park = 0;

}


/* port sends message to the endpoint
 */
void EndpointAppPBX::ea_message_port(unsigned long port_id, int message_type, union parameter *param)
{
	struct port_list *portlist;
	struct message *message;
	class Port *port;

	portlist = ea_endpoint->ep_portlist;
	while(portlist)
	{
		if (port_id == portlist->port_id)
			break;
		portlist = portlist->next;
	}
	if (!portlist)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) warning: port is not related to this endpoint. This may happen, if port has been released after the message was created.\n", ea_endpoint->ep_serial);
		return;
	}

//	PDEBUG(DEBUG_EPOINT, "received message %d (terminal %s, caller id %s)\n", message, e_terminal, e_callerinfo.id);
	switch(message_type)
	{
		case MESSAGE_DATA: /* data from port */
		/* send back to source for recording */
		if (port_id)
		{
			message = message_create(ea_endpoint->ep_serial, port_id, EPOINT_TO_PORT, message_type);
			memcpy(&message->param, param, sizeof(union parameter));
			message_put(message);
		}

		/* check if there is a call */
		if (!ea_endpoint->ep_call_id)
			break;
		/* continue if only one portlist */
		if (ea_endpoint->ep_portlist->next != NULL)
			break;
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, message_type);
		memcpy(&message->param, param, sizeof(union parameter));
		message_put(message);
		break;

		case MESSAGE_TONE_EOF: /* tone is end of file */
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) current tone is now end of file.\n", ea_endpoint->ep_serial);
		if (e_action)
		{
			if (e_action->index == ACTION_VBOX_PLAY)
			{
				vbox_message_eof();
			}
			if (e_action->index == ACTION_EFI)
			{
				efi_message_eof();
			}
		}
		break;

		case MESSAGE_TONE_COUNTER: /* counter info received */
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) received counter information: %d / %d seconds after start of tone.\n", ea_endpoint->ep_serial, param->counter.current, param->counter.max);
		if (e_action)
		if (e_action->index == ACTION_VBOX_PLAY)
		{
			e_vbox_counter = param->counter.current;
			if (param->counter.max >= 0)
				e_vbox_counter_max = param->counter.max;
		}
		break;

		/* PORT sends SETUP message */
		case MESSAGE_SETUP:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) incoming call from callerid=%s, dialing=%s\n", ea_endpoint->ep_serial, param->setup.callerinfo.id, param->setup.dialinginfo.number);
		if (e_state!=EPOINT_STATE_IDLE)
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) ignored because we are not in idle state.\n", ea_endpoint->ep_serial);
			break;
		}
		port_setup(portlist, message_type, param);
		break;

		/* PORT sends INFORMATION message */
		case MESSAGE_INFORMATION: /* additional digits received */
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) incoming call dialing more=%s (terminal '%s', caller id '%s')\n", ea_endpoint->ep_serial, param->information.number, e_terminal, e_callerinfo.id);
		port_information(portlist, message_type, param);
		break;

		/* PORT sends FACILITY message */
		case MESSAGE_FACILITY:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) incoming facility (terminal '%s', caller id '%s')\n", ea_endpoint->ep_serial, e_terminal, e_callerinfo.id);
		port_facility(portlist, message_type, param);
		break;

		/* PORT sends DTMF message */
		case MESSAGE_DTMF: /* dtmf digits received */
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) dtmf digit=%c (terminal '%s', caller id '%s')\n", ea_endpoint->ep_serial, param->dtmf, e_terminal, e_callerinfo.id);
		port_dtmf(portlist, message_type, param);
		break;

		/* PORT sends CRYPT message */
		case MESSAGE_CRYPT: /* crypt response received */
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) crypt response=%d\n", ea_endpoint->ep_serial, param->crypt.type);
		port_crypt(portlist, message_type, param);
		break;

		/* PORT sends MORE message */
		case MESSAGE_OVERLAP:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) outgoing call is accepted [overlap dialing] (terminal '%s', caller id '%s')\n", ea_endpoint->ep_serial, e_terminal, e_callerinfo.id);
		if (e_state != EPOINT_STATE_OUT_SETUP)
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) ignored because we are not in setup state (for port_list: another portlist might have changed the state already).\n", ea_endpoint->ep_serial);
			break;
		}
		port_overlap(portlist, message_type, param);
		break;

		/* PORT sends PROCEEDING message */
		case MESSAGE_PROCEEDING: /* port is proceeding */
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) outgoing call is proceeding (terminal '%s', caller id '%s')\n", ea_endpoint->ep_serial, e_terminal, e_callerinfo.id);
		if (e_state!=EPOINT_STATE_OUT_SETUP
		 && e_state!=EPOINT_STATE_OUT_OVERLAP)
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) ignored because we are not in overlap state (for port_list: another portlist might have changed the state already).\n", ea_endpoint->ep_serial);
			break;
		}
		port_proceeding(portlist, message_type, param);
		break;

		/* PORT sends ALERTING message */
		case MESSAGE_ALERTING:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) outgoing call is ringing (terminal '%s', caller id '%s')\n", ea_endpoint->ep_serial, e_terminal, e_callerinfo.id);
		if (e_state!=EPOINT_STATE_OUT_SETUP
		 && e_state!=EPOINT_STATE_OUT_OVERLAP
		 && e_state!=EPOINT_STATE_OUT_PROCEEDING)
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) ignored because we are not in setup or proceeding state (for port_list: another portlist might have changed the state already).\n", ea_endpoint->ep_serial);
			break;
		}
		port_alerting(portlist, message_type, param);
		break;

		/* PORT sends CONNECT message */
		case MESSAGE_CONNECT:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) outgoing call connected to %s (terminal '%s', caller id '%s')\n", ea_endpoint->ep_serial, e_connectinfo.id, e_terminal, e_callerinfo.id);
		if (e_state!=EPOINT_STATE_OUT_SETUP
		 && e_state!=EPOINT_STATE_OUT_OVERLAP
		 && e_state!=EPOINT_STATE_OUT_PROCEEDING
		 && e_state!=EPOINT_STATE_OUT_ALERTING)
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) ignored because we are not in setup, proceeding or alerting state.\n", ea_endpoint->ep_serial);
			break;
		}
		port_connect(portlist, message_type, param);
		break;

		/* PORT sends DISCONNECT message */
		case MESSAGE_DISCONNECT: /* port is disconnected */
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) call disconnect with cause=%d location=%d (terminal '%s', caller id '%s')\n", ea_endpoint->ep_serial, param->disconnectinfo.cause, param->disconnectinfo.location, e_terminal, e_callerinfo.id);
		port_disconnect_release(portlist, message_type, param);
		break;

		/* PORT sends a RELEASE message */
		case MESSAGE_RELEASE: /* port releases */
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) release with cause=%d location=%d (terminal '%s', caller id '%s')\n", ea_endpoint->ep_serial, param->disconnectinfo.cause, param->disconnectinfo.location, e_terminal, e_callerinfo.id);
		/* portlist is release at port_disconnect_release, thanx Paul */
		port_disconnect_release(portlist, message_type, param);
		break;

		/* PORT sends a TIMEOUT message */
		case MESSAGE_TIMEOUT:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) epoint with terminal '%s' (caller id '%s') received timeout (state=%d).\n", ea_endpoint->ep_serial, e_terminal, e_callerinfo.id, param->state);
		port_timeout(portlist, message_type, param);
		break; /* release */

		/* PORT sends a NOTIFY message */
		case MESSAGE_NOTIFY:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) epoint with terminal '%s' (caller id '%s') received notify.\n", ea_endpoint->ep_serial, e_terminal, e_callerinfo.id);
		port_notify(portlist, message_type, param);
		break;

		/* PORT sends a SUSPEND message */
		case MESSAGE_SUSPEND:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) epoint with terminal '%s' (caller id '%s') received suspend.\n", ea_endpoint->ep_serial, e_terminal, e_callerinfo.id);
		port_suspend(portlist, message_type, param);
		break; /* suspend */

		/* PORT sends a RESUME message */
		case MESSAGE_RESUME:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) epoint with terminal '%s' (caller id '%s') received resume.\n", ea_endpoint->ep_serial, e_terminal, e_callerinfo.id);
		port_resume(portlist, message_type, param);
		break;

		default:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) epoint with terminal '%s' (caller id '%s') received a wrong message: %d\n", ea_endpoint->ep_serial, e_terminal, e_callerinfo.id, message);
	}

	/* Note: this endpoint may be destroyed, so we MUST return */
}


/* messages from port
 */
/* call MESSAGE_CRYPT */
void EndpointAppPBX::call_crypt(struct port_list *portlist, int message_type, union parameter *param)
{
	switch(param->crypt.type)
	{
		/* message from remote port to "crypt manager" */
		case CU_ACTK_REQ:           /* activate key-exchange */
		case CU_ACTS_REQ:            /* activate shared key */
		case CU_DACT_REQ:          /* deactivate */
		case CU_INFO_REQ:         /* request last info message */
		cryptman_message(param->crypt.type, param->crypt.data, param->crypt.len);
		break;

		/* message from "crypt manager" to user */
		case CU_ACTK_CONF:          /* key-echange done */
		case CU_ACTS_CONF:          /* shared key done */
		case CU_DACT_CONF:           /* deactivated */
		case CU_DACT_IND:           /* deactivated */
		case CU_ERROR_IND:         /* receive error message */
		case CU_INFO_IND:         /* receive info message */
		case CU_INFO_CONF:         /* receive info message */
		encrypt_result(param->crypt.type, (char *)param->crypt.data);
		break;

		default:
		PERROR("EPOINT(%d) epoint with terminal '%s' (caller id '%s') unknown crypt message: '%d'\n", ea_endpoint->ep_serial, e_terminal, e_callerinfo.id, param->crypt.type);
	}
}

/* call MESSAGE_INFORMATION */
void EndpointAppPBX::call_information(struct port_list *portlist, int message_type, union parameter *param)
{
	struct message *message;

	e_overlap = 1;

	while(portlist)
	{
		message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_INFORMATION);
		memcpy(&message->param.information, &param->information, sizeof(struct dialing_info));
		message_put(message);
		logmessage(message);
		portlist = portlist->next;
	}
}

/* call MESSAGE_FACILITY */
void EndpointAppPBX::call_facility(struct port_list *portlist, int message_type, union parameter *param)
{
	struct message *message;

	if (!e_ext.facility)
	{
		return;
	}

	while(portlist)
	{
		message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_FACILITY);
		memcpy(&message->param.facilityinfo, &param->facilityinfo, sizeof(struct facility_info));
		message_put(message);
		logmessage(message);
		portlist = portlist->next;
	}
}

/* call MESSAGE_MORE */
void EndpointAppPBX::call_overlap(struct port_list *portlist, int message_type, union parameter *param)
{
	struct message *message;

	new_state(EPOINT_STATE_IN_OVERLAP);
	
	/* own dialtone */
	if (e_call_pattern && e_ext.own_setup)
	{
		/* disconnect audio */
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_CHANNEL);
		message->param.channel = CHANNEL_STATE_HOLD;
		message_put(message);
	}
	if (e_action) if (e_action->index == ACTION_OUTDIAL || e_action->index == ACTION_EXTERNAL)
	{
			set_tone(portlist, "dialtone");
			return;
	}
	if (e_terminal[0])
		set_tone(portlist, "dialpbx");
	else
		set_tone(portlist, "dialtone");
}

/* call MESSAGE_PROCEEDING */
void EndpointAppPBX::call_proceeding(struct port_list *portlist, int message_type, union parameter *param)
{
	struct message *message;

	new_state(EPOINT_STATE_IN_PROCEEDING);

	/* own proceeding tone */
	if (e_call_pattern)
	{
		/* connect / disconnect audio */
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_CHANNEL);
		if (e_ext.own_proceeding)
			message->param.channel = CHANNEL_STATE_HOLD;
		else
			message->param.channel = CHANNEL_STATE_CONNECT;
		message_put(message);
	}
//			UCPY(e_call_tone, "proceeding");
	if (portlist)
	{
		message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_PROCEEDING);
		message_put(message);
		logmessage(message);
	}
	set_tone(portlist, "proceeding");
}

/* call MESSAGE_ALERTING */
void EndpointAppPBX::call_alerting(struct port_list *portlist, int message_type, union parameter *param)
{
	struct message *message;

	new_state(EPOINT_STATE_IN_ALERTING);

	/* own alerting tone */
	if (e_call_pattern)
	{
		/* connect / disconnect audio */
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_CHANNEL);
		if (e_ext.own_alerting)
			message->param.channel = CHANNEL_STATE_HOLD;
		else
			message->param.channel = CHANNEL_STATE_CONNECT;
		message_put(message);
	}
	if (portlist)
	{
		message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_ALERTING);
		message_put(message);
		logmessage(message);
	}
	if (e_action) if (e_action->index == ACTION_OUTDIAL || e_action->index == ACTION_EXTERNAL)
	{
		set_tone(portlist, "ringing");
		return;
	}
	if (e_terminal[0])
		set_tone(portlist, "ringpbx");
	else
		set_tone(portlist, "ringing");
}

/* call MESSAGE_CONNECT */
void EndpointAppPBX::call_connect(struct port_list *portlist, int message_type, union parameter *param)
{
	struct message *message;

	new_state(EPOINT_STATE_CONNECT);
//			UCPY(e_call_tone, "");
	if (e_terminal[0])
		e_dtmf = 1; /* allow dtmf */
	e_powerdialing = 0;
	memcpy(&e_connectinfo, &param->connectinfo, sizeof(e_callerinfo));
	if(portlist)
	{
		message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_CONNECT);
		memcpy(&message->param, param, sizeof(union parameter));
		/* screen by interface */
		if (e_connectinfo.interface[0])
		{
			/* screen incoming caller id */
			screen(1, e_connectinfo.id, sizeof(e_connectinfo.id), &e_connectinfo.ntype, &e_connectinfo.present);
		}
		memcpy(&message->param.connnectinfo, e_connectinfo);

		/* screen clip if prefix is required */
		if (e_terminal[0] && message->param.connectinfo.id[0] && e_ext.clip_prefix[0])
		{
			SCPY(message->param.connectinfo.id, e_ext.clip_prefix);
			SCAT(message->param.connectinfo.id, numberrize_callerinfo(e_connectinfo.id,e_connectinfo.ntype));
			message->param.connectinfo.ntype = INFO_NTYPE_UNKNOWN;
		}

		/* use internal caller id */
		if (e_terminal[0] && e_connectinfo.intern[0] && (message->param.connectinfo.present!=INFO_PRESENT_RESTRICTED || e_ext.anon_ignore))
		{
			SCPY(message->param.connectinfo.id, e_connectinfo.intern);
			message->param.connectinfo.ntype = INFO_NTYPE_UNKNOWN;
		}

		/* handle restricted caller ids */
		apply_callerid_restriction(e_ext.anon_ignore, portlist->port_type, message->param.connectinfo.id, &message->param.connectinfo.ntype, &message->param.connectinfo.present, &message->param.connectinfo.screen, message->param.connectinfo.voip, message->param.connectinfo.intern, message->param.connectinfo.name);
		/* display callerid if desired for extension */
		SCPY(message->param.connectinfo.display, apply_callerid_display(message->param.connectinfo.id, message->param.connectinfo.itype, message->param.connectinfo.ntype, message->param.connectinfo.present, message->param.connectinfo.screen, message->param.connectinfo.voip, message->param.connectinfo.intern, message->param.connectinfo.name));

		/* use conp, if enabld */
		if (!e_ext.centrex)
			message->param.connectinfo.name[0] = '\0';

		/* send connect */
		message_put(message);
		logmessage(message);
	}
	set_tone(portlist, NULL);
	e_call_pattern = 0;
	message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_CHANNEL);
	message->param.channel = CHANNEL_STATE_CONNECT;
	message_put(message);
	e_start = now;
}

/* call MESSAGE_DISCONNECT MESSAGE_RELEASE */
void EndpointAppPBX::call_disconnect_release(struct port_list *portlist, int message_type, union parameter *param)
{
	char cause[16];
	struct message *message;


	/* be sure that we are active */
	notify_active();
	e_tx_state = NOTIFY_STATE_ACTIVE;

	/* we are powerdialing, if e_powerdialing is set and limit is not exceeded if given */
	if (e_powerdialing && ((e_powercount+1)<e_powerlimit || e_powerlimit<1))
	{
		release(RELEASE_CALL, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL); /* RELEASE_TYPE, call, port */

		/* set time for power dialing */
		e_powerdialing = now_d + e_powerdelay; /* set redial in the future */
		e_powercount++;

		/* set redial tone */
		if (ea_endpoint->ep_portlist)
		{
			e_call_pattern = 0;
		}
		set_tone(ea_endpoint->ep_portlist, "redial");
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) epoint with terminal '%s' (caller id '%s') redialing in %d seconds\n", ea_endpoint->ep_serial, e_terminal, e_callerinfo.id, (int)e_powerdelay);
		/* send proceeding when powerdialing and still setup (avoid dialing timeout) */
		if (e_state==EPOINT_STATE_IN_OVERLAP)
		{
			new_state(EPOINT_STATE_IN_PROCEEDING);
			if (portlist)
			{
				message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_PROCEEDING);
				message_put(message);
				logmessage(message);
			}
/* caused the error, that the first knock sound was not there */
/*					set_tone(portlist, "proceeding"); */
		}
		/* send display of powerdialing */
		if (e_ext.display_dialing)
		{
			while (portlist)
			{
				message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_NOTIFY);
				if (e_powerlimit)
					SPRINT(message->param.notifyinfo.display, "Retry %d of %d", e_powercount, e_powerlimit);
				else
					SPRINT(message->param.notifyinfo.display, "Retry %d", e_powercount);
				message_put(message);
				logmessage(message);
				portlist = portlist->next;
			}
		}
		return;
	}

	/* set stop time */
	e_stop = now;

	if ((e_state!=EPOINT_STATE_CONNECT
	  && e_state!=EPOINT_STATE_OUT_DISCONNECT
	  && e_state!=EPOINT_STATE_IN_OVERLAP
	  && e_state!=EPOINT_STATE_IN_PROCEEDING
	  && e_state!=EPOINT_STATE_IN_ALERTING)
	 || !ea_endpoint->ep_portlist) /* or no port */
	{
		process_hangup(param->disconnectinfo.cause, param->disconnectinfo.location);
		release(RELEASE_ALL, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL, LOCATION_PRIVATE_LOCAL, param->disconnectinfo.cause); /* RELEASE_TYPE, call, port */
		return; /* must exit here */
	}
	/* save cause */
	if (!e_call_cause)
	{
		e_call_cause = param->disconnectinfo.cause;
		e_call_location = param->disconnectinfo.location;
	}

	/* on release we need the audio again! */
	if (message_type == MESSAGE_RELEASE)
	{
		e_call_pattern = 0;
		ea_endpoint->ep_call_id = 0;
	}
	/* disconnect and select tone */
	new_state(EPOINT_STATE_OUT_DISCONNECT);
	SPRINT(cause, "cause_%02x", param->disconnectinfo.cause);
	/* if own_cause, we must release the call */
	if (e_ext.own_cause /* own cause */
	 || !e_call_pattern) /* no patterns */
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) we have own cause or we have no patterns. (own_cause=%d pattern=%d)\n", ea_endpoint->ep_serial, e_ext.own_cause, e_call_pattern);
		if (message_type != MESSAGE_RELEASE)
			release(RELEASE_CALL, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL); /* RELEASE_TYPE, call, port */
		e_call_pattern = 0;
	} else /* else we enable audio */
	{
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_CHANNEL);
		message->param.channel = CHANNEL_STATE_CONNECT;
		message_put(message);
	}
	/* send disconnect message */
	SCPY(e_tone, cause);
	while(portlist)
	{
		set_tone(portlist, cause);
		message_disconnect_port(portlist, param->disconnectinfo.cause, param->disconnectinfo.location, "");
		portlist = portlist->next;
	}
}

/* call MESSAGE_SETUP */
void EndpointAppPBX::call_setup(struct port_list *portlist, int message_type, union parameter *param)
{
	struct message *message;

	/* if we already in setup state, we just update the dialing with new digits */
	if (e_state == EPOINT_STATE_OUT_SETUP
	 || e_state == EPOINT_STATE_OUT_OVERLAP)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) we are in setup state, so we do overlap dialing.\n", ea_endpoint->ep_serial);
		/* if digits changed, what we have already dialed */
		if (!!strncmp(e_dialinginfo.number,param->setup.dialinginfo.number,strlen(e_dialinginfo.number)))
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) we have dialed digits which have been changed or we have a new multidial, so we must redial.\n", ea_endpoint->ep_serial);
			/* release all ports */
			while((portlist = ea_endpoint->ep_portlist))
			{
				message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_RELEASE);
				message->param.disconnectinfo.cause = CAUSE_NORMAL; /* normal clearing */
				message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
				message_put(message);
				logmessage(message);
				ea_endpoint->free_portlist(portlist);
			}

			/* disconnect audio */
			message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_CHANNEL);
			message->param.channel = CHANNEL_STATE_HOLD;
			message_put(message);

			/* get dialing info */
			memcpy(&e_callerinfo, &param->setup.callerinfo, sizeof(e_callerinfo));
			memcpy(&e_dialinginfo, &param->setup.dialinginfo, sizeof(e_dialinginfo));
			memcpy(&e_redirinfo, &param->setup.redirinfo, sizeof(e_redirinfo));
			memcpy(&e_capainfo, &param->setup.capainfo, sizeof(e_capainfo));
			new_state(EPOINT_STATE_OUT_OVERLAP);

			/* get time */
			e_redial = now_d + 1; /* set redial one second in the future */
			return;
		}
		/* if we have a pending redial, so we just adjust the dialing number */
		if (e_redial)
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) redial in progress, so we update the dialing number to %s.\n", ea_endpoint->ep_serial, param->setup.dialinginfo.number);
			memcpy(&e_dialinginfo, &param->setup.dialinginfo, sizeof(e_dialinginfo));
			return;
		}
		if (!ea_endpoint->ep_portlist)
		{
			PERROR("ERROR: overlap dialing to a NULL port relation\n");
		}
		if (ea_endpoint->ep_portlist->next)
		{
			PERROR("ERROR: overlap dialing to a port_list port relation\n");
		}
		if (e_state == EPOINT_STATE_OUT_SETUP)
		{
			/* queue digits */
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) digits '%s' are queued because we didn't receive a setup acknowledge.\n", ea_endpoint->ep_serial, param->setup.dialinginfo.number);
			SCAT(e_dialing_queue, param->setup.dialinginfo.number + strlen(e_dialinginfo.number));
			
		} else
		{
			/* get what we have not dialed yet */
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) we have already dialed '%s', we received '%s', what's left '%s'.\n", ea_endpoint->ep_serial, e_dialinginfo.number, param->setup.dialinginfo.number, param->setup.dialinginfo.number+strlen(e_dialinginfo.number));
			message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_portlist->port_id, EPOINT_TO_PORT, MESSAGE_INFORMATION);
			SCPY(message->param.information.number, param->setup.dialinginfo.number + strlen(e_dialinginfo.number));
			message->param.information.ntype = INFO_NTYPE_UNKNOWN;
			message_put(message);
			logmessage(message);
		}
		/* always store what we have dialed or queued */
		memcpy(&e_dialinginfo, &param->setup.dialinginfo, sizeof(e_dialinginfo));
		
		return;
	}
	if (e_state != EPOINT_STATE_IDLE)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) ignored because we are not in idle state.\n", ea_endpoint->ep_serial);
		return;
	}
	/* if an internal extension is dialed, copy that number */
	if (param->setup.dialinginfo.itype==INFO_ITYPE_INTERN || param->setup.dialinginfo.itype==INFO_ITYPE_VBOX)
		SCPY(e_terminal, param->setup.dialinginfo.number);
	/* if an internal extension is dialed, get extension's info about caller */
	if (e_terminal[0]) 
	{
		if (!read_extension(&e_ext, e_terminal))
		{
			e_terminal[0] = '\0';
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) the called terminal='%s' is not found in directory tree!\n", ea_endpoint->ep_serial, e_terminal);
		}
	}

	memcpy(&e_callerinfo, &param->setup.callerinfo, sizeof(e_callerinfo));
	memcpy(&e_dialinginfo, &param->setup.dialinginfo, sizeof(e_dialinginfo));
	memcpy(&e_redirinfo, &param->setup.redirinfo, sizeof(e_redirinfo));
	memcpy(&e_capainfo, &param->setup.capainfo, sizeof(e_capainfo));

	/* screen by interface */
	if (e_callerinfo.interface[0])
	{
		/* screen incoming caller id */
		screen(1, e_callerinfo.id, sizeof(e_callerinfo.id), &e_callerinfo.ntype, &e_callerinfo.present);
	}

	/* process (voice over) data calls */
	if (e_ext.datacall && e_capainfo.bearer_capa!=INFO_BC_SPEECH && e_capainfo.bearer_capa!=INFO_BC_AUDIO)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) handling data call as audio call: '%s'\n", ea_endpoint->ep_serial, e_terminal);
		memset(&e_capainfo, 0, sizeof(e_capainfo));
		e_capainfo.bearer_capa = INFO_BC_AUDIO;
		e_capainfo.bearer_mode = INFO_BMODE_CIRCUIT;
		e_capainfo.bearer_info1 = (options.law=='u')?INFO_INFO1_ULAW:INFO_INFO1_ALAW;
	}

	new_state(EPOINT_STATE_OUT_SETUP);
	/* call special setup routine */
	out_setup();
}

/* call MESSAGE_mISDNSIGNAL */
void EndpointAppPBX::call_mISDNsignal(struct port_list *portlist, int message_type, union parameter *param)
{
	struct message *message;

	while(portlist)
	{
		message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_mISDNSIGNAL);
		memcpy(&message->param, param, sizeof(union parameter));
		message_put(message);
		portlist = portlist->next;
	}
}

/* call MESSAGE_NOTIFY */
void EndpointAppPBX::call_notify(struct port_list *portlist, int message_type, union parameter *param)
{
	struct message *message;
	int new_state;

	if (param->notifyinfo.notify)
	{
		new_state = track_notify(e_tx_state, param->notifyinfo.notify);
//		/* if notification was generated locally, we turn hold music on/off */ 
//		if (param->notifyinfo.local)
// NOTE: we always assume that we send hold music on suspension of call, because we don't track if audio is available or not (we assume that we always have no audio, to make it easier)
		{
			if (e_hold)
			{
				/* unhold if */
				if (new_state!=NOTIFY_STATE_HOLD && new_state!=NOTIFY_STATE_SUSPEND)
				{
					while(portlist)
					{
						set_tone(portlist, "");
						portlist = portlist->next;
					}
					portlist = ea_endpoint->ep_portlist;
					e_hold = 0;
				}
			} else {
				/* hold if */
				if (new_state==NOTIFY_STATE_HOLD || new_state==NOTIFY_STATE_SUSPEND)
				{
					while(portlist)
					{
						set_tone(portlist, "hold");
						portlist = portlist->next;
					}
					portlist = ea_endpoint->ep_portlist;
					e_hold = 1;
				}
			}
		}
		/* save new state */
		e_tx_state = new_state;
	}

	/* notify port(s) about it */
	while(portlist)
	{
		message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_NOTIFY);
		memcpy(&message->param.notifyinfo, &param->notifyinfo, sizeof(struct notify_info));
		/* handle restricted caller ids */
		apply_callerid_restriction(e_ext.anon_ignore, portlist->port_type, message->param.notifyinfo.id, &message->param.notifyinfo.ntype, &message->param.notifyinfo.present, NULL, message->param.notifyinfo.voip, message->param.notifyinfo.intern, 0);
		/* display callerid if desired for extension */
		SCPY(message->param.notifyinfo.display, apply_callerid_display(message->param.notifyinfo.id, message->param.notifyinfo.itype, message->param.notifyinfo.ntype, message->param.notifyinfo.present, 0, message->param.notifyinfo.voip, message->param.notifyinfo.intern, NULL));
		message_put(message);
		logmessage(message);
		portlist = portlist->next;
	}
}

/* call sends messages to the endpoint
 */
void EndpointAppPBX::ea_message_call(unsigned long call_id, int message_type, union parameter *param)
{
	struct port_list *portlist;
	struct message *message;

	if (!call_id)
	{
		PERROR("EPOINT(%d) error: call == NULL.\n", ea_endpoint->ep_serial);
		return;
	}

	portlist = ea_endpoint->ep_portlist;

	/* send MESSAGE_DATA to port */
	if (call_id == ea_endpoint->ep_call_id)
	{
		if (message_type == MESSAGE_DATA)
		{
			/* skip if no port relation */
			if (!portlist)
				return;
			/* skip if more than one port relation */
			if (portlist->next)
				return;
			/* send audio data to port */
			message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_DATA);
			memcpy(&message->param, param, sizeof(union parameter));
			message_put(message);
			return;
		}
	}

//	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) received message %d for active call (terminal %s, caller id %s state=%d)\n", ea_endpoint->ep_serial, message, e_terminal, e_callerinfo.id, e_state);
	switch(message_type)
	{
		/* CALL SENDS CRYPT message */
		case MESSAGE_CRYPT:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) epoint with terminal '%s' (caller id '%s') received crypt message: '%d'\n", ea_endpoint->ep_serial, e_terminal, e_callerinfo.id, param->crypt.type);
		call_crypt(portlist, message_type, param);
		break;

		/* CALL sends INFORMATION message */
		case MESSAGE_INFORMATION:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) epoint with terminal '%s' (caller id '%s') received more digits: '%s'\n", ea_endpoint->ep_serial, e_terminal, e_callerinfo.id, param->information.number);
		call_information(portlist, message_type, param);
		break;

		/* CALL sends FACILITY message */
		case MESSAGE_FACILITY:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) epoint with terminal '%s' (caller id '%s') received facility\n", ea_endpoint->ep_serial, e_terminal, e_callerinfo.id);
		call_facility(portlist, message_type, param);
		break;

		/* CALL sends OVERLAP message */
		case MESSAGE_OVERLAP:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) epoint with terminal '%s' (caller id '%s') received 'more info available'\n", ea_endpoint->ep_serial, e_terminal, e_callerinfo.id);
		if (e_state!=EPOINT_STATE_IN_SETUP
		 && e_state!=EPOINT_STATE_IN_OVERLAP)
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) ignored because we are not in setup state.\n", ea_endpoint->ep_serial);
			break;
		}
		call_overlap(portlist, message_type, param);
		break;

		/* CALL sends PROCEEDING message */
		case MESSAGE_PROCEEDING:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) epoint with terminal '%s (caller id '%s') received proceeding\n", ea_endpoint->ep_serial, e_terminal, e_callerinfo.id);
		if(e_state!=EPOINT_STATE_IN_OVERLAP)
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) ignored because we are not in setup state.\n", ea_endpoint->ep_serial);
			break;
		}
		call_proceeding(portlist, message_type, param);
		break;

		/* CALL sends ALERTING message */
		case MESSAGE_ALERTING:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) epoint with terminal '%s' (caller id '%s') received alerting\n", ea_endpoint->ep_serial, e_terminal, e_callerinfo.id);
		if (e_state!=EPOINT_STATE_IN_OVERLAP
		 && e_state!=EPOINT_STATE_IN_PROCEEDING)
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) ignored because we are not in setup or proceeding state.\n", ea_endpoint->ep_serial);
			break;
		}
		call_alerting(portlist, message_type, param);
		break;

		/* CALL sends CONNECT message */
		case MESSAGE_CONNECT:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) epoint with terminal '%s' (caller id '%s') received connect\n", ea_endpoint->ep_serial, e_terminal, e_callerinfo.id);
		if (e_state!=EPOINT_STATE_IN_OVERLAP
		 && e_state!=EPOINT_STATE_IN_PROCEEDING
		 && e_state!=EPOINT_STATE_IN_ALERTING)
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) ignored because we are not in setup, proceeding or alerting state.\n", ea_endpoint->ep_serial);
			break;
		}
		call_connect(portlist, message_type, param);
		break;

		/* CALL sends DISCONNECT/RELEASE message */
		case MESSAGE_DISCONNECT: /* call disconnect */
		case MESSAGE_RELEASE: /* call releases */
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) epoint with terminal '%s' (caller id '%s') received %s with cause %d location %d\n", ea_endpoint->ep_serial, e_terminal, e_callerinfo.id, (message_type==MESSAGE_DISCONNECT)?"disconnect":"release", param->disconnectinfo.cause, param->disconnectinfo.location);
		call_disconnect_release(portlist, message_type, param);
		break;

		/* CALL sends SETUP message */
		case MESSAGE_SETUP:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) epoint received setup from terminal='%s',id='%s' to id='%s' (dialing itype=%d)\n", ea_endpoint->ep_serial, param->setup.callerinfo.intern, param->setup.callerinfo.id, param->setup.dialinginfo.number, param->setup.dialinginfo.itype);
		call_setup(portlist, message_type, param);
		return;
		break;

		/* CALL sends special mISDNSIGNAL message */
		case MESSAGE_mISDNSIGNAL: /* isdn message to port */
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) epoint with terminal '%s' (caller id '%s') received mISDNsignal message.\n", ea_endpoint->ep_serial, e_terminal, e_callerinfo.id);
		call_mISDNsignal(portlist, message_type, param);
		break;

		/* CALL has pattern available */
		case MESSAGE_PATTERN: /* indicating pattern available */
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) epoint with terminal '%s' (caller id '%s') received pattern availability.\n", ea_endpoint->ep_serial, e_terminal, e_callerinfo.id);
		if (!e_call_pattern)
		{
			PDEBUG(DEBUG_EPOINT, "-> pattern becomes available\n");
			e_call_pattern = 1;
			SCPY(e_tone, "");
			while(portlist)
			{
				set_tone(portlist, NULL);
				portlist = portlist->next;
			}
			/* connect our audio tx and rx (blueboxing should be possibe before connect :)*/
			message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_CHANNEL);
			message->param.channel = CHANNEL_STATE_CONNECT;
			message_put(message);
//			/* tell remote epoint to connect audio also, because we like to hear the patterns */
//			message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_REMOTE_AUDIO);
//			message->param.channel = CHANNEL_STATE_CONNECT;
//			message_put(message);
// patterns are available, remote already connected audio
		}
		break;

		/* CALL has no pattern available */
		case MESSAGE_NOPATTERN: /* indicating no pattern available */
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) epoint with terminal '%s' (caller id '%s') received pattern NOT available.\n", ea_endpoint->ep_serial, e_terminal, e_callerinfo.id);
		if (e_call_pattern)
		{
			PDEBUG(DEBUG_EPOINT, "-> pattern becomes unavailable\n");
			e_call_pattern = 0;
			/* disconnect our audio tx and rx */
			message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_CHANNEL);
			message->param.channel = CHANNEL_STATE_HOLD;
			message_put(message);
		}
		break;

#if 0
		/* CALL (dunno at the moment) */
		case MESSAGE_REMOTE_AUDIO:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) epoint with terminal '%s' (caller id '%s') received audio remote request.\n", ea_endpoint->ep_serial, e_terminal, e_callerinfo.id);
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_CHANNEL);
		message->param.channel = param->channel;
		message_put(message);
		break;
#endif

		/* CALL sends a notify message */
		case MESSAGE_NOTIFY:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) epoint with terminal '%s' (caller id '%s') received notify.\n", ea_endpoint->ep_serial, e_terminal, e_callerinfo.id);
		call_notify(portlist, message_type, param);
		break;

		default:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) epoint with terminal '%s' (caller id '%s') received a wrong message: #%d\n", ea_endpoint->ep_serial, e_terminal, e_callerinfo.id, message);
	}
}


/* pick_call will connect the first incoming call found. the endpoint
 * will receivce a MESSAGE_CONNECT.
 */
int match_list(char *list, char *item)
{
	char *end, *next = NULL;

	/* no list make matching */
	if (!list)
		return(1);

	while(42)
	{
		/* eliminate white spaces */
		while (*list <= ' ')
			list++;
		if (*list == ',')
		{
			list++;
			continue;
		}
		/* if end of list is reached, we return */
		if (list[0] == '\0')
			return(0);
		/* if we have more than one entry (left) */
		if ((end = strchr(list, ',')))
			next = end + 1;
		else
			next = end = strchr(list, '\0');
		while (*(end-1) <= ' ')
			end--;
		/* if string part matches item */
		if (!strncmp(list, item, end-list))
			return(1);
		list = next;
	}
}

void EndpointAppPBX::pick_call(char *extensions)
{
	struct message *message;
	struct port_list *portlist;
	class Port *port;
	class EndpointAppPBX *eapp, *found;
	class Call *call;
	class CallPBX *callpbx;
	struct call_relation *relation;
	int vbox;

	/* find an endpoint that is ringing internally or vbox with higher priority */
	vbox = 0;
	found = NULL;
	eapp = apppbx_first;
	while(eapp)
	{
		if (eapp!=this && ea_endpoint->ep_portlist)
		{
			portlist = eapp->ea_endpoint->ep_portlist;
			while(portlist)
			{
				if ((port = find_port_id(portlist->port_id)))
				{
					if (port->p_type == PORT_TYPE_VBOX_OUT)
					{
						if (match_list(extensions, eapp->e_terminal))
						{
							found = eapp;
							vbox = 1;
							break;
						}
					}
					if ((port->p_type==PORT_TYPE_DSS1_NT_OUT || port->p_type==PORT_TYPE_DSS1_TE_OUT)
					 && port->p_state==PORT_STATE_OUT_ALERTING)
						if (match_list(extensions, eapp->e_terminal))
						{
							found = eapp;
						}
				}
				portlist = portlist->next;
			}
			if (portlist)
				break;
		}
		eapp = eapp->next;
	}

	/* if no endpoint found */
	if (!found)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) nobody is ringing internally (or we don't have her in the access list), so we disconnect.\n", ea_endpoint->ep_serial);
reject:
		set_tone(ea_endpoint->ep_portlist, "cause_10");
		message_disconnect_port(ea_endpoint->ep_portlist, CAUSE_NORMAL, LOCATION_PRIVATE_LOCAL, "");
		new_state(EPOINT_STATE_OUT_DISCONNECT);
		return;
	}
	eapp = found;

	if (ea_endpoint->ep_call_id)
	{
		PERROR("EPOINT(%d) we already have a call. SOFTWARE ERROR.\n", ea_endpoint->ep_serial);
		goto reject;
	}
	if (!eapp->ea_endpoint->ep_call_id)
	{
		PERROR("EPOINT(%d) ringing endpoint has no call.\n", ea_endpoint->ep_serial);
		goto reject;
	}
	call = find_call_id(eapp->ea_endpoint->ep_call_id);
	if (!call)
	{
		PERROR("EPOINT(%d) ringing endpoint's call not found.\n", ea_endpoint->ep_serial);
		goto reject;
	}
	if (callpbx->c_type != CALL_TYPE_PBX)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) ringing endpoint's call is not a PBX call, so we must reject.\n", ea_endpoint->ep_serial);
		goto reject;
	}
	callpbx = (class CallPBX *)call;
	relation = callpbx->c_relation;
	if (!relation)
	{
		PERROR("EPOINT(%d) ringing endpoint's call has no relation. SOFTWARE ERROR.\n", ea_endpoint->ep_serial);
		goto reject;
	}
	while (relation->epoint_id != eapp->ea_endpoint->ep_serial)
	{
		relation = relation->next;
		if (!relation)
		{
			PERROR("EPOINT(%d) ringing endpoint's call has no relation to that call. SOFTWARE ERROR.\n", ea_endpoint->ep_serial);
			goto reject;
		}
	}

	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) found ringing endpoint: %d.\n", ea_endpoint->ep_serial, eapp->ea_endpoint->ep_serial);

	if (options.deb & DEBUG_EPOINT)
	{
		class Call *debug_c = call_first;
		class Endpoint *debug_e = epoint_first;
		class Port *debug_p = port_first;

		callpbx_debug(callpbx, "EndpointAppPBX::pick_call(before)");

		PDEBUG(DEBUG_EPOINT, "showing all calls:\n");
		while(debug_c)
		{
			PDEBUG(DEBUG_EPOINT, "call=%ld\n", debug_c->c_serial);
			debug_c = debug_c->next;
		}
		PDEBUG(DEBUG_EPOINT, "showing all endpoints:\n");
		while(debug_e)
		{
			PDEBUG(DEBUG_EPOINT, "ep=%ld, call=%ld\n", debug_e->ep_serial, debug_e->ep_call_id);
			debug_e = debug_e->next;
		}
		PDEBUG(DEBUG_EPOINT, "showing all ports:\n");
		while(debug_p)
		{
			PDEBUG(DEBUG_EPOINT, "port=%ld, ep=%ld (active)\n", debug_p->p_serial, ACTIVE_EPOINT(debug_p->p_epointlist));
			debug_p = debug_p->next;
		}
	}

	/* relink call */
	ea_endpoint->ep_call_id = eapp->ea_endpoint->ep_call_id; /* we get the call */
	relation->epoint_id = ea_endpoint->ep_serial; /* the call gets us */
	eapp->ea_endpoint->ep_call_id = 0; /* the ringing endpoint will get disconnected */

	/* connnecting our endpoint */
	new_state(EPOINT_STATE_CONNECT);
	e_dtmf = 1;
	set_tone(ea_endpoint->ep_portlist, NULL);

	/* now we send a release to the ringing endpoint */
	message = message_create(ea_endpoint->ep_call_id, eapp->ea_endpoint->ep_serial, CALL_TO_EPOINT, MESSAGE_RELEASE);
	message->param.disconnectinfo.cause = 26; /* non selected user clearing */
	message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
	message_put(message);

	/* we send a connect to the call with our caller id */
	message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_CONNECT);
	SCPY(message->param.connectinfo.id, e_callerinfo.id);
	message->param.connectinfo.present = e_callerinfo.present;
	message->param.connectinfo.screen = e_callerinfo.screen;
	message->param.connectinfo.itype = e_callerinfo.itype;
	message->param.connectinfo.ntype = e_callerinfo.ntype;
	message_put(message);

	/* we send a connect to our port with the remote callerid */
	message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_portlist->port_id, EPOINT_TO_PORT, MESSAGE_CONNECT);
	SCPY(message->param.connectinfo.id, eapp->e_callerinfo.id);
	message->param.connectinfo.present = eapp->e_callerinfo.present;
	message->param.connectinfo.screen = eapp->e_callerinfo.screen;
	message->param.connectinfo.itype = eapp->e_callerinfo.itype;
	message->param.connectinfo.ntype = eapp->e_callerinfo.ntype;
	/* handle restricted caller ids */
	apply_callerid_restriction(e_ext.anon_ignore, ea_endpoint->ep_portlist->port_type, message->param.connectinfo.id, &message->param.connectinfo.ntype, &message->param.connectinfo.present, &message->param.connectinfo.screen, message->param.connectinfo.voip, message->param.connectinfo.intern, message->param.connectinfo.name);
	/* display callerid if desired for extension */
	SCPY(message->param.connectinfo.display, apply_callerid_display(message->param.connectinfo.id, message->param.connectinfo.itype,  message->param.connectinfo.ntype, message->param.connectinfo.present, message->param.connectinfo.screen, message->param.connectinfo.voip, message->param.connectinfo.intern, message->param.connectinfo.name));
	message_put(message);

	/* we send a connect to the audio path (not for vbox) */
	message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_CHANNEL);
	message->param.channel = CHANNEL_STATE_CONNECT;
	message_put(message);

	/* beeing paranoid, we make call update */
	callpbx->c_mixer = 1;

	if (options.deb & DEBUG_EPOINT)
	{
		class Call *debug_c = call_first;
		class Endpoint *debug_e = epoint_first;
		class Port *debug_p = port_first;

		callpbx_debug(callpbx, "EndpointAppPBX::pick_call(after)");

		PDEBUG(DEBUG_EPOINT, "showing all calls:\n");
		while(debug_c)
		{
			PDEBUG(DEBUG_EPOINT, "call=%ld\n", debug_c->c_serial);
			debug_c = debug_c->next;
		}
		PDEBUG(DEBUG_EPOINT, "showing all endpoints:\n");
		while(debug_e)
		{
			PDEBUG(DEBUG_EPOINT, "ep=%ld, call=%ld\n", debug_e->ep_serial, debug_e->ep_call_id);
			debug_e = debug_e->next;
		}
		PDEBUG(DEBUG_EPOINT, "showing all ports:\n");
		while(debug_p)
		{
			PDEBUG(DEBUG_EPOINT, "port=%ld, ep=%ld\n", debug_p->p_serial, ACTIVE_EPOINT(debug_p->p_epointlist));
			debug_p = debug_p->next;
		}
	}
}


/* join calls (look for a call that is on hold (same isdn interface/terminal))
 */
void EndpointAppPBX::join_call(void)
{
	struct message *message;
	struct call_relation *our_relation, *other_relation;
	struct call_relation **our_relation_pointer, **other_relation_pointer;
	class Call *our_call, *other_call;
	class CallPBX *our_callpbx, *other_callpbx;
	class EndpointAppPBX *other_eapp; class Endpoint *temp_epoint;
	class Port *our_port, *other_port;
	class Pdss1 *our_pdss1, *other_pdss1;

	/* are we a candidate to join a call */
	our_call = find_call_id(ea_endpoint->ep_call_id);
	if (!our_call)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) cannot join: our call doesn't exist anymore.\n", ea_endpoint->ep_serial);
		return;
	}
	if (our_call->c_type != CALL_TYPE_PBX)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) cannot join: call is not a pbx call.\n", ea_endpoint->ep_serial);
		return;
	}
	our_callpbx = (class CallPBX *)our_call;
	if (!ea_endpoint->ep_portlist)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) cannot join: we have no port.\n", ea_endpoint->ep_serial);
		return;
	}
	if (!e_terminal[0])
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) cannot join: we are not internal extension.\n", ea_endpoint->ep_serial);
		return;
	}
	our_port = find_port_id(ea_endpoint->ep_portlist->port_id);
	if (!our_port)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) cannot join: our port doesn't exist anymore.\n", ea_endpoint->ep_serial);
		return;
	}
	if ((our_port->p_type&PORT_CLASS_mISDN_MASK) != PORT_CLASS_mISDN_DSS1)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) cannot join: our port is not isdn.\n", ea_endpoint->ep_serial);
		return;
	}
	our_pdss1 = (class Pdss1 *)our_port;

	/* find an endpoint that is on hold and has the same mISDNport that we are on */
	other_eapp = apppbx_first;
	while(other_eapp)
	{
		if (other_eapp == this)
		{
			other_eapp = other_eapp->next;
			continue;
		}
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) comparing other endpoint candiate: (ep%d) terminal='%s' port=%s call=%d.\n", ea_endpoint->ep_serial, other_eapp->ea_endpoint->ep_serial, other_eapp->e_terminal, (other_eapp->ea_endpoint->ep_portlist)?"YES":"NO", other_eapp->ea_endpoint->ep_call_id);
		if (other_eapp->e_terminal[0] /* has terminal */
		 && other_eapp->ea_endpoint->ep_portlist /* has port */
		 && other_eapp->ea_endpoint->ep_call_id) /* has call */
		{
			other_port = find_port_id(other_eapp->ea_endpoint->ep_portlist->port_id);
			if (other_port) /* port still exists */
			{
				if (other_port->p_type==PORT_TYPE_DSS1_NT_OUT
				 || other_port->p_type==PORT_TYPE_DSS1_NT_IN) /* port is isdn nt-mode */
				{
					other_pdss1 = (class Pdss1 *)other_port;
					PDEBUG(DEBUG_EPOINT, "EPOINT(%d) comparing other endpoint's port is of type isdn! comparing our portnum=%d with other's portnum=%d hold=%s ces=%d\n", ea_endpoint->ep_serial, our_pdss1->p_m_mISDNport->portnum, other_pdss1->p_m_mISDNport->portnum, (other_pdss1->p_m_hold)?"YES":"NO", other_pdss1->p_m_d_ces);
					if (other_pdss1->p_m_hold /* port is on hold */
					 && other_pdss1->p_m_mISDNport == our_pdss1->p_m_mISDNport /* same isdn interface */
					 && other_pdss1->p_m_d_ces == our_pdss1->p_m_d_ces) /* same tei+sapi */
						break;
				} else
				{
					PDEBUG(DEBUG_EPOINT, "EPOINT(%d) comparing other endpoint's port is of other type!\n", ea_endpoint->ep_serial);
				}
			} else
			{
				PDEBUG(DEBUG_EPOINT, "EPOINT(%d) comparing other endpoint's port doesn't exist enymore.\n", ea_endpoint->ep_serial);
			}
		}
		other_eapp = other_eapp->next;
	}
	if (!other_eapp)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) cannot join: no other endpoint on same isdn interface with port on hold.\n", ea_endpoint->ep_serial);
		return;
	}
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) port on hold found.\n", ea_endpoint->ep_serial);

	/* if we have the same call */
	if (other_eapp->ea_endpoint->ep_call_id == ea_endpoint->ep_call_id)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) cannot join: we an the other have the same call.\n", ea_endpoint->ep_serial);
		return;
	}
	other_call = find_call_id(other_eapp->ea_endpoint->ep_call_id);
	if (!other_call)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) cannot join: other call doesn't exist anymore.\n", ea_endpoint->ep_serial);
		return;
	}
	if (other_call->c_type != CALL_TYPE_PBX)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) cannot join: other call is not a pbx call.\n", ea_endpoint->ep_serial);
		return;
	}
	other_callpbx = (class CallPBX *)other_call;
	if (our_callpbx->c_partyline && other_callpbx->c_partyline)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) cannot join: both calls are partylines.\n", ea_endpoint->ep_serial);
		return;
	}

	/* remove relation to endpoint for call on hold */
	other_relation = other_callpbx->c_relation;
	other_relation_pointer = &other_callpbx->c_relation;
	while(other_relation)
	{
		if (other_relation->epoint_id == other_eapp->ea_endpoint->ep_serial)
		{
		/* detach other endpoint on hold */
			*other_relation_pointer = other_relation->next;
			memset(other_relation, 0, sizeof(struct call_relation));
			free(other_relation);
			cmemuse--;
			other_relation = *other_relation_pointer;
			other_eapp->ea_endpoint->ep_call_id = NULL;
			continue;
		}

		/* change call/hold pointer of endpoint to the new call */
		temp_epoint = find_epoint_id(other_relation->epoint_id);
		if (temp_epoint)
		{
			if (temp_epoint->ep_call_id == other_call->c_serial)
				temp_epoint->ep_call_id = our_call->c_serial;
		}

		other_relation_pointer = &other_relation->next;
		other_relation = other_relation->next;
	}
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) endpoint on hold removed, other enpoints on call relinked (to our call).\n", ea_endpoint->ep_serial);

	/* join call relations */
	our_relation = our_callpbx->c_relation;
	our_relation_pointer = &our_callpbx->c_relation;
	while(our_relation)
	{
		our_relation_pointer = &our_relation->next;
		our_relation = our_relation->next;
	}
	*our_relation_pointer = other_callpbx->c_relation;
	other_callpbx->c_relation = NULL;
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) relations joined.\n", ea_endpoint->ep_serial);

	/* release endpoint on hold */
	message = message_create(other_callpbx->c_serial, other_eapp->ea_endpoint->ep_serial, CALL_TO_EPOINT, MESSAGE_RELEASE);
	message->param.disconnectinfo.cause = CAUSE_NORMAL; /* normal */
	message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
	message_put(message);
	
	/* if we are not a partyline, we get partyline state from other call */
	our_callpbx->c_partyline += other_callpbx->c_partyline; 

	/* remove empty call */
	delete other_call;
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d)d-call completely removed!\n");

	/* mixer must update */
	our_callpbx->c_mixer = 1; /* update mixer flag */

	/* we send a retrieve to that endpoint */
	// mixer will update the hold-state of the call and send it to the endpoints is changes
}


/* check if we have an external call
 * this is used to check for encryption ability
 */
int EndpointAppPBX::check_external(char **errstr, class Port **port)
{
	struct call_relation *relation;
	class Call *call;
	class CallPBX *callpbx;
	class Endpoint *epoint;

	/* some paranoia check */
	if (!ea_endpoint->ep_portlist)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) error: we have no port.\n", ea_endpoint->ep_serial);
		*errstr = "No Call";
		return(1);
	}
	if (!e_terminal[0])
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) error: we are not internal extension.\n", ea_endpoint->ep_serial);
		*errstr = "No Call";
		return(1);
	}

	/* check if we have a call with 2 parties */
	call = find_call_id(ea_endpoint->ep_call_id);
	if (!call)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) we have currently no call.\n", ea_endpoint->ep_serial);
		*errstr = "No Call";
		return(1);
	}
	if (call->c_type != CALL_TYPE_PBX)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) call is nto a pbx call.\n", ea_endpoint->ep_serial);
		*errstr = "No PBX Call";
		return(1);
	}
	callpbx = (class CallPBX *)call;
	relation = callpbx->c_relation;
	if (!relation)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) call has no relation.\n", ea_endpoint->ep_serial);
		*errstr = "No Call";
		return(1);
	}
	if (!relation->next)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) call has no 2nd relation.\n", ea_endpoint->ep_serial);
		*errstr = "No Call";
		return(1);
	}
	if (relation->next->next)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) call has more than two relations.\n", ea_endpoint->ep_serial);
		*errstr = "Err: Conference";
		return(1);
	}
	if (relation->epoint_id == ea_endpoint->ep_serial)
	{
		relation = relation->next;
		if (relation->epoint_id == ea_endpoint->ep_serial)
		{
			PERROR("EPOINT(%d) SOFTWARE ERROR: both call relations are related to our endpoint.\n", ea_endpoint->ep_serial);
			*errstr = "Software Error";
			return(1);
		}
	}

	/* check remote port for external call */
	epoint = find_epoint_id(relation->epoint_id);
	if (!epoint)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) call has no 2nd endpoint.\n", ea_endpoint->ep_serial);
		*errstr = "No Call";
		return(1);
	}
	if (!epoint->ep_portlist)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) 2nd endpoint has not port.\n", ea_endpoint->ep_serial);
		*errstr = "No Call";
		return(1);
	}
	*port = find_port_id(epoint->ep_portlist->port_id);
	if (!(*port))
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) 2nd endpoint has an none existing port.\n", ea_endpoint->ep_serial);
		*errstr = "No Call";
		return(1);
	}
	if (((*port)->p_type&PORT_CLASS_mISDN_MASK)!=PORT_CLASS_mISDN_DSS1) /* port is not external isdn */
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) 2nd endpoint has not an external port.\n", ea_endpoint->ep_serial);
		*errstr = "No Ext Call";
		return(1);
	}
	if ((*port)->p_state != PORT_STATE_CONNECT)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) 2nd endpoint's port is not in connected state.\n", ea_endpoint->ep_serial);
		*errstr = "No Ext Connect";
		return(1);
	}
	return(0);
}

void EndpointAppPBX::logmessage(struct message *message)
{
	class Port *port;
	class Pdss1 *pdss1;
	char *logtext = "unknown";
	char buffer[64];

	if (message->flow != EPOINT_TO_PORT)
	{
		PERROR("EPOINT(%d) message not of correct flow type-\n", ea_endpoint->ep_serial);
		return;
	}

	switch(message->type)
	{
		case MESSAGE_SETUP:
		port = find_port_id(message->id_to);
		if (!port)
			return;
		if (port->p_type == PORT_TYPE_DSS1_NT_OUT)
		{
			pdss1 = (class Pdss1 *)port;
			printlog("%3d  outgoing SETUP from %s='%s'%s%s%s%s to intern='%s' port='%d' (NT)\n",
				ea_endpoint->ep_serial,
				(message->param.setup.callerinfo.intern[0])?"intern":"extern",
				(message->param.setup.callerinfo.intern[0])?e_callerinfo.intern:e_callerinfo.id,
				(message->param.setup.callerinfo.present==INFO_PRESENT_RESTRICTED)?" anonymous":"",
				(message->param.setup.redirinfo.id[0])?"redirected='":"",
				message->param.setup.redirinfo.id,
				(message->param.setup.redirinfo.id[0])?"'":"",
				message->param.setup.dialinginfo.number,
				pdss1->p_m_mISDNport->portnum
				);
		}
		if (port->p_type == PORT_TYPE_DSS1_TE_OUT)
		{
			pdss1 = (class Pdss1 *)port;
			printlog("%3d  outgoing SETUP from %s='%s'%s%s%s%s to extern='%s' port='%d' (TE)\n",
				ea_endpoint->ep_serial,
				(message->param.setup.callerinfo.intern[0])?"intern":"extern",
				(message->param.setup.callerinfo.intern[0])?e_callerinfo.intern:e_callerinfo.id,
				(message->param.setup.callerinfo.present==INFO_PRESENT_RESTRICTED)?" anonymous":"",
				(message->param.setup.redirinfo.id[0])?"redirected='":"",
				message->param.setup.redirinfo.id,
				(message->param.setup.redirinfo.id[0])?"'":"",
				message->param.setup.dialinginfo.number,
				pdss1->p_m_mISDNport->portnum
				);
		}
		if (port->p_type == PORT_TYPE_VBOX_OUT)
		{
			printlog("%3d  outgoing SETUP from %s='%s'%s%s%s%s to vbox='%s'\n",
				ea_endpoint->ep_serial,
				(message->param.setup.callerinfo.intern[0])?"intern":"extern",
				(message->param.setup.callerinfo.intern[0])?e_callerinfo.intern:e_callerinfo.id,
				(message->param.setup.callerinfo.present==INFO_PRESENT_RESTRICTED)?" anonymous":"",
				(message->param.setup.redirinfo.id[0])?"redirected='":"",
				message->param.setup.redirinfo.id,
				(message->param.setup.redirinfo.id[0])?"'":"",
				message->param.setup.dialinginfo.number
				);
		}
		break;

		case MESSAGE_OVERLAP:
		printlog("%3d  outgoing SETUP ACKNOWLEDGE\n",
			ea_endpoint->ep_serial
			);
		break;

		case MESSAGE_PROCEEDING:
		printlog("%3d  outgoing PROCEEDING\n",
			ea_endpoint->ep_serial
			);
		break;

		case MESSAGE_ALERTING:
		printlog("%3d  outgoing ALERTING\n",
			ea_endpoint->ep_serial
			);
		break;

		case MESSAGE_CONNECT:
		printlog("%3d  outgoing CONNECT id='%s'%s\n",
			ea_endpoint->ep_serial,
			message->param.connectinfo.id,
			(message->param.connectinfo.present==INFO_PRESENT_RESTRICTED)?" anonymous":""
		);
		break;

		case MESSAGE_DISCONNECT:
		printlog("%3d  outgoing DISCONNECT cause='%d' (%s) location='%d' (%s) display='%s'\n",
			ea_endpoint->ep_serial,
			message->param.disconnectinfo.cause,
			(message->param.disconnectinfo.cause>0 && message->param.disconnectinfo.cause<128)?isdn_cause[message->param.disconnectinfo.cause].english:"-",
			message->param.disconnectinfo.location,
			(message->param.disconnectinfo.location>=0 && message->param.disconnectinfo.location<16)?isdn_location[message->param.disconnectinfo.location].english:"-",
			message->param.disconnectinfo.display
			);
		break;

		case MESSAGE_RELEASE:
		printlog("%3d  outgoing RELEASE cause='%d' (%s) location='%d' (%s)\n",
			ea_endpoint->ep_serial,
			message->param.disconnectinfo.cause,
			(message->param.disconnectinfo.cause>0 && message->param.disconnectinfo.cause<128)?isdn_cause[message->param.disconnectinfo.cause].english:"-",
			message->param.disconnectinfo.location,
			(message->param.disconnectinfo.location>=0 && message->param.disconnectinfo.location<16)?isdn_location[message->param.disconnectinfo.location].english:"-"
			);
		break;

		case MESSAGE_NOTIFY:
		switch(message->param.notifyinfo.notify)
		{
			case 0x00:
			logtext = "NULL";
			break;
			case 0x80:
			logtext = "USER_SUSPENDED";
			break;
			case 0x82:
			logtext = "BEARER_SERVICE_CHANGED";
			break;
			case 0x81:
			logtext = "USER_RESUMED";
			break;
			case 0xc2:
			logtext = "CONFERENCE_ESTABLISHED";
			break;
			case 0xc3:
			logtext = "CONFERENCE_DISCONNECTED";
			break;
			case 0xc4:
			logtext = "OTHER_PARTY_ADDED";
			break;
			case 0xc5:
			logtext = "ISOLATED";
			break;
			case 0xc6:
			logtext = "REATTACHED";
			break;
			case 0xc7:
			logtext = "OTHER_PARTY_ISOLATED";
			break;
			case 0xc8:
			logtext = "OTHER_PARTY_REATTACHED";
			break;
			case 0xc9:
			logtext = "OTHER_PARTY_SPLIT";
			break;
			case 0xca:
			logtext = "OTHER_PARTY_DISCONNECTED";
			break;
			case 0xcb:
			logtext = "CONFERENCE_FLOATING";
			break;
			case 0xcc:
			logtext = "CONFERENCE_DISCONNECTED_PREEMTED";
			break;
			case 0xcf:
			logtext = "CONFERENCE_FLOATING_SERVED_USER_PREEMTED";
			break;
			case 0xe0:
			logtext = "CALL_IS_A_WAITING_CALL";
			break;
			case 0xe8:
			logtext = "DIVERSION_ACTIVATED";
			break;
			case 0xe9:
			logtext = "RESERVED_CT_1";
			break;
			case 0xea:
			logtext = "RESERVED_CT_2";
			break;
			case 0xee:
			logtext = "REVERSE_CHARGING";
			break;
			case 0xf9:
			logtext = "REMOTE_HOLD";
			break;
			case 0xfa:
			logtext = "REMOTE_RETRIEVAL";
			break;
			case 0xfb:
			logtext = "CALL_IS_DIVERTING";
			break;
			default:
			SPRINT(buffer, "indicator=%d", message->param.notifyinfo.notify - 0x80);
			logtext = buffer;

		}
		printlog("%3d  outgoing NOTIFY notify='%s' id='%s'%s display='%s'\n",
			ea_endpoint->ep_serial,
			logtext,
			message->param.notifyinfo.id,
			(message->param.notifyinfo.present==INFO_PRESENT_RESTRICTED)?" anonymous":"",
			message->param.notifyinfo.display
			);
		break;

		case MESSAGE_INFORMATION:
		printlog("%3d  outgoing INFORMATION information='%s'\n",
			ea_endpoint->ep_serial,
			message->param.information.number
			);
		break;

		case MESSAGE_FACILITY:
		printlog("%3d  outgoing FACILITY len='%d'\n",
			ea_endpoint->ep_serial,
			message->param.facilityinfo.len
			);
		break;

		case MESSAGE_TONE:
		printlog("%3d  outgoing TONE dir='%s' name='%s'\n",
			ea_endpoint->ep_serial,
			message->param.tone.dir,
			message->param.tone.name
			);
		break;

		default:
		PERROR("EPOINT(%d) message not of correct type (%d)\n", ea_endpoint->ep_serial, message->type);
	}
}

void EndpointAppPBX::message_disconnect_port(struct port_list *portlist, int cause, int location, char *display)
{
	struct message *message;

	if (!portlist)
		return;
	if (!portlist->port_id)
		return;

	if (!e_connectedmode)
	{
		message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_DISCONNECT);
		message->param.disconnectinfo.cause = cause;
		message->param.disconnectinfo.location = location;
		if (display[0])
			SCPY(message->param.disconnectinfo.display, display);
		else
			SCPY(message->param.disconnectinfo.display, get_isdn_cause(cause, location, e_ext.display_cause));
	} else
	{
		message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_NOTIFY);
		if (display[0])
			SCPY(message->param.notifyinfo.display, display);
		else
			SCPY(message->param.notifyinfo.display, get_isdn_cause(cause, location, e_ext.display_cause));
	}
	message_put(message);
	logmessage(message);
}
