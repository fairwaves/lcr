/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** Endpoint header file                                                      **
**                                                                           **
\*****************************************************************************/ 


/* structure of port_list */
struct port_list {
	struct port_list	*next;
	unsigned int		port_id;
	int			port_type;
	int			early_b; /* if patterns are available */
};

/* structure of an Enpoint */
class Endpoint
{
	public:
	Endpoint(unsigned int port_id, unsigned int join_id);
	~Endpoint();
	class Endpoint		*next;		/* next in list */
	unsigned int		ep_serial;	/* a unique serial to identify */

	/* applocaton relation */
	int			ep_app_type;
	class EndpointApp 	*ep_app;		/* link to application class */

	/* port relation */
	struct port_list 	*ep_portlist;	/* link to list of ports */
	struct port_list *portlist_new(unsigned int port_id, int port_type, int earlyb);
	void free_portlist(struct port_list *portlist);

	/* join relation */
	unsigned int 		ep_join_id;	/* link to join */

	/* if still used by threads */
	int			ep_use;
	struct lcr_work		ep_delete;

	/* application indipendant states */
	int			ep_park;		/* indicates that the epoint is parked */
	unsigned char		ep_park_callid[8];
	int			ep_park_len;
};

extern class Endpoint *epoint_first;

class Endpoint *find_epoint_id(unsigned int epoint_id);

