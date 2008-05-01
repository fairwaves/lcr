/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** mISDN channel handlin for remote application                              **
**                                                                           **
\*****************************************************************************/ 


struct bchannel {
	struct bchannel *next;
	struct chan_call *call;		/* link to call process */
	unsigned long handle;		/* handle for stack id */
#ifdef SOCKET_MISDN
	int b_sock;			/* socket for b-channel */
#else
	unsigned long b_stid;		/* stack id */
	unsigned long b_addr;		/* channel address */
#endif
	int b_state;
	int b_txdata;
	int b_delay;
	int b_tx_dejitter;
	int b_tx_gain, b_rx_gain;
	char b_pipeline[256];
	unsigned long b_conf;
	int b_echo;
	int b_tone;
	int b_rxoff;
	// int b_txmix;
	int b_dtmf;
	int b_crypt_len;
	int b_crypt_type;
	unsigned char b_crypt_key[128];

	void (*rx_data)(struct bchannel *bchannel, unsigned char *data, int len);
	void (*rx_dtmf)(struct bchannel *bchannel, char tone);
};


extern struct bchannel *bchannel_first;

int bchannel_initialize(void);
void bchannel_deinitialize(void);
int bchannel_create(struct bchannel *channel);
void bchannel_activate(struct bchannel *channel, int activate);
void bchannel_transmit(struct bchannel *channel, unsigned char *data, int len);
void bchannel_join(struct bchannel *channel, unsigned short id);
int bchannel_handle(void);
struct bchannel *find_bchannel_handle(unsigned long handle);
//struct bchannel *find_bchannel_ref(unsigned long ref);
struct bchannel *alloc_bchannel(unsigned long handle);
void free_bchannel(struct bchannel *channel);

