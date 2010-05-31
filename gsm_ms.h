extern "C" {
#include <osmocom/osmocom_data.h>
#include <osmocom/mncc.h>
}

/* GSM port class */
class Pgsm_ms : public Pgsm
{
	public:
	Pgsm_ms(int type, struct mISDNport *mISDNport, char *portname, struct port_settings *settings, int channel, int exclusive, int mode);
	~Pgsm_ms();

	void setup_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc);
	void message_setup(unsigned int epoint_id, int message_id, union parameter *param);
	int message_epoint(unsigned int epoint_id, int message_id, union parameter *param);
};

int handle_gsm_ms(void);
int gsm_ms_conf(struct gsm_conf *gsm_conf, char *conf_error);
int gsm_ms_exit(int rc);
int gsm_ms_init(void);
int gsm_ms_new(const char *name, const char *socket_path);
int gsm_ms_delete(const char *name);

