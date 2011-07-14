
/* GSM port class */
class Premote : public PmISDN
{
	public:
	Premote(int type, struct mISDNport *mISDNport, char *portname, struct port_settings *settings, int channel, int exclusive, int mode, int remote_id);
	~Premote();

	unsigned int p_m_r_ref;
	int p_m_r_remote_id; /* remote instance (socket) */
	char p_m_r_remote_app[32];
	unsigned int p_m_r_handle; /* 0, if no bchannel is exported */

	int message_epoint(unsigned int epoint_id, int message_id, union parameter *param);
	void message_remote(int message_type, union parameter *param);

	int hunt_bchannel(void);
};


