extern int new_callref;

struct gsm_conf {
	char debug[128];		/* debug info */
	char interface_bsc[64];		/* loopback interface BSC side */
	char interface_lcr[64];		/* loopback interface LCR side */
	char openbsc_cfg[128];		/* openbsc config file */
	char short_name[64];		/* short network name */
	char long_name[64];		/* long network name */
	char hlr[64];			/* database name */
	int allow_all;			/* accept unknown subscribers */
	int keep_l2;			/* keep layer 2 after exit */
	char pcapfile[128];		/* open capture file for BS11 links */
	int reject_cause;		/* reject cause for unsubcribed IMSIs */
};

struct lcr_gsm {
	void		*network;	/* OpenBSC network handle */
	struct gsm_conf	conf;		/* gsm.conf options */
	int		gsm_sock;	/* loopback interface GSM side */
	int		gsm_port;	/* loopback interface port number */
};

extern struct lcr_gsm *gsm;

/* GSM port class */
class Pgsm : public PmISDN
{
	public:
	Pgsm(int type, struct mISDNport *mISDNport, char *portname, struct port_settings *settings, int channel, int exclusive, int mode);
	~Pgsm();

	void *p_m_g_instance; /* pointer to network/ms instance */
	unsigned int p_m_g_callref; /* ref by OpenBSC/Osmocom-BB */
	struct lcr_work p_m_g_delete;		/* timer for audio transmission */
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
int send_and_free_mncc(void *instance, unsigned int msg_type, void *data);
void gsm_trace_header(struct mISDNport *mISDNport, class PmISDN *port, unsigned int msg_type, int direction);
int handle_gsm(void);
int gsm_conf(struct gsm_conf *gsm_conf, char *conf_error);
int gsm_exit(int rc);
int gsm_init(void);

