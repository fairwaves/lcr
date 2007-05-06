/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** h323 port                                                                 **
**                                                                           **
\*****************************************************************************/ 

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "main.h"


/*
 * initialize h323 port
 */
H323Port::H323Port(int type, char *portname, struct port_settings *settings) : Port(type, portname, settings)
{
	p_h323_channel_in = p_h323_channel_out = NULL;
	p_h323_connect = NULL;

	/* configure device */
	switch (type)
	{
		case PORT_TYPE_H323_IN:
		break;
		case PORT_TYPE_H323_OUT:
		SPRINT(p_name, "H323_outgoing_port_#%lu", p_serial);
		break;
	}
	if (options.law == 'u')
	{
	}
}


/*
 * destructor
 */
H323Port::~H323Port()
{
}


/*
 * endpoint sends messages to the interface
 */
int H323Port::message_epoint(unsigned long epoint_id, int message_id, union parameter *param)
{
	H323Connection *connection;
	H323Connection::CallEndReason h323_cause;
	char name[sizeof(p_name)];

	if (Port::message_epoint(epoint_id, message_id, param))
		return(1);

	switch(message_id)
	{
		case MESSAGE_mISDNSIGNAL: /* isdn command */
		PDEBUG(DEBUG_H323, "H323Port(%s) mISDN signal not supported.\n", p_name);
		break;

		case MESSAGE_INFORMATION: /* additional digits from endpoint */
		PDEBUG(DEBUG_H323, "H323Port(%s) h323 port with (caller id %s) received more digit information: '%s'\n", p_name, p_callerinfo.id, param->information.number);
/* queue to be done */
		if (p_state != PORT_STATE_OUT_OVERLAP)
		{
			PERROR("H323Port(%s) additinal digits are only possible in outgoing overlap state.\n", p_name);
			break;
		}
		if (strlen(param->information.number)>30)
		{
			PERROR("H323Port(%s) information string too long.\n", p_name);
			break;
		}
		SCAT((char *)p_dialinginfo.number, param->information.number);
		break;

		case MESSAGE_PROCEEDING: /* call of endpoint is proceeding */
		PDEBUG(DEBUG_H323, "H323Port(%s) h323 port with (caller id %s) received proceeding\n", p_name, p_callerinfo.id);
		if (p_state != PORT_STATE_IN_OVERLAP)
		{
			PERROR("H323Port(%s) proceeding command only possible in setup state.\n", p_name);
			break;
		}
		p_state = PORT_STATE_IN_PROCEEDING;
		break;

		case MESSAGE_ALERTING: /* call of endpoint is ringing */
		PDEBUG(DEBUG_H323, "H323Port(%s) h323 port with (caller id %s) received alerting\n", p_name, p_callerinfo.id);
		if (p_state != PORT_STATE_IN_OVERLAP
		 && p_state != PORT_STATE_IN_PROCEEDING)
		{
			PERROR("H323Port(%s) alerting command only possible in setup or proceeding state.\n", p_name);
			break;
		}
		p_state = PORT_STATE_IN_ALERTING;
		UCPY(name, p_name);
		mutex_h323.Signal();
		connection = h323_ep->FindConnectionWithLock(name);
		if (connection)
		{
			if (options.h323_ringconnect && !p_callerinfo.intern[0])
			{
				connection->AnsweringCall(H323Connection::AnswerCallNow);
				p_state = PORT_STATE_CONNECT;
			} else
				connection->AnsweringCall(H323Connection::AnswerCallPending);
			connection->Unlock();
		}
		mutex_h323.Wait();
		break;

		case MESSAGE_CONNECT: /* call of endpoint is connected */
		PDEBUG(DEBUG_H323, "H323Port(%s) h323 port with (caller id %s) received connect\n", p_name, p_callerinfo.id);
		if (p_state != PORT_STATE_IN_OVERLAP
		 && p_state != PORT_STATE_IN_PROCEEDING
		 && p_state != PORT_STATE_IN_ALERTING)
		{
			PDEBUG(DEBUG_H323, "H323Port(%s) connect command only possible in setup, proceeding or alerting state.\n", p_name);
			break;
		}
		new_state(PORT_STATE_CONNECT);
		/* copy connected information */
		memcpy(&p_connectinfo, &param->connectinfo, sizeof(p_connectinfo));
		p_connectinfo.itype = INFO_ITYPE_H323;
		UCPY(name, p_name);
		mutex_h323.Signal();
		connection = h323_ep->FindConnectionWithLock(name);
		if (connection)
		{
			int type, present, screen;
			PString connect_number;
			/* modify connectinfo (COLP) */
			if (p_connectinfo.present!=INFO_PRESENT_NULL)
			{
				connect_number = p_connectinfo.id;
				switch(p_connectinfo.ntype)
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
				switch(p_connectinfo.present)
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
				switch(p_connectinfo.screen)
				{
					case INFO_SCREEN_USER:
					screen = 0;
					break;
					default: /* INFO_SCREEN_NETWORK */
					screen = 3;
				}
#if 0
				if (p_h323_connect)
				{
//PDEBUG(DEBUG_H323, "DDDEBUG: number %s, type=%d, present %d, screen %d\n", p_connectinfo.id, type, present, screen);
					((Q931 *)p_h323_connect)->SetConnectedNumber(connect_number, Q931::ISDNPlan, type, present, screen);
				}
				else
					PERROR("missing p_h323_connect\n");
#endif
			}

			connection->AnsweringCall(H323Connection::AnswerCallNow);
			connection->Unlock();
		}
		mutex_h323.Wait();
		break;

		case MESSAGE_DISCONNECT: /* call has been disconnected */
#if 0
		PDEBUG(DEBUG_H323, "H323Port(%s) h323 port with (caller id %s) received disconnect cause=%d\n", p_name, p_callerinfo.id, param->disconnectinfo.cause);
		/* we just play what we hear from the remote site */
		if (p_state == PORT_STATE_IN_OVERLAP
		 || p_state == PORT_STATE_IN_PROCEEDING)
		{
			/* copy connected information */
			memcpy(&p_connectinfo, &param->connectinfo, sizeof(p_connectinfo));
			UCPY(name, p_name);
			mutex_h323.Signal();
			connection = h323_ep->FindConnectionWithLock(name);
			if (connection)
			{
				connection->AnsweringCall(H323Connection::AnswerCallNow);
				connection->Unlock();
			}
			mutex_h323.Wait();
		}
		new_state(PORT_STATE_DISCONNECT);
		break;
#endif

		case MESSAGE_RELEASE: /* release h323 port */
		PDEBUG(DEBUG_H323, "H323Port(%s) h323 port with (caller id %s) received disconnect cause=%d\n", p_name, p_callerinfo.id, param->disconnectinfo.cause);
		if (p_state != PORT_STATE_IN_OVERLAP
		 && p_state != PORT_STATE_IN_PROCEEDING
		 && p_state != PORT_STATE_IN_ALERTING
		 && p_state != PORT_STATE_OUT_SETUP
		 && p_state != PORT_STATE_OUT_OVERLAP
		 && p_state != PORT_STATE_OUT_PROCEEDING
		 && p_state != PORT_STATE_OUT_ALERTING
		 && p_state != PORT_STATE_CONNECT)
		{
			PERROR("H323Port(%s) disconnect command only possible in setup, proceeding, alerting or connect state.\n", p_name);
			break;
		}

		switch(param->disconnectinfo.cause)
		{
			case 1:
			h323_cause = H323Connection::EndedByNoUser;
			break;

			case 2:
			case 3:
			case 5:
			h323_cause = H323Connection::EndedByUnreachable;
			break;

			case 17:
			h323_cause = H323Connection::EndedByRemoteBusy;
			break;
	
			case 18:
			h323_cause = H323Connection::EndedByNoEndPoint;
			break;
	
			case 19:
			h323_cause = H323Connection::EndedByNoAnswer;
			break;
	
			case 21:
			h323_cause = H323Connection::EndedByRefusal;
			break;
	
			case 27:
			h323_cause = H323Connection::EndedByHostOffline;
			break;
	
			case 47:
			h323_cause = H323Connection::EndedByConnectFail;
			break;
	
			case 65:
			h323_cause = H323Connection::EndedByCapabilityExchange;
			break;
	
			case 42:
			h323_cause = H323Connection::EndedByRemoteCongestion;
			break;
	
			case 41:
			h323_cause = H323Connection::EndedByTemporaryFailure;
			break;
	
			default:
			h323_cause = H323Connection::EndedByRemoteUser;
			break;
		}
		UCPY(name, p_name);
		mutex_h323.Signal();
		h323_ep->ClearCall(name, h323_cause);
		mutex_h323.Wait();

		delete this;
		break;

		case MESSAGE_SETUP: /* dial-out command received from epoint */
		PDEBUG(DEBUG_H323, "H323Port(%s) h323 port received setup from '%s' to '%s'\n", p_name, param->setup.callerinfo.id, param->setup.dialinginfo.number);
		if (p_type!=PORT_TYPE_H323_OUT)
		{
			PERROR("H323Port(%s) cannot dial because h323 port not of outgoing type.\n", p_name);
			break;
		}
		if (p_state != PORT_STATE_IDLE)
		{
			PERROR("H323Port(%s) error: dialing command only possible in idle state.\n", p_name);
			break;
		}

		/* link relation */
		if (p_epointlist)
		{
			PERROR("H323Port(%s) software error: epoint pointer is set in idle state, how bad!! exitting.\n", p_name);
			exit(-1);
		}
		if (!(epointlist_new(epoint_id)))
		{
			PERROR("no memory for epointlist\n");
			exit(-1);
		}

		/* copy setup infos to port */
		memcpy(&p_callerinfo, &param->setup.callerinfo, sizeof(p_callerinfo));
		memcpy(&p_dialinginfo, &param->setup.dialinginfo, sizeof(p_dialinginfo));
		memcpy(&p_capainfo, &param->setup.capainfo, sizeof(p_capainfo));
		memcpy(&p_redirinfo, &param->setup.redirinfo, sizeof(p_redirinfo));

		p_state = PORT_STATE_OUT_SETUP;

		UCPY(name, p_name);
		mutex_h323.Signal();
		h323_ep->Call(name, param->setup.callerinfo.id, param->setup.dialinginfo.number);
		mutex_h323.Wait();
		break;

		default:
		PDEBUG(DEBUG_H323, "H323Port(%s) h323 port with (caller id %s) received an unsupported message: %d\n", p_name, p_callerinfo.id, message_id);
	}

	return(1);
}


