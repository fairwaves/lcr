/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** Asterisk socket client header                                             **
**                                                                           **
\*****************************************************************************/

/* structure for all calls */
struct bchannel;
struct chan_call {
	struct chan_call	*next;	/* link to next call instance */
	int			state;	/* current call state CHAN_LCR_STATE */
	unsigned int		ref;	/* callref for this channel */
	void			*ast;	/* current asterisk channel */
	int			pbx_started;
					/* indicates if pbx que is available */
	struct bchannel		*bchannel;
					/* reference to bchannel, if set */
	int			audiopath;
					/* audio is available */
	int			cause, location;
					/* store cause from lcr */
	char			dialque[64];
					/* queue dialing prior setup ack */
	char			oad[64];/* caller id in number format */

	struct connect_info	connectinfo;
					/* store connectinfo form lcr */
	int			bridge_id;
					/* current ID or 0 */
	struct chan_call	*bridge_call;
					/* remote instance or NULL */
	int			pipe[2];
					/* pipe for receive data */
	unsigned char		read_buff[1024];
					/* read buffer for frame */
	struct ast_frame	read_fr;
					/* frame for read */
	char			interface[32];
					/* LCR interface name for setup */
	char			dialstring[64];
					/* cached dial string for setup */
        char                    cid_num[64]; /* cached cid for setup */
	char                    cid_name[64]; /* cached cid for setup */
	char                    cid_rdnis[64]; /* cached cid for setup */
	char			display[128];
					/* display for setup */
	int			dtmf;
					/* shall dtmf be enabled */
	int			no_dtmf;
					/* dtmf disabled by option */
        int                     rebuffer; /* send only 160 bytes frames
					     to asterisk */
        int                     on_hold; /* track hold management, since
					    sip phones sometimes screw it up */
	char			pipeline[256];
					/* echo cancel pipeline by option */
	int			tx_gain, rx_gain;
					/* gain by option */
	unsigned char		bf_key[56];
	int			bf_len;	/* blowfish crypt key */
	int			nodsp, hdlc;
					/* flags for bchannel mode */
	char			queue_string[64];
					/* queue for asterisk */
		
};

enum {
	CHAN_LCR_STATE_IN_PREPARE = 0,
	CHAN_LCR_STATE_IN_SETUP,
	CHAN_LCR_STATE_IN_DIALING,
	CHAN_LCR_STATE_IN_PROCEEDING,
	CHAN_LCR_STATE_IN_ALERTING,
	CHAN_LCR_STATE_OUT_PREPARE,
	CHAN_LCR_STATE_OUT_SETUP,
	CHAN_LCR_STATE_OUT_DIALING,
	CHAN_LCR_STATE_OUT_PROCEEDING,
	CHAN_LCR_STATE_OUT_ALERTING,
	CHAN_LCR_STATE_CONNECT,
	CHAN_LCR_STATE_IN_DISCONNECT,
	CHAN_LCR_STATE_OUT_DISCONNECT,
	CHAN_LCR_STATE_RELEASE,
};

#define CHAN_LCR_STATE static const struct chan_lcr_state { \
	char *name; \
	char *meaning; \
} chan_lcr_state[] = { \
	{ "IN_PREPARE", \
	  "New call from ISDN is waiting for setup." }, \
	{ "IN_SETUP", \
	  "Call from ISDN is currently set up." }, \
	{ "IN_DIALING", \
	  "Call from ISDN is currently waiting for digits to be dialed." }, \
	{ "IN_PROCEEDING", \
	  "Call from ISDN is complete and proceeds to ring." }, \
	{ "IN_ALERTING", \
	  "Call from ISDN is ringing." }, \
	{ "OUT_PREPARE", \
	  "New call to ISDN is wating for setup." }, \
	{ "OUT_SETUP", \
	  "Call to ISDN is currently set up." }, \
	{ "OUT_DIALING", \
	  "Call to ISDN is currently waiting for digits to be dialed." }, \
	{ "OUT_PROCEEDING", \
	  "Call to ISDN is complete and proceeds to ring." }, \
	{ "OUT_ALERTING", \
	  "Call to ISDN is ringing." }, \
	{ "CONNECT", \
	  "Call has been answered." }, \
	{ "IN_DISCONNECT", \
	  "Call has been hung up on ISDN side." }, \
	{ "OUT_DISCONNECT", \
	  "Call has been hung up on Asterisk side." }, \
	{ "RELEASE", \
	  "Call is waiting for complete release." }, \
};


#define CERROR(call, ast, arg...) chan_lcr_log(__LOG_ERROR, __FILE__, __LINE__,  __FUNCTION__, call, ast, ##arg)
#define CDEBUG(call, ast, arg...) chan_lcr_log(__LOG_NOTICE, __FILE__, __LINE__,  __FUNCTION__, call, ast, ##arg)
void chan_lcr_log(int type, const char *file, int line, const char *function,  struct chan_call *call, struct ast_channel *ast, const char *fmt, ...);
extern unsigned char flip_bits[256];
void lcr_in_dtmf(struct chan_call *call, int val);
