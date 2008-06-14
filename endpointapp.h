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


/* structure of an EndpointApp */
class EndpointApp
{
	public:
	EndpointApp(class Endpoint *epoint, int origin);
	virtual ~EndpointApp();

	class Endpoint		*ea_endpoint;
	virtual int		handler(void);
	virtual void ea_message_port(unsigned int port_id, int message, union parameter *param);
	virtual void ea_message_join(unsigned int join_id, int message, union parameter *param);
};

