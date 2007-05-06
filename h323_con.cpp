///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// PBX4Linux                                                                 //
//                                                                           //
//---------------------------------------------------------------------------//
// Copyright: Andreas Eversberg                                              //
//                                                                           //
// h323_con connection class                                                 //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include "main.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

//
// constructor
//
H323_con::H323_con(H323_ep &endpoint, unsigned callReference) : H323Connection(endpoint, callReference)
{
	PDEBUG(DEBUG_H323, "H323 connection  constuctor\n");

	SetAudioJitterDelay(0, 0);
}


//
// destructor
//
H323_con::~H323_con()
{
	class H323Port *port;
	const unsigned char *token_string = callToken;
	struct message *message;

	mutex_h323.Wait();

	// get ioport
	port = (class H323Port *)find_port_with_token((char *)token_string);
	if (!port)
	{
		PERROR("no port with token '%s'\n", token_string);
	} else
	{
		/* sending release (if not already) */
		message = message_create(port->p_serial, ACTIVE_EPOINT(port->p_epointlist), PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = 16;
		message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		message_put(message);
	}

	mutex_h323.Signal();

	PDEBUG(DEBUG_H323, "H323 connection  destuctor\n");
}


//
// AnswerCallResponse (incoming call)
//
H323Connection::AnswerCallResponse H323_con::OnAnswerCall(const PString &, const H323SignalPDU &setupPDU, H323SignalPDU &connectPDU)
{  
	class H323Port *port;
	const char *calleraddress;
	char callerip[32], *extension;
	const char *dialing = NULL;
	const H225_Setup_UUIE &setup = setupPDU.m_h323_uu_pdu.m_h323_message_body;
	const H225_ArrayOf_AliasAddress &adr = setup.m_destinationAddress;
	PINDEX i;
	const unsigned char *token_string = callToken;
	struct message *message;
	class Endpoint *epoint;

	const Q931 setup_q931 = setupPDU.GetQ931();
	PString calling_number;
	PString redir_number;
	unsigned type, plan, present, screen, reason;

	struct caller_info *callerinfo;
	struct dialing_info *dialinginfo;
	struct capa_info *capainfo;
	struct redir_info *redirinfo;
	char option[64] = "";

	PDEBUG(DEBUG_H323, "H323 connection  incoming call\n");

	mutex_h323.Wait();

	// alloc new h323 port structure
	if (!(port = new H323Port(PORT_TYPE_H323_IN, (char *)token_string, NULL)))
	{
		mutex_h323.Signal();
		return AnswerCallDenied;
	}
	callerinfo = &port->p_callerinfo;
	redirinfo = &port->p_redirinfo;
	capainfo = &port->p_capainfo;
	dialinginfo = &port->p_dialinginfo;

	memset(callerinfo, 0, sizeof(struct caller_info));
	memset(redirinfo, 0, sizeof(struct redir_info));
	memset(capainfo, 0, sizeof(struct capa_info));
	memset(dialinginfo, 0, sizeof(struct dialing_info));

	callerinfo->itype = INFO_ITYPE_H323;

	// get calling party information
	if (setup_q931.GetCallingPartyNumber(calling_number, &plan, &type, &present, &screen))
	{
		SCPY(callerinfo->id, calling_number.GetPointer());
		switch (present)
		{
			case 1:
			callerinfo->present = INFO_PRESENT_RESTRICTED;
			break;
			case 2:
			callerinfo->present = INFO_PRESENT_NOTAVAIL;
			break;
			default:
			callerinfo->present = INFO_PRESENT_ALLOWED;
			break;
		}
		switch (type)
		{
			case Q931::InternationalType:
			callerinfo->ntype = INFO_NTYPE_INTERNATIONAL;
			break;
			case Q931::NationalType:
			callerinfo->ntype = INFO_NTYPE_NATIONAL;
			break;
			case Q931::SubscriberType:
			callerinfo->ntype = INFO_NTYPE_SUBSCRIBER;
			break;
			default:
			callerinfo->ntype = INFO_NTYPE_UNKNOWN;
			break;
		}
		switch (screen)
		{
			case 0:
			callerinfo->screen = INFO_SCREEN_USER;
			break;
			default:
			callerinfo->screen = INFO_SCREEN_NETWORK;
			break;
		}
	}
	redirinfo->itype = INFO_ITYPE_H323;
	// get redirecting number information
	if (setup_q931.GetRedirectingNumber(redir_number, &plan, &type, &present, &screen, &reason))
	{
		SCPY(redirinfo->id, redir_number.GetPointer());
		switch (present)
		{
			case 1:
			redirinfo->present = INFO_PRESENT_RESTRICTED;
			break;
			case 2:
			redirinfo->present = INFO_PRESENT_NOTAVAIL;
			break;
			default:
			redirinfo->present = INFO_PRESENT_ALLOWED;
			break;
		}
		switch (type)
		{
			case Q931::InternationalType:
			redirinfo->ntype = INFO_NTYPE_INTERNATIONAL;
			break;
			case Q931::NationalType:
			redirinfo->ntype = INFO_NTYPE_NATIONAL;
			break;
			case Q931::SubscriberType:
			redirinfo->ntype = INFO_NTYPE_SUBSCRIBER;
			break;
			default:
			redirinfo->ntype = INFO_NTYPE_UNKNOWN;
			break;
		}
		switch (screen)
		{
			case 0:
			redirinfo->screen = INFO_SCREEN_USER;
			break;
			default:
			redirinfo->screen = INFO_SCREEN_NETWORK;
			break;
		}
		switch (reason)
		{
			case 1:
			redirinfo->reason = INFO_REDIR_BUSY;
			break;
			case 2:
			redirinfo->reason = INFO_REDIR_NORESPONSE;
			break;
			case 15:
			redirinfo->reason = INFO_REDIR_UNCONDITIONAL;
			break;
			case 10:
			redirinfo->reason = INFO_REDIR_CALLDEFLECT;
			break;
			case 9:
			redirinfo->reason = INFO_REDIR_OUTOFORDER;
			break;
			default:
			redirinfo->reason = INFO_REDIR_UNKNOWN;
			break;
		}
	}

	// get remote party h323-address information
	calleraddress = GetRemotePartyAddress();
	callerip[0] = '\0';
	if (calleraddress)
	{
		if (strstr(calleraddress, "ip$"))
		{
			SCPY(callerip, strstr(calleraddress, "ip$")+3);
			if (strchr(callerip, ':'))
				*strchr(callerip, ':') = '\0';
			memmove(strstr(calleraddress, "ip$"), strstr(calleraddress, "ip$")+3, strlen(strstr(calleraddress, "ip$")+3)+1);
		}
		if (strchr(calleraddress, ':'))
			*strchr(calleraddress, ':') = '\0';
	}

	// get dialing information
	for(i=0; i<adr.GetSize(); i++)
		if (adr[i].GetTag() == H225_AliasAddress::e_dialedDigits)
			dialing = H323GetAliasAddressString(adr[i]);
	if (!dialing)
		dialing = "";

	// fill port's information
	if (calleraddress)
		SCPY(callerinfo->voip, (char *)calleraddress);
	capainfo->bearer_mode = INFO_BMODE_CIRCUIT;
	capainfo->bearer_info1 = (options.law=='u')?INFO_INFO1_ULAW:INFO_INFO1_ALAW;
	capainfo->bearer_capa = INFO_BC_SPEECH;

	// change to incoming setup state
	port->new_state(PORT_STATE_IN_OVERLAP);

	// allocate new endpoint
	if (!(epoint = new Endpoint(port->p_serial, 0)))
	{
		// error allocating endpoint
		PDEBUG(DEBUG_H323, "h323-connection(%s) rejecting call because cannot create epoint for '%s'\n", port->p_name, callerinfo->id);
		delete port;
		port = NULL;
		mutex_h323.Signal();
		return AnswerCallDenied;
	}
	if (!(epoint->ep_app = new DEFAULT_ENDPOINT_APP(epoint)))
	{
		PERROR("no memory for application\n");
		exit(-1);
	}
	if (!(port->epointlist_new(epoint->ep_serial)))
	{
		PERROR("no memory for epointlist\n");
		exit(-1);
	}
	port->set_tone(NULL, "");

	// send setup message to endpoint
	message = message_create(port->p_serial, ACTIVE_EPOINT(port->p_epointlist), PORT_TO_EPOINT, MESSAGE_SETUP);
	message->param.setup.port_type = port->p_type;

	// before we start, we may check for h323_gateway entry
	if (callerip[0])
	{
		extension = parse_h323gateway(callerip, option, sizeof(option));
		if (extension)
		{
			PDEBUG(DEBUG_H323, "h323-connection(%s) gateway '%s' is mapped to extension '%s' (option= '%s')\n", port->p_name, callerip, extension, option);
			SCPY(callerinfo->id, extension);
			SCPY(callerinfo->intern, extension);
			callerinfo->itype = INFO_ITYPE_INTERN;
			callerinfo->screen = INFO_SCREEN_NETWORK;
		} else
		{
			PDEBUG(DEBUG_H323, "h323-connection(%s) gateway '%s' is not mapped to any extension. (port_type=0x%x)\n", port->p_name, callerip, port->p_type);
			// get the default dialing external dialing string
		}
	}

	// default dialing for extenal calls
	if (!callerinfo->intern[0] && !dialing[0])
		dialing = options.h323_icall_prefix;

	// dialing information
	if (callerip[0] || dialing[0])
	{
		SCPY(dialinginfo->number, (char *)dialing);
		dialinginfo->ntype = INFO_NTYPE_UNKNOWN;
	}

	memcpy(&message->param.setup.callerinfo, callerinfo, sizeof(struct caller_info));
	memcpy(&message->param.setup.dialinginfo, dialinginfo, sizeof(struct dialing_info));
	memcpy(&message->param.setup.redirinfo, redirinfo, sizeof(struct redir_info));
	memcpy(&message->param.setup.capainfo, capainfo, sizeof(struct capa_info));
	message->param.setup.dtmf = 1;
	message_put(message);

	port->p_h323_connect = &(connectPDU.GetQ931());

	mutex_h323.Signal();

	if (!strcasecmp(option, "connect") || !strcasecmp(option, "dtmf"))
	{
		port->new_state(PORT_STATE_CONNECT);
		return AnswerCallNow;
	} else
	{
		return AnswerCallDeferred;
	}
}


//
// OnOutgoingCall (outgoing call)
//
BOOL H323_con::OnOutgoingCall(const H323SignalPDU &connectPDU)
{  
	class H323Port *port;
	const char *calleraddress;
	char callerip[32];
	const unsigned char *token_string = callToken;
	struct message *message;
//	H225_Connect_UUIE &connect_uuie = connectPDU.m_h323_uu_pdu.m_h323_message_body;

	const Q931 connect_q931 = connectPDU.GetQ931();
	PString connect_number;
	unsigned type = 0, plan = 0, present = 0, screen = 0;
	struct connect_info *connectinfo;

	PDEBUG(DEBUG_H323, "H323 connection  outgoing call is connected.\n");

	mutex_h323.Wait();

	if (!(port = (class H323Port *)find_port_with_token((char *)token_string)))
	{
		PERROR(" cannot find port with token '%s'\n", token_string);
		mutex_h323.Signal();
		return FALSE;
	}
	connectinfo = &port->p_connectinfo;

	if (port->p_type == PORT_TYPE_H323_IN)
	{
		PDEBUG(DEBUG_H323, "H323 endpoint  OnConnectionEstablished() incoming port\n");
	}
	if (port->p_type == PORT_TYPE_H323_OUT)
	{
		PDEBUG(DEBUG_H323, "H323 endpoint  OnConnectionEstablished() outgoing port\n");
		if (port->p_state==PORT_STATE_OUT_SETUP
		 || port->p_state==PORT_STATE_OUT_OVERLAP
		 || port->p_state==PORT_STATE_OUT_PROCEEDING
		 || port->p_state==PORT_STATE_OUT_ALERTING)
		{
			// get remote party h323-address information
			calleraddress = GetRemotePartyAddress();
			callerip[0] = '\0';
			if (calleraddress)
			{
				if (strchr(calleraddress, '$'))
				{
					SCPY(callerip, strchr(calleraddress, '$'));
					callerip[sizeof(callerip)-1] = '\0';
					if (strchr(callerip, ':'))
						*strchr(callerip, ':') = '\0';
				}
				SCPY(connectinfo->voip, (char *)calleraddress);
			}

			// get COLP
			memset(connectinfo, 0, sizeof(struct connect_info));
			connectinfo->itype = INFO_ITYPE_H323;
			if (connect_q931.GetConnectedNumber(connect_number, &plan, &type, &present, &screen))
			{
				SCPY(connectinfo->id, connect_number.GetPointer());
				switch (present)
				{
					case 1:
					connectinfo->present = INFO_PRESENT_RESTRICTED;
					break;
					case 2:
					connectinfo->present = INFO_PRESENT_NOTAVAIL;
					break;
					default:
					connectinfo->present = INFO_PRESENT_ALLOWED;
				}
				switch (type)
				{
					case Q931::InternationalType:
					connectinfo->ntype = INFO_NTYPE_INTERNATIONAL;
					break;
					case Q931::NationalType:
					connectinfo->ntype = INFO_NTYPE_NATIONAL;
					break;
					case Q931::SubscriberType:
					connectinfo->ntype = INFO_NTYPE_SUBSCRIBER;
					break;
					default:
					connectinfo->ntype = INFO_NTYPE_UNKNOWN;
				}
				switch (screen)
				{
					case 0:
					connectinfo->screen = INFO_SCREEN_USER;
					break;
					default:
					connectinfo->screen = INFO_SCREEN_NETWORK;
				}
			}
			port->new_state(PORT_STATE_CONNECT);
			message = message_create(port->p_serial, ACTIVE_EPOINT(port->p_epointlist), PORT_TO_EPOINT, MESSAGE_CONNECT);
			memcpy(&message->param.connectinfo, connectinfo, sizeof(struct connect_info));
			message_put(message);
		}
	}

	mutex_h323.Signal();

	return H323Connection::OnOutgoingCall(connectPDU);
}


//
// send setup information to the called h323 user
//
BOOL H323_con::OnSendSignalSetup(H323SignalPDU &setupPDU)
{
	H225_Setup_UUIE &setup = setupPDU.m_h323_uu_pdu.m_h323_message_body;
	H225_ArrayOf_AliasAddress &adr = setup.m_sourceAddress;
	H225_AliasAddress new_alias;
	PString calling_number;
	PString calling_alias;
	PString dialing_number;
	PString redir_number;
	int type, present, screen, reason;
	class H323Port *port;
	const unsigned char *token_string = callToken;

	struct caller_info *callerinfo;
	struct dialing_info *dialinginfo;
	struct capa_info *capainfo;
	struct redir_info *redirinfo;

	mutex_h323.Wait();

	if (!(port = (class H323Port *)find_port_with_token((char *)token_string)))
	{
		PERROR(" no port with token '%s'\n", token_string);
		mutex_h323.Signal();
		return FALSE;
	}
	callerinfo = &port->p_callerinfo;
	redirinfo = &port->p_redirinfo;
	capainfo = &port->p_capainfo;
	dialinginfo = &port->p_dialinginfo;

	PDEBUG(DEBUG_H323, "H323-connection  sending modified setup signal '%s'->'%s'\n", callerinfo->id, dialinginfo->number);


	if (callerinfo->present!=INFO_PRESENT_NULL)
	{
		calling_alias = numberrize_callerinfo(callerinfo->id, callerinfo->ntype);
		H323SetAliasAddress(calling_alias, new_alias);
		adr.SetSize(adr.GetSize()+1);
		adr[adr.GetSize()-1] = new_alias;

		calling_number = callerinfo->id;
		switch(callerinfo->ntype)
		{
			case INFO_NTYPE_SUBSCRIBER:
			type = Q931::SubscriberType;
			break;
			case INFO_NTYPE_NATIONAL:
			type = Q931::NationalType;
			break;
			case INFO_NTYPE_INTERNATIONAL:
			type = Q931::InternationalType;
			break;
			default: /* INFO_NTYPE_UNKNOWN */
			type = Q931::UnknownType;
		}

		switch(callerinfo->present)
		{
			case INFO_PRESENT_RESTRICTED:
			present = 1;
			break;
			case INFO_PRESENT_NOTAVAIL:
			present = 2;
			break;
			default: /* INFO_PRESENT_ALLOWED */
			present = 0;
		}
		switch(callerinfo->screen)
		{
			case INFO_SCREEN_USER:
			screen = 0;
			break;
			default: /* INFO_SCREEN_NETWORK */
			screen = 3;
		}

		Q931 &new_q931 = setupPDU.GetQ931();
		new_q931.SetCallingPartyNumber(calling_number, Q931::ISDNPlan, type, present, screen);
	}

	if (redirinfo->present!=INFO_PRESENT_NULL)
	{
		if (redirinfo->present==INFO_PRESENT_ALLOWED)
		{
			redir_number = callerinfo->id;
		} else
			redir_number = "";

		switch(redirinfo->ntype)
		{
			case INFO_NTYPE_SUBSCRIBER:
			type = Q931::SubscriberType;
			break;
			case INFO_NTYPE_NATIONAL:
			type = Q931::NationalType;
			break;
			case INFO_NTYPE_INTERNATIONAL:
			type = Q931::InternationalType;
			break;
			default: /* INFO_TYPE_UNKNOWN */
			type = Q931::UnknownType;
		}

		switch(redirinfo->present)
		{
			case INFO_PRESENT_RESTRICTED:
			present = 1;
			break;
			case INFO_PRESENT_NOTAVAIL:
			present = 2;
			break;
			default: /* INFO_PRESENT_ALLOWED */
			present = 0;
		}

		switch(redirinfo->reason)
		{
			case INFO_REDIR_BUSY:
			reason = 1;
			break;
			case INFO_REDIR_NORESPONSE:
			reason = 2;
			break;
			case INFO_REDIR_UNCONDITIONAL:
			reason = 15;
			break;
			case INFO_REDIR_OUTOFORDER:
			reason = 9;
			break;
			case INFO_REDIR_CALLDEFLECT:
			reason = 10;
			break;
			default: /* INFO_REDIR_UNKNOWN */
			reason = 0;
		}

		Q931 &new_q931 = setupPDU.GetQ931();
		new_q931.SetRedirectingNumber(redir_number, Q931::ISDNPlan, type, present, screen, reason);
	}

	if (dialinginfo->number[0])
	{
		dialing_number = dialinginfo->number;

		Q931 &new_q931 = setupPDU.GetQ931();
		new_q931.SetCalledPartyNumber(dialing_number);
	}
	
	mutex_h323.Signal();

	return H323Connection::OnSendSignalSetup(setupPDU);
}


//
// callback for start of channel
//
BOOL H323_con::OnStartLogicalChannel(H323Channel &channel)
{
	if (!H323Connection::OnStartLogicalChannel(channel))
	{
		PERROR("starting logical channel failed!\n");
		return FALSE;
	}

	PDEBUG(DEBUG_H323, "H323 connection  starting logical channel using \"%s\" codec %s :%s\n",
		 channel.GetCapability().GetFormatName().GetPointer(),
		 (channel.GetDirection()==H323Channel::IsTransmitter)?"transmit":"receive",
		 callToken.GetPointer());

	return H323Connection::OnStartLogicalChannel(channel);
}


//
// user input received
//
void H323_con::OnUserInputString (const PString &value)
{
	class H323Port *port;
	const unsigned char *token_string = callToken;
	const unsigned char *value_string = value;
	struct message *message;

	PDEBUG(DEBUG_H323, "H323-connection  received user input'%s'\n", value_string);

	mutex_h323.Wait();

	if (!(port = (class H323Port *)find_port_with_token((char *)token_string)))
	{
		PERROR("no port with token '%s'\n", token_string);
	} else
	{
		while(*value_string)
		{
			message = message_create(port->p_serial, ACTIVE_EPOINT(port->p_epointlist), PORT_TO_EPOINT, MESSAGE_DTMF);
			message->param.dtmf = *value_string++;
			message_put(message);
		}
#if 0
		message = message_create(port->p_serial, ACTIVE_EPOINT(port->p_epointlist), PORT_TO_EPOINT, MESSAGE_INFORMATION);
		SCPY(message->param.information.number, (char *)value_string);
		message_put(message);
#endif
	}

	mutex_h323.Signal();
}
             
