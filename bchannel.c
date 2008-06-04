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

#include <asterisk/frame.h>


#include "extension.h"
#include "message.h"
#include "lcrsocket.h"
#include "cause.h"
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
	return(0);
}

void bchannel_deinitialize(void)
{
}

/*
 * send control information to the channel (dsp-module)
 */
static void ph_control(unsigned long handle, unsigned long c1, unsigned long c2, char *trace_name, int trace_value)
{
	unsigned char buffer[MISDN_HEADER_LEN+sizeof(int)+sizeof(int)];
	struct mISDNhead *ctrl = (struct mISDNhead *)buffer;
	unsigned long *d = (unsigned long *)(buffer+MISDN_HEADER_LEN);
	int ret;

	CDEBUG(NULL, NULL, "Sending PH_CONTROL %d,%d\n", c1, c2);
	ctrl->prim = PH_CONTROL_REQ;
	ctrl->id = 0;
	*d++ = c1;
	*d++ = c2;
	ret = sendto(handle, buffer, MISDN_HEADER_LEN+sizeof(int)*2, 0, NULL, 0);
	if (ret < 0)
		CERROR(NULL, NULL, "Failed to send to socket %d\n", handle);
#if 0
	chan_trace_header(mISDNport, isdnport, "BCHANNEL control", DIRECTION_OUT);
	if (c1 == CMX_CONF_JOIN)
		add_trace(trace_name, NULL, "0x%08x", trace_value);
	else
		add_trace(trace_name, NULL, "%d", trace_value);
	end_trace();
#endif
}

static void ph_control_block(unsigned long handle, unsigned long c1, void *c2, int c2_len, char *trace_name, int trace_value)
{
	unsigned char buffer[MISDN_HEADER_LEN+sizeof(int)+c2_len];
	struct mISDNhead *ctrl = (struct mISDNhead *)buffer;
	unsigned long *d = (unsigned long *)(buffer+MISDN_HEADER_LEN);
	int ret;

	CDEBUG(NULL, NULL, "Sending PH_CONTROL (block) %d\n", c1);
	ctrl->prim = PH_CONTROL_REQ;
	ctrl->id = 0;
	*d++ = c1;
	memcpy(d, c2, c2_len);
	ret = sendto(handle, buffer, MISDN_HEADER_LEN+sizeof(int)+c2_len, 0, NULL, 0);
	if (ret < 0)
		CERROR(NULL, NULL, "Failed to send to socket %d\n", handle);
#if 0
	chan_trace_header(mISDNport, isdnport, "BCHANNEL control", DIRECTION_OUT);
	add_trace(trace_name, NULL, "%d", trace_value);
	end_trace();
#endif
}


/*
 * create stack
 */
int bchannel_create(struct bchannel *bchannel)
{
	int ret;
	unsigned long on = 1;
	struct sockaddr_mISDN addr;

	if (bchannel->b_sock)
	{
		CERROR(NULL, NULL, "Socket already created for handle 0x%x\n", bchannel->handle);
		return(0);
	}

	/* open socket */
	bchannel->b_sock = socket(PF_ISDN, SOCK_DGRAM, ISDN_P_B_L2DSP);
	if (bchannel->b_sock < 0)
	{
		CERROR(NULL, NULL, "Failed to open bchannel-socket for handle 0x%x with mISDN-DSP layer. Did you load mISDNdsp.ko?\n", bchannel->handle);
		return(0);
	}
	
	/* set nonblocking io */
	ret = ioctl(bchannel->b_sock, FIONBIO, &on);
	if (ret < 0)
	{
		CERROR(NULL, NULL, "Failed to set bchannel-socket handle 0x%x into nonblocking IO\n", bchannel->handle);
		close(bchannel->b_sock);
		bchannel->b_sock = -1;
		return(0);
	}

	/* bind socket to bchannel */
	addr.family = AF_ISDN;
	addr.dev = (bchannel->handle>>8)-1;
	addr.channel = bchannel->handle & 0xff;
	ret = bind(bchannel->b_sock, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0)
	{
		CERROR(NULL, NULL, "Failed to bind bchannel-socket for handle 0x%x with mISDN-DSP layer. (port %d, channel %d) Did you load mISDNdsp.ko?\n", bchannel->handle, addr.dev + 1, addr.channel);
		close(bchannel->b_sock);
		bchannel->b_sock = -1;
		return(0);
	}

#if 0
	chan_trace_header(mISDNport, mISDNport->b_port[i], "BCHANNEL create socket", DIRECTION_OUT);
	add_trace("channel", NULL, "%d", i+1+(i>=15));
	add_trace("socket", NULL, "%d", mISDNport->b_socket[i]);
	end_trace();
#endif
	return(1);
}


/*
 * activate / deactivate request
 */
void bchannel_activate(struct bchannel *bchannel, int activate)
{
	struct mISDNhead act;
	int ret;

	/* activate bchannel */
	CDEBUG(NULL, NULL, "%sActivating B-channel.\n", activate?"":"De-");
	act.prim = (activate)?DL_ESTABLISH_REQ:DL_RELEASE_REQ; 
	act.id = 0;
	ret = sendto(bchannel->b_sock, &act, MISDN_HEADER_LEN, 0, NULL, 0);
	if (ret < 0)
		CERROR(NULL, NULL, "Failed to send to socket %d\n", bchannel->b_sock);

	bchannel->b_state = (activate)?BSTATE_ACTIVATING:BSTATE_DEACTIVATING;
#if 0
	/* trace */
	chan_trace_header(mISDNport, mISDNport->b_port[i], activate?(char*)"BCHANNEL activate":(char*)"BCHANNEL deactivate", DIRECTION_OUT);
	add_trace("channel", NULL, "%d", i+1+(i>=15));
	end_trace();
#endif
}


/*
 * set features
 */
static void bchannel_activated(struct bchannel *bchannel)
{
	int handle;

	handle = bchannel->b_sock;

	/* set dsp features */
	if (bchannel->b_txdata)
		ph_control(handle, (bchannel->b_txdata)?DSP_TXDATA_ON:DSP_TXDATA_OFF, 0, "DSP-TXDATA", bchannel->b_txdata);
	if (bchannel->b_delay)
		ph_control(handle, DSP_DELAY, bchannel->b_delay, "DSP-DELAY", bchannel->b_delay);
	if (bchannel->b_tx_dejitter)
		ph_control(handle, (bchannel->b_tx_dejitter)?DSP_TX_DEJITTER:DSP_TX_DEJ_OFF, 0, "DSP-TX_DEJITTER", bchannel->b_tx_dejitter);
	if (bchannel->b_tx_gain)
		ph_control(handle, DSP_VOL_CHANGE_TX, bchannel->b_tx_gain, "DSP-TX_GAIN", bchannel->b_tx_gain);
	if (bchannel->b_rx_gain)
		ph_control(handle, DSP_VOL_CHANGE_RX, bchannel->b_rx_gain, "DSP-RX_GAIN", bchannel->b_rx_gain);
	if (bchannel->b_pipeline[0])
		ph_control_block(handle, DSP_PIPELINE_CFG, bchannel->b_pipeline, strlen(bchannel->b_pipeline)+1, "DSP-PIPELINE", 0);
	if (bchannel->b_conf)
		ph_control(handle, DSP_CONF_JOIN, bchannel->b_conf, "DSP-CONF", bchannel->b_conf);
	if (bchannel->b_echo)
		ph_control(handle, DSP_ECHO_ON, 0, "DSP-ECHO", 1);
	if (bchannel->b_tone)
		ph_control(handle, DSP_TONE_PATT_ON, bchannel->b_tone, "DSP-TONE", bchannel->b_tone);
	if (bchannel->b_rxoff)
		ph_control(handle, DSP_RECEIVE_OFF, 0, "DSP-RXOFF", 1);
//	if (bchannel->b_txmix)
//		ph_control(handle, DSP_MIX_ON, 0, "DSP-MIX", 1);
	if (bchannel->b_dtmf)
		ph_control(handle, DTMF_TONE_START, 0, "DSP-DTMF", 1);
	if (bchannel->b_crypt_len)
		ph_control_block(handle, DSP_BF_ENABLE_KEY, bchannel->b_crypt_key, bchannel->b_crypt_len, "DSP-CRYPT", bchannel->b_crypt_len);
	if (bchannel->b_conf)
		ph_control(handle, DSP_CONF_JOIN, bchannel->b_conf, "DSP-CONF", bchannel->b_conf);

	bchannel->b_state = BSTATE_ACTIVE;
}

/*
 * destroy stack
 */
static void bchannel_destroy(struct bchannel *bchannel)
{
#if 0
	chan_trace_header(mISDNport, mISDNport->b_port[i], "BCHANNEL remove socket", DIRECTION_OUT);
	add_trace("channel", NULL, "%d", i+1+(i>=15));
	add_trace("socket", NULL, "%d", mISDNport->b_socket[i]);
	end_trace();
#endif
	if (bchannel->b_sock > -1)
	{
		close(bchannel->b_sock);
		bchannel->b_sock = -1;
	}
	bchannel->b_state = BSTATE_IDLE;
}


/*
 * whenever we get audio data from bchannel, we process it here
 */
static void bchannel_receive(struct bchannel *bchannel, unsigned long prim, unsigned long dinfo, unsigned char *data, int len)
{
	unsigned long cont = *((unsigned long *)data);
//	unsigned char *data_temp;
//	unsigned long length_temp;
//	unsigned char *p;
//	int l;

	if (prim == PH_CONTROL_IND)
	{
		if (len < 4)
		{
			CERROR(NULL, NULL, "SHORT READ OF PH_CONTROL INDICATION\n");
			return;
		}
		if ((cont&(~DTMF_TONE_MASK)) == DTMF_TONE_VAL)
		{
#if 0
			chan_trace_header(p_m_mISDNport, this, "BCHANNEL control", DIRECTION_IN);
			add_trace("DTMF", NULL, "%c", cont & DTMF_TONE_MASK);
			end_trace();
#endif
			if (bchannel->rx_dtmf)
				bchannel->rx_dtmf(bchannel, cont & DTMF_TONE_MASK);
			return;
		}
		switch(cont)
		{
			case DSP_BF_REJECT:
#if 0
			chan_trace_header(p_m_mISDNport, this, "BCHANNEL control", DIRECTION_IN);
			add_trace("DSP-CRYPT", NULL, "error");
			end_trace();
#endif
			break;

			case DSP_BF_ACCEPT:
#if 0
			chan_trace_header(p_m_mISDNport, this, "BCHANNEL control", DIRECTION_IN);
			add_trace("DSP-CRYPT", NULL, "ok");
			end_trace();
#endif
			break;

			default:
#if 0
			chan_trace_header(p_m_mISDNport, this, "BCHANNEL control", DIRECTION_IN);
			add_trace("unknown", NULL, "0x%x", cont);
			end_trace();
#else
			;
#endif
		}
		return;
	}
	if (prim == PH_DATA_REQ)
	{
		if (!bchannel->b_txdata)
		{
			/* if tx is off, it may happen that fifos send us pending informations, we just ignore them */
			CDEBUG(NULL, NULL, "ignoring tx data, because 'txdata' is turned off\n");
			return;
		}
		return;
	}
	if (prim != PH_DATA_IND && prim != DL_DATA_IND)
	{
		CERROR(NULL, NULL, "Bchannel received unknown primitve: 0x%lx\n", prim);
		return;
	}
	/* calls will not process any audio data unless
	 * the call is connected OR interface features audio during call setup.
	 */

	/* if rx is off, it may happen that fifos send us pending informations, we just ignore them */
	if (bchannel->b_rxoff)
	{
		CDEBUG(NULL, NULL, "ignoring data, because rx is turned off\n");
		return;
	}

	if (!bchannel->call)
	{
		CDEBUG(NULL, NULL, "ignoring data, because no call associated with bchannel\n");
		return;
	}
	if (!bchannel->call->audiopath)
	{
		/* return, because we have no audio from port */
		return;
	}
	if (bchannel->call->pipe[1] > -1)
	{
		len = write(bchannel->call->pipe[1], data, len);
		if (len < 0)
		{
			close(bchannel->call->pipe[1]);
			bchannel->call->pipe[1] = -1;
			CDEBUG(NULL, NULL, "broken pipe on bchannel pipe\n");
			return;
		}
	}
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
	for (i = 0; i < len; i++)
		*p++ = flip_bits[*data++];
	frm->prim = DL_DATA_REQ;
	frm->id = 0;
	ret = sendto(bchannel->b_sock, buff, MISDN_HEADER_LEN+len, 0, NULL, 0);
	if (ret < 0)
		CERROR(NULL, NULL, "Failed to send to socket %d\n", bchannel->b_sock);
}


/*
 * join bchannel
 */
void bchannel_join(struct bchannel *bchannel, unsigned short id)
{
	int handle;

	handle = bchannel->b_sock;
	if (id) {
		bchannel->b_conf = (id<<16) + bchannel_pid;
		bchannel->b_rxoff = 1;
	} else {
		bchannel->b_conf = 0;
		bchannel->b_rxoff = 0;
	}
	if (bchannel->b_state == BSTATE_ACTIVE)
	{
		ph_control(handle, DSP_RECEIVE_OFF, bchannel->b_rxoff, "DSP-RX_OFF", bchannel->b_conf);
		ph_control(handle, DSP_CONF_JOIN, bchannel->b_conf, "DSP-CONF", bchannel->b_conf);
	}
}


/*
 * main loop for processing messages from mISDN
 */
int bchannel_handle(void)
{
	int ret, work = 0;
	struct bchannel *bchannel;
	char buffer[2048+MISDN_HEADER_LEN];
	struct mISDNhead *hh = (struct mISDNhead *)buffer;

	/* process all bchannels */
	bchannel = bchannel_first;
	while(bchannel)
	{
		/* handle message from bchannel */
		if (bchannel->b_sock > -1)
		{
			ret = recv(bchannel->b_sock, buffer, sizeof(buffer), 0);
			if (ret >= (int)MISDN_HEADER_LEN)
			{
				work = 1;
				switch(hh->prim)
				{
					/* we don't care about confirms, we use rx data to sync tx */
					case PH_DATA_CNF:
					break;

					/* we receive audio data, we respond to it AND we send tones */
					case PH_DATA_IND:
					case PH_DATA_REQ:
					case DL_DATA_IND:
					case PH_CONTROL_IND:
					bchannel_receive(bchannel, hh->prim, hh->id, buffer+MISDN_HEADER_LEN, ret-MISDN_HEADER_LEN);
					break;

					case PH_ACTIVATE_IND:
					case DL_ESTABLISH_IND:
					case PH_ACTIVATE_CNF:
					case DL_ESTABLISH_CNF:
					CDEBUG(NULL, NULL, "DL_ESTABLISH confirm: bchannel is now activated (socket %d).\n", bchannel->b_sock);
					bchannel_activated(bchannel);
					break;

					case PH_DEACTIVATE_IND:
					case DL_RELEASE_IND:
					case PH_DEACTIVATE_CNF:
					case DL_RELEASE_CNF:
					CDEBUG(NULL, NULL, "DL_RELEASE confirm: bchannel is now de-activated (socket %d).\n", bchannel->b_sock);
//					bchannel_deactivated(bchannel);
					break;

					default:
					CERROR(NULL, NULL, "child message not handled: prim(0x%x) socket(%d) data len(%d)\n", hh->prim, bchannel->b_sock, ret - MISDN_HEADER_LEN);
				}
			} else
			{
				if (ret < 0 && errno != EWOULDBLOCK)
					CERROR(NULL, NULL, "Read from socket %d failed with return code %d\n", bchannel->b_sock, ret);
			}
		}
		bchannel = bchannel->next;
	}

	/* if we received at least one b-frame, we will return 1 */
	return(work);
}


/*
 * bchannel channel handling
 */
struct bchannel *bchannel_first = NULL;
struct bchannel *find_bchannel_handle(unsigned long handle)
{
	struct bchannel *bchannel = bchannel_first;

	while(bchannel)
	{
		if (bchannel->handle == handle)
			break;
		bchannel = bchannel->next;
	}
	return(bchannel);
}

#if 0
struct bchannel *find_bchannel_ref(unsigned long ref)
{
	struct bchannel *bchannel = bchannel_first;

	while(bchannel)
	{
		if (bchannel->ref == ref)
			break;
		bchannel = bchannel->next;
	}
	return(bchannel);
}
#endif

struct bchannel *alloc_bchannel(unsigned long handle)
{
	struct bchannel **bchannelp = &bchannel_first;

	while(*bchannelp)
		bchannelp = &((*bchannelp)->next);

	*bchannelp = (struct bchannel *)calloc(1, sizeof(struct bchannel));
	if (!*bchannelp)
		return(NULL);
	(*bchannelp)->handle = handle;
	(*bchannelp)->b_state = BSTATE_IDLE;
		
	return(*bchannelp);
}

void free_bchannel(struct bchannel *bchannel)
{
	struct bchannel **temp = &bchannel_first;

	while(*temp)
	{
		if (*temp == bchannel)
		{
			*temp = (*temp)->next;
			if (bchannel->b_sock > -1)
				bchannel_destroy(bchannel);
			if (bchannel->call)
			{
				if (bchannel->call->bchannel)
					bchannel->call->bchannel = NULL;
			}
			free(bchannel);
			return;
		}
		temp = &((*temp)->next);
	}
}


