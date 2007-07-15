/*****************************************************************************\
**                                                                           **
** LCR                                                                       **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** call header file for Asterisk interface                                   **
**                                                                           **
\*****************************************************************************/ 

class CallAsterisk : public Call
{
	public:
	CallAsterisk(unsigned long serial);
	~CallAsterisk();
	void message_epoint(unsigned long epoint_id, int message, union parameter *param);
	void message_asterisk(unsigned long callref, int message_type, union parameter *param);
	int handler(void);

	unsigned long c_epoint_id;
}; 


