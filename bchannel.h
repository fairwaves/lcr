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

/* this test produces a test tone to test gaps in audio stream */
//#define SEAMLESS_TEST

#define QUEUE_BUFFER_SIZE		8192 /* must be the power of two */
#define QUEUE_BUFFER_MAX		4096 /* half of size */

struct bchannel {
	struct bchannel *next;
	struct chan_call *call;		/* link to call process */
	unsigned int handle;		/* handle for stack id */
	int b_sock;			/* socket for b-channel */
	struct lcr_fd lcr_fd;		/* socket register */
	int b_mode;			/* dsp, raw, dsphdlc */
	int b_state;
	int b_txdata;
	int b_delay;
	int b_tx_dejitter;
	int b_tx_gain, b_rx_gain;
	char b_pipeline[256];
	unsigned int b_conf;
	int b_echo;
	int b_tone;
	int b_rxoff;
	// int b_txmix;
	int b_dtmf;
	int b_bf_len;
	unsigned char b_bf_key[128];
	int nodsp_queue;		/* enables nodsp_queue_buffer */
	unsigned char nodsp_queue_buffer[QUEUE_BUFFER_SIZE];
					/* buffer for seamless transmission */
	unsigned int nodsp_queue_in, nodsp_queue_out;
					/* in and out pointers */
	int queue_sent;			/* data for mISDN was not confrmed yet */
#ifdef SEAMLESS_TEST
	int test;
#endif
};


extern struct bchannel *bchannel_first;
extern pid_t bchannel_pid;

int bchannel_initialize(void);
void bchannel_deinitialize(void);
void bchannel_destroy(struct bchannel *bchannel);
int bchannel_create(struct bchannel *channel, int mode, int queue);
void bchannel_activate(struct bchannel *channel, int activate);
void bchannel_transmit(struct bchannel *channel, unsigned char *data, int len);
void bchannel_join(struct bchannel *channel, unsigned short id);
void bchannel_dtmf(struct bchannel *channel, int on);
void bchannel_blowfish(struct bchannel *bchannel, unsigned char *key, int len);
void bchannel_pipeline(struct bchannel *bchannel, char *pipeline);
void bchannel_gain(struct bchannel *bchannel, int gain, int tx);
struct bchannel *find_bchannel_handle(unsigned int handle);
//struct bchannel *find_bchannel_ref(unsigned int ref);
struct bchannel *alloc_bchannel(unsigned int handle);
void free_bchannel(struct bchannel *channel);

