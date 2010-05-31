extern "C" {
#include <openbsc/gsm_data.h>
}

/* GSM port class */
class Pgsm_bs : public Pgsm
{
	public:
	Pgsm_bs(int type, struct mISDNport *mISDNport, char *portname, struct port_settings *settings, int channel, int exclusive, int mode);
	~Pgsm_bs();

	void setup_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc);
	void start_dtmf_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc);
	void stop_dtmf_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc);
	void hold_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc);
	void retr_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc);
	void message_setup(unsigned int epoint_id, int message_id, union parameter *param);
	int message_epoint(unsigned int epoint_id, int message_id, union parameter *param);
};

int handle_gsm_bs(void);
int gsm_bs_conf(struct gsm_conf *gsm_conf, char *conf_error);
int gsm_bs_exit(int rc);
int gsm_bs_init(void);
