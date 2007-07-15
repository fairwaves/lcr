/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** call functions                                                            **
**                                                                           **
\*****************************************************************************/ 

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "main.h"
#define __u8 unsigned char
#define __u16 unsigned short
#define __u32 unsigned long
#include "linux/isdnif.h"


/* notify endpoint about state change (if any) */
static int notify_state_change(int call_id, int epoint_id, int old_state, int new_state)
{
	int notify_off = 0, notify_on = 0;
	struct message *message;

	if (old_state == new_state)
		return(old_state);

	switch(old_state)
	{
		case NOTIFY_STATE_ACTIVE:
		switch(new_state)
		{
			case NOTIFY_STATE_HOLD:
			notify_on = INFO_NOTIFY_REMOTE_HOLD;
			break;
			case NOTIFY_STATE_SUSPEND:
			notify_on = INFO_NOTIFY_USER_SUSPENDED;
			break;
			case NOTIFY_STATE_CONFERENCE:
			notify_on = INFO_NOTIFY_CONFERENCE_ESTABLISHED;
			break;
		}
		break;

		case NOTIFY_STATE_HOLD:
		switch(new_state)
		{
			case NOTIFY_STATE_ACTIVE:
			notify_off = INFO_NOTIFY_REMOTE_RETRIEVAL;
			break;
			case NOTIFY_STATE_SUSPEND:
			notify_off = INFO_NOTIFY_REMOTE_RETRIEVAL;
			notify_on = INFO_NOTIFY_USER_SUSPENDED;
			break;
			case NOTIFY_STATE_CONFERENCE:
			notify_off = INFO_NOTIFY_REMOTE_RETRIEVAL;
			notify_on = INFO_NOTIFY_CONFERENCE_ESTABLISHED;
			break;
		}
		break;

		case NOTIFY_STATE_SUSPEND:
		switch(new_state)
		{
			case NOTIFY_STATE_ACTIVE:
			notify_off = INFO_NOTIFY_USER_RESUMED;
			break;
			case NOTIFY_STATE_HOLD:
			notify_off = INFO_NOTIFY_USER_RESUMED;
			notify_on = INFO_NOTIFY_REMOTE_HOLD;
			break;
			case NOTIFY_STATE_CONFERENCE:
			notify_off = INFO_NOTIFY_USER_RESUMED;
			notify_on = INFO_NOTIFY_CONFERENCE_ESTABLISHED;
			break;
		}
		break;

		case NOTIFY_STATE_CONFERENCE:
		switch(new_state)
		{
			case NOTIFY_STATE_ACTIVE:
			notify_off = INFO_NOTIFY_CONFERENCE_DISCONNECTED;
			break;
			case NOTIFY_STATE_HOLD:
			notify_off = INFO_NOTIFY_CONFERENCE_DISCONNECTED;
			notify_on = INFO_NOTIFY_REMOTE_HOLD;
			break;
			case NOTIFY_STATE_SUSPEND:
			notify_off = INFO_NOTIFY_CONFERENCE_DISCONNECTED;
			notify_on = INFO_NOTIFY_USER_SUSPENDED;
			break;
		}
		break;
	}

	if (call_id && notify_off)
	{
		message = message_create(call_id, epoint_id, CALL_TO_EPOINT, MESSAGE_NOTIFY);
		message->param.notifyinfo.notify = notify_off;
		message_put(message);
	}

	if (call_id && notify_on)
	{
		message = message_create(call_id, epoint_id, CALL_TO_EPOINT, MESSAGE_NOTIFY);
		message->param.notifyinfo.notify = notify_on;
		message_put(message);
	}

	return(new_state);
}


/* debug function for call */
void callpbx_debug(class CallPBX *callpbx, char *function)
{
	struct call_relation *relation;
	struct port_list *portlist;
	class Endpoint *epoint;
	class Port *port;
	char buffer[512];

	if (!(options.deb & DEBUG_CALL))
		return;

	PDEBUG(DEBUG_CALL, "CALL(%d) start (called from %s)\n", callpbx->c_serial, function);

	relation = callpbx->c_relation;

	if (!relation)
		PDEBUG(DEBUG_CALL, "call has no relations\n");
	while(relation)
	{
		epoint = find_epoint_id(relation->epoint_id);
		if (!epoint)
		{
			PDEBUG(DEBUG_CALL, "warning: relations epoint id=%ld doesn't exists!\n", relation->epoint_id);
			relation = relation->next;
			continue;
		}
		buffer[0] = '\0';
		UPRINT(strchr(buffer,0), "*** ep%ld", relation->epoint_id);
		UPRINT(strchr(buffer,0), " ifs=");
		portlist = epoint->ep_portlist;
		while(portlist)
		{
			port = find_port_id(portlist->port_id);
			if (port)
				UPRINT(strchr(buffer,0), "%s,", port->p_name);
			else
				UPRINT(strchr(buffer,0), "<port %ld doesn't exist>,", portlist->port_id);
			portlist = portlist->next;
		}
//		UPRINT(strchr(buffer,0), " endpoint=%d on=%s hold=%s", epoint->ep_serial, (epoint->ep_call_id==callpbx->c_serial)?"yes":"no", (epoint->get_hold_id()==callpbx->c_serial)?"yes":"no");
		UPRINT(strchr(buffer,0), " endpoint=%d on=%s", epoint->ep_serial, (epoint->ep_call_id==callpbx->c_serial)?"yes":"no");
		switch(relation->type)
		{
			case RELATION_TYPE_CALLING:
			UPRINT(strchr(buffer,0), " type=CALLING");
			break;
			case RELATION_TYPE_SETUP:
			UPRINT(strchr(buffer,0), " type=SETUP");
			break;
			case RELATION_TYPE_CONNECT:
			UPRINT(strchr(buffer,0), " type=CONNECT");
			break;
			default:
			UPRINT(strchr(buffer,0), " type=unknown");
			break;
		}
		switch(relation->channel_state)
		{
			case CHANNEL_STATE_CONNECT:
			UPRINT(strchr(buffer,0), " channel=CONNECT");
			break;
			case CHANNEL_STATE_HOLD:
			UPRINT(strchr(buffer,0), " channel=HOLD");
			break;
			default:
			UPRINT(strchr(buffer,0), " channel=unknown");
			break;
		}
		switch(relation->tx_state)
		{
			case NOTIFY_STATE_ACTIVE:
			UPRINT(strchr(buffer,0), " tx_state=ACTIVE");
			break;
			case NOTIFY_STATE_HOLD:
			UPRINT(strchr(buffer,0), " tx_state=HOLD");
			break;
			case NOTIFY_STATE_SUSPEND:
			UPRINT(strchr(buffer,0), " tx_state=SUSPEND");
			break;
			case NOTIFY_STATE_CONFERENCE:
			UPRINT(strchr(buffer,0), " tx_state=CONFERENCE");
			break;
			default:
			UPRINT(strchr(buffer,0), " tx_state=unknown");
			break;
		}
		switch(relation->rx_state)
		{
			case NOTIFY_STATE_ACTIVE:
			UPRINT(strchr(buffer,0), " rx_state=ACTIVE");
			break;
			case NOTIFY_STATE_HOLD:
			UPRINT(strchr(buffer,0), " rx_state=HOLD");
			break;
			case NOTIFY_STATE_SUSPEND:
			UPRINT(strchr(buffer,0), " rx_state=SUSPEND");
			break;
			case NOTIFY_STATE_CONFERENCE:
			UPRINT(strchr(buffer,0), " rx_state=CONFERENCE");
			break;
			default:
			UPRINT(strchr(buffer,0), " rx_state=unknown");
			break;
		}
		PDEBUG(DEBUG_CALL, "%s\n", buffer);
		relation = relation->next;
	}

	PDEBUG(DEBUG_CALL, "end\n");
}


/*
 * constructor for a new call 
 * the call will have a relation to the calling endpoint
 */
CallPBX::CallPBX(class Endpoint *epoint) : Call()
{
	struct call_relation *relation;
//	char filename[256];

	if (!epoint)
		FATAL("epoint is NULL.\n");

	PDEBUG(DEBUG_CALL, "creating new call and connecting it to the endpoint.\n");

	c_type = CALL_TYPE_PBX;
	c_caller[0] = '\0';
	c_caller_id[0] = '\0';
	c_dialed[0] = '\0';
	c_todial[0] = '\0';
	c_pid = getpid();
	c_updatebridge = 0;
	c_partyline = 0;
	c_multicause = CAUSE_NOUSER;
	c_multilocation = LOCATION_PRIVATE_LOCAL;

	/* initialize a relation only to the calling interface */
	relation = c_relation = (struct call_relation *)MALLOC(sizeof(struct call_relation));
	cmemuse++;
	relation->type = RELATION_TYPE_CALLING;
	relation->channel_state = CHANNEL_STATE_HOLD; /* audio is assumed on a new call */
	relation->tx_state = NOTIFY_STATE_ACTIVE; /* new calls always assumed to be active */
	relation->rx_state = NOTIFY_STATE_ACTIVE; /* new calls always assumed to be active */
	relation->epoint_id = epoint->ep_serial;


	if (options.deb & DEBUG_CALL)
		callpbx_debug(this, "CallPBX::Constructor(new call)");
}


/*
 * call descructor
 */
CallPBX::~CallPBX()
{
	struct call_relation *relation, *rtemp;

	relation = c_relation;
	while(relation)
	{
		rtemp = relation->next;
		FREE(relation, sizeof(struct call_relation));
		cmemuse--;
		relation = rtemp;
	}
}


/* bridge sets the audio flow of all bchannels assiociated to 'this' call
 * also it changes and notifies active/hold/conference states
 */
void CallPBX::bridge(void)
{
	struct call_relation *relation;
	struct message *message;
	int numconnect = 0, relations = 0;
	class Endpoint *epoint;
	struct port_list *portlist;
	class Port *port;
#ifdef DEBUG_COREBRIDGE
	int allmISDN = 0; // never set for debug purpose
#else
	int allmISDN = 1; // set until a non-mISDN relation is found
#endif

	relation = c_relation;
	while(relation)
	{
		/* count all relations */
		relations++;

		/* check for relation's objects */
		epoint = find_epoint_id(relation->epoint_id);
		if (!epoint)
		{
			PERROR("software error: relation without existing endpoints.\n");
			relation = relation->next;
			continue;
		}
		portlist = epoint->ep_portlist;
		if (!portlist)
		{
			PDEBUG(DEBUG_CALL, "CALL%d ignoring relation without port object.\n", c_serial);
//#warning testing: keep on hold until single audio stream available
			relation->channel_state = CHANNEL_STATE_HOLD;
			relation = relation->next;
			continue;
		}
		if (portlist->next)
		{
			PDEBUG(DEBUG_CALL, "CALL%d ignoring relation with ep%d due to port_list.\n", c_serial, epoint->ep_serial);
//#warning testing: keep on hold until single audio stream available
			relation->channel_state = CHANNEL_STATE_HOLD;
			relation = relation->next;
			continue;
		}
		port = find_port_id(portlist->port_id);
		if (!port)
		{
			PDEBUG(DEBUG_CALL, "CALL%d ignoring relation without existing port object.\n", c_serial);
			relation = relation->next;
			continue;
		}
		if ((port->p_type&PORT_CLASS_MASK)!=PORT_CLASS_mISDN)
		{
			PDEBUG(DEBUG_CALL, "CALL%d ignoring relation ep%d because it's port is not mISDN.\n", c_serial, epoint->ep_serial);
			if (allmISDN)
			{
				PDEBUG(DEBUG_CALL, "CALL%d not all endpoints are mISDN.\n", c_serial);
				allmISDN = 0;
			}
			relation = relation->next;
			continue;
		}
		
		relation = relation->next;
	}

	PDEBUG(DEBUG_CALL, "CALL%d members=%d %s\n", c_serial, relations, (allmISDN)?"(all are mISDN-members)":"(not all are mISDN-members)");
	/* we notify all relations about rxdata. */
	relation = c_relation;
	while(relation)
	{
		/* count connected relations */
		if ((relation->channel_state == CHANNEL_STATE_CONNECT)
		 && (relation->rx_state != NOTIFY_STATE_SUSPEND)
		 && (relation->rx_state != NOTIFY_STATE_HOLD))
			numconnect ++;

		/* remove unconnected parties from conference, also remove remotely disconnected parties so conference will not be disturbed. */
		if (relation->channel_state == CHANNEL_STATE_CONNECT
		 && relation->rx_state != NOTIFY_STATE_HOLD
		 && relation->rx_state != NOTIFY_STATE_SUSPEND
		 && relations>1 // no conf with one member
		 && allmISDN) // no conf if any member is not mISDN
		{
			message = message_create(c_serial, relation->epoint_id, CALL_TO_EPOINT, MESSAGE_mISDNSIGNAL);
			message->param.mISDNsignal.message = mISDNSIGNAL_CONF;
			message->param.mISDNsignal.conf = c_serial<<16 | c_pid;
			PDEBUG(DEBUG_CALL, "CALL%d EP%d +on+ id: 0x%08x\n", c_serial, relation->epoint_id, message->param.mISDNsignal.conf);
			message_put(message);
		} else
		{
			message = message_create(c_serial, relation->epoint_id, CALL_TO_EPOINT, MESSAGE_mISDNSIGNAL);
			message->param.mISDNsignal.message = mISDNSIGNAL_CONF;
			message->param.mISDNsignal.conf = 0;
			PDEBUG(DEBUG_CALL, "CALL%d EP%d +off+ id: 0x%08x\n", c_serial, relation->epoint_id, message->param.mISDNsignal.conf);
			message_put(message);
		}

		/*
		 * request data from endpoint/port if:
		 * - two relations
		 * - any without mISDN
		 * in this case we bridge
		 */
		message = message_create(c_serial, relation->epoint_id, CALL_TO_EPOINT, MESSAGE_mISDNSIGNAL);
		message->param.mISDNsignal.message = mISDNSIGNAL_CALLDATA;
		message->param.mISDNsignal.calldata = (relations==2 && !allmISDN);
		PDEBUG(DEBUG_CALL, "CALL%d EP%d set calldata=%d\n", c_serial, relation->epoint_id, message->param.mISDNsignal.calldata);
		message_put(message);

		relation = relation->next;
	}

	/* two people just exchange their states */
	if (relations==2 && !c_partyline)
	{
		PDEBUG(DEBUG_CALL, "CALL%d 2 relations / no partyline\n", c_serial);
		relation = c_relation;
		relation->tx_state = notify_state_change(c_serial, relation->epoint_id, relation->tx_state, relation->next->rx_state);
		relation->next->tx_state = notify_state_change(c_serial, relation->next->epoint_id, relation->next->tx_state, relation->rx_state);
	} else
	/* one member in a call, so we put her on hold */
	if (relations==1 || numconnect==1)
	{
		PDEBUG(DEBUG_CALL, "CALL%d 1 member or only 1 connected, put on hold\n");
		relation = c_relation;
		while(relation)
		{
			if ((relation->channel_state == CHANNEL_STATE_CONNECT)
			 && (relation->rx_state != NOTIFY_STATE_SUSPEND)
			 && (relation->rx_state != NOTIFY_STATE_HOLD))
				relation->tx_state = notify_state_change(c_serial, relation->epoint_id, relation->tx_state, NOTIFY_STATE_HOLD);
			relation = relation->next;
		}
	} else
	/* if conference/partyline or (more than two members and more than one is connected), so we set conference state */ 
	{
		PDEBUG(DEBUG_CALL, "CALL%d %d members, %d connected, signal conference\n", relations, numconnect);
		relation = c_relation;
		while(relation)
		{
			if ((relation->channel_state == CHANNEL_STATE_CONNECT)
			 && (relation->rx_state != NOTIFY_STATE_SUSPEND)
			 && (relation->rx_state != NOTIFY_STATE_HOLD))
				relation->tx_state = notify_state_change(c_serial, relation->epoint_id, relation->tx_state, NOTIFY_STATE_CONFERENCE);
			relation = relation->next;
		}
	}
}

/*
 * bridging is only possible with two connected endpoints
 */
void CallPBX::bridge_data(unsigned long epoint_from, struct call_relation *relation_from, union parameter *param)
{
	struct call_relation *relation_to;

	/* if we are alone */
	if (!c_relation->next)
		return;

	/* if we are more than two */
	if (c_relation->next->next)
		return;

	/* skip if source endpoint has NOT audio mode CONNECT */
	if (relation_from->channel_state != CHANNEL_STATE_CONNECT)
		return;

	/* get destination relation */
	relation_to = c_relation;
	if (relation_to == relation_from)
	{
		/* oops, we are the first, so destination is: */
		relation_to = relation_to->next;
	}

	/* skip if destination endpoint has NOT audio mode CONNECT */
	if (relation_to->channel_state != CHANNEL_STATE_CONNECT)
		return;

	/* now we may send our data to the endpoint where it
	 * will be delivered to the port
	 */
//printf("from %d, to %d\n", relation_from->epoint_id, relation_to->epoint_id);
	message_forward(c_serial, relation_to->epoint_id, CALL_TO_EPOINT, param);
}

/* release call from endpoint
 * if the call has two relations, all relations are freed and the call will be
 * destroyed
 * on outgoing relations, the cause is collected, if not connected
 * returns if call has been destroyed
 */
int CallPBX::release(struct call_relation *relation, int location, int cause)
{
	struct call_relation *reltemp, **relationpointer;
	struct message *message;
	class Call *call;
	int destroy = 0;

	/* remove from bridge */
	if (relation->channel_state != CHANNEL_STATE_HOLD)
	{
		relation->channel_state = CHANNEL_STATE_HOLD;
		c_updatebridge = 1; /* update bridge flag */
		// note: if call is not released, bridge must be updated
	}

	/* detach given interface */
	reltemp = c_relation;
	relationpointer = &c_relation;
	while(reltemp)
	{
		/* endpoint of function call */
		if (relation == reltemp)
			break;
		relationpointer = &reltemp->next;
		reltemp = reltemp->next;
	}
	if (!reltemp)
		FATAL("relation not in list of our relations. this must not happen.\n");
	*relationpointer = reltemp->next;
	FREE(reltemp, sizeof(struct call_relation));
	cmemuse--;
	relation = reltemp = NULL; // just in case of reuse fault;

	/* if no more relation */
	if (!c_relation)
	{
		PDEBUG(DEBUG_CALL, "call is completely removed.\n");
		/* there is no more endpoint related to the call */
		destroy = 1;
		delete this;
		// end of call object!
		PDEBUG(DEBUG_CALL, "call completely removed!\n");
	} else
	/* if call is a party line */
	if (c_partyline)
	{
		PDEBUG(DEBUG_CALL, "call is a conference room, so we keep it alive until the last party left.\n");
	} else
	/* if only one relation left */
	if (!c_relation->next)
	{
		PDEBUG(DEBUG_CALL, "call has one relation left, so we send it a release with the given cause %d.\n", cause);
		message = message_create(c_serial, c_relation->epoint_id, CALL_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = cause;
		message->param.disconnectinfo.location = location;
		message_put(message);
		destroy = 1;
		delete this;
		// end of call object!
		PDEBUG(DEBUG_CALL, "call completely removed!\n");
	}

	call = call_first;
	while(call)
	{
		if (options.deb & DEBUG_CALL && call->c_type==CALL_TYPE_PBX)
			callpbx_debug((class CallPBX *)call, "call_release{all calls left}");
		call = call->next;
	}
	PDEBUG(DEBUG_CALL, "call_release(): ended.\n");
	return(destroy);
}

/* count number of relations in a call
 */
int callpbx_countrelations(unsigned long call_id)
{
	struct call_relation *relation;
	int i;
	class Call *call;
	class CallPBX *callpbx;

	call = find_call_id(call_id);

	if (!call)
		return(0);

	if (call->c_type != CALL_TYPE_ASTERISK)
		return(2);

	if (call->c_type != CALL_TYPE_PBX)
		return(0);
	callpbx = (class CallPBX *)call;

	i = 0;
	relation = callpbx->c_relation;
	while(relation)
	{
		i++;
		relation = relation->next;
	}

	return(i);
}

void CallPBX::remove_relation(struct call_relation *relation)
{
	struct call_relation *temp, **tempp;

	if (!relation)
		return;

	temp = c_relation;
	tempp = &c_relation;
	while(temp)
	{
		if (temp == relation)
			break;
		tempp = &temp->next;
		temp = temp->next;
	}
	if (!temp)
	{
		PERROR("relation not in call.\n");
		return;
	}

	PDEBUG(DEBUG_CALL, "removing relation.\n");
	*tempp = relation->next;
	FREE(temp, sizeof(struct call_relation));
	cmemuse--;
}	


struct call_relation *CallPBX::add_relation(void)
{
	struct call_relation *relation;

	if (!c_relation)
	{
		PERROR("there is no first relation to this call\n");
		return(NULL);
	}
	relation = c_relation;
	while(relation->next)
		relation = relation->next;

	relation->next = (struct call_relation *)MALLOC(sizeof(struct call_relation));
	cmemuse++;
	/* the record pointer is set at the first time the data is received for the relation */

//	if (options.deb & DEBUG_CALL)
//		callpbx_debug(call, "add_relation");
	return(relation->next);
}

/* epoint sends a message to a call
 *
 */
void CallPBX::message_epoint(unsigned long epoint_id, int message_type, union parameter *param)
{
	class Call *cl;
	struct call_relation *relation, *reltemp;
	int num;
	int new_state;
	struct message *message;
//	int size, writesize, oldpointer;
	char *number, *numbers;

	if (!epoint_id)
	{
		PERROR("software error, epoint == NULL\n");
		return;
	}

//	if (options.deb & DEBUG_CALL)
//	{
//		PDEBUG(DEBUG_CALL, "message %d received from ep%d.\n", message, epoint->ep_serial);
//		callpbx_debug(call,"Call::message_epoint");
//	}
	if (options.deb & DEBUG_CALL)
	{
		if (message_type != MESSAGE_DATA)
		{
			cl = call_first;
			while(cl)
			{
				if (cl->c_type == CALL_TYPE_PBX)
					callpbx_debug((class CallPBX *)cl, "Call::message_epoint{all calls before processing}");
				cl = cl->next;
			}
		}
	}

	/* check relation */
	relation = c_relation;
	while(relation)
	{
		if (relation->epoint_id == epoint_id)
			break;
		relation = relation->next;
	}
	if (!relation)
	{
		PDEBUG(DEBUG_CALL, "no relation back to the endpoint found, ignoring (call=%d, endpoint=%d)\n", c_serial, epoint_id);
		return;
	}

	switch(message_type)
	{
		/* process channel message */
		case MESSAGE_CHANNEL:
		PDEBUG(DEBUG_CALL, "call received channel message: %d.\n", param->channel);
		if (relation->channel_state != param->channel)
		{
			relation->channel_state = param->channel;
			c_updatebridge = 1; /* update bridge flag */
			if (options.deb & DEBUG_CALL)
				callpbx_debug(this, "Call::message_epoint{after setting new channel state}");
		}
		return;

		/* track notify */
		case MESSAGE_NOTIFY:
		switch(param->notifyinfo.notify)
		{
			case INFO_NOTIFY_USER_SUSPENDED:
			case INFO_NOTIFY_USER_RESUMED:
			case INFO_NOTIFY_REMOTE_HOLD:
			case INFO_NOTIFY_REMOTE_RETRIEVAL:
			case INFO_NOTIFY_CONFERENCE_ESTABLISHED:
			case INFO_NOTIFY_CONFERENCE_DISCONNECTED:
			new_state = track_notify(relation->rx_state, param->notifyinfo.notify);
			if (new_state != relation->rx_state)
			{
				relation->rx_state = new_state;
				c_updatebridge = 1;
				if (options.deb & DEBUG_CALL)
					callpbx_debug(this, "Call::message_epoint{after setting new rx state}");
			}
			break;

			default:
			/* send notification to all other endpoints */
			reltemp = c_relation;
			while(reltemp)
			{
				if (reltemp->epoint_id!=epoint_id && reltemp->epoint_id)
				{
					message = message_create(c_serial, reltemp->epoint_id, CALL_TO_EPOINT, MESSAGE_NOTIFY);
					memcpy(&message->param, param, sizeof(union parameter));
					message_put(message);
				}
				reltemp = reltemp->next;
			}
		}
		return;

		/* audio data */
		case MESSAGE_DATA:
		/* now send audio data to the other endpoint */
		bridge_data(epoint_id, relation, param);
		return;

		/* relations sends a connect */
		case MESSAGE_CONNECT:
		/* outgoing setup type becomes connected */
		if (relation->type == RELATION_TYPE_SETUP)
			relation->type = RELATION_TYPE_CONNECT;
		/* release other relations in setup state */
		release_again:
		relation = c_relation;
		while(relation)
		{
			if (relation->type == RELATION_TYPE_SETUP)
			{
				if (release(relation, LOCATION_PRIVATE_LOCAL, CAUSE_NONSELECTED))
					return; // must return, because call IS destroyed
				goto release_again;
			}
			relation = relation->next;
		}
		break; // continue with our message

		/* release is sent by endpoint */
		case MESSAGE_RELEASE:
		if (relation->type == RELATION_TYPE_SETUP)
		{
			/* collect cause and send collected cause */
			collect_cause(&c_multicause, &c_multilocation, param->disconnectinfo.cause, param->disconnectinfo.location);
			release(relation, c_multilocation, c_multicause);
		} else
		{
			/* send current cause */
			release(relation, param->disconnectinfo.location, param->disconnectinfo.cause);
		}
		return; // must return, because call may be destroyed
	}

	/* process party line */
	if (message_type == MESSAGE_SETUP) if (param->setup.partyline)
	{
		PDEBUG(DEBUG_CALL, "respsone with connect in partyline mode.\n");
		c_partyline = param->setup.partyline;
		message = message_create(c_serial, epoint_id, CALL_TO_EPOINT, MESSAGE_CONNECT);
		message->param.setup.partyline = c_partyline;
		message_put(message);
		c_updatebridge = 1; /* update bridge flag */
	}
	if (c_partyline)
	{
		if (message_type == MESSAGE_DISCONNECT)
		{
			PDEBUG(DEBUG_CALL, "releasing after receiving disconnect, because call in partyline mode.\n");
			message = message_create(c_serial, epoint_id, CALL_TO_EPOINT, MESSAGE_RELEASE);
			message->param.disconnectinfo.cause = CAUSE_NORMAL;
			message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
			message_put(message);
			return;
		}
	}
	if (c_partyline)
	{
		PDEBUG(DEBUG_CALL, "ignoring message, because call in partyline mode.\n");
		return;
	}

	/* count relations */
	num=callpbx_countrelations(c_serial);

	/* check number of relations */
	if (num > 2)
	{
		PDEBUG(DEBUG_CALL, "call has more than two relations so there is no need to send a message.\n");
		return;
	}

	/* find interfaces not related to calling epoint */
	relation = c_relation;
	while(relation)
	{
		if (relation->epoint_id != epoint_id)
			break;
		relation = relation->next;
	}
	if (!relation)
	{
		switch(message_type)
		{
			case MESSAGE_SETUP:
			if (param->setup.dialinginfo.itype == INFO_ITYPE_ISDN_EXTENSION)
			{
				numbers = param->setup.dialinginfo.id;
				while((number = strsep(&numbers, ",")))
				{
					if (out_setup(epoint_id, message_type, param, number))
						return; // call destroyed
				}
				break;
			}
			if (out_setup(epoint_id, message_type, param, NULL))
				return; // call destroyed
			break;

			default:
			PDEBUG(DEBUG_CALL, "no need to send a message because there is no other endpoint than the calling one.\n");
		}
	} else
	{
		PDEBUG(DEBUG_CALL, "sending message ep%ld -> ep%ld.\n", epoint_id, relation->epoint_id);
		message = message_create(c_serial, relation->epoint_id, CALL_TO_EPOINT, message_type);
		memcpy(&message->param, param, sizeof(union parameter));
		message_put(message);
		PDEBUG(DEBUG_CALL, "message sent.\n");
	}
}


/* call process is called from the main loop
 * it processes the current calling state.
 * returns 0 if call nothing was done
 */
int CallPBX::handler(void)
{
//	struct call_relation *relation;
//	char dialing[32][32];
//	int port[32];
//	int found;
//	int i, j;
//	char *p;

	/* the bridge must be updated */
	if (c_updatebridge)
	{
		bridge();
		c_updatebridge = 0;
		return(1);
	}

	return(0);
}


int track_notify(int oldstate, int notify)
{
	int newstate = oldstate;

	switch(notify)
	{
		case INFO_NOTIFY_USER_RESUMED:
		case INFO_NOTIFY_REMOTE_RETRIEVAL:
		case INFO_NOTIFY_CONFERENCE_DISCONNECTED:
		case INFO_NOTIFY_RESERVED_CT_1:
		case INFO_NOTIFY_RESERVED_CT_2:
		case INFO_NOTIFY_CALL_IS_DIVERTING:
		newstate = NOTIFY_STATE_ACTIVE;
		break;

		case INFO_NOTIFY_USER_SUSPENDED:
		newstate = NOTIFY_STATE_SUSPEND;
		break;

		case INFO_NOTIFY_REMOTE_HOLD:
		newstate = NOTIFY_STATE_HOLD;
		break;

		case INFO_NOTIFY_CONFERENCE_ESTABLISHED:
		newstate = NOTIFY_STATE_CONFERENCE;
		break;
	}

	return(newstate);
}


/*
 * setup to exactly one endpoint
 * if it fails, the calling endpoint is released.
 * if other outgoing endpoints already exists, they are release as well.
 * note: if this functions fails, it will destroy its own call object!
 */
int CallPBX::out_setup(unsigned long epoint_id, int message_type, union parameter *param, char *newnumber)
{
	struct call_relation *relation;
	struct message *message;
	class Endpoint *epoint;

	PDEBUG(DEBUG_CALL, "no endpoint found, so we will create an endpoint and send the setup message we have.\n");
	/* create a new relation */
	if (!(relation=add_relation()))
		FATAL("No memory for relation.\n");
	relation->type = RELATION_TYPE_SETUP;
	relation->channel_state = CHANNEL_STATE_HOLD; /* audio is assumed on a new call */
	relation->tx_state = NOTIFY_STATE_ACTIVE; /* new calls always assumed to be active */
	relation->rx_state = NOTIFY_STATE_ACTIVE; /* new calls always assumed to be active */
	/* create a new endpoint */
	epoint = new Endpoint(0, c_serial, 0);
	if (!epoint)
		FATAL("No memory for Endpoint instance\n");
	if (!(epoint->ep_app = new DEFAULT_ENDPOINT_APP(epoint)))
		FATAL("No memory for Endpoint Application instance\n");
	relation->epoint_id = epoint->ep_serial;
	/* send setup message to new endpoint */
//printf("JOLLY DEBUG: %d\n",call_countrelations(c_serial));
//i			if (options.deb & DEBUG_CALL)
//				callpbx_debug(call, "Call::message_epoint");
	message = message_create(c_serial, relation->epoint_id, CALL_TO_EPOINT, message_type);
	memcpy(&message->param, param, sizeof(union parameter));
	if (newnumber)
		SCPY(message->param.setup.dialinginfo.id, newnumber);
	PDEBUG(DEBUG_CALL, "setup message sent to ep %d with number='%s'.\n", relation->epoint_id, message->param.setup.dialinginfo.id);
	message_put(message);
	return(0);
}


