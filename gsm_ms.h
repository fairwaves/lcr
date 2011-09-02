
/* GSM port class */
class Pgsm_ms : public Pgsm
{
	public:
	Pgsm_ms(int type, struct mISDNport *mISDNport, char *portname, struct port_settings *settings, int channel, int exclusive, int mode);
	~Pgsm_ms();

	int p_m_g_dtmf_state;
	int p_m_g_dtmf_index;
	char p_m_g_dtmf[128];
	struct lcr_timer p_m_g_dtmf_timer;
	void dtmf_statemachine(struct gsm_mncc *mncc);

	void setup_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc);
	void message_setup(unsigned int epoint_id, int message_id, union parameter *param);
	void message_dtmf(unsigned int epoint_id, int message_id, union parameter *param);
	void message_information(unsigned int epoint_id, int message_id, union parameter *param);
	int message_epoint(unsigned int epoint_id, int message_id, union parameter *param);
};

int gsm_ms_conf(struct gsm_conf *gsm_conf, char *conf_error);
int gsm_ms_exit(int rc);
int gsm_ms_init(void);
int gsm_ms_new(const char *name);
int gsm_ms_delete(const char *name);
int message_ms(struct lcr_gsm *lcr_gsm, int msg_type, void *arg);

