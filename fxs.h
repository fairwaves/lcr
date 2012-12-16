/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** fxs-port header file                                                      **
**                                                                           **
\*****************************************************************************/ 

/* FXS port classes */
class Pfxs : public PmISDN
{
	public:
	Pfxs(int type, struct mISDNport *mISDNport, char *portname, struct port_settings *settings, struct interface *interface, int mode);
	~Pfxs();

	struct lcr_work p_m_fxs_delete;
	struct lcr_timer p_m_fxs_dtmf_timer;
	int p_m_fxs_allow_dtmf;
	int p_m_fxs_age;
	int p_m_fxs_knocking;

	int ph_control_pots(unsigned int cont, unsigned char *data, int len);
	int hunt_bchannel(void);

	void pickup_ind(unsigned int cont);
	void hangup_ind(unsigned int cont);
	void answer_ind(unsigned int cont);
	void hold_ind(unsigned int cont);
	void retrieve_ind(unsigned int cont);
	void keypulse_ind(unsigned int cont);
	void flash_ind(unsigned int cont);
	void reject_ind(unsigned int cont);

	void message_setup(unsigned int epoint_id, int message_id, union parameter *param);
	void message_information(unsigned int epoint_id, int message_id, union parameter *param);
	void message_release(unsigned int epoint_id, int message_id, union parameter *param);
	void message_proceeding(unsigned int epoint_id, int message_id, union parameter *param);
	void message_alerting(unsigned int epoint_id, int message_id, union parameter *param);
	void message_connect(unsigned int epoint_id, int message_id, union parameter *param);
	void message_disconnect(unsigned int epoint_id, int message_id, union parameter *param);
	int message_epoint(unsigned int epoint_id, int message, union parameter *param);
};

int stack2manager_fxs(struct mISDNport *mISDNport, unsigned int cont);

