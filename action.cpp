/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** all actions (and hangup) are processed here                               **
**                                                                           **
\*****************************************************************************/ 

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
#include "main.h"
#include "linux/isdnif.h"

extern char **environ;


/* create caller id from digits by comparing with national and international
 * prefixes.
 */
char *nationalize_callerinfo(char *string, int *ntype)
{
	if (!strncmp(options.international, string, strlen(options.international)))
	{
		*ntype = INFO_NTYPE_INTERNATIONAL;
		return(string+strlen(options.international)); 
	}
	if (!strncmp(options.national, string, strlen(options.national)))
	{
		*ntype = INFO_NTYPE_NATIONAL;
		return(string+strlen(options.national)); 
	}
	*ntype = INFO_NTYPE_SUBSCRIBER;
	return(string);
}

/* create number (including access codes) from caller id
 * prefixes.
 */
char *numberrize_callerinfo(char *string, int ntype)
{
	static char result[256];

	switch(ntype)
	{
		case INFO_NTYPE_INTERNATIONAL:
		UCPY(result, options.international);
		SCAT(result, string);
		return(result);
		break;

		case INFO_NTYPE_NATIONAL:
		UCPY(result, options.national);
		SCAT(result, string);
		return(result);
		break;

		default:
		return(string);
	}
}


/*
 * process init 'internal' / 'external' / 'chan' / 'vbox-record' / 'partyline'...
 */
void EndpointAppPBX::_action_init_call(int chan)
{
	class Call		*call;
	struct port_list	*portlist = ea_endpoint->ep_portlist;

	/* a created call, this should never happen */
	if (ea_endpoint->ep_call_id)
	{
		if (options.deb & DEBUG_EPOINT)
			PERROR("EPOINT(%d): We already have a call instance, this should never happen!\n", ea_endpoint->ep_serial);
		return;
	}

	/* create call */
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d): Creating new call instance.\n", ea_endpoint->ep_serial);
	if (chan)
		call = new CallChan(ea_endpoint);
	else
		call = new CallPBX(ea_endpoint);
	if (!call)
	{
		/* resource not available */
		message_disconnect_port(portlist, CAUSE_RESSOURCEUNAVAIL, LOCATION_PRIVATE_LOCAL, "");
		new_state(EPOINT_STATE_OUT_DISCONNECT);
		set_tone(portlist,"cause_22");
		return;
	}
	ea_endpoint->ep_call_id = call->c_serial;
}
void EndpointAppPBX::action_init_call(void)
{
	_action_init_call(0);
}
void EndpointAppPBX::action_init_chan(void)
{
	_action_init_call(1);
}

/*
 * process dialing 'internal'
 */
void EndpointAppPBX::action_dialing_internal(void)
{
	struct capa_info	capainfo;
	struct caller_info	callerinfo;
	struct redir_info	redirinfo;
	struct dialing_info	dialinginfo;
	struct port_list	*portlist = ea_endpoint->ep_portlist;
	struct message		*message;
	struct extension	ext;
	struct route_param	*rparam;

	/* send proceeding, because number is complete */
	set_tone(portlist, "proceeding");
	message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_PROCEEDING);
	message_put(message);
	logmessage(message);
	new_state(EPOINT_STATE_IN_PROCEEDING);

	/* create bearer/caller/dialinginfo */
	memcpy(&capainfo, &e_capainfo, sizeof(capainfo));
	memcpy(&callerinfo, &e_callerinfo, sizeof(callerinfo));
	memcpy(&redirinfo, &e_redirinfo, sizeof(redirinfo));
	memset(&dialinginfo, 0, sizeof(dialinginfo));
	dialinginfo.itype = INFO_ITYPE_INTERN;
	SCPY(dialinginfo.number, e_dialinginfo.number);

	/* process extension */
	if ((rparam = routeparam(e_action, PARAM_EXTENSION)))
		SCPY(dialinginfo.number, rparam->string_value);

	/* process number type */
	if ((rparam = routeparam(e_action, PARAM_TYPE)))
		dialinginfo.ntype = rparam->integer_value;

	/* process service */
	if ((rparam = routeparam(e_action, PARAM_CAPA)))
	{
		capainfo.bearer_capa = rparam->integer_value;
		if (capainfo.bearer_capa != INFO_BC_SPEECH
		 && capainfo.bearer_capa != INFO_BC_AUDIO)
		{
			capainfo.bearer_mode = INFO_BMODE_PACKET;
		}
		capainfo.bearer_info1 = INFO_INFO1_NONE;
	}
	if ((rparam = routeparam(e_action, PARAM_BMODE)))
	{
		capainfo.bearer_mode = rparam->integer_value;
	}
	if ((rparam = routeparam(e_action, PARAM_INFO1)))
	{
		capainfo.bearer_info1 = rparam->integer_value;
	}
	if ((rparam = routeparam(e_action, PARAM_HLC)))
	{
		capainfo.hlc = rparam->integer_value;
	}
	if ((rparam = routeparam(e_action, PARAM_EXTHLC)))
	{
		capainfo.exthlc = rparam->integer_value;
	}

	/* process presentation */
	if ((rparam = routeparam(e_action, PARAM_PRESENT)))
	{
		callerinfo.present = (rparam->integer_value)?INFO_PRESENT_ALLOWED:INFO_PRESENT_RESTRICTED;
	}

	/* check if extension exists AND only if not multiple extensions */
	if (!read_extension(&ext, dialinginfo.number) && !strchr(dialinginfo.number,','))
	{
		printlog("%3d  action   INTERN dialed extension %s doesn't exist.\n", ea_endpoint->ep_serial, dialinginfo.number);
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): extension %s doesn't exist\n", ea_endpoint->ep_serial, dialinginfo.number);
		new_state(EPOINT_STATE_OUT_DISCONNECT);
		message_disconnect_port(portlist, CAUSE_UNALLOCATED, LOCATION_PRIVATE_LOCAL, "");
		set_tone(portlist, "cause_86");
		return;
	}
	/* check if internal calls are denied */
	if (e_ext.rights < 1)
	{
		printlog("%3d  action   INTERN access to internal phones are denied for this caller.\n", ea_endpoint->ep_serial);
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): internal call from terminal %s denied.\n", ea_endpoint->ep_serial, e_ext.number);
		new_state(EPOINT_STATE_OUT_DISCONNECT);
		message_disconnect_port(portlist, CAUSE_REJECTED, LOCATION_PRIVATE_LOCAL, "");
		set_tone(portlist, "cause_81");
		return;
	}

	/* add or update internal call */
	printlog("%3d  action   INTERN call to extension %s.\n", ea_endpoint->ep_serial, dialinginfo.number);
	message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_SETUP);
	memcpy(&message->param.setup.dialinginfo, &dialinginfo, sizeof(struct dialing_info));
	memcpy(&message->param.setup.redirinfo, &redirinfo, sizeof(struct redir_info));
	memcpy(&message->param.setup.callerinfo, &callerinfo, sizeof(struct caller_info));
	memcpy(&message->param.setup.capainfo, &capainfo, sizeof(struct capa_info));
	message_put(message);
}

/* process dialing external
 */
void EndpointAppPBX::action_dialing_external(void)
{
	struct capa_info capainfo;
	struct caller_info callerinfo;
	struct redir_info redirinfo;
	struct dialing_info dialinginfo;
	char *p;
	struct port_list *portlist = ea_endpoint->ep_portlist;
	struct message *message;
	struct route_param *rparam;

	/* special processing of delete characters '*' and '#' */
	if (e_ext.delete_ext)
	{
		/* dialing a # causes a clearing of complete number */
		if (strchr(e_extdialing, '#'))
		{
			e_extdialing[0] = '\0';
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d): '#' detected: terminal '%s' selected caller id '%s' and continues dialing: '%s'\n", ea_endpoint->ep_serial, e_ext.number, e_callerinfo.id, e_extdialing);
		}
		/* eliminate digits before '*', which is a delete digit
		 */
		if (strchr(e_extdialing, '*'))
		{
			/* remove digits */
			while((p=strchr(e_extdialing, '*')))
			{
				if (p > e_extdialing) /* only if there is a digit in front */
				{
					UCPY(p-1, p);
					p--;
				}
				UCPY(p, p+1); /* remove '*' */
			}
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d): terminal %s deleted digits and got new string: %s\n", ea_endpoint->ep_serial, e_ext.number, e_extdialing);
		}
	}

	/* create bearer/caller/dialinginfo */
	memcpy(&capainfo, &e_capainfo, sizeof(capainfo));
	memcpy(&callerinfo, &e_callerinfo, sizeof(callerinfo));
	memcpy(&redirinfo, &e_redirinfo, sizeof(redirinfo));
	memset(&dialinginfo, 0, sizeof(dialinginfo));
	dialinginfo.itype = INFO_ITYPE_EXTERN;
	dialinginfo.sending_complete = 0;
	SCPY(dialinginfo.number, e_extdialing);

	/* process prefix */
	if ((rparam = routeparam(e_action, PARAM_PREFIX)))
		SPRINT(dialinginfo.number, "%s%s", rparam->string_value, e_extdialing);

	/* process number complete */
	if ((rparam = routeparam(e_action, PARAM_COMPLETE)))
		if ((rparam = routeparam(e_action, PARAM_PREFIX)))
			SCPY(dialinginfo.number, rparam->string_value);
		dialinginfo.sending_complete = 1;

	/* process number type */
	if ((rparam = routeparam(e_action, PARAM_TYPE)))
		dialinginfo.ntype = rparam->integer_value;

	/* process service */
	if ((rparam = routeparam(e_action, PARAM_CAPA)))
	{
		capainfo.bearer_capa = rparam->integer_value;
		if (capainfo.bearer_capa != INFO_BC_SPEECH
		 && capainfo.bearer_capa != INFO_BC_AUDIO)
		{
			capainfo.bearer_mode = INFO_BMODE_PACKET;
		}
		capainfo.bearer_info1 = INFO_INFO1_NONE;
	}
	if ((rparam = routeparam(e_action, PARAM_BMODE)))
	{
		capainfo.bearer_mode = rparam->integer_value;
	}
	if ((rparam = routeparam(e_action, PARAM_INFO1)))
	{
		capainfo.bearer_info1 = rparam->integer_value;
	}
	if ((rparam = routeparam(e_action, PARAM_HLC)))
	{
		capainfo.hlc = rparam->integer_value;
	}
	if ((rparam = routeparam(e_action, PARAM_EXTHLC)))
	{
		capainfo.exthlc = rparam->integer_value;
	}


	/* process callerid */
	if ((rparam = routeparam(e_action, PARAM_CALLERID)))
	{
		SCPY(callerinfo.id, rparam->string_value);
	}
	if ((rparam = routeparam(e_action, PARAM_CALLERIDTYPE)))
	{
		callerinfo.ntype = rparam->integer_value;
	}

	/* process presentation */
	if ((rparam = routeparam(e_action, PARAM_PRESENT)))
	{
		callerinfo.present = (rparam->integer_value)?INFO_PRESENT_ALLOWED:INFO_PRESENT_RESTRICTED;
	}

	/* process interfaces */
	if ((rparam = routeparam(e_action, PARAM_INTERFACES)))
		SCPY(dialinginfo.interfaces, rparam->string_value);

	/* check if local calls are denied */
	if (e_ext.rights < 2)
	{
		printlog("%3d  action   EXTERN calls are denied for this caller.\n", ea_endpoint->ep_serial);
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): external call from terminal denied: %s\n", ea_endpoint->ep_serial, e_ext.number);
		release(RELEASE_CALL, LOCATION_PRIVATE_LOCAL, CAUSE_REJECTED, LOCATION_PRIVATE_LOCAL, 0);
		set_tone(portlist, "cause_82");
		denied:
		message_disconnect_port(portlist, CAUSE_REJECTED, LOCATION_PRIVATE_LOCAL, "");
		new_state(EPOINT_STATE_OUT_DISCONNECT);
		return;
	}

	if (!strncmp(dialinginfo.number, options.national, strlen(options.national))
	 || dialinginfo.ntype == INFO_NTYPE_NATIONAL
	 || dialinginfo.ntype == INFO_NTYPE_INTERNATIONAL)
	{
		/* check if national calls are denied */
		if (e_ext.rights < 3)
		{
			printlog("%3d  action   EXTERN national calls are denied for this caller.\n", ea_endpoint->ep_serial);
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d): national call from terminal %s denied.\n", ea_endpoint->ep_serial, e_ext.number);
			release(RELEASE_CALL, LOCATION_PRIVATE_LOCAL, CAUSE_REJECTED, LOCATION_PRIVATE_LOCAL, 0);
			set_tone(portlist, "cause_83");
			goto denied;
		}
	}

	if (!strncmp(dialinginfo.number, options.international, strlen(options.international))
	 || dialinginfo.ntype == INFO_NTYPE_INTERNATIONAL)
	{
		/* check if international calls are denied */
		if (e_ext.rights < 4)
		{
			printlog("%3d  action   EXTERN international calls are denied for this caller.\n", ea_endpoint->ep_serial);
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d): international call from terminal %s denied.\n", ea_endpoint->ep_serial, e_ext.number);
			release(RELEASE_CALL, LOCATION_PRIVATE_LOCAL, CAUSE_REJECTED, LOCATION_PRIVATE_LOCAL, 0);
			set_tone(portlist, "cause_84");
			goto denied;
		}
	}

	/* add or update outgoing call */
	printlog("%3d  action   EXTERN call to destination %s.\n", ea_endpoint->ep_serial, dialinginfo.number);
	message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_SETUP);
	memcpy(&message->param.setup.dialinginfo, &dialinginfo, sizeof(struct dialing_info));
	memcpy(&message->param.setup.redirinfo, &redirinfo, sizeof(struct redir_info));
	memcpy(&message->param.setup.callerinfo, &callerinfo, sizeof(struct caller_info));
	memcpy(&message->param.setup.capainfo, &capainfo, sizeof(struct capa_info));
	message_put(message);
}


void EndpointAppPBX::action_dialing_chan(void)
{
	struct port_list *portlist = ea_endpoint->ep_portlist;

	printlog("%3d  action   channel API not implemented.\n", ea_endpoint->ep_serial);
	message_disconnect_port(portlist, CAUSE_UNIMPLEMENTED, LOCATION_PRIVATE_LOCAL, "");
	new_state(EPOINT_STATE_OUT_DISCONNECT);
	set_tone(portlist,"cause_4f");
}


/*
 * process dialing the "am" and record
 */
void EndpointAppPBX::action_dialing_vbox_record(void)
{
	struct dialing_info dialinginfo;
	struct port_list *portlist = ea_endpoint->ep_portlist;
	struct message *message;
	struct extension ext;
	struct route_param *rparam;

	portlist = ea_endpoint->ep_portlist;

	/* check for given extension */
	if (!(rparam = routeparam(e_action, PARAM_EXTENSION)))
	{
		printlog("%3d  action   VBOX-RECORD extension not given by parameter.\n", ea_endpoint->ep_serial);
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): cannot record, because no 'extension' parameter has been specified.\n", ea_endpoint->ep_serial);

		new_state(EPOINT_STATE_OUT_DISCONNECT);
		message_disconnect_port(portlist, CAUSE_SERVICEUNAVAIL, LOCATION_PRIVATE_LOCAL, "");
		set_tone(portlist, "cause_3f");
		return;
	}

	/* check if extension exists */
	if (!read_extension(&ext, rparam->string_value))
	{
		printlog("%3d  action   VBOX-RECORD given extension %s doesn't exist.\n", ea_endpoint->ep_serial, rparam->string_value);
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): extension %s doesn't exist\n", ea_endpoint->ep_serial, rparam->string_value);
		new_state(EPOINT_STATE_OUT_DISCONNECT);
		message_disconnect_port(portlist, CAUSE_UNALLOCATED, LOCATION_PRIVATE_LOCAL, "");
		set_tone(portlist, "cause_86");
		return;
	}

	PDEBUG(DEBUG_EPOINT, "EPOINT(%d): terminal %s dialing extension: %s\n", ea_endpoint->ep_serial, e_ext.number, rparam->string_value);

	/* check if internal calls are denied */
	if (e_ext.rights < 1)
	{
		printlog("%3d  action   VBOX-RECORD calls are denied for this caller.\n", ea_endpoint->ep_serial);
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): internal call from terminal %s denied.\n", ea_endpoint->ep_serial, e_ext.number);
		new_state(EPOINT_STATE_OUT_DISCONNECT);
		message_disconnect_port(portlist, CAUSE_REJECTED, LOCATION_PRIVATE_LOCAL, "");
		set_tone(portlist, "cause_81");
		return;
	}

	set_tone(portlist, "proceeding");
	message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_PROCEEDING);
	message_put(message);
	logmessage(message);
	new_state(EPOINT_STATE_IN_PROCEEDING);

	memset(&dialinginfo, 0, sizeof(dialinginfo));
	dialinginfo.itype = INFO_ITYPE_VBOX;
	dialinginfo.sending_complete = 1;
	SCPY(dialinginfo.number, rparam->string_value);

	/* append special announcement (if given) */
	if ((rparam = routeparam(e_action, PARAM_ANNOUNCEMENT)))
	if (rparam->string_value[0])
	{
		SCAT(dialinginfo.number, ",");
		SCAT(dialinginfo.number, rparam->string_value);
	}

	/* add or update internal call */
	printlog("%3d  action   VBOX-RECORD call to extension %s.\n", ea_endpoint->ep_serial, dialinginfo.number);
	message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_SETUP);
	memcpy(&message->param.setup.dialinginfo, &dialinginfo, sizeof(struct dialing_info));
	memcpy(&message->param.setup.redirinfo, &e_redirinfo, sizeof(struct redir_info));
	memcpy(&message->param.setup.callerinfo, &e_callerinfo, sizeof(struct caller_info));
	memcpy(&message->param.setup.capainfo, &e_capainfo, sizeof(struct capa_info));
	message_put(message);
}


/*
 * process partyline
 */
void EndpointAppPBX::action_init_partyline(void)
{
	class Call *call;
	class CallPBX *callpbx;
	struct port_list *portlist = ea_endpoint->ep_portlist;
	struct message *message;
	struct route_param *rparam;
	int partyline;
	struct call_relation *relation;

	portlist = ea_endpoint->ep_portlist;

	/* check for given extension */
	if (!(rparam = routeparam(e_action, PARAM_ROOM)))
	{
		printlog("%3d  action   PARTYLINE no 'room' parameter given at routing.\n", ea_endpoint->ep_serial);
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): missing parameter 'room'.\n", ea_endpoint->ep_serial);
		noroom:
		new_state(EPOINT_STATE_OUT_DISCONNECT);
		message_disconnect_port(portlist, CAUSE_SERVICEUNAVAIL, LOCATION_PRIVATE_LOCAL, "");
		set_tone(portlist, "cause_3f");
		return;
	}
	if (rparam->integer_value <= 0)
	{
		printlog("%3d  action   PARTYLINE 'room' value must be greate 0.\n", ea_endpoint->ep_serial);
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): invalid value for 'room'.\n", ea_endpoint->ep_serial);
		goto noroom;
	}
	partyline = rparam->integer_value;

	/* don't create call if partyline exists */
	call = call_first;
	while(call)
	{
		if (call->c_type == CALL_TYPE_PBX)
		{
			callpbx = (class CallPBX *)call;
			if (callpbx->c_partyline == rparam->integer_value)
				break;
		}
		call = call->next;
	}
	if (!call)
	{
		/* create call */
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): Creating new call instance.\n", ea_endpoint->ep_serial);
		if (!(call = new CallPBX(ea_endpoint)))
		{
			nores:
			/* resource not available */
			message_disconnect_port(portlist, CAUSE_RESSOURCEUNAVAIL, LOCATION_PRIVATE_LOCAL, "");
			new_state(EPOINT_STATE_OUT_DISCONNECT);
			set_tone(portlist,"cause_22");
			return;
		}
	} else
	{
//NOTE: callpbx must be set here
		/* add relation to existing call */
		if (!(relation=callpbx->add_relation()))
		{
			goto nores;
		}
		relation->type = RELATION_TYPE_SETUP;
		relation->channel_state = CHANNEL_STATE_CONNECT;
		relation->rx_state = NOTIFY_STATE_ACTIVE;
		relation->tx_state = NOTIFY_STATE_ACTIVE;
		relation->epoint_id = ea_endpoint->ep_serial;

	}
	ea_endpoint->ep_call_id = call->c_serial;

	PDEBUG(DEBUG_EPOINT, "EPOINT(%d): terminal %s dialing room: %d\n", ea_endpoint->ep_serial, e_ext.number, partyline);

	set_tone(portlist, "proceeding");
	message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_PROCEEDING);
	message_put(message);
	logmessage(message);
	new_state(EPOINT_STATE_IN_PROCEEDING);

	/* send setup to call */
	printlog("%3d  action   PARTYLINE call to room %d.\n", ea_endpoint->ep_serial, partyline);
	message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_call_id, EPOINT_TO_CALL, MESSAGE_SETUP);
	message->param.setup.partyline = partyline;
	memcpy(&message->param.setup.dialinginfo, &e_dialinginfo, sizeof(struct dialing_info));
	memcpy(&message->param.setup.redirinfo, &e_redirinfo, sizeof(struct redir_info));
	memcpy(&message->param.setup.callerinfo, &e_callerinfo, sizeof(struct caller_info));
	memcpy(&message->param.setup.capainfo, &e_capainfo, sizeof(struct capa_info));
	message_put(message);
}


/*
 * process hangup of all calls
 */
void EndpointAppPBX::action_hangup_call(void)
{
	int i;

	printlog("%3d  action   CALL to '%s' hangs up.\n", ea_endpoint->ep_serial, e_dialinginfo.number);
	/* check */
	if (e_ext.number[0] == '\0')
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): terminal %s: cannot store last dialed number '%s' because caller is unknown (not internal).\n", ea_endpoint->ep_serial, e_ext.number, e_dialinginfo.number);
		return;
	}
	if (!(read_extension(&e_ext, e_ext.number)))
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): terminal %s: cannot store last dialed number '%s' because cannot read settings.\n", ea_endpoint->ep_serial, e_ext.number, e_dialinginfo.number);
		return;
	}
	if (e_dialinginfo.number[0] == '\0')
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): terminal %s: cannot store last dialed number because nothing was dialed.\n", ea_endpoint->ep_serial, e_ext.number);
		return;
	}
	if (!strcmp(e_dialinginfo.number, e_ext.last_out[0]))
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): terminal %s: cannot store last dialed number '%s' because it is identical with the last one.\n", ea_endpoint->ep_serial, e_ext.number, e_dialinginfo.number);
		return;
	}

	/* insert */
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d): terminal %s: storing last number '%s'.\n", ea_endpoint->ep_serial, e_ext.number, e_dialinginfo.number);
	i = MAX_REMEMBER-1;
	while(i)
	{
		UCPY(e_ext.last_out[i], e_ext.last_out[i-1]);
		i--;
	}
	SCPY(e_ext.last_out[0], e_dialinginfo.number);

	/* write extension */
	write_extension(&e_ext, e_ext.number);
}


/*
 * process dialing 'login'
 */
void EndpointAppPBX::action_dialing_login(void)
{
	struct port_list *portlist = ea_endpoint->ep_portlist;
	struct message *message;
	char *extension;
	struct route_param *rparam;

	/* extension parameter */
	if ((rparam = routeparam(e_action, PARAM_EXTENSION)))
	{
		/* extension is given by parameter */
		extension = rparam->string_value;
		if (extension[0] == '\0')
			return;
		if (!read_extension(&e_ext, extension))
		{
			printlog("%3d  action   LOGIN given extension %s doesn't exist.\n", ea_endpoint->ep_serial, extension);
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d): given extension %s not found.\n", ea_endpoint->ep_serial, extension);
			/* extension doesn't exist */
			new_state(EPOINT_STATE_OUT_DISCONNECT);
			message_disconnect_port(portlist, CAUSE_UNALLOCATED, LOCATION_PRIVATE_LOCAL, "");
			set_tone(portlist, "cause_86");
			return;
		}
	} else
	{
		/* extension must be given by dialstring */
		extension = e_extdialing;
		if (extension[0] == '\0')
			return;
		if (!read_extension(&e_ext, extension))
		{
			printlog("%3d  action   LOGIN given extension %s incomplete or not found..\n", ea_endpoint->ep_serial, extension);
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d): extension %s incomplete or not found\n", ea_endpoint->ep_serial, extension);
			return;
		}
	}

	/* we changed our extension */
	SCPY(e_ext.number, extension);
	new_state(EPOINT_STATE_CONNECT);
	e_dtmf = 1;
	e_connectedmode = 1;

	/* send connect with extension's caller id (COLP) */
	message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_CONNECT);
	SCPY(message->param.connectinfo.id, e_ext.callerid);
	message->param.connectinfo.ntype = e_ext.callerid_type;
	if (e_ext.callerid_present==INFO_PRESENT_ALLOWED && e_callerinfo.present==INFO_PRESENT_RESTRICTED)
		message->param.connectinfo.present = INFO_PRESENT_RESTRICTED;
	else	message->param.connectinfo.present = e_ext.callerid_present;
	/* handle restricted caller ids */
	apply_callerid_restriction(e_ext.anon_ignore, portlist->port_type, message->param.connectinfo.id, &message->param.connectinfo.ntype, &message->param.connectinfo.present, &message->param.connectinfo.screen, message->param.connectinfo.voip, message->param.connectinfo.intern, message->param.connectinfo.name);
	/* display callerid if desired for extension */
	SCPY(message->param.connectinfo.display, apply_callerid_display(message->param.connectinfo.id, message->param.connectinfo.itype, message->param.connectinfo.ntype, message->param.connectinfo.present, message->param.connectinfo.screen, message->param.connectinfo.voip, message->param.connectinfo.intern, message->param.connectinfo.name));
	message->param.connectinfo.ntype = e_ext.callerid_type;
	message_put(message);
	logmessage(message);

	/* set our caller id */
	SCPY(e_callerinfo.id, e_ext.callerid);
	e_callerinfo.ntype = e_ext.callerid_type;
	e_callerinfo.present = e_ext.callerid_present;

	/* enable connectedmode */
	e_connectedmode = 1;
	e_dtmf = 1;

	if (!(rparam = routeparam(e_action, PARAM_NOPASSWORD)))
	{
		/* make call state to enter password */
		printlog("%3d  action   LOGIN to extension %s, ask for password.\n", ea_endpoint->ep_serial, e_ext.number);
		new_state(EPOINT_STATE_IN_OVERLAP);
		e_ruleset = NULL;
		e_rule = NULL;
		e_action = &action_password;
		e_match_timeout = 0;
		e_match_to_action = NULL;
		e_dialinginfo.number[0] = '\0';
		e_extdialing = strchr(e_dialinginfo.number, '\0');

		/* set timeout */
		e_password_timeout = now+20;

		/* do dialing */
		process_dialing();
	} else 
	{
		/* make call state  */
		new_state(EPOINT_STATE_IN_OVERLAP);
		e_ruleset = ruleset_main;
		if (e_ruleset)
			e_rule = e_ruleset->rule_first;
		e_action = NULL;
		e_dialinginfo.number[0] = '\0';
		e_extdialing = e_dialinginfo.number;
		set_tone(portlist, "dialpbx");
	}
}


/*
 * process init 'change_callerid'
 */
void EndpointAppPBX::action_init_change_callerid(void)
{
	struct port_list *portlist = ea_endpoint->ep_portlist;

	if (!e_ext.change_callerid)
	{
		/* service not available */
		printlog("%3d  action   CHANGE-CALLERID denied for this caller.\n", ea_endpoint->ep_serial);
		message_disconnect_port(portlist, CAUSE_SERVICEUNAVAIL, LOCATION_PRIVATE_LOCAL, "");
		new_state(EPOINT_STATE_OUT_DISCONNECT);
		set_tone(portlist,"cause_87");
		return;
	}
}

/* process dialing callerid
 */
void EndpointAppPBX::_action_callerid_calleridnext(int next)
{
	struct port_list *portlist = ea_endpoint->ep_portlist;
	struct route_param *rparam;
	char buffer[64], *callerid;

	if ((rparam = routeparam(e_action, PARAM_CALLERID)))
	{
		/* the caller ID is given by parameter */
		callerid = rparam->string_value;
	} else
	{
		/* caller ID is dialed */
		if (!strchr(e_extdialing, '#'))
		{
			/* no complete ID yet */
			return;
		}
		*strchr(e_extdialing, '#') = '\0';
		callerid = e_extdialing;
	}

	/* given callerid type */
	if ((rparam = routeparam(e_action, PARAM_CALLERIDTYPE)))
		switch(rparam->integer_value)
	       	{
			case INFO_NTYPE_SUBSCRIBER:
			SPRINT(buffer, "s%s", callerid);
			callerid = buffer;
			break;
			case INFO_NTYPE_NATIONAL:
			SPRINT(buffer, "n%s", callerid);
			callerid = buffer;
			break;
			case INFO_NTYPE_INTERNATIONAL:
			SPRINT(buffer, "i%s", callerid);
			callerid = buffer;
			break;
			default:
			SPRINT(buffer, "%s", callerid);
			callerid = buffer;
			break;
		}

	/* caller id complete, dialing with new caller id */
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d): terminal %s: storing callerid '%s' for all following calls.\n", ea_endpoint->ep_serial, e_ext.number, callerid);
	/* write new parameters */
	if (read_extension(&e_ext, e_ext.number))
	{
		if (callerid[0] == '\0')
		{
			/* no caller id */
			(!next)?e_ext.callerid_present:e_ext.id_next_call_present = INFO_PRESENT_RESTRICTED;
		} else
		{
			/* new caller id */
			(!next)?e_ext.callerid_present:e_ext.id_next_call_present = INFO_PRESENT_ALLOWED;
			if ((rparam = routeparam(e_action, PARAM_PRESENT))) if (rparam->integer_value == 0)
				(!next)?e_ext.callerid_present:e_ext.id_next_call_present = INFO_PRESENT_RESTRICTED;
			if (e_ext.callerid_type == INFO_NTYPE_UNKNOWN) /* if callerid is unknown, the given id is not nationalized */
			{
				SCPY((!next)?e_ext.callerid:e_ext.id_next_call, callerid);
				(!next)?e_ext.callerid_type:e_ext.id_next_call_type = INFO_NTYPE_UNKNOWN;
			} else
			{
				SCPY((!next)?e_ext.callerid:e_ext.id_next_call, nationalize_callerinfo(callerid,&((!next)?e_ext.callerid_type:e_ext.id_next_call_type)));
			}
			if (!next) e_ext.id_next_call_type = -1;
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d): nationalized callerid: '%s' type=%d\n", ea_endpoint->ep_serial, (!next)?e_ext.callerid:e_ext.id_next_call, (!next)?e_ext.callerid_type:e_ext.id_next_call_type);
		}
		write_extension(&e_ext, e_ext.number);
	}

	/* function activated */
	printlog("%3d  action   CHANGE-CALLERID caller changes caller id%s to '%s'.\n", ea_endpoint->ep_serial, next?" of next call":"", callerid);
	message_disconnect_port(portlist, CAUSE_NORMAL, LOCATION_PRIVATE_LOCAL, "");
	new_state(EPOINT_STATE_OUT_DISCONNECT);
	set_tone(portlist,"activated");
}

/* process dialing callerid for all call
 */
void EndpointAppPBX::action_dialing_callerid(void)
{
	_action_callerid_calleridnext(0);
}

/* process dialing callerid for next call
 */
void EndpointAppPBX::action_dialing_calleridnext(void)
{
	_action_callerid_calleridnext(1);
}


/*
 * process init 'change_forward'
 */
void EndpointAppPBX::action_init_change_forward(void)
{
	struct port_list *portlist = ea_endpoint->ep_portlist;

	if (!e_ext.change_forward)
	{
		printlog("%3d  action   CHANGE-FORWARD denied for this caller.\n", ea_endpoint->ep_serial);
		/* service not available */		
		message_disconnect_port(portlist, CAUSE_SERVICEUNAVAIL, LOCATION_PRIVATE_LOCAL, "");
		new_state(EPOINT_STATE_OUT_DISCONNECT);
		set_tone(portlist,"cause_87");
		return;
	}
}

/* process dialing forwarding
 */
void EndpointAppPBX::action_dialing_forward(void)
{
	struct port_list *portlist = ea_endpoint->ep_portlist;
	int diversion = INFO_DIVERSION_CFU;
	char *dest = e_extdialing;
	struct route_param *rparam;

	/* if diversion type is given */
	if ((rparam = routeparam(e_action, PARAM_DIVERSION)))
		diversion = rparam->integer_value;

	if ((rparam = routeparam(e_action, PARAM_DEST)))
	{
		/* if destination is given */
		dest = rparam->string_value;
	} else
	{
		if (!strchr(e_extdialing, '#'))
			return;
		*strchr(e_extdialing, '#') = '\0';
		dest = e_extdialing;
	}

	PDEBUG(DEBUG_EPOINT, "EPOINT(%d): terminal %s: storing forwarding to '%s'.\n", ea_endpoint->ep_serial, e_ext.number, dest);
	if (read_extension(&e_ext, e_ext.number))
	{
		switch(diversion)
		{
			case INFO_DIVERSION_CFU:
			printlog("%3d  action   CHANGE-FORWARD changing CFU (unconditional) to '%s'.\n", ea_endpoint->ep_serial, dest);
			SCPY(e_ext.cfu, dest);
			break;
			case INFO_DIVERSION_CFB:
			printlog("%3d  action   CHANGE-FORWARD changing CFB (busy) to '%s'.\n", ea_endpoint->ep_serial, dest);
			SCPY(e_ext.cfb, dest);
			break;
			case INFO_DIVERSION_CFNR:
			if ((rparam = routeparam(e_action, PARAM_DELAY)))
				e_ext.cfnr_delay = rparam->integer_value;
			printlog("%3d  action   CHANGE-FORWARD changing CFNR (no response) to '%s' with delay=%d.\n", ea_endpoint->ep_serial, dest, e_ext.cfnr_delay);
			SCPY(e_ext.cfnr, dest);
			break;
			case INFO_DIVERSION_CFP:
			printlog("%3d  action   CHANGE-FORWARD changing CFP (parallel) to '%s'.\n", ea_endpoint->ep_serial, dest);
			SCPY(e_ext.cfp, dest);
			break;
		}
		write_extension(&e_ext, e_ext.number);
	}
	/* function (de)activated */
	message_disconnect_port(portlist, CAUSE_NORMAL, LOCATION_PRIVATE_LOCAL, "");
	new_state(EPOINT_STATE_OUT_DISCONNECT);
	if (dest[0])
		set_tone(portlist,"activated");
	else
		set_tone(portlist,"deactivated");
}


/* process dialing redial
*/
void EndpointAppPBX::action_init_redial_reply(void)
{
	struct port_list *portlist = ea_endpoint->ep_portlist;

	e_select = 0;
	if (!e_ext.last_out[0])
	{
		printlog("%3d  action   REDIAL/REPLY no number available to dial.\n", ea_endpoint->ep_serial);
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): no stored last number.\n", ea_endpoint->ep_serial);
		new_state(EPOINT_STATE_OUT_DISCONNECT);
		message_disconnect_port(portlist, CAUSE_SERVICEUNAVAIL, LOCATION_PRIVATE_LOCAL, "");
		set_tone(portlist, "cause_3f");
		return;
	}
}

/* process dialing redial
*/
void EndpointAppPBX::_action_redial_reply(int in)
{
	struct message *message;
	char *last;
	struct route_param *rparam;

	last = (in)?e_ext.last_in[0]:e_ext.last_out[0];

	/* if no display is available */
	if (!e_ext.display_menu)
		goto nodisplay;
	if (ea_endpoint->ep_portlist->port_type!=PORT_TYPE_DSS1_NT_IN && ea_endpoint->ep_portlist->port_type!=PORT_TYPE_DSS1_NT_OUT)
		goto nodisplay;

	/* if select is not given */
	if (!(rparam = routeparam(e_action, PARAM_SELECT)))
		goto nodisplay;

	/* scroll menu */
	if (e_extdialing[0]=='*' || e_extdialing[0]=='1')
	{
		/* find prev entry */
		e_select--;
		if (e_select < 0)
			e_select = 0;

	}
	if (e_extdialing[0]=='#' || e_extdialing[0]=='3')
	{
		/* find next entry */
		e_select++;
		if (e_select >= MAX_REMEMBER)
			e_select--;
		else if (in)
			if (e_ext.last_in[e_select][0] == '\0')
				e_select--;
		else
			if (e_ext.last_out[e_select][0] == '\0')
				e_select--;

	}

	last = (in)?e_ext.last_in[e_select]:e_ext.last_out[e_select];
	if (e_extdialing[0]=='0' || e_extdialing[0]=='2')
	{
		nodisplay:
		printlog("%3d  action   REDIAL/REPLY dialing '%s'.\n", ea_endpoint->ep_serial, last);
		SCPY(e_dialinginfo.number, last);
		e_extdialing = e_dialinginfo.number;
		e_action = NULL;
		process_dialing();
		return;
	}
	e_extdialing[0] = '\0';
	
	/* send display message to port */
	message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_portlist->port_id, EPOINT_TO_PORT, MESSAGE_NOTIFY);
	if (!strncmp(last, "extern:", 7))
		SPRINT(message->param.notifyinfo.display, "(%d) %s ext", e_select+1, last+7);
	else
	if (!strncmp(last, "intern:", 7))
		SPRINT(message->param.notifyinfo.display, "(%d) %s int", e_select+1, last+7);
	else
	if (!strncmp(last, "chan:", 4))
		SPRINT(message->param.notifyinfo.display, "(%d) %s chan", e_select+1, last+5);
	else
	if (!strncmp(last, "vbox:", 5))
		SPRINT(message->param.notifyinfo.display, "(%d) %s vbox", e_select+1, last+5);
	else
		SPRINT(message->param.notifyinfo.display, "(%d) %s", e_select+1, (last[0])?last:"- empty -");
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d): terminal %s sending display:%s\n", ea_endpoint->ep_serial, e_ext.number, message->param.notifyinfo.display);
	message_put(message);
	logmessage(message);
}

/* process dialing redial
*/
void EndpointAppPBX::action_dialing_redial(void)
{
	_action_redial_reply(0);
}

/* process dialing reply
*/
void EndpointAppPBX::action_dialing_reply(void)
{
	_action_redial_reply(1);
}


/* dialing powerdialing delay
 */
void EndpointAppPBX::action_dialing_powerdial(void)
{
	struct port_list *portlist = ea_endpoint->ep_portlist;
	struct message *message;
	struct route_param *rparam;

	/* power dialing only possible if we have a last dialed number */
	if (!e_ext.last_out[0])
	{
		printlog("%3d  action   POWERDIAL no number available to redial.\n", ea_endpoint->ep_serial);
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): no stored last number.\n", ea_endpoint->ep_serial);
		new_state(EPOINT_STATE_OUT_DISCONNECT);
		message_disconnect_port(portlist, CAUSE_SERVICEUNAVAIL, LOCATION_PRIVATE_LOCAL, "");
		set_tone(portlist, "cause_3f");
		return;
	}

	/* limit */
	if ((rparam = routeparam(e_action, PARAM_LIMIT)))
	{
		e_powerlimit = rparam->integer_value;
	} else
	{
		e_powerlimit = 0;
	}

	/* delay */
	if ((rparam = routeparam(e_action, PARAM_DELAY)))
	{
		e_powerdelay = rparam->integer_value;
	} else
	{
		/* delay incomplete */
		if (!strchr(e_extdialing, '#'))
			return;
		*strchr(e_extdialing, '#') = '\0';
		e_powerdelay = e_extdialing[0]?atoi(e_extdialing): 0;
	}

	if (e_powerdelay < 1)
		e_powerdelay = 0.2;
	printlog("%3d  action   POWERDIAL to '%s' with delay=%d.\n", ea_endpoint->ep_serial, e_ext.last_out[0], (int)e_powerdelay);
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d): powerdialing to '%s' (delay=%d).\n", ea_endpoint->ep_serial, e_ext.last_out[0], (int)e_powerdelay);

	/* send connect to avoid overlap timeout */
//	new_state(EPOINT_STATE_CONNECT); connect may prevent further dialing
	if (e_ext.number[0])
		e_dtmf = 1;
	memset(&e_connectinfo, 0, sizeof(e_connectinfo));
	message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_portlist->port_id, EPOINT_TO_PORT, MESSAGE_CONNECT);
	message_put(message);
	logmessage(message);

	/* do dialing */
	SCPY(e_dialinginfo.number, e_ext.last_out[0]);
	e_powerdialing = -1; /* indicates the existence of powerdialing but no redial time given */
	e_powercount = 0;
	e_action = NULL;
	process_dialing();
}


/* dialing callback
 */
void EndpointAppPBX::action_dialing_callback(void)
{
	struct port_list *portlist = ea_endpoint->ep_portlist;
	struct route_param *rparam;
	struct extension cbext;

	portlist = ea_endpoint->ep_portlist;

	/* check given extension */
	if (!(rparam = routeparam(e_action, PARAM_EXTENSION)))
	{
		printlog("%3d  action   CALLBACK no extension was specified in routing.conf.\n", ea_endpoint->ep_serial);
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): rejecting callback, because no extension was specified in routing.conf\n", ea_endpoint->ep_serial);

		disconnect:
		new_state(EPOINT_STATE_OUT_DISCONNECT);
		message_disconnect_port(portlist, CAUSE_SERVICEUNAVAIL, LOCATION_PRIVATE_LOCAL, "");
		set_tone(portlist, "cause_3f");
		e_action = NULL;
		e_cbcaller[0] = e_cbdialing[0] = '\0';
		return;
	}

	/* if extension is given */
	SCPY(e_cbcaller, rparam->string_value);
	if (e_cbcaller[0] == '\0')
	{
		printlog("%3d  action   CALLBACK extension specified in routing.conf is an empty string.\n", ea_endpoint->ep_serial);
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): rejecting callback, because given extension is an empty string.\n", ea_endpoint->ep_serial);
		goto disconnect;
	}

	/* read callback extension */
	memset(&cbext, 0, sizeof(cbext));
	if (!read_extension(&cbext, e_cbcaller))
	{
		printlog("%3d  action   CALLBACK extension '%s' specified in routing.conf doesn't exist.\n", ea_endpoint->ep_serial, e_cbcaller);
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): rejecting callback, because given extension does not exist.\n", ea_endpoint->ep_serial);
		goto disconnect;
	}

	/* if password is not given */
	if (cbext.password[0] == '\0')
	{
		printlog("%3d  action   CALLBACK extension '%s' has no password specified.\n", ea_endpoint->ep_serial, e_cbcaller);
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): rejecting callback, because no password is available in given extension (%s).\n", ea_endpoint->ep_serial, e_cbcaller);
		goto disconnect;
	}

	/* callback only possible if callerid exists OR it is given */
	if ((rparam = routeparam(e_action, PARAM_CALLTO)))
		SCPY(e_cbto, rparam->string_value);
	if (e_cbto[0])
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): callback to given number: '%s'\n", ea_endpoint->ep_serial, e_cbto);
		printlog("%3d  action   CALLBACK callback to given number: '%s'\n", ea_endpoint->ep_serial, e_cbto);
		SCPY(e_callerinfo.id, e_cbto);
		e_callerinfo.ntype = INFO_NTYPE_UNKNOWN;
		e_callerinfo.present = INFO_PRESENT_ALLOWED;
	}
	if (e_callerinfo.id[0]=='\0' || e_callerinfo.present==INFO_PRESENT_NOTAVAIL)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): rejecting callback, because no caller id is available\n", ea_endpoint->ep_serial);
		printlog("%3d  action   CALLBACK not possible because caller ID is not available.\n", ea_endpoint->ep_serial);
		goto disconnect;
	}
	/* present id */
	e_callerinfo.present = INFO_PRESENT_ALLOWED;

}

/*
 * process hangup 'callback'
 */
void EndpointAppPBX::action_hangup_callback(void)
{
	struct route_param *rparam;
	int delay;

	/* set delay */
	delay = 2; /* default value */
	if ((rparam = routeparam(e_action, PARAM_DELAY)))
	if (rparam->integer_value>0)
		delay = rparam->integer_value;

	/* dialing after callback */
	if ((rparam = routeparam(e_action, PARAM_PREFIX)))
		SCPY(e_cbdialing, rparam->string_value);
	else
		SCPY(e_cbdialing, e_extdialing);

	printlog("%3d  action   CALLBACK extension=%s callerid='%s' delay='%d' dialing after callback='%s' .\n", ea_endpoint->ep_serial, e_cbcaller, e_callerinfo.id, delay, e_cbdialing);
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d): caller '%s', callerid '%s', dialing '%s', delay %d\n", ea_endpoint->ep_serial, e_cbcaller, e_callerinfo.id, e_cbdialing, delay);

	/* set time to callback */
	e_callback = now_d + delay;
}


/*
 * dialing action abbreviation
 */
void EndpointAppPBX::action_dialing_abbrev(void)
{
	struct port_list *portlist = ea_endpoint->ep_portlist;
	char *abbrev, *phone, *name;
	int result;

	portlist = ea_endpoint->ep_portlist;

	/* abbrev dialing is only possible if we have a caller defined */
	if (!e_ext.number[0])
	{
		printlog("%3d  action   ABBREVIATION only possible for internal callers.\n", ea_endpoint->ep_serial);
		new_state(EPOINT_STATE_OUT_DISCONNECT);
		message_disconnect_port(portlist, CAUSE_SERVICEUNAVAIL, LOCATION_PRIVATE_LOCAL, "");
		set_tone(portlist, "cause_3f");
		return;
	}

	/* check abbreviation */
	abbrev = e_extdialing;
	phone = NULL;
	name = NULL;
	result = parse_phonebook(e_ext.number, &abbrev, &phone, &name);
	if (result == 0)
	{
		printlog("%3d  action   ABBREVIATION '%s' not found.\n", ea_endpoint->ep_serial, abbrev);
		new_state(EPOINT_STATE_OUT_DISCONNECT);
		message_disconnect_port(portlist, CAUSE_UNALLOCATED, LOCATION_PRIVATE_LOCAL, "");
		set_tone(portlist, "cause_01");
		return;
	}
	if (result == -1) /* may match if more digits are dialed */
	{
		return;
	}

	/* dial abbreviation */	
	printlog("%3d  action   ABBREVIATION mapping '%s' to '%s' (%s), dialing...\n", ea_endpoint->ep_serial, abbrev, phone, name?name:"unknown");
	SCPY(e_dialinginfo.number, phone);
	e_extdialing = e_dialinginfo.number;
	e_action = NULL;
	process_dialing();
}


/* process dialing 'test'
 */
void EndpointAppPBX::action_dialing_test(void)
{
	unsigned int cause;
	char causestr[16];
	struct port_list *portlist = ea_endpoint->ep_portlist;
	struct message *message;
	class Port *port;
	char testcode[32] = "";
	struct route_param *rparam;

	/* given testcode */
	if ((rparam = routeparam(e_action, PARAM_PREFIX)))
		SCPY(testcode, rparam->string_value);
	SCAT(testcode, e_extdialing);

	switch(testcode[0])
	{
		case '1':
		printlog("%3d  action   TESTMODE executing 'proceeding' test.\n", ea_endpoint->ep_serial);
		new_state(EPOINT_STATE_IN_PROCEEDING);
		set_tone(portlist, "proceeding");
		message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_PROCEEDING);
		message_put(message);
		logmessage(message);
		break;
		
		case '2':
		printlog("%3d  action   TESTMODE executing 'alerting' test.\n", ea_endpoint->ep_serial);
		new_state(EPOINT_STATE_IN_ALERTING);
		set_tone(portlist, "ringpbx");
		message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_ALERTING);
		message_put(message);
		logmessage(message);
		break;
		
		case '3':
		printlog("%3d  action   TESTMODE executing 'echo connect' test.\n", ea_endpoint->ep_serial);
		new_state(EPOINT_STATE_CONNECT);
		if (e_ext.number[0])
			e_dtmf = 1;
		set_tone(portlist, NULL);
		memset(&e_connectinfo, 0, sizeof(e_connectinfo));
		SCPY(e_connectinfo.id, e_callerinfo.id);
		SCPY(e_connectinfo.intern, e_callerinfo.intern);
		SCPY(e_connectinfo.voip, e_callerinfo.voip);
		e_connectinfo.itype = e_callerinfo.itype;
		e_connectinfo.ntype = e_callerinfo.ntype;
		e_connectinfo.present = e_callerinfo.present;
		e_connectinfo.screen = e_callerinfo.screen;
		message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_CONNECT);
		memcpy(&message->param.connectinfo, &e_connectinfo, sizeof(struct connect_info));
		/* handle restricted caller ids */
		apply_callerid_restriction(e_ext.anon_ignore, portlist->port_type, message->param.connectinfo.id, &message->param.connectinfo.ntype, &message->param.connectinfo.present, &message->param.connectinfo.screen, message->param.connectinfo.voip, message->param.connectinfo.intern, message->param.connectinfo.name);
		/* display callerid if desired for extension */
		SCPY(message->param.connectinfo.display, apply_callerid_display(message->param.connectinfo.id, message->param.connectinfo.itype, message->param.connectinfo.ntype, message->param.connectinfo.present, message->param.connectinfo.screen, message->param.connectinfo.voip, message->param.connectinfo.intern, message->param.connectinfo.name));
		message_put(message);
		logmessage(message);

		port = find_port_id(portlist->port_id);
		if (port)
		{
			port->set_echotest(1);
		}
		break;
		
		case '4':
		printlog("%3d  action   TESTMODE executing 'tone connect' test.\n", ea_endpoint->ep_serial);
		new_state(EPOINT_STATE_CONNECT);
		if (e_ext.number[0])
			e_dtmf = 1;
		memset(&e_connectinfo, 0, sizeof(e_connectinfo));
		message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_CONNECT);
		message_put(message);
		logmessage(message);
		set_tone(portlist, "test");
		break;
		
		case '5':
		printlog("%3d  action   TESTMODE executing 'hold music' test.\n", ea_endpoint->ep_serial);
		new_state(EPOINT_STATE_CONNECT);
		if (e_ext.number[0])
			e_dtmf = 1;
		memset(&e_connectinfo, 0, sizeof(e_connectinfo));
		message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_CONNECT);
		message_put(message);
		logmessage(message);
		set_tone(portlist, "hold");
		break;
		
		case '6':
		if (strlen(testcode) < 4)
			break;
		testcode[4] = '\0';
		cause = atoi(testcode+1);
		if (cause > 255)
			cause = 0;
		printlog("%3d  action   TESTMODE executing 'announcement' test with cause %d.\n", ea_endpoint->ep_serial, cause);
		new_state(EPOINT_STATE_CONNECT);
		if (e_ext.number[0])
			e_dtmf = 1;
		SPRINT(causestr,"cause_%02x",cause);
		memset(&e_connectinfo, 0, sizeof(e_connectinfo));
		message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_CONNECT);
		message_put(message);
		logmessage(message);
		set_tone(portlist, causestr);
		break;
		
		case '7':
		if (strlen(testcode) < 4)
			break;
		testcode[4] = '\0';
		cause = atoi(testcode+1);
		if (cause > 127)
			cause = 0;
		printlog("%3d  action   TESTMODE executing 'disconnect' test with cause %d.\n", ea_endpoint->ep_serial, cause);
		new_state(EPOINT_STATE_OUT_DISCONNECT);
		SPRINT(causestr,"cause_%02x",cause);
		message_disconnect_port(portlist, cause, LOCATION_PRIVATE_LOCAL, "");
		set_tone(portlist, causestr);
		break;

		case '8': /* release */
		printlog("%3d  action   TESTMODE executing 'release' test.\n", ea_endpoint->ep_serial);
		new_state(EPOINT_STATE_OUT_DISCONNECT);
		message_disconnect_port(portlist, CAUSE_NORMAL, LOCATION_PRIVATE_LOCAL, "");
		set_tone(portlist, "release");
		break;

		case '9': /* text callerid test */
		printlog("%3d  action   TESTMODE executing 'caller id' test.\n", ea_endpoint->ep_serial);
		new_state(EPOINT_STATE_CONNECT);
		if (e_ext.number[0])
			e_dtmf = 1;
		memset(&e_connectinfo, 0, sizeof(e_connectinfo));
		SCPY(e_connectinfo.id, "12345678");
		SCPY(e_connectinfo.name, "Welcome to Linux");
		SCPY(e_connectinfo.display, "Welcome to Linux");
		e_connectinfo.ntype = INFO_NTYPE_UNKNOWN;
		e_connectinfo.present = INFO_PRESENT_ALLOWED;
		message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_CONNECT);
		memcpy(&message->param.connectinfo, &e_connectinfo, sizeof(message->param.connectinfo));
		message_put(message);
		logmessage(message);
		set_tone(portlist, "hold");
		break;
	}
}


/* process init play
 */
void EndpointAppPBX::action_init_play(void)
{
	struct route_param *rparam;
	struct port_list *portlist = ea_endpoint->ep_portlist;

	/* check given sample */
	if (!(rparam = routeparam(e_action, PARAM_SAMPLE)))
	{
		printlog("%3d  action   PLAY no sample given.\n", ea_endpoint->ep_serial);
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): cannot play, because no sample has been specified\n", ea_endpoint->ep_serial);

		disconnect:
		new_state(EPOINT_STATE_OUT_DISCONNECT);
		message_disconnect_port(portlist, CAUSE_UNSPECIFIED, LOCATION_PRIVATE_LOCAL, "");
		set_tone(portlist, "cause_3f");
		e_action = NULL;
		return;
	}

	/* if sample is given */
	if (rparam->string_value[0] == '\0')
	{
		printlog("%3d  action   PLAY sample name with empty string given.\n", ea_endpoint->ep_serial);
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): cannot play, because given sample is an empty string.\n", ea_endpoint->ep_serial);
		goto disconnect;
	}

	if (e_ext.number[0])
		e_dtmf = 1;

	set_tone(ea_endpoint->ep_portlist, rparam->string_value);
}


/*
 * action_*_vbox_play is implemented in "action_vbox.cpp"
 */


/*
 * process dialing of calculator
 */
void EndpointAppPBX::action_dialing_calculator(void)
{
	struct port_list *portlist = ea_endpoint->ep_portlist;
	struct message *message;
	double value1, value2, v, sign1;
	int komma1, komma2, k, state, mode, first;
	char *p;

	portlist = ea_endpoint->ep_portlist;

	/* remove error message */
	if (!strncmp(e_extdialing, "Error", 5))
	{
		UCPY(e_extdialing, e_extdialing+5);
	}
	if (!strncmp(e_extdialing, "inf", 3))
	{
		UCPY(e_extdialing, e_extdialing+3);
	}
	if (!strncmp(e_extdialing, "-inf", 4))
	{
		UCPY(e_extdialing, e_extdialing+4);
	}

	/* process dialing */
	state = 0;
	value1 = 0;
	value2 = 0;
	komma1 = 0;
	komma2 = 0;
	sign1 = 1;
	p = e_extdialing;
	if (!p)
		return;
	first = 1;
	while(*p)
	{
		if (*p>='0' && *p<='9')
		{
#if 0
			if (first)
			{
				UCPY(p, p+1);
				continue;
			}
			if ((p[-1]<'0' || p[-1]>'0') && p[-1]!='.')
			{
				p--;
				UCPY(p, p+1);
				continue;
			}
#endif
			switch(state)
			{
				case 0: /* first number */
				if (!komma1)
				{
					value1 = value1*10 + (*p-'0');
				} else
				{
					k = komma1++;
					v = *p-'0';
					while(k--)
						v /= 10;
					value1 += v; 
				}
				break;
				case 1: /* second number */
				if (!komma2)
				{
					value2 = value2*10 + (*p-'0');
				} else
				{
					k = komma2++;
					v = *p-'0';
					while(k--)
						v /= 10;
					value2 += v; 
				}
				break;
			}
		} else
		switch(*p)
		{
			case '*':
			if (first)
			{
				UCPY(e_extdialing, "Error");
				goto done;
			}
			/* if there is a multiplication, we change to / */
			if (p[-1] == '*')
			{
				mode = 1;
				p[-1] = '/';
				UCPY(p, p+1);
				p--;
				break;
			}
			/* if there is a division, we change to + */
			if (p[-1] == '/')
			{
				mode = 2;
				p[-1] = '+';
				UCPY(p, p+1);
				p--;
				break;
			}
			/* if there is a addition, we change to - */
			if (p[-1] == '+')
			{
				mode = 3;
				p[-1] = '-';
				UCPY(p, p+1);
				p--;
				break;
			}
			/* if there is a substraction and a comma, we change to * */
			if (p[-1]=='-' && komma1)
			{
				mode = 0;
				p[-1] = '*';
				UCPY(p, p+1);
				p--;
				break;
			}
			/* if there is a substraction and no comma and the first or second value, we change to , */
			if (p[-1]=='-')
			{
				p[-1] = '.';
				UCPY(p, p+1);
				p--;
				komma1 = 1;
				break;
			}
			/* if there is a komma and we are at the first value, we change to * */
			if (p[-1]=='.' && state==0)
			{
				mode = 0;
				p[-1] = '*';
				UCPY(p, p+1);
				p--;
				komma1 = 0;
				break;
			}
			/* if there is a komma and we are at the second value, we display error */
			if (komma2 && state==1)
			{
				UCPY(e_extdialing, "Error");
				goto done;
			}
			/* if we are at state 1, we write a comma */
			if (state == 1)
			{
				*p = '.';
				komma2 = 1;
				break;
			}
			/* we assume multiplication */
			mode = 0;
			state = 1;
			komma1 = 0;
			break;

			case '#':
			/* if just a number is displayed, the input is cleared */
			if (state==0)
			{
				*e_extdialing = '\0';
				break;
			}
			/* calculate the result */
			switch(mode)
			{
				case 0: /* multiply */
				UNPRINT(e_extdialing, sizeof(e_dialinginfo.number)-strlen(e_dialinginfo.number), "%.8f", sign1*value1*value2);
				break;
				case 1: /* divide */
				UNPRINT(e_extdialing, sizeof(e_dialinginfo.number)-strlen(e_dialinginfo.number), "%.8f", sign1*value1/value2);
				break;
				case 2: /* add */
				UNPRINT(e_extdialing, sizeof(e_dialinginfo.number)-strlen(e_dialinginfo.number), "%.8f", sign1*value1+value2);
				break;
				case 3: /* substract */
				UNPRINT(e_extdialing, sizeof(e_dialinginfo.number)-strlen(e_dialinginfo.number), "%.8f", sign1*value1-value2);
				break;
			}
			e_dialinginfo.number[sizeof(e_dialinginfo.number)-1] = '\0';
			if (strchr(e_extdialing, '.')) /* remove zeroes */
			{
				while (e_extdialing[strlen(e_extdialing)-1] == '0')
					e_extdialing[strlen(e_extdialing)-1] = '\0';
				if (e_extdialing[strlen(e_extdialing)-1] == '.')
					e_extdialing[strlen(e_extdialing)-1] = '\0'; /* and remove dot */
			}
			p = strchr(e_extdialing,'\0')-1;
			break;

			case '.':
			if (state)
				komma2 = 1;
			else	komma1 = 1;
			break;

			case '/':
			komma2 = 0;
			state = 1;
			mode = 1;
			break;

			case '+':
			komma2 = 0;
			state = 1;
			mode = 2;
			break;

			case '-':
			if (first)
			{
				sign1=-1;
				break;
			}
			komma2 = 0;
			state = 1;
			mode = 3;
			break;

			default:
			UCPY(e_extdialing, "Error");
			goto done;
		}

		p++;
		first = 0;
	}
	done:

	/* display dialing */	
	message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_NOTIFY);
	SPRINT(message->param.notifyinfo.display, ">%s", e_extdialing);
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d): terminal %s displaying interpreted dialing '%s' internal values: %f %f\n", ea_endpoint->ep_serial, e_ext.number, e_extdialing, value1, value2);
	message_put(message);
	logmessage(message);

}

/*
 * process dialing of timer
 */
void EndpointAppPBX::action_dialing_timer(void)
{
}


/*
 * process 'goto' or 'menu'
 */
void EndpointAppPBX::_action_goto_menu(int mode)
{
	struct port_list *portlist = ea_endpoint->ep_portlist;
	struct route_param *rparam;

	/* check given ruleset */
	if (!(rparam = routeparam(e_action, PARAM_RULESET)))
	{
		no_ruleset:
		printlog("%3d  action   GOTO/MENU no ruleset ginven in options.conf\n", ea_endpoint->ep_serial);
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): no ruleset was secified for action '%s' in routing.conf\n", ea_endpoint->ep_serial, (mode)?"menu":"goto");

		disconnect:
		new_state(EPOINT_STATE_OUT_DISCONNECT);
		message_disconnect_port(portlist, CAUSE_SERVICEUNAVAIL, LOCATION_PRIVATE_LOCAL, "");
		set_tone(portlist, "cause_3f");
		e_action = NULL;
		return;
	}
	if (rparam->string_value[0] == '\0')
		goto no_ruleset;
	e_ruleset = getrulesetbyname(rparam->string_value);
	if (!e_ruleset)
	{
		printlog("%3d  action   GOTO/MENU given ruleset '%s' not found in options.conf\n", ea_endpoint->ep_serial, rparam->string_value);
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): given ruleset '%s' for action '%s' was not found in routing.conf\n", ea_endpoint->ep_serial, rparam->string_value, (mode)?"menu":"goto");
		goto disconnect;
	}
	printlog("%3d  action   GOTO/MENU changing to ruleset '%s'\n", ea_endpoint->ep_serial, rparam->string_value);

	/* if the 'menu' was selected, we will flush all digits */
	if (mode)
	{
		//SCPY(e_dialinginfo.number, e_extdialing);
		e_dialinginfo.number[0] = 0;
		e_extdialing = e_dialinginfo.number;
	} else
	{
	}

	/* play sample */
	if ((rparam = routeparam(e_action, PARAM_SAMPLE)))
	{
		printlog("%3d  action   GOTO/MENU start playing sample '%s'\n", ea_endpoint->ep_serial, rparam->string_value);
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): playing sample '%s'\n", ea_endpoint->ep_serial, rparam->string_value);
		set_tone(ea_endpoint->ep_portlist, rparam->string_value);
	}

	/* do dialing with new ruleset */
	e_action = NULL;
	process_dialing();
}

/* process dialing goto
*/
void EndpointAppPBX::action_dialing_goto(void)
{
	_action_goto_menu(0);
}

/* process dialing menu
*/
void EndpointAppPBX::action_dialing_menu(void)
{
	_action_goto_menu(1);
}


/*
 * process dialing disconnect
 */
void EndpointAppPBX::action_dialing_disconnect(void)
{
	struct route_param *rparam;
	struct port_list *portlist = ea_endpoint->ep_portlist;
	struct message *message;
	int cause = CAUSE_NORMAL; /* normal call clearing */
	int location = LOCATION_PRIVATE_LOCAL;
	char cause_string[256] = "", display[84] = "";

	/* check cause parameter */
	if ((rparam = routeparam(e_action, PARAM_CAUSE)))
	{
		cause = rparam->integer_value;
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): 'cause' is given: %d\n", ea_endpoint->ep_serial, cause);
	}
	if ((rparam = routeparam(e_action, PARAM_LOCATION)))
	{
		location = rparam->integer_value;
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): 'location' is given: %d\n", ea_endpoint->ep_serial, location);
	}


	/* use cause as sample, if not given later */
	SPRINT(cause_string, "cause_%02x", cause);

	/* check sample parameter */
	if ((rparam = routeparam(e_action, PARAM_SAMPLE)))
	{
		SCPY(cause_string, rparam->string_value);
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): 'sample' is given: %s\n", ea_endpoint->ep_serial, cause_string);
	}

	/* check display */
	if ((rparam = routeparam(e_action, PARAM_DISPLAY)))
	{
		SCPY(display, rparam->string_value);
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): 'display' is given: %s\n", ea_endpoint->ep_serial, display);
	}

	/* disconnect only if connect parameter is not given */
	printlog("%3d  action   DISCONNECT with cause %d, location %d, sample '%s', display '%s'\n", ea_endpoint->ep_serial, cause, location, cause_string, display);
	new_state(EPOINT_STATE_OUT_DISCONNECT);
	set_tone(portlist, cause_string);
	if (!(rparam = routeparam(e_action, PARAM_CONNECT)))
	{
		message_disconnect_port(portlist, cause, location, display);
	} else
	{
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_portlist->port_id, EPOINT_TO_PORT, MESSAGE_NOTIFY);
		SCPY(message->param.notifyinfo.display, display);
		message_put(message);
		logmessage(message);
	}
	e_action = NULL;
}


/*
 * process dialing help
 */
void EndpointAppPBX::action_dialing_help(void)
{
	/* show all things that would match */
#if 0
	struct numbering *numbering = numbering_int;
	char dialing[sizeof(e_dialinginfo.number)];
	int i;
	struct message *message;
	struct route_param *rparam;

	/* in case we have no menu (this should never happen) */
	if (!numbering)
		return;

	/* scroll menu */
	if (strchr(e_dialinginfo.number,'*'))
	{
		e_menu--;
		e_dialinginfo.number[0] = '\0';
	}
	if (strchr(e_dialinginfo.number,'#'))
	{
		e_menu++;
		e_dialinginfo.number[0] = '\0';
	}
	
	/* get position in menu */
	if (e_menu < 0)
	{
		/* get last menu position */
		e_menu = 0;
		while(numbering->next)
		{
			e_menu++;
			numbering = numbering->next;
		}
	} else
	{
		/* get menu position */
		i = 0;
		while(i < e_menu)
		{
			numbering = numbering->next;
			if (!numbering)
			{
				e_menu = 0;
				numbering = numbering_int;
				break;
			}
			i++;
		}
	}

	/* if we dial something else we need to add the prefix and change the action */
	if (e_dialinginfo.number[0])
	{
		e_action = NUMB_ACTION_NONE;
		SCPY(dialing, numbering->prefix);
		//we ignore the first digit after selecting
		//SCAT(dialing, e_dialinginfo.number);
		SCPY(e_dialinginfo.number, dialing);
		e_extdialing = e_dialinginfo.number+strlen(numbering->prefix);
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): terminal %s selected a new menu '%s' dialing: %s\n", ea_endpoint->ep_serial, e_ext.number, numb_actions[numbering->action], e_dialinginfo.number);
nesting?:
		process_dialing();
		return;
	}

	/* send display message to port */
	message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_portlist->port_id, EPOINT_TO_PORT, MESSAGE_NOTIFY);
	SPRINT(message->param.notifyinfo.display, ">%s %s%s%s", numbering->prefix, numb_actions[numbering->action], (numbering->param[0])?" ":"", numbering->param);
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d): terminal %s selected a new menu '%s' sending display:%s\n", ea_endpoint->ep_serial, e_ext.number, numb_actions[numbering->action], message->param.notifyinfo.display);
	message_put(message);
	logmessage(message);
#endif
}


/*
 * process dialing deflect
 */
void EndpointAppPBX::action_dialing_deflect(void)
{
}


/*
 * process dialing setforward
 */
void EndpointAppPBX::action_dialing_setforward(void)
{
}


/*
 * process hangup 'execute'
 */
void EndpointAppPBX::action_hangup_execute(void)
{
	struct route_param *rparam;
	char *command = "", isdn_port[10];
	char *argv[8+1]; /* check also number of args below */
	int i = 0;

	/* get script / command */
	if ((rparam = routeparam(e_action, PARAM_EXECUTE)))
		command = rparam->string_value;
	if (command[0] == '\0')
	{
		printlog("%3d  action   EXECUTE no 'execute' parameter given at routing.conf.\n", ea_endpoint->ep_serial);
		PERROR("EPOINT(%d): terminal %s: NO PARAMETER GIVEN for 'execute' action. see routing.conf\n", ea_endpoint->ep_serial, e_ext.number);
		return;
	}
	printlog("%3d  action   EXECUTE command='%s'\n", ea_endpoint->ep_serial, command);
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d): terminal %s: executing '%s'.\n", ea_endpoint->ep_serial, e_ext.number, command);

	argv[0] = command;
	while(strchr(argv[0], '/'))
		argv[0] = strchr(argv[0], '/')+1;
	if ((rparam = routeparam(e_action, PARAM_PARAM)))
	{
		argv[1] = rparam->string_value;
		i++;
	}
	argv[1+i] = e_extdialing;
	argv[2+i] = numberrize_callerinfo(e_callerinfo.id, e_callerinfo.ntype);
	argv[3+i] = e_callerinfo.intern;
	argv[4+i] = e_callerinfo.voip;
	argv[5+i] = e_callerinfo.name;
	SPRINT(isdn_port, "%d", e_callerinfo.isdn_port);
	argv[6+i] = isdn_port;
	argv[7+i] = NULL; /* check also number of args above */
	execve("/bin/sh", argv, environ);
}


/*
 * process hangup 'file'
 */
void EndpointAppPBX::action_hangup_file(void)
{
	struct route_param *rparam;
	char *file, *content, *mode;
	FILE *fp;

	/* get file / content */
	if (!(rparam = routeparam(e_action, PARAM_FILE)))
		file = rparam->string_value;
	else
		file = "";
	if (!(rparam = routeparam(e_action, PARAM_CONTENT)))
		content = rparam->string_value;
	else
		content = e_extdialing;
	if (!(rparam = routeparam(e_action, PARAM_APPEND)))
		mode = "a";
	else
		mode = "w";
	if (file[0] == '\0')
	{
		printlog("%3d  action   FILE no filename given.\n", ea_endpoint->ep_serial);
		PERROR("EPOINT(%d): terminal %s: NO FILENAME GIVEN for 'file' action. see routing.conf\n", ea_endpoint->ep_serial, e_ext.number);
		return;
	}
	if (!(fp = fopen(file, mode)))
	{
		printlog("%3d  action   FILE file '%s' cannot be opened. (errno = %d)\n", ea_endpoint->ep_serial, file, errno);
		PERROR("EPOINT(%d): terminal %s: given file '%s' cannot be opened. see routing.conf\n", ea_endpoint->ep_serial, e_ext.number, file);
		return;
	}
	printlog("%3d  action   FILE file='%s' content='%s'\n", ea_endpoint->ep_serial, file, content);
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d): terminal %s: writing file '%s' with content '%s'.\n", ea_endpoint->ep_serial, e_ext.number, file, content);
	fprintf(fp, "%s\n", content);
	fclose(fp);
}


/*
 * process init 'pick'
 */
void EndpointAppPBX::action_init_pick(void)
{
	struct route_param *rparam;
	char *extensions = NULL;

	if ((rparam = routeparam(e_action, PARAM_EXTENSIONS)))
		extensions = rparam->string_value;
	
	printlog("%3d  action   PICK\n", ea_endpoint->ep_serial);
	pick_call(extensions);
}


/*
 * process dialing 'password'
 */
void EndpointAppPBX::action_dialing_password(void)
{
	struct port_list *portlist = ea_endpoint->ep_portlist;

	/* prompt for password */
	if (e_extdialing[0] == '\0')
	{
		/* give password tone */
		set_tone(portlist, "password");
	} else // ELSE!!
	if (e_extdialing[1] == '\0')
	{
		/* give password tone */
		set_tone(portlist, "dialing");
	}

	/* wait until all digits are dialed */
	if (strlen(e_ext.password) != strlen(e_extdialing))
		return; /* more digits needed */

	/* check the password */
	if (e_ext.password[0]=='\0' || (strlen(e_ext.password)==strlen(e_extdialing) && !!strcmp(e_ext.password,e_extdialing)))
	{
		printlog("%3d  action   PASSWORD WRITE password wrong\n", ea_endpoint->ep_serial);
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): password wrong %s\n", ea_endpoint->ep_serial, e_extdialing);
		e_connectedmode = 0;
		e_dtmf = 0;
		new_state(EPOINT_STATE_OUT_DISCONNECT);
		message_disconnect_port(portlist, CAUSE_NORMAL, LOCATION_PRIVATE_LOCAL, "");
		set_tone(portlist, "cause_10");
		return;
	}

	/* write caller id if ACTION_PASSWORD_WRITE was selected */
	if (e_action)
	if (e_action->index == ACTION_PASSWORD_WRITE)
	{
		append_callbackauth(e_ext.number, &e_callbackinfo);
		printlog("%3d  action   PASSWORD WRITE password written\n", ea_endpoint->ep_serial);
	}

	/* make call state  */
	new_state(EPOINT_STATE_IN_OVERLAP);
	e_ruleset = ruleset_main;
	if (e_ruleset)
		e_rule = e_ruleset->rule_first;
	e_action = NULL;
	e_dialinginfo.number[0] = '\0';
	e_extdialing = e_dialinginfo.number;
	set_tone(portlist, "dialpbx");
}

void EndpointAppPBX::action_dialing_password_wr(void)
{
	action_dialing_password();
}


/* general process dialing of incoming call
 * depending on the detected prefix, subfunctions above (action_*) will be
 * calles.
 */
void EndpointAppPBX::process_dialing(void)
{
	struct port_list *portlist = ea_endpoint->ep_portlist;
	struct message *message;
	struct route_param *rparam;

//#warning Due to HANG-BUG somewhere here, I added some HANG-BUG-DEBUGGING output that cannot be disabled. after bug has been found, this will be removed.
//PDEBUG(~0, "HANG-BUG-DEBUGGING: entered porcess_dialing\n");
	portlist = ea_endpoint->ep_portlist;
	/* check if we have a port instance linked to our epoint */
	if (!portlist)
	{
		portlist_error:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): note: dialing call requires exactly one port object to process dialing. this case could happen due to a parked call. we end dialing here.\n", ea_endpoint->ep_serial, e_ext.number);
		e_action_timeout = 0;
		e_match_timeout = 0;
		return;
	}
	if (portlist->next)
	{
		goto portlist_error;
	}

	/* check nesting levels */
	if (++e_rule_nesting > RULE_NESTING)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): rules are nesting too deep. (%d levels) check for infinite loops in routing.conf\n", ea_endpoint->ep_serial, e_rule_nesting);
		new_state(EPOINT_STATE_OUT_DISCONNECT);
		message_disconnect_port(portlist, CAUSE_UNSPECIFIED, LOCATION_PRIVATE_LOCAL, "");
		set_tone(portlist, "cause_3f");
		e_action_timeout = 0;
		e_match_timeout = 0;
		goto end;
	}

//PDEBUG(~0, "HANG-BUG-DEBUGGING: before action-timeout processing\n");
	/* process timeout */
	if (e_action_timeout)
	{
		e_action_timeout = 0;
		if (e_state == EPOINT_STATE_CONNECT)
		{
			PDEBUG(DEBUG_ROUTE|DEBUG_EPOINT, "EPOINT(%d): action timed out, but we already have connected, so we stop timer and continue.\n", ea_endpoint->ep_serial);
			goto end;
		}
		if (e_action->index == ACTION_DISCONNECT
		 || e_state == EPOINT_STATE_OUT_DISCONNECT)
		{
			/* release after disconnect */
			release(RELEASE_ALL, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL);
			goto end;
		}
		release(RELEASE_CALL, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL, LOCATION_PRIVATE_LOCAL, 0);
		e_action = e_action->next;
		if (!e_action)
		{
			/* nothing more, so we release */
			PDEBUG(DEBUG_ROUTE|DEBUG_EPOINT, "EPOINT(%d): action timed out, and we have no next action, so we disconnect.\n", ea_endpoint->ep_serial);
			new_state(EPOINT_STATE_OUT_DISCONNECT);
			message_disconnect_port(portlist, CAUSE_NORMAL, LOCATION_PRIVATE_LOCAL, "");
			set_tone(portlist, "cause_3f");
			goto end;
		}
		PDEBUG(DEBUG_ROUTE|DEBUG_EPOINT, "EPOINT(%d): continueing with action '%s'.\n", ea_endpoint->ep_serial, action_defs[e_action->index].name);
		goto action_timeout;
	}

//PDEBUG(~0, "HANG-BUG-DEBUGGING: before setup/overlap state checking\n");
	if (e_state!=EPOINT_STATE_IN_SETUP
	 && e_state!=EPOINT_STATE_IN_OVERLAP)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): we are not in incomming setup/overlap state, so we ignore init/dialing process.\n", ea_endpoint->ep_serial, e_rule_nesting);
		e_match_timeout = 0;
		goto end;
	}

#if 0
	/* check if we do menu selection */
	if (e_action==NUMB_ACTION_NONE && (e_dialinginfo.number[0]=='*' || e_dialinginfo.number[0]=='#'))
	/* do menu selection */
	if (e_ext.display_menu)
	{
		if (portlist->port_type==PORT_TYPE_DSS1_NT_IN || portlist->port_type==PORT_TYPE_DSS1_NT_OUT) /* only if the dialing terminal is an isdn telephone connected to an internal port */
		{
			e_dialinginfo.number[0] = '\0';
			e_action = NUMB_ACTION_MENU;
			e_menu = 0;
			process_dialing();
			e_match_timeout = 0;
			goto end;
		}
		/* invalid dialing */
		message_disconnect_port(portlist, CAUSE_INCALID, LOCATION_PRIVATE_LOCAL, "");
			message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_DISCONNECT);
			message->param.disconnectinfo.cause = CAUSE_INVALID;
			message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
				} else
				{
					message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_NOTIFY);
					SCPY(message->param.notifyinfo.display,get_isdn_cause(LOCATION_PRIVATE_LOCAL, epoint->e_ext.display_cause, param->disconnectinfo.location, param->disconnectinfo.cause));
				}
			message_put(message);
			logmessage(message);
		}
		new_state(EPOINT_STATE_OUT_DISCONNECT);
		set_tone(portlist,"cause_1c");
		e_match_timeout = 0;
		goto end;
	}
#endif

//PDEBUG(~0, "HANG-BUG-DEBUGGING: before e_action==NULL\n");
	/* if no action yet, we will call try to find a matching rule */
	if (!e_action)
	{
		/* be sure that all selectors are initialized */
		e_select = 0;

		/* check for external call */
		if (!strncmp(e_dialinginfo.number, "extern:", 7))
		{
			e_extdialing = e_dialinginfo.number+7;
			e_action = &action_external;
			goto process_action;
		}
		/* check for internal call */
		if (!strncmp(e_dialinginfo.number, "intern:", 7))
		{
			e_extdialing = e_dialinginfo.number+7;
			e_action = &action_internal;
			goto process_action;
		}
		/* check for chan call */
		if (!strncmp(e_dialinginfo.number, "chan:", 5))
		{
			e_extdialing = e_dialinginfo.number+4;
			e_action = &action_chan;
			goto process_action;
		}
		/* check for vbox call */
		if (!strncmp(e_dialinginfo.number, "vbox:", 5))
		{
			e_extdialing = e_dialinginfo.number+5;
			e_action = &action_vbox;
			goto process_action;
		}

		if (e_match_timeout && now_d>=e_match_timeout)
		{
			/* return timeout rule */
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d): terminal '%s' dialing: '%s', timeout in ruleset '%s'\n", ea_endpoint->ep_serial, e_ext.number, e_dialinginfo.number, e_ruleset->name);
			e_match_timeout = 0;
			e_action = e_match_to_action;
			e_extdialing = e_match_to_extdialing;
			printlog("%3d  routing  TIMEOUT processing action '%s' (line %d)\n", ea_endpoint->ep_serial, action_defs[e_action->index].name, e_action->line);

		} else
		{
//PDEBUG(~0, "HANG-BUG-DEBUGGING: before routing\n");
			/* check for matching rule */
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d): terminal '%s' dialing: '%s', checking matching rule of ruleset '%s'\n", ea_endpoint->ep_serial, e_ext.number, e_dialinginfo.number, e_ruleset->name);
			if (e_ruleset)
			{
				e_action = route(e_ruleset);
				if (e_action)
					printlog("%3d  routing  MATCH processing action '%s' (line %d)\n", ea_endpoint->ep_serial, action_defs[e_action->index].name, e_action->line);
			} else
			{
				e_action = &action_disconnect;
				if (e_action)
					printlog("%3d  routing  NO MAIN RULESET, DISCONNECTING! '%s'\n", ea_endpoint->ep_serial, action_defs[e_action->index].name);
			}
//PDEBUG(~0, "HANG-BUG-DEBUGGING: after routing\n");
		}
		if (!e_action)
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d): no rule within the current ruleset matches yet.\n", ea_endpoint->ep_serial, e_ext.number);
			goto display;
		}

		/* matching */
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): a rule with action '%s' matches.\n", ea_endpoint->ep_serial, action_defs[e_action->index].name);

		action_timeout:

		/* set timeout */
		e_action_timeout = 0;
		if (e_action->timeout)
		{
			e_action_timeout = now_d + e_action->timeout;
			PDEBUG(DEBUG_ROUTE|DEBUG_EPOINT, "EPOINT(%d): action has a timeout of %d secods.\n", ea_endpoint->ep_serial, e_action->timeout);
		}

		process_action:
		/* check param proceeding / alerting / connect */
		if ((rparam = routeparam(e_action, PARAM_CONNECT)))
		{
			/* NOTE: we may not change our state to connect, because dialing will then not possible */
			e_dtmf = 1;
			memset(&e_connectinfo, 0, sizeof(e_connectinfo));
			message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_CONNECT);
			message_put(message);
			logmessage(message);
		} else
		if ((rparam = routeparam(e_action, PARAM_ALERTING)))
		{
			/* NOTE: we may not change our state to alerting, because dialing will then not possible */
			memset(&e_connectinfo, 0, sizeof(e_connectinfo));
			message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_ALERTING);
			message_put(message);
			logmessage(message);
		} else
		if ((rparam = routeparam(e_action, PARAM_PROCEEDING)))
		{
			/* NOTE: we may not change our state to proceeding, because dialing will then not possible */
			memset(&e_connectinfo, 0, sizeof(e_connectinfo));
			message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_PROCEEDING);
			message_put(message);
			logmessage(message);
		}

		if (action_defs[e_action->index].init_func)
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d): terminal %s: current action '%s' has a init function, so we call it...\n", ea_endpoint->ep_serial, e_ext.number, action_defs[e_action->index].name);
			(this->*(action_defs[e_action->index].init_func))();
		}
		if (e_state!=EPOINT_STATE_IN_SETUP
		 && e_state!=EPOINT_STATE_IN_OVERLAP)
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d): AFTER init process: we are not in incomming setup/overlap state anymore, so we ignore further dialing process.\n", ea_endpoint->ep_serial, e_rule_nesting);
			goto display_action;
		}
	}

	/* show what we are doing */
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d): terminal '%s' action: %s (dialing '%s')\n", ea_endpoint->ep_serial, e_ext.number, action_defs[e_action->index].name, e_extdialing);
	/* go to action's dialing function */
	if (action_defs[e_action->index].dialing_func)
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): terminal %s: current action '%s' has a dialing function, so we call it...\n", ea_endpoint->ep_serial, e_ext.number, action_defs[e_action->index].name);
		(this->*(action_defs[e_action->index].dialing_func))();
	}

	/* display selected dialing action if enabled and still in setup state */
	display_action:
	if (e_action)
	{
		if (e_action->index==ACTION_MENU
		 || e_action->index==ACTION_REDIAL
		 || e_action->index==ACTION_REPLY
		 || e_action->index==ACTION_TIMER
		 || e_action->index==ACTION_CALCULATOR
		 || e_action->index==ACTION_TEST)
			goto end;
	}
	display:
	if (!e_ext.display_dialing)
		goto end;
	if (e_state==EPOINT_STATE_IN_OVERLAP || e_state==EPOINT_STATE_IN_PROCEEDING || e_state==EPOINT_STATE_IN_ALERTING || e_state==EPOINT_STATE_CONNECT/* || e_state==EPOINT_STATE_IN_DISCONNECT || e_state==EPOINT_STATE_OUT_DISCONNECT*/)
	if (portlist->port_type==PORT_TYPE_DSS1_NT_IN || portlist->port_type==PORT_TYPE_DSS1_NT_OUT) /* only if the dialing terminal is an isdn telephone connected to an internal port */
	{
		message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_NOTIFY);

		if (!e_action)
		{
			SPRINT(message->param.notifyinfo.display, "> %s", e_dialinginfo.number);
		} else
		{
			SPRINT(message->param.notifyinfo.display, "%s%s%s", action_defs[e_action->index].name, (e_extdialing[0])?" ":"", e_extdialing);
		}

		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): terminal %s displaying interpreted dialing '%s'\n", ea_endpoint->ep_serial, e_ext.number, message->param.notifyinfo.display);
		message_put(message);
		logmessage(message);
	}

end:
	e_rule_nesting--;
	return;
}


/* some services store information after hangup */
void EndpointAppPBX::process_hangup(int cause, int location)
{
	char callertext[256], dialingtext[256];
	int writeext = 0, i;

	PDEBUG(DEBUG_EPOINT, "EPOINT(%d): terminal '%s'\n", ea_endpoint->ep_serial, e_ext.number);
	if (e_ext.number[0])
	{
		if (read_extension(&e_ext, e_ext.number))
			writeext = 0x10;

		if (!e_start)
		{
			time(&e_start);
			e_stop = 0;
		} else
		if (!e_stop)
			time(&e_stop);
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d): writing connect from %s to %s into logfile of %s\n", ea_endpoint->ep_serial, e_callerinfo.id, e_dialinginfo.number, e_ext.number);
		switch(e_dialinginfo.itype)
		{
			case INFO_ITYPE_CHAN:
			SPRINT(dialingtext, "chan:%s", e_dialinginfo.number);
			break;
			case INFO_ITYPE_INTERN:
			SPRINT(dialingtext, "intern:%s", e_dialinginfo.number);
			break;
			case INFO_ITYPE_VBOX:
			SPRINT(dialingtext, "vbox:%s", e_dialinginfo.number);
			break;
			default:
			SPRINT(dialingtext, "%s", e_dialinginfo.number);
		}

		if (e_callerinfo.id[0])
			SPRINT(callertext, "%s", numberrize_callerinfo(e_callerinfo.id, e_callerinfo.ntype));
		else
			SPRINT(callertext, "unknown");
		/* allpy restriction */
		if (!e_ext.anon_ignore && e_callerinfo.present==INFO_PRESENT_RESTRICTED)
			SPRINT(callertext, "anonymous");
		if (e_callerinfo.intern[0]) /* add intern if present */
			UNPRINT(strchr(callertext,'\0'), sizeof(callertext)-1+strlen(callertext), " (intern %s)", e_callerinfo.intern);
		if (e_callerinfo.voip[0]) /* add voip if present */
			UNPRINT(strchr(callertext,'\0'), sizeof(callertext)-1+strlen(callertext), " (voip %s)", e_callerinfo.voip);
		write_log(e_ext.number, callertext, dialingtext, e_start, e_stop, 0, cause, location);

		/* store last received call for reply-list */
		if (e_callerinfo.id[0] || e_callerinfo.intern[0])
		if (e_ext.anon_ignore || e_callerinfo.present!=INFO_PRESENT_RESTRICTED)
		{
			if (e_callerinfo.intern[0])
				SPRINT(callertext, "intern:%s", e_callerinfo.intern);
			else
				SPRINT(callertext, "extern:%s", numberrize_callerinfo(e_callerinfo.id, e_callerinfo.ntype));
			if (!!strcmp(callertext, e_ext.last_in[0]))
			{
				i = MAX_REMEMBER-1;
				while(i)
				{
					UCPY(e_ext.last_in[i], e_ext.last_in[i-1]);
					i--;
				}
				SCPY(e_ext.last_in[0], callertext);
				writeext |= 1; /* store extension later */
				PDEBUG(DEBUG_EPOINT, "EPOINT(%d): terminal %s: storing last received caller id '%s'.\n", ea_endpoint->ep_serial, e_ext.number, e_ext.last_in[0]);
			} else
				PDEBUG(DEBUG_EPOINT, "EPOINT(%d): terminal %s: cannot store last received id '%s' because it is identical with the last one.\n", ea_endpoint->ep_serial, e_ext.number, callertext);
		}
	}

	/* write extension if needed */
	if (writeext == 0x11)
		write_extension(&e_ext, e_ext.number);

	if (e_action)
	{
		if (action_defs[e_action->index].hangup_func)
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d): terminal %s: current action '%s' has a hangup function, so we call it...\n", ea_endpoint->ep_serial, e_ext.number, action_defs[e_action->index].name);
			(this->*(action_defs[e_action->index].hangup_func))();
		}
	}
}

