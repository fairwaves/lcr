/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** call functions for channel driver                                         **
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
 * constructor for a new call 
 * the call will have a relation to the calling endpoint
 */
CallAsterisk::CallAsterisk(unsigned long serial) : Call()
{
	PDEBUG(DEBUG_CALL, "Constructor(new call)");

	c_type = CALL_TYPE_ASTERISK;

	c_epoint_id = serial;
	if (c_epoint_id)
		PDEBUG(DEBUG_CALL, "New call connected to endpoint id %lu\n", c_epoint_id);
}


/*
 * call descructor
 */
CallAsterisk::~CallAsterisk()
{

}


/* call process is called from the main loop
 * it processes the current calling state.
 * returns 0 if call nothing was done
 */
int CallAsterisk::handler(void)
{
	return(0);
}


void CallAsterisk::message_epoint(unsigned long epoint_id, int message_type, union parameter *param)
{
	/* if endpoint has just been removed, but still a message in the que */
	if (epoint_id != c_epoint_id)
		return;
	
	/* look for asterisk's interface */
	if (admin_message_from_join(epoint_id, message_type, param)<0)
	{
		PERROR("No socket with asterisk found, this shall not happen. Closing socket shall cause release of all asterisk calls\n");
		return;		
	}

	if (message_type == MESSAGE_RELEASE)
	{
		delete this;
		return;
	}
}

void CallAsterisk::message_asterisk(unsigned long callref, int message_type, union parameter *param)
{
	struct message *message;

	/* create relation if no relation exists */
	if (!c_epoint_id)
	{
		class Endpoint		*epoint;

		if (!(epoint = new Endpoint(0, c_serial, callref)))
			FATAL("No memory for Endpoint instance\n");
		if (!(epoint->ep_app = new DEFAULT_ENDPOINT_APP(epoint)))
			FATAL("No memory for Endpoint Application instance\n");
	}

	message = message_create(c_serial, c_epoint_id, CALL_TO_EPOINT, message_type);
	memcpy(&message->param, param, sizeof(message->param));
	message_put(message);

	if (message_type == MESSAGE_RELEASE)
	{
		delete this;
		return;
	}
}



