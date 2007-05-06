/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** mISDN-port header file                                                    **
**                                                                           **
\*****************************************************************************/ 


#define B_STATE_IDLE		0
#define B_STATE_ACTIVATING	1
#define B_STATE_ACTIVE		2
#define B_STATE_DEACTIVATING	3


extern int entity;
extern int mISDNdevice;

/* mISDN port structure list */
struct mISDNport {
	net_stack_t nst; /* MUST be the first entry, so &nst equals &mISDNlist */
	manager_t mgr;
	struct mISDNport *next;
	struct interface_port *ifport; /* link to interface_port */
//	int iftype; /* IF_* */
//	int multilink; /* if set, this port will not support callwaiting */
	int portnum; /* port number */
	int ptp; /* if ptp is set, we keep track of l2link */
	int l1link; /* if l1 is available (only works with nt-mode) */
	int l2link; /* if l2 is available (at PTP we take this serious) */
//	time_t l1timeout; /* timout when establishing link */
	time_t l2establish; /* time until establishing after link failure */
	int use; /* counts the number of port that uses this port */
	int ntmode; /* is TRUE if port is nt mode */
	int pri; /* is TRUE if port is a primary rate interface */
	int upper_id; /* id to transfer data down */
	int lower_id; /* id to transfer data up */
	int d_stid;
	int b_num; /* number of bchannels */
	int b_reserved; /* number of bchannels reserved or in use */
	class PmISDN *b_port[128]; /* maximum number of ports shall be 128 due to S0 / E1 / special E1 */
	int b_stid[128];
	int b_addr[128];
	int b_state[128]; /* state 0 = IDLE */
	int procids[128]; /* keep track of free ids */
	msg_queue_t downqueue;		/* l4->l3 */
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
void mISDN_port_info(void);
struct mISDNport *mISDNport_open(int port, int ptp);
void mISDNport_close_all(void);
void mISDNport_close(struct mISDNport *mISDNport);
void mISDN_port_reorder(void);
int mISDN_handler(void);
void enc_ie_cause_standalone(unsigned char **ntmode, msg_t *msg, int location, int cause);
void ph_control(unsigned long b_addr, int c1, int c2);
void ph_control_block(unsigned long b_addr, int c1, void *c2, int c2_len);
msg_t *create_l2msg(int prim, int dinfo, int size);
void setup_queue(struct mISDNport *mISDNport, int link);
int stack2manager_nt(void *dat, void *arg);
int stack2manager_te(struct mISDNport *mISDNport, msg_t *msg);


/* mISDN port classes */
class PmISDN : public Port
{
	public:
	PmISDN(int type, struct mISDNport *mISDNport, char *portname, struct port_settings *settings, int channel);
	~PmISDN();
	void bchannel_receive(iframe_t *frm);
	int handler(void);
	int message_epoint(unsigned long epoint_id, int message, union parameter *param);
	void message_mISDNsignal(unsigned long epoint_id, int message_id, union parameter *param);
	void message_crypt(unsigned long epoint_id, int message_id, union parameter *param);
	struct mISDNport *p_m_mISDNport;	/* pointer to port */
	int p_m_delay;				/* use delay instead of dejitter */
	int p_m_txvol, p_m_rxvol;		/* volume shift (0 = no change) */
	int p_m_echo, p_m_conf;			/* remote echo, conference number */
	int p_m_tone;				/* current kernel space tone */
	int p_m_rxoff;				/* rx from driver is disabled */
	int p_m_nodata;				/* all parties within a conf are isdn ports, so pure bridging is possible */
	int p_m_txmix;				/* mix tx with conference */
	int p_m_txmix_on;			/* delay for turning back on after sending a binary message, must be signed */
	int p_m_dtmf;				/* dtmf decoding is enabled */

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
//	int p_m_b_reserved;			/* set if channel is reserved */
	int p_m_b_stid;				/* current stack id */
	int p_m_b_addr;				/* current layer address */
	long long p_m_jittercheck;		/* time of audio data */
	long long p_m_jitterdropped;		/* number of bytes dropped */
	int p_m_delete;				/* true if obj. must del. */
	int p_m_hold;				/* if port is on hold */
	unsigned long p_m_timeout;		/* timeout of timers */
	time_t p_m_timer;			/* start of timer */

	int alloc_bchannel(int channel, int exclusive);
	void free_bchannel(void);
};

extern unsigned char mISDN_rand[256]; /* noisy randomizer */

