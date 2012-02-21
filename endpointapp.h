/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** EndpointApp header file                                                   **
**                                                                           **
\*****************************************************************************/ 

#define EAPP_TYPE_PBX		0
#define EAPP_TYPE_BRIDGE	1

/* structure of an EndpointApp */
class EndpointApp
{
	public:
	EndpointApp(class Endpoint *epoint, int origin, int type);
	virtual ~EndpointApp();

	int ea_type;
	class Endpoint		*ea_endpoint;
	virtual void ea_message_port(unsigned int port_id, int message, union parameter *param);
	virtual void ea_message_join(unsigned int join_id, int message, union parameter *param);
	virtual void trace_header(const char *name, int direction);

#ifdef WITH_MISDN
	struct mISDNport *hunt_port(char *ifname, int *channel);
#endif
	struct interface *hunt_interface(char *ifname);
};

class EndpointApp *new_endpointapp(class Endpoint *epoint, int origin, int type);

