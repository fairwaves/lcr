/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** join functions                                                            **
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
static int notify_state_change(int join_id, int epoint_id, int old_state, int new_state)
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

	if (join_id && notify_off)
	{
		message = message_create(join_id, epoint_id, JOIN_TO_EPOINT, MESSAGE_NOTIFY);
		message->param.notifyinfo.notify = notify_off;
		message_put(message);
	}

	if (join_id && notify_on)
	{
		message = message_create(join_id, epoint_id, JOIN_TO_EPOINT, MESSAGE_NOTIFY);
		message->param.notifyinfo.notify = notify_on;
		message_put(message);
	}

	return(new_state);
}


/* debug function for join */
void joinpbx_debug(class JoinPBX *joinpbx, char *function)
{
	struct join_relation *relation;
	struct port_list *portlist;
	class Endpoint *epoint;
	class Port *port;
	char buffer[512];

	if (!(options.deb & DEBUG_JOIN))
		return;

	PDEBUG(DEBUG_JOIN, "join(%d) start (called from %s)\n", joinpbx->j_serial, function);

	relation = joinpbx->j_relation;

	if (!relation)
		PDEBUG(DEBUG_JOIN, "join has no relations\n");
	while(relation)
	{
		epoint = find_epoint_id(relation->epoint_id);
		if (!epoint)
		{
			PDEBUG(DEBUG_JOIN, "warning: relations epoint id=%ld doesn't exists!\n", relation->epoint_id);
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
//		UPRINT(strchr(buffer,0), " endpoint=%d on=%s hold=%s", epoint->ep_serial, (epoint->ep_join_id==joinpbx->j_serial)?"yes":"no", (epoint->get_hold_id()==joinpbx->j_serial)?"yes":"no");
		UPRINT(strchr(buffer,0), " endpoint=%d on=%s", epoint->ep_serial, (epoint->ep_join_id==joinpbx->j_serial)?"yes":"no");
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
		PDEBUG(DEBUG_JOIN, "%s\n", buffer);
		relation = relation->next;
	}

	PDEBUG(DEBUG_JOIN, "end\n");
}


/*
 * constructor for a new join 
 * the join will have a relation to the calling endpoint
 */
JoinPBX::JoinPBX(class Endpoint *epoint) : Join()
{
	struct join_relation *relation;
//	char filename[256];

	if (!epoint)
		FATAL("epoint is NULL.\n");

	PDEBUG(DEBUG_JOIN, "creating new join and connecting it to the endpoint.\n");

	j_type = JOIN_TYPE_PBX;
	j_caller[0] = '\0';
	j_caller_id[0] = '\0';
	j_dialed[0] = '\0';
	j_todial[0] = '\0';
	j_pid = getpid();
	j_updatebridge = 0;
	j_partyline = 0;
	j_multicause = 0;
	j_multilocation = 0;

	/* initialize a relation only to the calling interface */
	relation = j_relation = (struct join_relation *)MALLOC(sizeof(struct join_relation));
	cmemuse++;
	relation->type = RELATION_TYPE_CALLING;
	relation->channel_state = CHANNEL_STATE_HOLD; /* audio is assumed on a new join */
	relation->tx_state = NOTIFY_STATE_ACTIVE; /* new joins always assumed to be active */
	relation->rx_state = NOTIFY_STATE_ACTIVE; /* new joins always assumed to be active */
	relation->epoint_id = epoint->ep_serial;


	if (options.deb & DEBUG_JOIN)
		joinpbx_debug(this, "JoinPBX::Constructor(new join)");
}


/*
 * join descructor
 */
JoinPBX::~JoinPBX()
{
	struct join_relation *relation, *rtemp;

	relation = j_relation;
	while(relation)
	{
		rtemp = relation->next;
		FREE(relation, sizeof(struct join_relation));
		cmemuse--;
		relation = rtemp;
	}
}


/* bridge sets the audio flow of all bchannels assiociated to 'this' join
 * also it changes and notifies active/hold/conference states
 */
void JoinPBX::bridge(void)
{
	struct join_relation *relation;
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

	relation = j_relation;
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
			PDEBUG(DEBUG_JOIN, "join%d ignoring relation without port object.\n", j_serial);
//#warning testing: keep on hold until single audio stream available
			relation->channel_state = CHANNEL_STATE_HOLD;
			relation = relation->next;
			continue;
		}
		if (portlist->next)
		{
			PDEBUG(DEBUG_JOIN, "join%d ignoring relation with ep%d due to port_list.\n", j_serial, epoint->ep_serial);
//#warning testing: keep on hold until single audio stream available
			relation->channel_state = CHANNEL_STATE_HOLD;
			relation = relation->next;
			continue;
		}
		port = find_port_id(portlist->port_id);
		if (!port)
		{
			PDEBUG(DEBUG_JOIN, "join%d ignoring relation without existing port object.\n", j_serial);
			relation = relation->next;
			continue;
		}
		if ((port->p_type&PORT_CLASS_MASK)!=PORT_CLASS_mISDN)
		{
			PDEBUG(DEBUG_JOIN, "join%d ignoring relation ep%d because it's port is not mISDN.\n", j_serial, epoint->ep_serial);
			if (allmISDN)
			{
				PDEBUG(DEBUG_JOIN, "join%d not all endpoints are mISDN.\n", j_serial);
				allmISDN = 0;
			}
			relation = relation->next;
			continue;
		}
		
		relation = relation->next;
	}

	PDEBUG(DEBUG_JOIN, "join%d members=%d %s\n", j_serial, relations, (allmISDN)?"(all are mISDN-members)":"(not all are mISDN-members)");
	/* we notify all relations about rxdata. */
	relation = j_relation;
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
			message = message_create(j_serial, relation->epoint_id, JOIN_TO_EPOINT, MESSAGE_mISDNSIGNAL);
			message->param.mISDNsignal.message = mISDNSIGNAL_CONF;
			message->param.mISDNsignal.conf = j_serial<<16 | j_pid;
			PDEBUG(DEBUG_JOIN, "join%d EP%d +on+ id: 0x%08x\n", j_serial, relation->epoint_id, message->param.mISDNsignal.conf);
			message_put(message);
		} else
		{
			message = message_create(j_serial, relation->epoint_id, JOIN_TO_EPOINT, MESSAGE_mISDNSIGNAL);
			message->param.mISDNsignal.message = mISDNSIGNAL_CONF;
			message->param.mISDNsignal.conf = 0;
			PDEBUG(DEBUG_JOIN, "join%d EP%d +off+ id: 0x%08x\n", j_serial, relation->epoint_id, message->param.mISDNsignal.conf);
			message_put(message);
		}

		/*
		 * request data from endpoint/port if:
		 * - two relations
		 * - any without mISDN
		 * in this case we bridge
		 */
		message = message_create(j_serial, relation->epoint_id, JOIN_TO_EPOINT, MESSAGE_mISDNSIGNAL);
		message->param.mISDNsignal.message = mISDNSIGNAL_JOINDATA;
		message->param.mISDNsignal.joindata = (relations==2 && !allmISDN);
		PDEBUG(DEBUG_JOIN, "join%d EP%d set joindata=%d\n", j_serial, relation->epoint_id, message->param.mISDNsignal.joindata);
		message_put(message);

		relation = relation->next;
	}

	/* two people just exchange their states */
	if (relations==2 && !j_partyline)
	{
		PDEBUG(DEBUG_JOIN, "join%d 2 relations / no partyline\n", j_serial);
		relation = j_relation;
		relation->tx_state = notify_state_change(j_serial, relation->epoint_id, relation->tx_state, relation->next->rx_state);
		relation->next->tx_state = notify_state_change(j_serial, relation->next->epoint_id, relation->next->tx_state, relation->rx_state);
	} else
	/* one member in a join, so we put her on hold */
	if (relations==1 || numconnect==1)
	{
		PDEBUG(DEBUG_JOIN, "join%d 1 member or only 1 connected, put on hold\n");
		relation = j_relation;
		while(relation)
		{
			if ((relation->channel_state == CHANNEL_STATE_CONNECT)
			 && (relation->rx_state != NOTIFY_STATE_SUSPEND)
			 && (relation->rx_state != NOTIFY_STATE_HOLD))
				relation->tx_state = notify_state_change(j_serial, relation->epoint_id, relation->tx_state, NOTIFY_STATE_HOLD);
			relation = relation->next;
		}
	} else
	/* if conference/partyline or (more than two members and more than one is connected), so we set conference state */ 
	{
		PDEBUG(DEBUG_JOIN, "join%d %d members, %d connected, signal conference\n", relations, numconnect);
		relation = j_relation;
		while(relation)
		{
			if ((relation->channel_state == CHANNEL_STATE_CONNECT)
			 && (relation->rx_state != NOTIFY_STATE_SUSPEND)
			 && (relation->rx_state != NOTIFY_STATE_HOLD))
				relation->tx_state = notify_state_change(j_serial, relation->epoint_id, relation->tx_state, NOTIFY_STATE_CONFERENCE);
			relation = relation->next;
		}
	}
}

/*
 * bridging is only possible with two connected endpoints
 */
void JoinPBX::bridge_data(unsigned long epoint_from, struct join_relation *relation_from, union parameter *param)
{
	struct join_relation *relation_to;

	/* if we are alone */
	if (!j_relation->next)
		return;

	/* if we are more than two */
	if (j_relation->next->next)
		return;

	/* skip if source endpoint has NOT audio mode CONNECT */
	if (relation_from->channel_state != CHANNEL_STATE_CONNECT)
		return;

	/* get destination relation */
	relation_to = j_relation;
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
	message_forward(j_serial, relation_to->epoint_id, JOIN_TO_EPOINT, param);
}

/* release join from endpoint
 * if the join has two relations, all relations are freed and the join will be
 * destroyed
 * on outgoing relations, the cause is collected, if not connected
 * returns if join has been destroyed
 */
int JoinPBX::release(struct join_relation *relation, int location, int cause)
{
	struct join_relation *reltemp, **relationpointer;
	struct message *message;
	class Join *join;
	int destroy = 0;

	/* remove from bridge */
	if (relation->channel_state != CHANNEL_STATE_HOLD)
	{
		relation->channel_state = CHANNEL_STATE_HOLD;
		j_updatebridge = 1; /* update bridge flag */
		// note: if join is not released, bridge must be updated
	}

	/* detach given interface */
	reltemp = j_relation;
	relationpointer = &j_relation;
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
//printf("releasing relation %d\n", reltemp->epoint_id);
	*relationpointer = reltemp->next;
	FREE(reltemp, sizeof(struct join_relation));
	cmemuse--;
	relation = reltemp = NULL; // just in case of reuse fault;

	/* if no more relation */
	if (!j_relation)
	{
		PDEBUG(DEBUG_JOIN, "join is completely removed.\n");
		/* there is no more endpoint related to the join */
		destroy = 1;
		delete this;
		// end of join object!
		PDEBUG(DEBUG_JOIN, "join completely removed!\n");
	} else
	/* if join is a party line */
	if (j_partyline)
	{
		PDEBUG(DEBUG_JOIN, "join is a conference room, so we keep it alive until the last party left.\n");
	} else
	/* if only one relation left */
	if (!j_relation->next)
	{
		PDEBUG(DEBUG_JOIN, "join has one relation left, so we send it a release with the given cause %d.\n", cause);
		message = message_create(j_serial, j_relation->epoint_id, JOIN_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = cause;
		message->param.disconnectinfo.location = location;
		message_put(message);
		destroy = 1;
		delete this;
		// end of join object!
		PDEBUG(DEBUG_JOIN, "join completely removed!\n");
	}

	join = join_first;
	while(join)
	{
		if (options.deb & DEBUG_JOIN && join->j_type==JOIN_TYPE_PBX)
			joinpbx_debug((class JoinPBX *)join, "join_release{all joins left}");
		join = join->next;
	}
	PDEBUG(DEBUG_JOIN, "join_release(): ended.\n");
	return(destroy);
}

/* count number of relations in a join
 */
int joinpbx_countrelations(unsigned long join_id)
{
	struct join_relation *relation;
	int i;
	class Join *join;
	class JoinPBX *joinpbx;

	join = find_join_id(join_id);

	if (!join)
		return(0);

	if (join->j_type != JOIN_TYPE_REMOTE)
		return(2);

	if (join->j_type != JOIN_TYPE_PBX)
		return(0);
	joinpbx = (class JoinPBX *)join;

	i = 0;
	relation = joinpbx->j_relation;
	while(relation)
	{
		i++;
		relation = relation->next;
	}

	return(i);
}

void JoinPBX::remove_relation(struct join_relation *relation)
{
	struct join_relation *temp, **tempp;

	if (!relation)
		return;

	temp = j_relation;
	tempp = &j_relation;
	while(temp)
	{
		if (temp == relation)
			break;
		tempp = &temp->next;
		temp = temp->next;
	}
	if (!temp)
	{
		PERROR("relation not in join.\n");
		return;
	}

	PDEBUG(DEBUG_JOIN, "removing relation.\n");
	*tempp = relation->next;
	FREE(temp, sizeof(struct join_relation));
	cmemuse--;
}	


struct join_relation *JoinPBX::add_relation(void)
{
	struct join_relation *relation;

	if (!j_relation)
	{
		PERROR("there is no first relation to this join\n");
		return(NULL);
	}
	relation = j_relation;
	while(relation->next)
		relation = relation->next;

	relation->next = (struct join_relation *)MALLOC(sizeof(struct join_relation));
	cmemuse++;
	/* the record pointer is set at the first time the data is received for the relation */

//	if (options.deb & DEBUG_JOIN)
//		joinpbx_debug(join, "add_relation");
	return(relation->next);
}

/* epoint sends a message to a join
 *
 */
void JoinPBX::message_epoint(unsigned long epoint_id, int message_type, union parameter *param)
{
	class Join *cl;
	struct join_relation *relation, *reltemp;
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

//	if (options.deb & DEBUG_JOIN)
//	{
//		PDEBUG(DEBUG_JOIN, "message %d received from ep%d.\n", message, epoint->ep_serial);
//		joinpbx_debug(join,"Join::message_epoint");
//	}
	if (options.deb & DEBUG_JOIN)
	{
		if (message_type != MESSAGE_DATA)
		{
			cl = join_first;
			while(cl)
			{
				if (cl->j_type == JOIN_TYPE_PBX)
					joinpbx_debug((class JoinPBX *)cl, "Join::message_epoint{all joins before processing}");
				cl = cl->next;
			}
		}
	}

	/* check relation */
	relation = j_relation;
	while(relation)
	{
		if (relation->epoint_id == epoint_id)
			break;
		relation = relation->next;
	}
	if (!relation)
	{
		PDEBUG(DEBUG_JOIN, "no relation back to the endpoint found, ignoring (join=%d, endpoint=%d)\n", j_serial, epoint_id);
		return;
	}

	/* process party line */
	if (message_type == MESSAGE_SETUP) if (param->setup.partyline)
	{
		j_partyline = param->setup.partyline;
	}
	if (j_partyline)
	{
		switch(message_type)
		{
			case MESSAGE_SETUP:
			PDEBUG(DEBUG_JOIN, "respsone with connect in partyline mode.\n");
			relation->type = RELATION_TYPE_CONNECT;
			message = message_create(j_serial, epoint_id, JOIN_TO_EPOINT, MESSAGE_CONNECT);
			SPRINT(message->param.connectinfo.id, "%d", j_partyline);
			message->param.connectinfo.ntype = INFO_NTYPE_UNKNOWN;
			message_put(message);
			j_updatebridge = 1; /* update bridge flag */
			break;
			
			case MESSAGE_AUDIOPATH:
			PDEBUG(DEBUG_JOIN, "join received channel message: %d.\n", param->audiopath);
			if (relation->channel_state != param->audiopath)
			{
				relation->channel_state = param->audiopath;
				j_updatebridge = 1; /* update bridge flag */
				if (options.deb & DEBUG_JOIN)
					joinpbx_debug(this, "Join::message_epoint{after setting new channel state}");
			}
			break;

			case MESSAGE_DISCONNECT:
			PDEBUG(DEBUG_JOIN, "releasing after receiving disconnect, because join in partyline mode.\n");
			message = message_create(j_serial, epoint_id, JOIN_TO_EPOINT, MESSAGE_RELEASE);
			message->param.disconnectinfo.cause = CAUSE_NORMAL;
			message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
			message_put(message);
			// fall through

			case MESSAGE_RELEASE:
			PDEBUG(DEBUG_JOIN, "releasing from join\n");
			release(relation, 0, 0);
			break;

			default:
			PDEBUG(DEBUG_JOIN, "ignoring message, because join in partyline mode.\n");
		}
		return;
	}


	/* process messages */
	switch(message_type)
	{
		/* process audio path message */
		case MESSAGE_AUDIOPATH:
		PDEBUG(DEBUG_JOIN, "join received channel message: %d.\n", param->audiopath);
		if (relation->channel_state != param->audiopath)
		{
			relation->channel_state = param->audiopath;
			j_updatebridge = 1; /* update bridge flag */
			if (options.deb & DEBUG_JOIN)
				joinpbx_debug(this, "Join::message_epoint{after setting new channel state}");
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
				j_updatebridge = 1;
				if (options.deb & DEBUG_JOIN)
					joinpbx_debug(this, "Join::message_epoint{after setting new rx state}");
			}
			break;

			default:
			/* send notification to all other endpoints */
			reltemp = j_relation;
			while(reltemp)
			{
				if (reltemp->epoint_id!=epoint_id && reltemp->epoint_id)
				{
					message = message_create(j_serial, reltemp->epoint_id, JOIN_TO_EPOINT, MESSAGE_NOTIFY);
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
		reltemp = j_relation;
		while(reltemp)
		{
//printf("connect, checking relation %d\n", reltemp->epoint_id);
			if (reltemp->type == RELATION_TYPE_SETUP)
			{
//printf("relation %d is of type setup, releasing\n", reltemp->epoint_id);
				/* send release to endpoint */
				message = message_create(j_serial, reltemp->epoint_id, JOIN_TO_EPOINT, MESSAGE_RELEASE);
				message->param.disconnectinfo.cause = CAUSE_NONSELECTED;
				message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
				message_put(message);

				if (release(reltemp, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL)) // dummy cause, should not be used, since calling and connected endpoint still exist afterwards.
					return; // must return, because join IS destroyed
				goto release_again;
			}
			if (reltemp->type == RELATION_TYPE_CALLING)
				reltemp->type = RELATION_TYPE_CONNECT;
			reltemp = reltemp->next;
		}
		break; // continue with our message

		/* release is sent by endpoint */
		case MESSAGE_RELEASE:
		switch(relation->type)
		{
			case RELATION_TYPE_SETUP: /* by called */
			/* collect cause and send collected cause */
			collect_cause(&j_multicause, &j_multilocation, param->disconnectinfo.cause, param->disconnectinfo.location);
			if (j_multicause)
				release(relation, j_multilocation, j_multicause);
			else
				release(relation, LOCATION_PRIVATE_LOCAL, CAUSE_UNSPECIFIED);
			break;

			case RELATION_TYPE_CALLING: /* by calling */
			/* remove all relations that are in called */
			release_again2:
			reltemp = j_relation;
			while(reltemp)
			{
				if (reltemp->type == RELATION_TYPE_SETUP)
				{
					/* send release to endpoint */
					message = message_create(j_serial, reltemp->epoint_id, JOIN_TO_EPOINT, message_type);
					memcpy(&message->param, param, sizeof(union parameter));
					message_put(message);

					if (release(reltemp, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL))
						return; // must return, because join IS destroyed
					goto release_again2;
				}
				reltemp = reltemp->next;
			}
			PERROR("we are still here, this should not happen\n");
			break;

			default: /* by connected */
			/* send current cause */
			release(relation, param->disconnectinfo.location, param->disconnectinfo.cause);
		}
		return; // must return, because join may be destroyed
	}

	/* count relations */
	num=joinpbx_countrelations(j_serial);

	/* check number of relations */
	if (num > 2)
	{
		PDEBUG(DEBUG_JOIN, "join has more than two relations so there is no need to send a message.\n");
		return;
	}

	/* find interfaces not related to calling epoint */
	relation = j_relation;
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
						return; // join destroyed
				}
				break;
			}
			if (out_setup(epoint_id, message_type, param, NULL))
				return; // join destroyed
			break;

			default:
			PDEBUG(DEBUG_JOIN, "no need to send a message because there is no other endpoint than the calling one.\n");
		}
	} else
	{
		PDEBUG(DEBUG_JOIN, "sending message ep%ld -> ep%ld.\n", epoint_id, relation->epoint_id);
		message = message_create(j_serial, relation->epoint_id, JOIN_TO_EPOINT, message_type);
		memcpy(&message->param, param, sizeof(union parameter));
		message_put(message);
		PDEBUG(DEBUG_JOIN, "message sent.\n");
	}
}


/* join process is called from the main loop
 * it processes the current calling state.
 * returns 0 if join nothing was done
 */
int JoinPBX::handler(void)
{
//	struct join_relation *relation;
//	char dialing[32][32];
//	int port[32];
//	int found;
//	int i, j;
//	char *p;

	/* the bridge must be updated */
	if (j_updatebridge)
	{
		bridge();
		j_updatebridge = 0;
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
 * note: if this functions fails, it will destroy its own join object!
 */
int JoinPBX::out_setup(unsigned long epoint_id, int message_type, union parameter *param, char *newnumber)
{
	struct join_relation *relation;
	struct message *message;
	class Endpoint *epoint;

	PDEBUG(DEBUG_JOIN, "no endpoint found, so we will create an endpoint and send the setup message we have.\n");
	/* create a new relation */
	if (!(relation=add_relation()))
		FATAL("No memory for relation.\n");
	relation->type = RELATION_TYPE_SETUP;
	relation->channel_state = CHANNEL_STATE_HOLD; /* audio is assumed on a new join */
	relation->tx_state = NOTIFY_STATE_ACTIVE; /* new joins always assumed to be active */
	relation->rx_state = NOTIFY_STATE_ACTIVE; /* new joins always assumed to be active */
	/* create a new endpoint */
	epoint = new Endpoint(0, j_serial);
	if (!epoint)
		FATAL("No memory for Endpoint instance\n");
	if (!(epoint->ep_app = new DEFAULT_ENDPOINT_APP(epoint)))
		FATAL("No memory for Endpoint Application instance\n");
	relation->epoint_id = epoint->ep_serial;
	/* send setup message to new endpoint */
//printf("JOLLY DEBUG: %d\n",join_countrelations(j_serial));
//i			if (options.deb & DEBUG_JOIN)
//				joinpbx_debug(join, "Join::message_epoint");
	message = message_create(j_serial, relation->epoint_id, JOIN_TO_EPOINT, message_type);
	memcpy(&message->param, param, sizeof(union parameter));
	if (newnumber)
		SCPY(message->param.setup.dialinginfo.id, newnumber);
	PDEBUG(DEBUG_JOIN, "setup message sent to ep %d with number='%s'.\n", relation->epoint_id, message->param.setup.dialinginfo.id);
	message_put(message);
	return(0);
}


