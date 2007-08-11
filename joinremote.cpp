/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** join functions for remote application                                     **
**                                                                           **
\*****************************************************************************/ 

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
//#include <unistd.h>
//#include <poll.h>
//#include <sys/types.h>
//#include <sys/stat.h>
//#include <fcntl.h>
#include "main.h"
//#define __u8 unsigned char
//#define __u16 unsigned short
//#define __u32 unsigned long
//#include "linux/isdnif.h"


/*
 * constructor for a new join 
 * the join will have a relation to the calling endpoint
 */
JoinRemote::JoinRemote(unsigned long serial, char *remote_name, int remote_id) : Join()
{
	PDEBUG(DEBUG_JOIN, "Constructor(new join)");
	union parameter *param;

	SCPY(j_remote_name, remote_name);
	j_remote_id = remote_id;
	j_type = JOIN_TYPE_REMOTE;

	j_epoint_id = serial; /* this is the endpoint, if created by epoint */
	if (j_epoint_id)
		PDEBUG(DEBUG_JOIN, "New remote join connected to endpoint id %lu and application %s\n", j_epoint_id, remote_name);

	/* send new ref to remote socket */
	memset(&param, 0, sizeof(param));
	/* the j_serial is assigned by Join() parent. this is sent as new ref */
	if (admin_message_from_join(j_remote_id, j_serial, MESSAGE_NEWREF, param)<0)
		FATAL("No socket with remote application '%s' found, this shall not happen. because we already created one.\n", j_remote_name);
}


/*
 * join descructor
 */
JoinRemote::~JoinRemote()
{
}


/* join process is called from the main loop
 * it processes the current calling state.
 * returns 0 if join nothing was done
 */
int JoinRemote::handler(void)
{
	return(0);
}


void JoinRemote::message_epoint(unsigned long epoint_id, int message_type, union parameter *param)
{
	/* if endpoint has just been removed, but still a message in the que */
	if (epoint_id != j_epoint_id)
		return;
	
	/* look for Remote's interface */
	if (admin_message_from_join(j_remote_id, j_serial, message_type, param)<0)
	{
		PERROR("No socket with remote application '%s' found, this shall not happen. Closing socket shall cause release of all joins.\n", j_remote_name);
		return;		
	}

	if (message_type == MESSAGE_RELEASE)
	{
		delete this;
		return;
	}
}

void JoinRemote::message_remote(int message_type, union parameter *param)
{
	struct message *message;

	/* create relation if no relation exists */
	if (!j_epoint_id)
	{
		class Endpoint		*epoint;

		if (!(epoint = new Endpoint(0, j_serial)))
			FATAL("No memory for Endpoint instance\n");
		j_epoint_id = epoint->ep_serial;
		if (!(epoint->ep_app = new DEFAULT_ENDPOINT_APP(epoint)))
			FATAL("No memory for Endpoint Application instance\n");
	}

	/* set serial on bchannel message
	 * also ref is given, so we send message with ref */
	if (message_type == MESSAGE_BCHANNEL)
	{
		message_bchannel_from_join(this, param->bchannel.type, param->bchannel.addr);
		return;
	}
	
	/* cannot just forward, because param is not of container "struct message" */
	message = message_create(j_serial, j_epoint_id, JOIN_TO_EPOINT, message_type);
	memcpy(&message->param, param, sizeof(message->param));
	message_put(message);

	if (message_type == MESSAGE_RELEASE)
	{
		delete this;
		return;
	}
}

void message_bchannel_to_join(int serial, int type, unsigned long addr)
{
	union parameter param;
	class Join *join;
	class JoinRemote *joinremote;

	/* find join serial */
	join = find_join_id(serial);
	if (!join)
	{
		PDEBUG(DEBUG_JOIN | DEBUG_BCHANNEL, "Join %d not found\n", serial);
		return;
	}
	if (!join->j_type != JOIN_TYPE_REMOTE)
	{
		PERROR("Join %d not of remote type. This shall not happen.\n", serial);
		return;
	}
	joinremote = (class JoinRemote *)join;

	memset(&param, 0, sizeof(union parameter));
	param.bchannel.type = type;
	param.bchannel.addr = addr;
	if (admin_message_from_join(joinremote->j_remote_id, joinremote->j_serial, MESSAGE_BCHANNEL, &param)<0)
	{
		PERROR("No socket with remote application '%s' found, this shall not happen. Closing socket shall cause release of all joins.\n", joinremote->j_remote_name);
		return;		
	}
}



