/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** The Endpoint is the link between the join and the port.                   **
**                                                                           **
\*****************************************************************************/ 

#include "main.h"

unsigned int epoint_serial = 1; /* initial value must be 1, because 0== no epoint */

class Endpoint *epoint_first = NULL;


/*
 * find the epoint with epoint_id
 */ 
class Endpoint *find_epoint_id(unsigned int epoint_id)
{
	class Endpoint *epoint = epoint_first;

	while(epoint) {
//printf("comparing: '%s' with '%s'\n", name, epoint->name);
		if (epoint->ep_serial == epoint_id)
			return(epoint);
		epoint = epoint->next;
	}

	return(NULL);
}

int delete_endpoint(struct lcr_work *work, void *instance, int index);

/*
 * endpoint constructor (link with either port or join id)
 */
Endpoint::Endpoint(unsigned int port_id, unsigned int join_id)
{
	class Port *port;
	class Endpoint **epointpointer;
	int earlyb = 0;

	/* epoint structure */
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d): Allocating enpoint %d and connecting it with:%s%s\n", epoint_serial, epoint_serial, (port_id)?" ioport":"", (join_id)?" join":"");

        ep_portlist = NULL;
	ep_app = NULL;
	memset(&ep_delete, 0, sizeof(ep_delete));
	add_work(&ep_delete, delete_endpoint, this, 0);
	ep_use = 1;

	/* add endpoint to chain */
	next = NULL;
	epointpointer = &epoint_first;
	while(*epointpointer)
		epointpointer = &((*epointpointer)->next);
	*epointpointer = this;

	/* serial */
	ep_serial = epoint_serial++;

	/* link to join or port */
	if (port_id) {
		port = find_port_id(port_id);
		if (port) {
			if ((port->p_type&PORT_CLASS_MASK) == PORT_CLASS_mISDN)
				earlyb = ((class PmISDN *)port)->p_m_mISDNport->earlyb;
			if (!portlist_new(port_id, port->p_type, earlyb))
				FATAL("No memory for portlist.\n");
		}
	}
	ep_join_id = join_id;

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
	if (ep_join_id) {
		PERROR("warning: still relation to join.\n");
	}

	/* free portlist */
	portlist = ep_portlist;
	while(portlist) {
		if (portlist->port_id) {
			PERROR("warning: still relation to port (portlist list)\n");
		}
		mtemp = portlist;
		portlist = portlist->next;
		memset(mtemp, 0, sizeof(struct port_list));
		FREE(mtemp, sizeof(struct port_list));
		ememuse--;
	}

	/* detach */
	temp =epoint_first;
	tempp = &epoint_first;
	while(temp) {
		if (temp == this)
			break;

		tempp = &temp->next;
		temp = temp->next;
	}
	if (temp == 0)
		FATAL("Endpoint not in Endpoint's list.\n");
	*tempp = next;

	del_work(&ep_delete);

	/* free */
	PDEBUG(DEBUG_EPOINT, "removed endpoint %d.\n", ep_serial);
}

/* create new portlist relation
 */
struct port_list *Endpoint::portlist_new(unsigned int port_id, int port_type, int earlyb)
{
	struct port_list *portlist, **portlistpointer;

	/* portlist structure */
	portlist = (struct port_list *)MALLOC(sizeof(struct port_list));
	ememuse++;
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) allocating port_list.\n", ep_serial);

	/* add port_list to chain */
	portlist->next = NULL;
	portlistpointer = &ep_portlist;
	while(*portlistpointer)
		portlistpointer = &((*portlistpointer)->next);
	*portlistpointer = portlist;

	/* link to join or port */
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
	while(temp) {
		if (temp == portlist)
			break;

		tempp = &temp->next;
		temp = temp->next;
	}
	if (!temp)
		FATAL("port_list not in Endpoint's list.\n");
	/* detach */
	*tempp=portlist->next;

	/* free */
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) removed port_list from endpoint\n", ep_serial);
	FREE(portlist, sizeof(struct port_list));
	ememuse--;
}


int delete_endpoint(struct lcr_work *work, void *instance, int index)
{
	class Endpoint *ep = (class Endpoint *)instance;

	if (ep->ep_use <= 0)
		delete ep;

	return 0;
}

