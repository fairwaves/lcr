/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** join header file for Asterisk interface                                   **
**                                                                           **
\*****************************************************************************/ 

class JoinAsterisk : public Join
{
	public:
	JoinAsterisk(unsigned long serial);
	~JoinAsterisk();
	void message_epoint(unsigned long epoint_id, int message, union parameter *param);
	void message_asterisk(unsigned long ref, int message_type, union parameter *param);
	int handler(void);

	unsigned long c_epoint_id;
}; 


