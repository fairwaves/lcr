/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** mISDN-port header file                                                    **
**                                                                           **
\*****************************************************************************/ 

#define FROMUP_BUFFER_SIZE 1024
#define FROMUP_BUFFER_MASK 1023

extern int entity;
extern int mISDNdevice;

extern int mISDNsocket;

enum {
	B_EVENT_USE,		/* activate/export bchannel */
	B_EVENT_EXPORTREQUEST,	/* remote app requests bchannel */
	B_EVENT_IMPORTREQUEST,	/* remote app releases bchannel */
	B_EVENT_ACTIVATED,	/* DL_ESTABLISH received */
	B_EVENT_DROP,		/* deactivate/re-import bchannel */
	B_EVENT_DEACTIVATED,	/* DL_RELEASE received */
	B_EVENT_EXPORTED,	/* BCHANNEL_ASSIGN received */
	B_EVENT_IMPORTED,	/* BCHANNEL_REMOVE received */
	B_EVENT_TIMEOUT,	/* timeout for bchannel state */
};

/* mISDN port structure list */
struct mISDNport {
	struct mlayer3 *ml3;
	struct mISDNport *next;
	char name[64]; /* name of port, if available */
	struct interface_port *ifport; /* link to interface_port */
//	int iftype; /* IF_* */
//	int multilink; /* if set, this port will not support callwaiting */
	int portnum; /* port number 1..n */
	int ptp; /* if ptp is set, we keep track of l2link */
	int l1link; /* if l1 is available (only works with nt-mode) */
	int l2link; /* if l2 is available (at PTP we take this serious) */
	unsigned char l2mask[16]; /* 128 bits for each tei */
	int l1hold; /* set, if layer 1 should be holt */
	int l2hold; /* set, if layer 2 must be hold/checked */
	struct lcr_timer l2establish; /* time until establishing after link failure */
	int use; /* counts the number of port that uses this port */
	int ntmode; /* is TRUE if port is NT mode */
	int tespecial; /* is TRUE if port uses special TE mode */
	int pri; /* is TRUE if port is a primary rate interface */
	int tones; /* TRUE if tones are sent outside connect state */
	int earlyb; /* TRUE if tones are received outside connect state */
	int b_num; /* number of bchannels */
	int b_reserved; /* number of bchannels reserved or in use */
	class PmISDN *b_port[128]; /* bchannel assigned to port object */
	struct mqueue upqueue;
	struct lcr_fd b_sock[128]; /* socket list elements */
	int b_mode[128]; /* B_MODE_* */
	int b_state[128]; /* statemachine, 0 = IDLE */
	struct lcr_timer b_timer[128]; /* timer for bchannel state machine */
	int b_remote_id[128]; /* the socket currently exported (0=none) */
	unsigned int b_remote_ref[128]; /* the ref currently exported */
	int locally; /* local causes are sent as local causes not remote */
	int los, ais, rdi, slip_rx, slip_tx;

	/* gsm */
#ifdef WITH_GSM_BS
	int gsm_bs; /* this is the (only) GSM BS interface */
#endif
#ifdef WITH_GSM_MS
	int gsm_ms; /* this is the an GSM MS interface */
#endif
#if defined WITH_GSM_BS || defined WITH_GSM_MS
	int lcr_sock; /* socket of loopback on LCR side */
#endif

	/* ss5 */
	unsigned int ss5; /* set, if SS5 signalling enabled, also holds feature bits */
};
extern mISDNport *mISDNport_first;

/*

   notes on bchannels:

if a b-channel is in use, the b_port[channel] is linked to the port using it.
also each used b-channel counts b_inuse.
to assign a bchannel, that is not jet defined due to remote channel assignment,
the b_inuse is also increased to reserve channel

'use' is the number of port instances using this mISDNport. this counts also
calls with no bchannel (call waiting, call on hold).

*/


/* mISDN none-object functions */
int mISDN_initialize(void);
void mISDN_deinitialize(void);
int mISDN_getportbyname(int sock, int cnt, char *portname);
struct mISDNport *mISDNport_open(struct interface_port *ifport);
void mISDNport_static(struct mISDNport *mISDNport);
void mISDNport_close_all(void);
void mISDNport_close(struct mISDNport *mISDNport);
void mISDN_port_reorder(void);
void enc_ie_cause_standalone(struct l3_msg *l3m, int location, int cause);
int stack2manager(struct mISDNport *mISDNport, unsigned int cmd, unsigned int pid, struct l3_msg *l3m);
void ph_control(struct mISDNport *mISDNport, class PmISDN *isdnport, unsigned int handle, unsigned int c1, unsigned int c2, const char *trace_name, int trace_value);
void ph_control_block(struct mISDNport *mISDNport, unsigned int handle, unsigned int c1, void *c2, int c2_len, const char *trace_name, int trace_value);
void chan_trace_header(struct mISDNport *mISDNport, class PmISDN *port, const char *msgtext, int direction);
void l1l2l3_trace_header(struct mISDNport *mISDNport, class PmISDN *port, unsigned int prim, int direction);
void bchannel_event(struct mISDNport *mISDNport, int i, int event);
void message_bchannel_from_remote(class JoinRemote *joinremote, int type, unsigned int handle);


/* mISDN port classes */
class PmISDN : public Port
{
	public:
	PmISDN(int type, struct mISDNport *mISDNport, char *portname, struct port_settings *settings, int channel, int exclusive, int mode);
	~PmISDN();
	void bchannel_receive(struct mISDNhead *hh, unsigned char *data, int len);
	void transmit(unsigned char *buffer, int length);
	int message_epoint(unsigned int epoint_id, int message, union parameter *param);
	void message_mISDNsignal(unsigned int epoint_id, int message_id, union parameter *param);
	void message_crypt(unsigned int epoint_id, int message_id, union parameter *param);
	struct mISDNport *p_m_mISDNport;	/* pointer to port */
	int p_m_delay;				/* use delay instead of dejitter */
	int p_m_tx_gain, p_m_rx_gain;		/* volume shift (0 = no change) */
	char p_m_pipeline[256];			/* filter pipeline */
	int p_m_echo, p_m_conf;			/* remote echo, conference number */
	int p_m_mute;				/* if set, conf is disconnected */
	int p_m_tone;				/* current kernel space tone */
	int p_m_rxoff;				/* rx from driver is disabled */
//	int p_m_nodata;				/* all parties within a conf are isdn ports, so pure bridging is possible */
	int p_m_txdata;				/* get what we transmit */
	int p_m_dtmf;				/* dtmf decoding is enabled */
	int p_m_joindata;			/* the call requires data due to no briging capability */

	struct lcr_timer p_m_loadtimer;		/* timer for audio transmission */
	virtual void update_load(void);
	void load_tx(void);
	int p_m_load;				/* current data in dsp tx buffer */
	unsigned int p_m_last_tv_sec;		/* time stamp of last tx_load call, (to sync audio data */
	unsigned int p_m_last_tv_msec;
//	int p_m_fromup_buffer_readp;		/* buffer for audio from remote endpoint */
//	int p_m_fromup_buffer_writep;
//	unsigned char p_m_fromup_buffer[FROMUP_BUFFER_SIZE];
	void txfromup(unsigned char *data, int length);

	int p_m_crypt;				/* encryption is enabled */
	int p_m_crypt_msg_loops;		/* sending a message */
	int p_m_crypt_msg_len;
	unsigned char p_m_crypt_msg[1100];
	int p_m_crypt_msg_current;
	unsigned char p_m_crypt_key[128];
	int p_m_crypt_key_len;
	int p_m_crypt_listen;
	int p_m_crypt_listen_state;
	int p_m_crypt_listen_len;
	unsigned char p_m_crypt_listen_msg[1100];
	unsigned int p_m_crypt_listen_crc;
	void cryptman_listen_bch(unsigned char *p, int l);

	void set_tone(const char *dir, const char *name);
	void set_echotest(int echotest);
	void set_conf(int oldconf, int newconf);

	int p_m_portnum;			/* used port number (1...n) */
	int p_m_b_index;			/* index 0,1 0..29 */
	int p_m_b_channel;			/* number 1,2 1..15,17... */
	int p_m_b_exclusive;			/* if bchannel is exclusive */
	int p_m_b_reserve;			/* set if channel is reserved */
//	long long p_m_jittercheck;		/* time of audio data */
//	long long p_m_jitterdropped;		/* number of bytes dropped */
	int p_m_b_mode;				/* bchannel mode */
	int p_m_hold;				/* if port is on hold */
	struct lcr_timer p_m_timeout;		/* timeout of timers */
	unsigned int p_m_remote_ref;		/* join to export bchannel to */
	int p_m_remote_id;			/* sock to export bchannel to */

	int p_m_inband_send_on;			/* triggers optional send function */
	int p_m_inband_receive_on;		/* triggers optional receive function */
	int p_m_mute_on;			/* if mute is on, bridge is removed */
	virtual int inband_send(unsigned char *buffer, int len);
	void inband_send_on(void);
	void inband_send_off(void);
	virtual void inband_receive(unsigned char *buffer, int len);
	void inband_receive_on(void);
	void inband_receive_off(void);
	void mute_on(void);
	void mute_off(void);
	void update_rxoff(void);

	int seize_bchannel(int channel, int exclusive); /* requests / reserves / links bchannels, but does not open it! */
	void drop_bchannel(void);
};

extern unsigned char mISDN_rand[256]; /* noisy randomizer */

