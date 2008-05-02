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

/* mISDN port structure list */
struct mISDNport {
#ifdef SOCKET_MISDN
	struct mlayer3 *ml3;
#else
	net_stack_t nst; /* MUST be the first entry, so &nst equals &mISDNlist */
	manager_t mgr;
	int upper_id; /* id to transfer data down */
	int lower_id; /* id to transfer data up */
	int d_stid;
	msg_queue_t downqueue;		/* l4->l3 */
#endif
	struct mISDNport *next;
	char name[64]; /* name of port, if available */
	struct interface_port *ifport; /* link to interface_port */
//	int iftype; /* IF_* */
//	int multilink; /* if set, this port will not support callwaiting */
	int portnum; /* port number 1..n */
	int ptp; /* if ptp is set, we keep track of l2link */
	int l1link; /* if l1 is available (only works with nt-mode) */
	int l2link; /* if l2 is available (at PTP we take this serious) */
	int l2hold; /* set, if layer 2 must be hold/checked */
	time_t l2establish; /* time until establishing after link failure */
	int use; /* counts the number of port that uses this port */
	int ntmode; /* is TRUE if port is nt mode */
	int pri; /* is TRUE if port is a primary rate interface */
	int tones; /* TRUE if tones are sent outside connect state */
	int earlyb; /* TRUE if tones are received outside connect state */
	int b_num; /* number of bchannels */
	int b_reserved; /* number of bchannels reserved or in use */
	class PmISDN *b_port[128]; /* bchannel assigned to port object */
#ifdef SOCKET_MISDN
	struct mqueue upqueue;
	int b_socket[128];
#else
	int procids[256]; /* keep track of free ids */
	int b_stid[128];
	unsigned long b_addr[128];
#endif
	int b_state[128]; /* statemachine, 0 = IDLE */
	double b_timer[128]; /* timer for state machine */
	unsigned long b_remote_id[128]; /* the socket currently exported */
	unsigned long b_remote_ref[128]; /* the ref currently exported */
	int locally; /* local causes are sent as local causes not remote */
	int los, ais, rdi, slip_rx, slip_tx;
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
void mISDN_port_info(void);
struct mISDNport *mISDNport_open(int port, int ptp, int force_nt, int l2hold, struct interface *interface);
void mISDNport_close_all(void);
void mISDNport_close(struct mISDNport *mISDNport);
void mISDN_port_reorder(void);
int mISDN_handler(void);
#ifdef SOCKET_MISDN
void enc_ie_cause_standalone(struct l3_msg *l3m, int location, int cause);
int stack2manager(struct mISDNport *mISDNport, unsigned int cmd, unsigned int pid, struct l3_msg *l3m);
#else
void enc_ie_cause_standalone(unsigned char **ntmode, msg_t *msg, int location, int cause);
int stack2manager_te(struct mISDNport *mISDNport, msg_t *msg);
int stack2manager_nt(void *dat, void *arg);
void setup_queue(struct mISDNport *mISDNport, int link);
msg_t *create_l2msg(int prim, int dinfo, int size);
#endif
void ph_control(struct mISDNport *mISDNport, class PmISDN *isdnport, unsigned long handle, unsigned long c1, unsigned long c2, char *trace_name, int trace_value);
void ph_control_block(struct mISDNport *mISDNport, unsigned long handle, unsigned long c1, void *c2, int c2_len, char *trace_name, int trace_value);
void chan_trace_header(struct mISDNport *mISDNport, class PmISDN *port, char *msgtext, int direction);
void l1l2l3_trace_header(struct mISDNport *mISDNport, class PmISDN *port, unsigned long prim, int direction);
void bchannel_event(struct mISDNport *mISDNport, int i, int event);
void message_bchannel_from_join(class JoinRemote *joinremote, int type, unsigned long handle);


/* mISDN port classes */
class PmISDN : public Port
{
	public:
	PmISDN(int type, struct mISDNport *mISDNport, char *portname, struct port_settings *settings, int channel, int exclusive);
	~PmISDN();
#ifdef SOCKET_MISDN
	void bchannel_receive(struct mISDNhead *hh, unsigned char *data, int len);
#else
	void bchannel_receive(iframe_t *frm);
#endif
	int handler(void);
	void transmit(unsigned char *buffer, int length);
	int message_epoint(unsigned long epoint_id, int message, union parameter *param);
	void message_mISDNsignal(unsigned long epoint_id, int message_id, union parameter *param);
	void message_crypt(unsigned long epoint_id, int message_id, union parameter *param);
	struct mISDNport *p_m_mISDNport;	/* pointer to port */
	int p_m_delay;				/* use delay instead of dejitter */
	int p_m_tx_gain, p_m_rx_gain;		/* volume shift (0 = no change) */
	char p_m_pipeline[256];			/* filter pipeline */
	int p_m_echo, p_m_conf;			/* remote echo, conference number */
	int p_m_tone;				/* current kernel space tone */
	int p_m_rxoff;				/* rx from driver is disabled */
//	int p_m_nodata;				/* all parties within a conf are isdn ports, so pure bridging is possible */
	int p_m_txdata;				/* get what we transmit */
	int p_m_dtmf;				/* dtmf decoding is enabled */
	int p_m_joindata;			/* the call requires data due to no briging capability */

	int p_m_load;				/* current data in dsp tx buffer */
	unsigned long p_m_last_tv_sec;		/* time stamp of last handler call, (to sync audio data */
	unsigned long p_m_last_tv_msec;
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
	unsigned long p_m_crypt_listen_crc;
	void cryptman_listen_bch(unsigned char *p, int l);

	void set_tone(char *dir, char *name);
	void set_echotest(int echotest);

	int p_m_portnum;			/* used port number (1...n) */
	int p_m_b_index;			/* index 0,1 0..29 */
	int p_m_b_channel;			/* number 1,2 1..15,17... */
	int p_m_b_exclusive;			/* if bchannel is exclusive */
	int p_m_b_reserve;			/* set if channel is reserved */
//	long long p_m_jittercheck;		/* time of audio data */
//	long long p_m_jitterdropped;		/* number of bytes dropped */
	int p_m_delete;				/* true if obj. must del. */
	int p_m_hold;				/* if port is on hold */
	unsigned long p_m_timeout;		/* timeout of timers */
	time_t p_m_timer;			/* start of timer */
	unsigned char p_m_remote_ref;		/* join to export bchannel to */
	unsigned char p_m_remote_id;		/* sock to export bchannel to */

	int seize_bchannel(int channel, int exclusive); /* requests / reserves / links bchannels, but does not open it! */
	void drop_bchannel(void);
};

extern unsigned char mISDN_rand[256]; /* noisy randomizer */

