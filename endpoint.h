/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
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
	unsigned long		port_id;
	int			port_type;
	int			early_b; /* if patterns are available */
};

/* structure of an Enpoint */
class Endpoint
{
	public:
	Endpoint(int port_id, int call_id);
	~Endpoint();
	class Endpoint		*next;		/* next in list */
	unsigned long		ep_serial;	/* a unique serial to identify */
	int			handler(void);

	/* applocaton relation */
	class EndpointApp 	*ep_app;		/* link to application class */

	/* port relation */
	struct port_list 	*ep_portlist;	/* link to list of ports */
	struct port_list *portlist_new(unsigned long port_id, int port_type);
	void free_portlist(struct port_list *portlist);

	/* call relation */
	unsigned long 		ep_call_id;	/* link to call */

	/* if still used by threads */
	int			ep_use;

	/* application indipendant states */
	int			ep_park;		/* indicates that the epoint is parked */
	unsigned char		ep_park_callid[8];
	int			ep_park_len;
};

extern class Endpoint *epoint_first;

class Endpoint *find_epoint_id(unsigned long epoint_id);

