
#include <sys/un.h>

extern int new_callref;

struct mncc_q_entry {
	struct mncc_q_entry *next;
	unsigned int len;
	char data[0];			/* struct gsm_mncc */
};

enum {
	LCR_GSM_TYPE_NETWORK,
	LCR_GSM_TYPE_MS,
};

struct lcr_gsm {
	struct lcr_gsm	*gsm_ms_next;	/* list of MS instances, in case of MS */
	char		name[16];	/* name of MS instance, in case of MS */
	int		type;		/* LCR_GSM_TYPE_*/

	struct lcr_fd	mncc_lfd;	/* Unix domain socket to OpenBSC MNCC */
	struct mncc_q_entry *mncc_q_hd;
	struct mncc_q_entry *mncc_q_tail;
	struct lcr_timer socket_retry;	/* Timer to re-try connecting to BSC socket */
	struct sockaddr_un sun;		/* Socket address of MNCC socket */
};

/* GSM port class */
class Pgsm : public PmISDN
{
	public:
	Pgsm(int type, struct mISDNport *mISDNport, char *portname, struct port_settings *settings, int channel, int exclusive, int mode);
	~Pgsm();

	struct lcr_gsm *p_m_g_lcr_gsm; /* pointer to network/ms instance */
	unsigned int p_m_g_callref; /* ref by OpenBSC/Osmocom-BB */
	struct lcr_work p_m_g_delete; /* queue destruction of GSM port instance */
	unsigned int p_m_g_mode; /* data/transparent mode */
	int p_m_g_gsm_b_sock; /* gsm bchannel socket */
	struct lcr_fd p_m_g_gsm_b_fd; /* event node */
	int p_m_g_gsm_b_index; /* gsm bchannel socket index to use */
	int p_m_g_gsm_b_active; /* gsm bchannel socket is activated */
	struct lcr_msg *p_m_g_notify_pending;	/* queue for NOTIFY if not connected */
	void *p_m_g_encoder, *p_m_g_decoder;	/* gsm handle */
	signed short p_m_g_rxdata[160]; /* receive audio buffer */
	int p_m_g_rxpos; /* position in audio buffer 0..159 */
	int p_m_g_tch_connected; /* indicates if audio is connected */

	void bchannel_close(void);
	int bchannel_open(int index);
	void bchannel_receive(struct mISDNhead *hh, unsigned char *data, int len);
	void bchannel_send(unsigned int prim, unsigned int id, unsigned char *data, int len);

	void frame_send(void *_frame);
	void frame_receive(void *_frame);

	int hunt_bchannel(void);
	void call_conf_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *gsm);
	void call_proc_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc);
	void alert_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc);
	void setup_cnf(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc);
	void setup_compl_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc);
	void disc_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc);
	void rel_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc);
	void notify_ind(unsigned int msg_type, unsigned int callref, struct gsm_mncc *mncc);
	void message_notify(unsigned int epoint_id, int message_id, union parameter *param);
	void message_alerting(unsigned int epoint_id, int message_id, union parameter *param);
	void message_connect(unsigned int epoint_id, int message_id, union parameter *param);
	void message_disconnect(unsigned int epoint_id, int message_id, union parameter *param);
	void message_release(unsigned int epoint_id, int message_id, union parameter *param);
	int message_epoint(unsigned int epoint_id, int message_id, union parameter *param);
};

struct gsm_mncc *create_mncc(int msg_type, unsigned int callref);
int send_and_free_mncc(struct lcr_gsm *lcr_gsm, unsigned int msg_type, void *data);
void gsm_trace_header(struct mISDNport *mISDNport, class PmISDN *port, unsigned int msg_type, int direction);
int gsm_conf(struct gsm_conf *gsm_conf, char *conf_error);
int gsm_exit(int rc);
int gsm_init(void);
int mncc_socket_retry_cb(struct lcr_timer *timer, void *inst, int index);

