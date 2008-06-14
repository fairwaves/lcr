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
	JoinRemote(unsigned int serial, char *remote_name, int remote_id);
	~JoinRemote();
	void message_epoint(unsigned int epoint_id, int message, union parameter *param);
	void message_remote(int message_type, union parameter *param);
	int handler(void);

	int j_remote_id;
	char j_remote_name[32];
	unsigned int j_epoint_id;
}; 


void message_bchannel_to_remote(unsigned int remote_id, unsigned int ref, int type, unsigned int handle, int tx_gain, int rx_gain, char *pipeline, unsigned char *crypt, int crypt_len, int crypt_type);
