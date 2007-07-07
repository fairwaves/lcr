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
CallChan::CallChan(class Endpoint *epoint) : Call(epoint)
{
	if (!epoint)
	{
		PERROR("software error, epoint is NULL.\n");
		exit(-1);
	}

	PDEBUG(DEBUG_CALL, "creating new call and connecting it to the endpoint.\n");

	c_type = CALL_TYPE_ASTERISK;
	c_epoint_id = epoint->ep_serial;

	PDEBUG(DEBUG_CALL, "Constructor(new call)");
}


/*
 * call descructor
 */
CallChan::~CallChan()
{

}


/* release call from endpoint
 * if the call has two relations, all relations are freed and the call will be
 * destroyed 
 */
void CallChan::release(unsigned long epoint_id, int hold, int location, int cause)
{
	if (!epoint_id)
	{
		PERROR("software error, epoint is NULL.\n");
		return;
	}

	c_epoint_id = 0;
	
	PDEBUG(DEBUG_CALL, "call_release(): ended.\n");
}


/* call process is called from the main loop
 * it processes the current calling state.
 * returns 0 if call nothing was done
 */
int CallChan::handler(void)
{
	return(0);
}

void CallChan::message_epoint(unsigned long epoint_id, int message_type, union parameter *param)
{
}

