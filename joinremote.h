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
	JoinRemote(unsigned long serial, char *remote_name, int remote_id);
	~JoinRemote();
	void message_epoint(unsigned long epoint_id, int message, union parameter *param);
	void message_remote(unsigned long ref, int message_type, union parameter *param);
	int handler(void);

	int j_remote_id;
	char j_remote_name[32];
	unsigned long j_epoint_id;
}; 


