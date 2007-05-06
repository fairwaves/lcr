/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** EndpointApp header file                                                   **
**                                                                           **
\*****************************************************************************/ 


/* structure of an EndpointApp */
class EndpointApp
{
	public:
	EndpointApp(class Endpoint *epoint);
	virtual ~EndpointApp();

	class Endpoint		*ea_endpoint;
	virtual int		handler(void);
	virtual void ea_message_port(unsigned long port_id, int message, union parameter *param);
	virtual void ea_message_call(unsigned long call_id, int message, union parameter *param);
};

