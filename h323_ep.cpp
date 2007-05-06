///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// PBX4Linux                                                                 //
//                                                                           //
//---------------------------------------------------------------------------//
// Copyright: Andreas Eversberg                                              //
//                                                                           //
// h323_ep class                                                             //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

/*
 NOTE:

 The code was inspired by the isdn2h323 gateway my marco bode.
 Thanx to marco budde for lerarning to program h323 and c++ from your code.
 His homepage is www.telos.de, there you'll find the isdn2h323 gateway.

 Also thanx to others who write documents and applications for OpenH323.

 Andreas Eversberg
*/

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include "main.h"

//#include <gsmcodec.h>
//#include <g7231codec.h>
//#include <g729codec.h>
//#include "g726codec.h"
//#include <speexcodec.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

//
// constructor
//
H323_ep::H323_ep(void)
{
	terminalType = e_GatewayOnly;

	PDEBUG(DEBUG_H323, "H323 endpoint  constuctor\n");
}

//
// destructor
//
H323_ep::~H323_ep()
{
	// clear all calls to remote endpoints  
	ClearAllCalls();

	PDEBUG(DEBUG_H323, "H323 endpoint  destuctor\n");
}


//
// create connection
//
H323Connection *H323_ep::CreateConnection(unsigned callReference)
{
	PDEBUG(DEBUG_H323, "H323 endpoint  create connection\n");

	return new H323_con(*this, callReference);
}    


//
// on establishment of conneciton
//
void H323_ep::OnConnectionEstablished(H323Connection &connection, const PString &token)
{
	const unsigned char *token_string = token;

	PDEBUG(DEBUG_H323, "H323 endpoint  connection established to: %s\n", token_string);

	H323EndPoint::OnConnectionEstablished(connection, token);
}

//
// on remote alerting
//
BOOL H323_ep::OnAlerting(H323Connection &connection, const H323SignalPDU &alertingPDU, const PString &user)
{
	class H323Port *port;
	const unsigned char *token_string = connection.GetCallToken();
	const unsigned char *user_string = user;
	struct message *message;

	PDEBUG(DEBUG_H323, "H323 endpoint  alerting at: %s\n", user_string);

	mutex_h323.Wait();

	if (!(port=(class H323Port *)find_port_with_token((char *)token_string)))
	{
		PERROR("cannot find port with token '%s'\n", token_string);
		mutex_h323.Signal();
		return FALSE;
	}
	if (port->p_state==PORT_STATE_OUT_SETUP
	 || port->p_state==PORT_STATE_OUT_OVERLAP
	 || port->p_state==PORT_STATE_OUT_PROCEEDING)
	{
		port->new_state(PORT_STATE_OUT_ALERTING);
		message = message_create(port->p_serial, ACTIVE_EPOINT(port->p_epointlist), PORT_TO_EPOINT, MESSAGE_ALERTING);
		message_put(message);
	}

	mutex_h323.Signal();

	return TRUE;
}


//
// on clearing of connection
//
void H323_ep::OnConnectionCleared(H323Connection &connection, const PString &token)
{
	int cause;
	class H323Port *port;
	const unsigned char *token_string = token;
	struct message *message;

	PDEBUG(DEBUG_H323, "H323 endpoint  connection cleared.\n");

	mutex_h323.Wait();

	if (!(port=(class H323Port *)find_port_with_token((char *)token_string)))
	{
		PERROR("cannot find port with token '%s'\n", token_string);
		mutex_h323.Signal();
		return;
	}

	switch(connection.GetCallEndReason())
	{
		case H323Connection::EndedByRemoteUser:
		case H323Connection::EndedByCallerAbort:
		case H323Connection::EndedByGatekeeper:
		case H323Connection::EndedByCallForwarded:
		cause = 16; // normal call clearing
		break;
		
		case H323Connection::EndedByRefusal:
		case H323Connection::EndedBySecurityDenial:
		cause = 21; // call rejected
		break;
		
		case H323Connection::EndedByNoAnswer:
		cause = 19; // no answer from user
		break;
		
		case H323Connection::EndedByTransportFail:
		cause = 47; // resource unavaiable, unspecified
		break;
		
		case H323Connection::EndedByNoBandwidth:
		cause = 49; // quality of service not available
		break;
		
		case H323Connection::EndedByNoUser:
		cause = 1; // unallocated number
		break;
		
		case H323Connection::EndedByCapabilityExchange:
		cause = 65; // bearer capability not implemented
		break;
		
		case H323Connection::EndedByRemoteBusy:
		cause = 17; // user busy
		break;
		
		case H323Connection::EndedByRemoteCongestion:
		cause = 42; // switching equipment congestion
		break;
		
		case H323Connection::EndedByUnreachable:
		cause = 2; // no route ...
		break;

		case H323Connection::EndedByNoEndPoint:
		case H323Connection::EndedByConnectFail:
		cause = 18; // no user responding
		break;
		
		case H323Connection::EndedByHostOffline:
		cause = 27; // destination out of order
		break;
		
		case H323Connection::EndedByTemporaryFailure:
		cause = 41; // temporary failure
		break;
		
		default:
		cause = 31; // normal, unspecified
		break;
		
	}

	// delete channels
	if (port->p_h323_channel_in)
		delete port->p_h323_channel_in;
	port->p_h323_channel_in = NULL;
	if (port->p_h323_channel_out)
		delete port->p_h323_channel_out;
	port->p_h323_channel_out = NULL;

	/* release endpoint */
	message = message_create(port->p_serial, ACTIVE_EPOINT(port->p_epointlist), PORT_TO_EPOINT, MESSAGE_RELEASE);
	message->param.disconnectinfo.cause = cause;
	message->param.disconnectinfo.location = LOCATION_BEYOND;
	message_put(message);

	/* delete port */
	delete port;

	mutex_h323.Signal();
}


//
// open audio channel
//
BOOL H323_ep::OpenAudioChannel(H323Connection &connection, BOOL isEncoding, unsigned bufferSize, H323AudioCodec &codec)
{
	H323_chan *channel;
	class H323Port *port;
	const unsigned char *token_string = connection.GetCallToken();

	PDEBUG(DEBUG_H323, "H323 endpoint  audio channel open (isEndcoding=%d).\n", isEncoding);

	// disable the silence detection
	codec.SetSilenceDetectionMode (H323AudioCodec::NoSilenceDetection); 

	// create channels
	if (isEncoding)
	{
		channel = new H323_chan(connection.GetCallToken(), TRUE);
	} else
	{
		channel = new H323_chan(connection.GetCallToken(), FALSE);
	}
	if (!channel)
	{
		PERROR("channel for token '%s' not set", token_string);
		return FALSE;
	}

	// return the channel object
	mutex_h323.Wait();
	if (!(port=(class H323Port *)find_port_with_token((char *)token_string)))
	{
		PERROR("cannot find port with token '%s'\n", token_string);
		mutex_h323.Signal();
		return FALSE;
	}

	// set channels
	if (isEncoding)
	{
		port->p_h323_channel_out = channel;
	} else
	{
		port->p_h323_channel_in = channel;
	}

	mutex_h323.Signal();
	return codec.AttachChannel(channel, FALSE);
}


//
// open video channel
//
BOOL H323_ep::OpenVideoChannel(H323Connection &connection, BOOL isEncoding, H323VideoCodec &codec)
{
	PDEBUG(DEBUG_H323, "H323 endpoint  video channel open (isEndcoding=%d).\n", isEncoding);


	return FALSE;
}


//
// initialize H323 endpoint
//
BOOL H323_ep::Init(void)
{
	H323ListenerTCP  *listener;
	int pri;

	PDEBUG(DEBUG_H323, "H323 endpoint  initialize\n");

	// add keypad capability
	H323_UserInputCapability::AddAllCapabilities(capabilities, 0, P_MAX_INDEX);

	/* will add codec in order of priority 1 = highest, 0 = don't use */ 
	pri = 1;
	while (pri < 256)
	{
#warning codecs are temporarily disabled due to api change
#if 0
		if (options.h323_gsm_pri == pri)
		{ 
			H323_GSM0610Capability * gsm_cap;
			MicrosoftGSMAudioCapability * msgsm_cap;

			SetCapability(0, 0, gsm_cap = new H323_GSM0610Capability);
			gsm_cap->SetTxFramesInPacket(options.h323_gsm_opt);
			SetCapability(0, 0, msgsm_cap = new MicrosoftGSMAudioCapability);
			msgsm_cap->SetTxFramesInPacket(options.h323_gsm_opt);
		}
		if (options.h323_g726_pri == pri)
		{
			if (options.h323_g726_opt > 4) 
				SetCapability(0, 0, new H323_G726_Capability(*this, H323_G726_Capability::e_40k));
			if (options.h323_g726_opt > 3) 
				SetCapability(0, 0, new H323_G726_Capability(*this, H323_G726_Capability::e_32k));
			if (options.h323_g726_opt > 2) 
				SetCapability(0, 0, new H323_G726_Capability(*this, H323_G726_Capability::e_24k));
			SetCapability(0, 0, new H323_G726_Capability(*this, H323_G726_Capability::e_16k));
		}
		if (options.h323_g7231_pri == pri)
		{
#if 0  
			SetCapability(0, 0, new H323_G7231Capability(FALSE));
#endif
		}
		if (options.h323_g729a_pri == pri)
		{
#if 0  
			SetCapability(0, 0, new H323_G729Capability());
#endif
		}
		if (options.h323_lpc10_pri == pri)
		{  
			SetCapability(0, 0, new H323_LPC10Capability(*this));
		}
		if (options.h323_speex_pri == pri)
		{
			if (options.h323_speex_opt > 5) 
				SetCapability(0, 0, new SpeexNarrow6AudioCapability());
			if (options.h323_speex_opt > 4) 
				SetCapability(0, 0, new SpeexNarrow5AudioCapability());
			if (options.h323_speex_opt > 3) 
				SetCapability(0, 0, new SpeexNarrow4AudioCapability());
			if (options.h323_speex_opt > 2) 
				SetCapability(0, 0, new SpeexNarrow3AudioCapability());
			SetCapability(0, 0, new SpeexNarrow2AudioCapability());
		}
		if (options.h323_xspeex_pri == pri)
		{
			if (options.h323_xspeex_opt > 5) 
				SetCapability(0, 0, new XiphSpeexNarrow6AudioCapability());
			if (options.h323_xspeex_opt > 4) 
				SetCapability(0, 0, new XiphSpeexNarrow5AudioCapability());
			if (options.h323_xspeex_opt > 3) 
				SetCapability(0, 0, new XiphSpeexNarrow4AudioCapability());
			if (options.h323_xspeex_opt > 2) 
				SetCapability(0, 0, new XiphSpeexNarrow3AudioCapability());
			SetCapability(0, 0, new XiphSpeexNarrow2AudioCapability());
		}
#endif
		if (options.h323_law_pri == pri)
		{
			H323_G711Capability * g711uCap;
			H323_G711Capability * g711aCap;
			SetCapability(0, 0, g711uCap = new H323_G711Capability (H323_G711Capability::ALaw/*, H323_G711Capability::At64k*/));
#warning H323_law frame size is disabled due to bug in OpenH323
//			g711uCap->SetTxFramesInPacket(options.h323_law_opt);
			SetCapability(0, 0, g711aCap = new H323_G711Capability (H323_G711Capability::muLaw/*, H323_G711Capability::At64k*/));
//			g711aCap->SetTxFramesInPacket(options.h323_law_opt);
		}
		pri++;
	}

	// h323 user is the hostname or given by h323_name
	if (options.h323_name[0] == '\0')
	{
		if (getenv("HOSTNAME") == NULL)
		{
			cout << "OpenH323: Environment variable HOSTNAME not set. Please specify 'h323_name' in options.conf" << endl;
			return FALSE;
		}
	}
	SetLocalUserName((options.h323_name[0])?options.h323_name:getenv("HOSTNAME"));

	// create listener
	if (options.h323_icall)
	{
		PIPSocket::Address interfaceAddress(INADDR_ANY);
		listener = new H323ListenerTCP(*this, interfaceAddress, options.h323_port);
		if (!StartListener(listener))
		{
			cout << "OpenH323: Could not open H323 port " << listener->GetListenerPort() << endl;
			return FALSE;
		}
		cout << "OpenH323: Waiting for incoming H323 connections on port " << listener->GetListenerPort() << endl;
	}

	// register with gatekeeper
	if (options.h323_gatekeeper)
	{
		if (options.h323_gatekeeper_host[0] == '\0')
		{
			if (DiscoverGatekeeper(new H323TransportUDP(*this)))
			{
				cout << "OpenH323: Registering with gatekeeper " << gatekeeper->GetIdentifier() << " (automatically)" << endl;
			} else
			{
				cout << "OpenH323: Gatekeeper not found." << endl;
				sleep(2);
			}
		} else
		{
			if (SetGatekeeper(options.h323_gatekeeper_host) == TRUE)
			{
				cout << "OpenH323: Registering with gatekeeper " << gatekeeper->GetIdentifier() << " (automatically)" << endl;
			} else
			{
				cout << "OpenH323: Gatekeeper at " << gatekeeper->GetIdentifier() << " not found." << endl;
				sleep(2);
			}
		}
	}

	return TRUE;
}


//
// make an outgoing h323 call
//

BOOL H323_ep::Call(char *token_string, char *caller, char *host)
{
	PString address;
	PString token = "";
	BOOL failed = FALSE;
	class H323Port *port;
	struct message *message;
	char *newtoken_string;

	PDEBUG(DEBUG_H323, "H323 endpoint  call to host '%s'\n", host);

	address = host;

	if (!MakeCall(address, token))
	{
		PDEBUG(DEBUG_H323, "H323 endpoint  call to host '%s'\n", host);
		failed = TRUE;
	}

	// set new token
	mutex_h323.Wait();
	if (!(port=(class H323Port *)find_port_with_token((char *)token_string)))
	{
		PERROR("cannot find port with token '%s'\n", token_string);
		mutex_h323.Signal();
		return FALSE;
	}
	if (failed == TRUE)
	{
		PDEBUG(DEBUG_H323, "call of port '%s' failed.\n", token_string);
		message = message_create(port->p_serial, ACTIVE_EPOINT(port->p_epointlist), PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = 31;
		message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		message_put(message);
	} else
	{
		PDEBUG(DEBUG_H323, "changing port name from '%s' to token '%s'\n", token_string, token.GetPointer());
		newtoken_string = token.GetPointer();
		SCPY(port->p_name, newtoken_string);
	}
	mutex_h323.Signal();
	PDEBUG(DEBUG_H323, "H323 endpoint call to host '%s'\n", host);

	if (failed == TRUE)
		return FALSE;
	return TRUE;
}

void H323_ep::SetEndpointTypeInfo(H225_EndpointType &info) const
{
//	H225_VoiceCaps voicecaps;
	PDEBUG(DEBUG_H323, "H323 endpoint  set endpoint type info *TBD*\n");

	H323EndPoint::SetEndpointTypeInfo(info);

//	protocols.SetTag(H225_SupportedProtocols::e_voice);
//	(H225_VoiceCaps&)protocols = voicecaps;
//	a_protocols.SetSize(1);
//	a_protocols[0] = protocols;

//	gateway.IncludeOptionalField(H225_GatewayInfo::e_protocol);
//	gateway.m_protocol = a_protocols;
//	info.m_gateway = gateway;
}
