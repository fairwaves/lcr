/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** h323-port header file                                                     **
**                                                                           **
\*****************************************************************************/ 


/* h323 port class */
class H323_chan;
class H323Port : public Port
{
	public:
	H323Port(int type, char *portname, struct port_settings *settings);
	~H323Port();
	int message_epoint(unsigned long epoint_id, int message, union parameter *param);

	H323_chan *p_h323_channel_in;			/* channels of port */
	H323_chan *p_h323_channel_out;
	void *p_h323_connect; /* q931 object of connect PDU */
};


