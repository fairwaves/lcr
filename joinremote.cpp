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
JoinRemote::JoinRemote(unsigned long serial, char *remote) : Join()
{
	PDEBUG(DEBUG_JOIN, "Constructor(new join)");
	union parameter *param;

	SCPY(j_remote, remote);
	j_type = JOIN_TYPE_REMOTE;

	j_epoint_id = serial;
	if (j_epoint_id)
		PDEBUG(DEBUG_JOIN, "New remote join connected to endpoint id %lu and application %s\n", j_epoint_id, remote);

	/* send new ref to remote socket */
	memset(&param, 0, sizeof(param));
	if (admin_message_from_join(j_remote, j_serial, MESSAGE_NEWREF, param)<0)
		FATAL("No socket with remote application '%s' found, this shall not happen. because we already created one.\n", j_remote);
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
	if (admin_message_from_join(j_remote, j_serial, message_type, param)<0)
	{
		PERROR("No socket with remote application '%s' found, this shall not happen. Closing socket shall cause release of all joins.\n", j_remote);
		return;		
	}

	if (message_type == MESSAGE_RELEASE)
	{
		delete this;
		return;
	}
}

void JoinRemote::message_remote(unsigned long ref, int message_type, union parameter *param)
{
	struct message *message;

	/* create relation if no relation exists */
	if (!j_epoint_id)
	{
		class Endpoint		*epoint;

		if (!(epoint = new Endpoint(0, j_serial, ref)))
			FATAL("No memory for Endpoint instance\n");
		if (!(epoint->ep_app = new DEFAULT_ENDPOINT_APP(epoint)))
			FATAL("No memory for Endpoint Application instance\n");
	}

	message = message_create(j_serial, j_epoint_id, JOIN_TO_EPOINT, message_type);
	memcpy(&message->param, param, sizeof(message->param));
	message_put(message);

	if (message_type == MESSAGE_RELEASE)
	{
		delete this;
		return;
	}
}



