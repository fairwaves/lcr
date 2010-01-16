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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/udp.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#include <mISDNif.h>

#define AF_COMPATIBILITY_FUNC 1
#define MISDN_OLD_AF_COMPATIBILITY 1
#include <compat_af_isdn.h>

#define HAVE_ATTRIBUTE_always_inline 1
#define HAVE_ARPA_INET_H 1
#define HAVE_TIMERSUB 1

#include <asterisk/compiler.h>
#include <asterisk/frame.h>

/* Choose if you want to have chan_lcr for Asterisk 1.4.x or CallWeaver 1.2.x */
/* #define LCR_FOR_CALLWEAVER */

#ifdef LCR_FOR_CALLWEAVER
#include <asterisk/phone_no_utils.h>
#include <asterisk/logger.h>
#include <asterisk/module.h>
#include <asterisk/channel.h>
#endif

#include "extension.h"
#include "message.h"
#include "lcrsocket.h"
#include "cause.h"
#include "select.h"
#include "bchannel.h"
#include "chan_lcr.h"
#include "callerid.h"


#ifndef ISDN_PID_L4_B_USER
#define ISDN_PID_L4_B_USER 0x440000ff
#endif

pid_t	bchannel_pid;

enum {
	BSTATE_IDLE,
	BSTATE_ACTIVATING,
	BSTATE_ACTIVE,
	BSTATE_DEACTIVATING,
};


int bchannel_initialize(void)
{
  	init_af_isdn();

	return 0;
}

void bchannel_deinitialize(void)
{
}

/*
 * send control information to the channel (dsp-module)
 */
static void ph_control(int sock, unsigned int c1, unsigned int c2, char *trace_name, int trace_value, int b_mode)
{
	unsigned char buffer[MISDN_HEADER_LEN+sizeof(int)+sizeof(int)];
	struct mISDNhead *ctrl = (struct mISDNhead *)buffer;
	unsigned int *d = (unsigned int *)(buffer+MISDN_HEADER_LEN);
	int ret;

	if (b_mode != 0 && b_mode != 2)
		return;

	CDEBUG(NULL, NULL, "Sending PH_CONTROL %s %x,%x\n", trace_name, c1, c2);
	ctrl->prim = PH_CONTROL_REQ;
	ctrl->id = 0;
	*d++ = c1;
	*d++ = c2;
	ret = sendto(sock, buffer, MISDN_HEADER_LEN+sizeof(int)*2, 0, NULL, 0);
	if (ret < 0)
		CERROR(NULL, NULL, "Failed to send to socket %d\n", sock);
}

static void ph_control_block(int sock, unsigned int c1, void *c2, int c2_len, char *trace_name, int trace_value, int b_mode)
{
	unsigned char buffer[MISDN_HEADER_LEN+sizeof(int)+c2_len];
	struct mISDNhead *ctrl = (struct mISDNhead *)buffer;
	unsigned int *d = (unsigned int *)(buffer+MISDN_HEADER_LEN);
	int ret;

	if (b_mode != 0 && b_mode != 2)
		return;

	CDEBUG(NULL, NULL, "Sending PH_CONTROL (block) %s %x\n", trace_name, c1);
	ctrl->prim = PH_CONTROL_REQ;
	ctrl->id = 0;
	*d++ = c1;
	memcpy(d, c2, c2_len);
	ret = sendto(sock, buffer, MISDN_HEADER_LEN+sizeof(int)+c2_len, 0, NULL, 0);
	if (ret < 0)
		CERROR(NULL, NULL, "Failed to send to socket %d\n", sock);
}

static int bchannel_handle(struct lcr_fd *fd, unsigned int what, void *instance, int index);

/*
 * create stack
 */
int bchannel_create(struct bchannel *bchannel, int mode)
{
	int ret;
	struct sockaddr_mISDN addr;

	if (bchannel->b_sock > -1) {
		CERROR(bchannel->call, NULL, "Socket already created for handle 0x%x\n", bchannel->handle);
		return 0;
	}

	/* open socket */
	bchannel->b_mode = mode;
	switch(bchannel->b_mode) {
		case 0:
		CDEBUG(bchannel->call, NULL, "Open DSP audio\n");
		bchannel->b_sock = socket(PF_ISDN, SOCK_DGRAM, ISDN_P_B_L2DSP);
		break;
		case 1:
		CDEBUG(bchannel->call, NULL, "Open audio\n");
		bchannel->b_sock = socket(PF_ISDN, SOCK_DGRAM, ISDN_P_B_RAW);
		break;
		case 2:
		CDEBUG(bchannel->call, NULL, "Open DSP HDLC\n");
		bchannel->b_sock = socket(PF_ISDN, SOCK_DGRAM, ISDN_P_B_L2DSPHDLC);
		break;
		case 3:
		CDEBUG(bchannel->call, NULL, "Open HDLC\n");
		bchannel->b_sock = socket(PF_ISDN, SOCK_DGRAM, ISDN_P_B_HDLC);
		break;
	}
	if (bchannel->b_sock < 0) {
		CERROR(bchannel->call, NULL, "Failed to open bchannel-socket for handle 0x%x with mISDN-DSP layer. Did you load mISDN_dsp.ko?\n", bchannel->handle);
		return 0;
	}

	/* register fd */
	memset(&bchannel->lcr_fd, 0, sizeof(bchannel->lcr_fd));
	bchannel->lcr_fd.fd = bchannel->b_sock;
	register_fd(&bchannel->lcr_fd, LCR_FD_READ | LCR_FD_EXCEPT, bchannel_handle, bchannel, 0);

	/* bind socket to bchannel */
	addr.family = AF_ISDN;
	addr.dev = (bchannel->handle>>8);
	addr.channel = bchannel->handle & 0xff;
	ret = bind(bchannel->b_sock, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		CERROR(bchannel->call, NULL, "Failed to bind bchannel-socket for handle 0x%x with mISDN-DSP layer. (port %d, channel %d) Did you load mISDN_dsp.ko?\n", bchannel->handle, addr.dev, addr.channel);
		unregister_fd(&bchannel->lcr_fd);
		close(bchannel->b_sock);
		bchannel->b_sock = -1;
		return 0;
	}
	return 1;
}


/*
 * activate / deactivate request
 */
void bchannel_activate(struct bchannel *bchannel, int activate)
{
	struct mISDNhead act;
	int ret;

	/* activate bchannel */
	CDEBUG(bchannel->call, NULL, "%sActivating B-channel.\n", activate?"":"De-");
	switch(bchannel->b_mode) {
		case 0:
		case 2:
		act.prim = (activate)?DL_ESTABLISH_REQ:DL_RELEASE_REQ; 
		break;
		case 1:
		case 3:
		act.prim = (activate)?PH_ACTIVATE_REQ:PH_DEACTIVATE_REQ; 
		break;
	}
	act.id = 0;
	ret = sendto(bchannel->b_sock, &act, MISDN_HEADER_LEN, 0, NULL, 0);
	if (ret < 0)
		CERROR(bchannel->call, NULL, "Failed to send to socket %d\n", bchannel->b_sock);

	bchannel->b_state = (activate)?BSTATE_ACTIVATING:BSTATE_DEACTIVATING;
}


/*
 * set features
 */
static void bchannel_activated(struct bchannel *bchannel)
{
	int sock;

	sock = bchannel->b_sock;

	/* set dsp features */
	if (bchannel->b_txdata)
		ph_control(sock, (bchannel->b_txdata)?DSP_TXDATA_ON:DSP_TXDATA_OFF, 0, "DSP-TXDATA", bchannel->b_txdata, bchannel->b_mode);
	if (bchannel->b_delay && bchannel->b_mode == 0)
		ph_control(sock, DSP_DELAY, bchannel->b_delay, "DSP-DELAY", bchannel->b_delay, bchannel->b_mode);
	if (bchannel->b_tx_dejitter && bchannel->b_mode == 0)
		ph_control(sock, (bchannel->b_tx_dejitter)?DSP_TX_DEJITTER:DSP_TX_DEJ_OFF, 0, "DSP-TX_DEJITTER", bchannel->b_tx_dejitter, bchannel->b_mode);
	if (bchannel->b_tx_gain && bchannel->b_mode == 0)
		ph_control(sock, DSP_VOL_CHANGE_TX, bchannel->b_tx_gain, "DSP-TX_GAIN", bchannel->b_tx_gain, bchannel->b_mode);
	if (bchannel->b_rx_gain && bchannel->b_mode == 0)
		ph_control(sock, DSP_VOL_CHANGE_RX, bchannel->b_rx_gain, "DSP-RX_GAIN", bchannel->b_rx_gain, bchannel->b_mode);
	if (bchannel->b_pipeline[0] && bchannel->b_mode == 0)
		ph_control_block(sock, DSP_PIPELINE_CFG, bchannel->b_pipeline, strlen(bchannel->b_pipeline)+1, "DSP-PIPELINE", 0, bchannel->b_mode);
	if (bchannel->b_conf)
		ph_control(sock, DSP_CONF_JOIN, bchannel->b_conf, "DSP-CONF", bchannel->b_conf, bchannel->b_mode);
	if (bchannel->b_echo)
		ph_control(sock, DSP_ECHO_ON, 0, "DSP-ECHO", 1, bchannel->b_mode);
	if (bchannel->b_tone && bchannel->b_mode == 0)
		ph_control(sock, DSP_TONE_PATT_ON, bchannel->b_tone, "DSP-TONE", bchannel->b_tone, bchannel->b_mode);
	if (bchannel->b_rxoff)
		ph_control(sock, DSP_RECEIVE_OFF, 0, "DSP-RXOFF", 1, bchannel->b_mode);
//	if (bchannel->b_txmix && bchannel->b_mode == 0)
//		ph_control(sock, DSP_MIX_ON, 0, "DSP-MIX", 1, bchannel->b_mode);
	if (bchannel->b_dtmf && bchannel->b_mode == 0)
		ph_control(sock, DTMF_TONE_START, 0, "DSP-DTMF", 1, bchannel->b_mode);
	if (bchannel->b_bf_len && bchannel->b_mode == 0)
		ph_control_block(sock, DSP_BF_ENABLE_KEY, bchannel->b_bf_key, bchannel->b_bf_len, "DSP-CRYPT", bchannel->b_bf_len, bchannel->b_mode);
	if (bchannel->b_conf)
		ph_control(sock, DSP_CONF_JOIN, bchannel->b_conf, "DSP-CONF", bchannel->b_conf, bchannel->b_mode);

	bchannel->b_state = BSTATE_ACTIVE;
}

/*
 * destroy stack
 */
void bchannel_destroy(struct bchannel *bchannel)
{
	if (bchannel->b_sock > -1) {
		unregister_fd(&bchannel->lcr_fd);
		close(bchannel->b_sock);
		bchannel->b_sock = -1;
	}
	bchannel->b_state = BSTATE_IDLE;
}


/*
 * whenever we get audio data from bchannel, we process it here
 */
static void bchannel_receive(struct bchannel *bchannel, unsigned char *buffer, int len)
{
	struct mISDNhead *hh = (struct mISDNhead *)buffer;
	unsigned char *data = buffer + MISDN_HEADER_LEN;
	unsigned int cont = *((unsigned int *)data);
	unsigned char *d;
	int i;
	struct bchannel *remote_bchannel;
	int ret;

	if (hh->prim == PH_CONTROL_IND) {
		/* non dsp -> ignore ph_control */
		if (bchannel->b_mode == 1 || bchannel->b_mode == 3)
			return;
		if (len < 4) {
			CERROR(bchannel->call, NULL, "SHORT READ OF PH_CONTROL INDICATION\n");
			return;
		}
		if ((cont&(~DTMF_TONE_MASK)) == DTMF_TONE_VAL) {
			if (bchannel->call)
				lcr_in_dtmf(bchannel->call, cont & DTMF_TONE_MASK);
			return;
		}
		switch(cont) {
			case DSP_BF_REJECT:
			CERROR(bchannel->call, NULL, "Blowfish crypt rejected.\n");
			break;

			case DSP_BF_ACCEPT:
			CDEBUG(bchannel->call, NULL, "Blowfish crypt enabled.\n");
			break;

			default:
			CDEBUG(bchannel->call, NULL, "Unhandled bchannel control 0x%x.\n", cont);
		}
		return;
	}
	if (hh->prim == PH_DATA_REQ) {
		if (!bchannel->b_txdata) {
			/* if tx is off, it may happen that fifos send us pending informations, we just ignore them */
			CDEBUG(bchannel->call, NULL, "ignoring tx data, because 'txdata' is turned off\n");
			return;
		}
		return;
	}
	if (hh->prim != PH_DATA_IND && hh->prim != DL_DATA_IND) {
		CERROR(bchannel->call, NULL, "Bchannel received unknown primitve: 0x%lx\n", hh->prim);
		return;
	}
	/* if calls are bridged, but not via dsp (no b_conf), forward here */
	if (!bchannel->b_conf
	 && bchannel->call
	 && bchannel->call->bridge_call
	 && bchannel->call->bridge_call->bchannel) {
		remote_bchannel = bchannel->call->bridge_call->bchannel;
#if 0
		int i = 0;
		char string[4096] = "";
		while(i < len) {
			sprintf(string+strlen(string), " %02x", data[i]);
			i++;
		}
		CDEBUG(remote_bchannel->call, NULL, "Forwarding packet%s\n", string);
#endif
		hh->prim = PH_DATA_REQ;
		hh->id = 0;
		ret = sendto(remote_bchannel->b_sock, buffer, MISDN_HEADER_LEN+len, 0, NULL, 0);
		if (ret < 0)
			CERROR(remote_bchannel->call, NULL, "Failed to send to socket %d\n", bchannel->b_sock);
		return;
	}
	/* calls will not process any audio data unless
	 * the call is connected OR interface features audio during call setup.
	 */

	/* if rx is off, it may happen that fifos send us pending informations, we just ignore them */
	if (bchannel->b_rxoff) {
		CDEBUG(bchannel->call, NULL, "ignoring data, because rx is turned off\n");
		return;
	}

	if (!bchannel->call) {
		CDEBUG(bchannel->call, NULL, "ignoring data, because no call associated with bchannel\n");
		return;
	}
	if (!bchannel->call->audiopath) {
		/* return, because we have no audio from port */
		return;
	}

	if (bchannel->call->pipe[1] < 0) {
		/* nobody there */
		return;
	}

	/* if no hdlc */
	if (bchannel->b_mode == 0 || bchannel->b_mode == 1) {
		d = data;
		for (i = 0; i < len; i++) {
			*d = flip_bits[*d];
			d++;
		}
	}

	
	len = write(bchannel->call->pipe[1], data, len);
	if (len < 0)
		goto errout;

	return;
 errout:
	close(bchannel->call->pipe[1]);
	bchannel->call->pipe[1] = -1;
	CDEBUG(bchannel->call, NULL, "broken pipe on bchannel pipe\n");
}


/*
 * transmit data to bchannel
 */
void bchannel_transmit(struct bchannel *bchannel, unsigned char *data, int len)
{
	unsigned char buff[1024 + MISDN_HEADER_LEN], *p = buff + MISDN_HEADER_LEN;
	struct mISDNhead *frm = (struct mISDNhead *)buff;
	int ret;
	int i;

	if (bchannel->b_state != BSTATE_ACTIVE)
		return;
	if (len > 1024 || len < 1)
		return;
	switch(bchannel->b_mode) {
	case 0:
		for (i = 0; i < len; i++)
			*p++ = flip_bits[*data++];
		frm->prim = DL_DATA_REQ;
		break;
	case 1:
		for (i = 0; i < len; i++)
			*p++ = flip_bits[*data++];
		frm->prim = PH_DATA_REQ;
		break;
	case 2:
		memcpy(p, data, len);
		frm->prim = DL_DATA_REQ;
		break;
	case 3:
		memcpy(p, data, len);
		frm->prim = PH_DATA_REQ;
		break;
	}
	frm->id = 0;
	ret = sendto(bchannel->b_sock, buff, MISDN_HEADER_LEN+len, 0, NULL, 0);
	if (ret < 0)
		CERROR(bchannel->call, NULL, "Failed to send to socket %d\n", bchannel->b_sock);
}


/*
 * join bchannel
 */
void bchannel_join(struct bchannel *bchannel, unsigned short id)
{
	int sock;

	sock = bchannel->b_sock;
	if (id) {
		bchannel->b_conf = (id<<16) + bchannel_pid;
		bchannel->b_rxoff = 1;
	} else {
		bchannel->b_conf = 0;
		bchannel->b_rxoff = 0;
	}
	if (bchannel->b_state == BSTATE_ACTIVE) {
		ph_control(sock, DSP_RECEIVE_OFF, bchannel->b_rxoff, "DSP-RX_OFF", bchannel->b_conf, bchannel->b_mode);
		ph_control(sock, DSP_CONF_JOIN, bchannel->b_conf, "DSP-CONF", bchannel->b_conf, bchannel->b_mode);
	}
}


/*
 * dtmf bchannel
 */
void bchannel_dtmf(struct bchannel *bchannel, int on)
{
	int sock;

	sock = bchannel->b_sock;
	bchannel->b_dtmf = on;
	if (bchannel->b_state == BSTATE_ACTIVE && bchannel->b_mode == 0)
		ph_control(sock, on?DTMF_TONE_START:DTMF_TONE_STOP, 0, "DSP-DTMF", 1, bchannel->b_mode);
}


/*
 * blowfish bchannel
 */
void bchannel_blowfish(struct bchannel *bchannel, unsigned char *key, int len)
{
	int sock;

	sock = bchannel->b_sock;
	memcpy(bchannel->b_bf_key, key, len);
	bchannel->b_bf_len = len;
	if (bchannel->b_state == BSTATE_ACTIVE)
		ph_control_block(sock, DSP_BF_ENABLE_KEY, bchannel->b_bf_key, bchannel->b_bf_len, "DSP-CRYPT", bchannel->b_bf_len, bchannel->b_mode);
}


/*
 * pipeline bchannel
 */
void bchannel_pipeline(struct bchannel *bchannel, char *pipeline)
{
	int sock;

	sock = bchannel->b_sock;
	strncpy(bchannel->b_pipeline, pipeline, sizeof(bchannel->b_pipeline)-1);
	if (bchannel->b_state == BSTATE_ACTIVE)
		ph_control_block(sock, DSP_PIPELINE_CFG, bchannel->b_pipeline, strlen(bchannel->b_pipeline)+1, "DSP-PIPELINE", 0, bchannel->b_mode);
}


/*
 * gain bchannel
 */
void bchannel_gain(struct bchannel *bchannel, int gain, int tx)
{
	int sock;

	sock = bchannel->b_sock;
	if (tx)
		bchannel->b_tx_gain = gain;
	else
		bchannel->b_rx_gain = gain;
	if (bchannel->b_state == BSTATE_ACTIVE && bchannel->b_mode == 0)
		ph_control(sock, (tx)?DSP_VOL_CHANGE_TX:DSP_VOL_CHANGE_RX, gain, (tx)?"DSP-TX_GAIN":"DSP-RX_GAIN", gain, bchannel->b_mode);
}


/*
 * main loop for processing messages from mISDN
 */
static int bchannel_handle(struct lcr_fd *fd, unsigned int what, void *instance, int index)
{
	struct bchannel *bchannel = (struct bchannel *)instance;
	int ret;
	unsigned char buffer[2048+MISDN_HEADER_LEN];
	struct mISDNhead *hh = (struct mISDNhead *)buffer;

	ret = recv(bchannel->b_sock, buffer, sizeof(buffer), 0);
	if (ret >= (int)MISDN_HEADER_LEN) {
		switch(hh->prim) {
			/* we don't care about confirms, we use rx data to sync tx */
			case PH_DATA_CNF:
			break;

			/* we receive audio data, we respond to it AND we send tones */
			case PH_DATA_IND:
			case PH_DATA_REQ:
			case DL_DATA_IND:
			case PH_CONTROL_IND:
			bchannel_receive(bchannel, buffer, ret-MISDN_HEADER_LEN);
			break;

			case PH_ACTIVATE_IND:
			case DL_ESTABLISH_IND:
			case PH_ACTIVATE_CNF:
			case DL_ESTABLISH_CNF:
			CDEBUG(bchannel->call, NULL, "DL_ESTABLISH confirm: bchannel is now activated (socket %d).\n", bchannel->b_sock);
			bchannel_activated(bchannel);
			break;

			case PH_DEACTIVATE_IND:
			case DL_RELEASE_IND:
			case PH_DEACTIVATE_CNF:
			case DL_RELEASE_CNF:
			CDEBUG(bchannel->call, NULL, "DL_RELEASE confirm: bchannel is now de-activated (socket %d).\n", bchannel->b_sock);
//					bchannel_deactivated(bchannel);
			break;

			default:
			CERROR(bchannel->call, NULL, "child message not handled: prim(0x%x) socket(%d) data len(%d)\n", hh->prim, bchannel->b_sock, ret - MISDN_HEADER_LEN);
		}
	} else {
//		if (ret < 0 && errno != EWOULDBLOCK)
		CERROR(bchannel->call, NULL, "Read from socket %d failed with return code %d\n", bchannel->b_sock, ret);
	}

	/* if we received at least one b-frame, we will return 1 */
	return 0;
}


/*
 * bchannel channel handling
 */
struct bchannel *bchannel_first = NULL;
struct bchannel *find_bchannel_handle(unsigned int handle)
{
	struct bchannel *bchannel = bchannel_first;

	while(bchannel) {
		if (bchannel->handle == handle)
			break;
		bchannel = bchannel->next;
	}
	return bchannel;
}

#if 0
struct bchannel *find_bchannel_ref(unsigned int ref)
{
	struct bchannel *bchannel = bchannel_first;

	while(bchannel) {
		if (bchannel->ref == ref)
			break;
		bchannel = bchannel->next;
	}
	return bchannel;
}
#endif

struct bchannel *alloc_bchannel(unsigned int handle)
{
	struct bchannel **bchannelp = &bchannel_first;

	while(*bchannelp)
		bchannelp = &((*bchannelp)->next);

	*bchannelp = (struct bchannel *)calloc(1, sizeof(struct bchannel));
	if (!*bchannelp)
		return NULL;
	(*bchannelp)->handle = handle;
	(*bchannelp)->b_state = BSTATE_IDLE;
	(*bchannelp)->b_sock = -1;
		
	return *bchannelp;
}

void free_bchannel(struct bchannel *bchannel)
{
	struct bchannel **temp = &bchannel_first;

	while(*temp) {
		if (*temp == bchannel) {
			*temp = (*temp)->next;
			if (bchannel->b_sock > -1)
				bchannel_destroy(bchannel);
			if (bchannel->call) {
				if (bchannel->call->bchannel)
					bchannel->call->bchannel = NULL;
			}
			free(bchannel);
			return;
		}
		temp = &((*temp)->next);
	}
}


