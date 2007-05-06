/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** call header file for channel interface                                    **
**                                                                           **
\*****************************************************************************/ 

class CallChan : public Call
{
	public:
	CallChan(class Endpoint *epoint);
	~CallChan();
	void message_epoint(unsigned long epoint_id, int message, union parameter *param);
	int handler(void);
	void release(unsigned long epoint_id, int hold, int location, int cause);

	unsigned long c_epoint_id;
}; 


