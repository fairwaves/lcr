/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** EndpointAppBridge header file                                             **
**                                                                           **
\*****************************************************************************/ 


extern class EndpointAppBridge *appbridge_first;

/* structure of an EndpointAppBridge */
class EndpointAppBridge : public EndpointApp
{
	public:
	EndpointAppBridge(class Endpoint *epoint, int origin);
	~EndpointAppBridge();

	class EndpointAppBridge	*next;	/* next in list of apps */

	/* messages */
	void port_setup(struct port_list *portlist, int message_type, union parameter *param);
	void port_release(struct port_list *portlist, int message_type, union parameter *param);
	void port_other(struct port_list *portlist, int message_type, union parameter *param);
	void ea_message_port(unsigned int port_id, int message, union parameter *param);

	void trace_header(const char *name, int direction);
};



