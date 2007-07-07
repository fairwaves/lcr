/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** The Endpoint is the link between the call and the port.                   **
**                                                                           **
\*****************************************************************************/ 

#include <stdio.h>
#include "main.h"

unsigned long epoint_serial = 1; /* initial value must be 1, because 0== no epoint */

class Endpoint *epoint_first = NULL;


/*
 * find the epoint with epoint_id
 */ 
class Endpoint *find_epoint_id(unsigned long epoint_id)
{
	class Endpoint *epoint = epoint_first;

	while(epoint)
	{
//printf("comparing: '%s' with '%s'\n", name, epoint->name);
		if (epoint->ep_serial == epoint_id)
			return(epoint);
		epoint = epoint->next;
	}

	return(NULL);
}


/*
 * endpoint constructor (link with either port or call id)
 */
Endpoint::Endpoint(int port_id, int call_id)
{
	class Port *port;
	class Endpoint **epointpointer;
	int earlyb = 0;

	/* epoint structure */
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d): Allocating enpoint %d and connecting it with:%s%s\n", epoint_serial, epoint_serial, (port_id)?" ioport":"", (call_id)?" call":"");

        ep_portlist = NULL;
	ep_app = NULL;
	ep_use = 1;

	/* add endpoint to chain */
	next = NULL;
	epointpointer = &epoint_first;
	while(*epointpointer)
		epointpointer = &((*epointpointer)->next);
	*epointpointer = this;

	/* serial */
	ep_serial = epoint_serial++;

	/* link to call or port */
	if (port_id)
	{
		port = find_port_id(port_id);
		if (port)
		{
			if ((port->p_type&PORT_CLASS_mISDN_MASK) == PORT_CLASS_mISDN_DSS1)
				earlyb = ((class PmISDN *)port)->p_m_mISDNport->earlyb;
			if (!portlist_new(port_id, port->p_type, earlyb))
			{
				PERROR("no mem for portlist, exitting...\n");
				exit(-1);
			}
		}
	}
	ep_call_id = call_id;

	ep_park = 0;
	ep_park_len = 0;

	classuse++;
}


/*
 * endpoint destructor
 */
Endpoint::~Endpoint(void)
{
	class Endpoint *temp, **tempp;
	struct port_list *portlist, *mtemp;

	classuse--;

	/* remote application */
	if (ep_app)
		delete ep_app;
	
	/* free relations */
	if (ep_call_id)
	{
		PERROR("warning: still relation to call.\n");
	}

	/* free portlist */
	portlist = ep_portlist;
	while(portlist)
	{
		if (portlist->port_id)
		{
			PERROR("warning: still relation to port (portlist list)\n");
		}
		mtemp = portlist;
		portlist = portlist->next;
		memset(mtemp, 0, sizeof(struct port_list));
		free(mtemp);
		ememuse--;
	}

	/* detach */
	temp =epoint_first;
	tempp = &epoint_first;
	while(temp)
	{
		if (temp == this)
			break;

		tempp = &temp->next;
		temp = temp->next;
	}
	if (temp == 0)
	{
		PERROR("error: endpoint not in endpoint's list, exitting.\n");
		exit(-1);
	}
	*tempp = next;

	/* free */
	PDEBUG(DEBUG_EPOINT, "removed endpoint %d.\n", ep_serial);
}

/* create new portlist relation
 */
struct port_list *Endpoint::portlist_new(unsigned long port_id, int port_type, int earlyb)
{
	struct port_list *portlist, **portlistpointer;

	/* portlist structure */
	portlist = (struct port_list *)calloc(1, sizeof(struct port_list));
	if (!portlist)
	{
		PERROR("no mem for allocating port_list\n");
		return(0);
	}
	ememuse++;
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) allocating port_list.\n", ep_serial);
	memset(portlist, 0, sizeof(struct port_list));

	/* add port_list to chain */
	portlist->next = NULL;
	portlistpointer = &ep_portlist;
	while(*portlistpointer)
		portlistpointer = &((*portlistpointer)->next);
	*portlistpointer = portlist;

	/* link to call or port */
	portlist->port_id = port_id;
	portlist->port_type = port_type;
	portlist->early_b = earlyb;

	return(portlist);
}


/* free portlist relation
 */
void Endpoint::free_portlist(struct port_list *portlist)
{
	struct port_list *temp, **tempp;

	temp = ep_portlist;
	tempp = &ep_portlist;
	while(temp)
	{
		if (temp == portlist)
			break;

		tempp = &temp->next;
		temp = temp->next;
	}
	if (temp == 0)
	{
		PERROR("port_list not in endpoint's list, exitting.\n");
		exit(-1);
	}
	/* detach */
	*tempp=portlist->next;

	/* free */
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) removed port_list from endpoint\n", ep_serial);
	memset(portlist, 0, sizeof(struct port_list));
	free(portlist);
	ememuse--;
}


/* handler for endpoint
 */
int Endpoint::handler(void)
{
	if (ep_use <= 0)
	{
		delete this;
		return(-1);
	}

	/* call application handler */
	if (ep_app)
		return(ep_app->handler());
	return(0);
}
