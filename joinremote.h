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

class JoinRemote : public Join
{
	public:
	JoinRemote(unsigned long serial, char *remote);
	~JoinRemote();
	void message_epoint(unsigned long epoint_id, int message, union parameter *param);
	void message_remote(unsigned long ref, int message_type, union parameter *param);
	int handler(void);

	char j_remote[32];
	unsigned long j_epoint_id;
}; 


