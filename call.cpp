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
//#include <string.h>
//#include <stdlib.h>
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

unsigned long call_serial = 1; /* must be 1, because 0== no call */

//CALL_STATES

class Call *call_first = NULL;

/*
 * find the call with call_id
 */ 
class Call *find_call_id(unsigned long call_id)
{
	class Call *call = call_first;

	while(call)
	{
//printf("comparing: '%s' with '%s'\n", name, call->c_name);
		if (call->c_serial == call_id)
			return(call);
		call = call->next;
	}

	return(NULL);
}


/*
 * constructor for a new call 
 */
Call::Call(class Endpoint *epoint)
{
	class Call **callp;

	if (!epoint)
	{
		PERROR("software error, epoint is NULL.\n");
		exit(-1);
	}
	c_serial = call_serial++;
	c_type = CALL_TYPE_NONE;

	/* attach to chain */
	next = NULL;
	callp = &call_first;
	while(*callp)
		callp = &((*callp)->next);
	*callp = this;

	classuse++;
}


/*
 * call descructor
 */
Call::~Call()
{
	class Call *cl, **clp;

	classuse--;

	cl = call_first;
	clp = &call_first;
	while(cl)
	{
		if (cl == this)
			break;
		clp = &cl->next;
		cl = cl->next;
	}
	if (!cl)
	{
		PERROR("software error, call not in chain! exitting\n");
		exit(-1);
	}
	*clp = cl->next; /* detach from chain */
}



/* epoint sends a message to a call
 *
 */
void Call::message_epoint(unsigned long epoint_id, int message_type, union parameter *param)
{
}


/* call process is called from the main loop
 * it processes the current calling state.
 * returns 0 if call nothing was done
 */
int Call::handler(void)
{
	return(0);
}

void Call::release(unsigned long epoint_id, int hold, int location, int cause)
{
}
	
/* free all call structures */
void call_free(void)
{

	if (!call_first)
	{
		PDEBUG(DEBUG_CALL, "no more pending call(s), done!\n");
		return;
	}
	while(call_first)
	{
		if (options.deb & DEBUG_CALL)
		{
			PDEBUG(DEBUG_CALL, "freeing pending call\n");
		}

		delete call_first;
	}
}



