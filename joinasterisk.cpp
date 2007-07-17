/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** join functions for channel driver                                         **
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
JoinAsterisk::JoinAsterisk(unsigned long serial) : Join()
{
	PDEBUG(DEBUG_JOIN, "Constructor(new join)");

	c_type = JOIN_TYPE_ASTERISK;

	c_epoint_id = serial;
	if (c_epoint_id)
		PDEBUG(DEBUG_JOIN, "New join connected to endpoint id %lu\n", c_epoint_id);
}


/*
 * join descructor
 */
JoinAsterisk::~JoinAsterisk()
{

}


/* join process is called from the main loop
 * it processes the current calling state.
 * returns 0 if join nothing was done
 */
int JoinAsterisk::handler(void)
{
	return(0);
}


void JoinAsterisk::message_epoint(unsigned long epoint_id, int message_type, union parameter *param)
{
	/* if endpoint has just been removed, but still a message in the que */
	if (epoint_id != c_epoint_id)
		return;
	
	/* look for asterisk's interface */
	if (admin_message_from_join(epoint_id, message_type, param)<0)
	{
		PERROR("No socket with asterisk found, this shall not happen. Closing socket shall cause release of all asterisk joins\n");
		return;		
	}

	if (message_type == MESSAGE_RELEASE)
	{
		delete this;
		return;
	}
}

void JoinAsterisk::message_asterisk(unsigned long ref, int message_type, union parameter *param)
{
	struct message *message;

	/* create relation if no relation exists */
	if (!c_epoint_id)
	{
		class Endpoint		*epoint;

		if (!(epoint = new Endpoint(0, c_serial, ref)))
			FATAL("No memory for Endpoint instance\n");
		if (!(epoint->ep_app = new DEFAULT_ENDPOINT_APP(epoint)))
			FATAL("No memory for Endpoint Application instance\n");
	}

	message = message_create(c_serial, c_epoint_id, JOIN_TO_EPOINT, message_type);
	memcpy(&message->param, param, sizeof(message->param));
	message_put(message);

	if (message_type == MESSAGE_RELEASE)
	{
		delete this;
		return;
	}
}



