/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** mISDN port abstraction for dss1 and sip                                   **
**                                                                           **
\*****************************************************************************/ 


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "main.h"
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
extern "C" {
#include <net_l2.h>
}

#define ISDN_PID_L2_B_USER 0x420000ff
#define ISDN_PID_L3_B_USER 0x430000ff
#define ISDN_PID_L4_B_USER 0x440000ff

/* used for udevice */
int entity = 0;

/* noise randomizer */
unsigned char mISDN_rand[256];
int mISDN_rand_count = 0;

/* the device handler and port list */
int mISDNdevice = -1;

/* list of mISDN ports */
struct mISDNport *mISDNport_first;

/*
 * constructor
 */
PmISDN::PmISDN(int type, mISDNport *mISDNport, char *portname, struct port_settings *settings, int channel, int exclusive) : Port(type, portname, settings)
{
	p_m_mISDNport = mISDNport;
	p_m_portnum = mISDNport->portnum;
	p_m_b_index = -1;
	p_m_b_channel = 0;
	p_m_b_exclusive = 0;
	p_m_b_reserve = 0;
	p_m_b_addr = 0;
	p_m_b_stid = 0;
	p_m_jittercheck = 0;
	p_m_delete = 0;
	p_m_hold = 0;
	p_m_txvol = p_m_rxvol = 0;
	p_m_conf = 0;
	p_m_txdata = 0;
#warning set delay by routing parameter or interface config
	p_m_delay = 0;
	p_m_echo = 0;
	p_m_tone = 0;
	p_m_rxoff = 0;
	p_m_nodata = 1; /* may be 1, because call always notifies us */
	p_m_dtmf = !options.nodtmf;
	sollen wir daraus eine interface-option machen?:
	p_m_timeout = 0;
	p_m_timer = 0;
#warning denke auch an die andere seite. also das setup sollte dies weitertragen

	p_m_crypt = 0;
	p_m_crypt_listen = 0;
	p_m_crypt_msg_loops = 0;
	p_m_crypt_msg_loops = 0;
	p_m_crypt_msg_len = 0;
	p_m_crypt_msg[0] = '\0';
	p_m_crypt_msg_current = 0;
	p_m_crypt_key[0] = '\0';
	p_m_crypt_key_len = 0;
	p_m_crypt_listen = 0;
	p_m_crypt_listen_state = 0;
	p_m_crypt_listen_len = 0;
	p_m_crypt_listen_msg[0] = '\0';
	p_m_crypt_listen_crc = 0;

	/* if any channel requested by constructor */
	if (channel == CHANNEL_ANY)
	{
		/* reserve channel */
		p_m_b_reserve = 1;
		mISDNport->b_reserved++;
	}

	} else
	/* reserve channel */
	if (channel) // only if constructor was called with a channel resevation
		seize_bchannel(channel, exclusive);

	/* we increase the number of objects: */
	mISDNport->use++;
	PDEBUG(DEBUG_ISDN, "Created new mISDNPort(%s). Currently %d objects use, port #%d\n", portname, mISDNport->use, p_m_portnum);
}


/*
 * destructor
 */
PmISDN::~PmISDN()
{
	struct message *message;

	/* remove bchannel relation */
	free_bchannel();

	/* release epoint */
	while (p_epointlist)
	{
		PDEBUG(DEBUG_ISDN, "destroy mISDNPort(%s). endpoint still exists, releaseing.\n", p_name);
		message = message_create(p_serial, p_epointlist->epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = 16;
		message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		message_put(message);
		/* remove from list */
		free_epointlist(p_epointlist);
	}

	/* we decrease the number of objects: */
	p_m_mISDNport->use--;
	PDEBUG(DEBUG_ISDN, "destroyed mISDNPort(%s). Currently %d objects\n", p_name, p_m_mISDNport->use);
}


/*
 * send control information to the channel (dsp-module)
 */
void ph_control(unsigned long b_addr, int c1, int c2)
{
	unsigned char buffer[mISDN_HEADER_LEN+sizeof(int)+sizeof(int)];
	iframe_t *ctrl = (iframe_t *)buffer; 
	unsigned long *d = (unsigned long *)&ctrl->data.p;

	ctrl->prim = PH_CONTROL | REQUEST;
	ctrl->addr = b_addr | FLG_MSG_DOWN;
	ctrl->dinfo = 0;
	ctrl->len = sizeof(unsigned long)*2;
	*d++ = c1;
	*d++ = c2;
	mISDN_write(mISDNdevice, ctrl, mISDN_HEADER_LEN+ctrl->len, TIMEOUT_1SEC);
}

void ph_control_block(unsigned long b_addr, int c1, void *c2, int c2_len)
{
	unsigned char buffer[mISDN_HEADER_LEN+sizeof(int)+c2_len];
	iframe_t *ctrl = (iframe_t *)buffer;
	unsigned long *d = (unsigned long *)&ctrl->data.p;

	ctrl->prim = PH_CONTROL | REQUEST;
	ctrl->addr = b_addr | FLG_MSG_DOWN;
	ctrl->dinfo = 0;
	ctrl->len = sizeof(unsigned long)*2;
	*d++ = c1;
	memcpy(d, c2, c2_len);
	mISDN_write(mISDNdevice, ctrl, mISDN_HEADER_LEN+ctrl->len, TIMEOUT_1SEC);
}


/*
 * activate / deactivate bchannel
 */
static void bchannel_activate(struct mISDNport *mISDNport, int i)
{
	iframe_t act;

	/* we must activate if we are deactivated */
	if (mISDNport->b_state[i] == B_STATE_IDLE)
	{
		/* activate bchannel */
		PDEBUG(DEBUG_BCHANNEL, "activating bchannel (index %d), because currently idle (address 0x%x).\n", i, mISDNport->b_addr[i]);
		act.prim = DL_ESTABLISH | REQUEST; 
		act.addr = mISDNport->b_addr[i] | FLG_MSG_DOWN;
		act.dinfo = 0;
		act.len = 0;
		mISDN_write(mISDNdevice, &act, mISDN_HEADER_LEN+act.len, TIMEOUT_1SEC);
		mISDNport->b_state[i] = B_STATE_ACTIVATING;
		return;
	}

	/* if we are active, we configure our channel */
	if (mISDNport->b_state[i] == B_STATE_ACTIVE)
	{
		unsigned char buffer[mISDN_HEADER_LEN+ISDN_PRELOAD];
		iframe_t *pre = (iframe_t *)buffer; /* preload data */
		unsigned char *p = (unsigned char *)&pre->data.p;

		/* it is an error if this channel is not associated with a port object */
		if (!mISDNport->b_port[i])
		{
			PERROR("bchannel index i=%d not associated with a port object\n", i);
			return;
		}

		/* configure dsp features */
		if (mISDNport->b_port[i]->p_m_txdata)
		{
			PDEBUG(DEBUG_BCHANNEL, "during activation, we set txdata to txdata=%d.\n", mISDNport->b_port[i]->p_m_txdata);
			ph_control(mISDNport->b_addr[i], (mISDNport->b_port[i]->p_m_txdata)?CMX_TXDATA_ON:CMX_TXDATA_OFF);
		}
		if (mISDNport->b_port[i]->p_m_delay)
		{
			PDEBUG(DEBUG_BCHANNEL, "during activation, we set delay to delay=%d.\n", mISDNport->b_port[i]->p_m_delay);
			ph_control(mISDNport->b_addr[i], CMX_DELAY, mISDNport->b_port[i]->p_m_delay);
		}
		if (mISDNport->b_port[i]->p_m_txvol)
		{
			PDEBUG(DEBUG_BCHANNEL, "during activation, we change tx-volume to shift=%d.\n", mISDNport->b_port[i]->p_m_txvol);
			ph_control(mISDNport->b_addr[i], VOL_CHANGE_TX, mISDNport->b_port[i]->p_m_txvol);
		}
		if (mISDNport->b_port[i]->p_m_rxvol)
		{
			PDEBUG(DEBUG_BCHANNEL, "during activation, we change rx-volume to shift=%d.\n", mISDNport->b_port[i]->p_m_rxvol);
			ph_control(mISDNport->b_addr[i], VOL_CHANGE_RX, mISDNport->b_port[i]->p_m_rxvol);
		}
//tone		if (mISDNport->b_port[i]->p_m_conf && !mISDNport->b_port[i]->p_m_tone)
		if (mISDNport->b_port[i]->p_m_conf)
		{
			PDEBUG(DEBUG_BCHANNEL, "during activation, we change conference to conf=%d.\n", mISDNport->b_port[i]->p_m_conf);
			ph_control(mISDNport->b_addr[i], CMX_CONF_JOIN, mISDNport->b_port[i]->p_m_conf);
		}
		if (mISDNport->b_port[i]->p_m_echo)
		{
			PDEBUG(DEBUG_BCHANNEL, "during activation, we set echo to echo=%d.\n", mISDNport->b_port[i]->p_m_echo);
			ph_control(mISDNport->b_addr[i], CMX_ECHO_ON, 0);
		}
		if (mISDNport->b_port[i]->p_m_tone)
		{
			PDEBUG(DEBUG_BCHANNEL, "during activation, we set tone to tone=%d.\n", mISDNport->b_port[i]->p_m_tone);
			ph_control(mISDNport->b_addr[i], TONE_PATT_ON, mISDNport->b_port[i]->p_m_tone);
		}
		if (mISDNport->b_port[i]->p_m_rxoff)
		{
			PDEBUG(DEBUG_BCHANNEL, "during activation, we set rxoff to rxoff=%d.\n", mISDNport->b_port[i]->p_m_rxoff);
			ph_control(mISDNport->b_addr[i], CMX_RECEIVE_OFF, 0);
		}
#if 0
		if (mISDNport->b_port[i]->p_m_txmix)
		{
			PDEBUG(DEBUG_BCHANNEL, "during activation, we set txmix to txmix=%d.\n", mISDNport->b_port[i]->p_m_txmix);
			ph_control(mISDNport->b_addr[i], CMX_MIX_ON, 0);
		}
#endif
		if (mISDNport->b_port[i]->p_m_dtmf)
		{
			PDEBUG(DEBUG_BCHANNEL, "during activation, we set dtmf to dtmf=%d.\n", mISDNport->b_port[i]->p_m_dtmf);
			ph_control(mISDNport->b_addr[i], DTMF_TONE_START, 0);
		}
		if (mISDNport->b_port[i]->p_m_crypt)
		{
			PDEBUG(DEBUG_BCHANNEL, "during activation, we set crypt to crypt=%d.\n", mISDNport->b_port[i]->p_m_crypt);
			ph_control_block(mISDNport->b_addr[i], BF_ENABLE_KEY, mISDNport->b_port[i]->p_m_crypt_key, mISDNport->b_port[i]->p_m_crypt_key_len);
		}

		/* preload tx-buffer */
		pre->len = mISDNport->b_port[i]->read_audio(p, ISDN_PRELOAD, 1);
		if (pre->len > 0)
		{
			PDEBUG(DEBUG_BCHANNEL, "port is activated, we fill our buffer (ISDN_PRELOAD = %d).\n", pre->len);
			/* flip bits */
#if 0
			q = p + pre->len;
			while(p != q)
				 *p++ = flip[*p];
#endif
			pre->prim = DL_DATA | REQUEST;
			pre->addr = mISDNport->b_addr[i] | FLG_MSG_DOWN;
			pre->dinfo = 0;
			mISDN_write(mISDNdevice, pre, mISDN_HEADER_LEN+pre->len, TIMEOUT_1SEC);
		}
	}
}

static void bchannel_deactivate(struct mISDNport *mISDNport, int i)
{
	iframe_t dact;

	if (mISDNport->b_state[i] == B_STATE_ACTIVE)
	{
		/* reset dsp features */
		if (mISDNport->b_port[i])
		{
			if (mISDNport->b_port[i]->p_m_delay)
			{
				PDEBUG(DEBUG_BCHANNEL, "during deactivation, we reset txdata from txdata=%d.\n", mISDNport->b_port[i]->p_m_txdata);
				ph_control(mISDNport->b_addr[i], CMX_TXDATA_OFF, 0);
			}
			if (mISDNport->b_port[i]->p_m_delay)
			{
				PDEBUG(DEBUG_BCHANNEL, "during deactivation, we reset delay from delay=%d.\n", mISDNport->b_port[i]->p_m_delay);
				ph_control(mISDNport->b_addr[i], CMX_JITTER, 0);
			}
			if (mISDNport->b_port[i]->p_m_txvol)
			{
				PDEBUG(DEBUG_BCHANNEL, "during deactivation, we reset tx-volume from shift=%d.\n", mISDNport->b_port[i]->p_m_txvol);
				ph_control(mISDNport->b_addr[i], VOL_CHANGE_TX, 0);
			}
			if (mISDNport->b_port[i]->p_m_rxvol)
			{
				PDEBUG(DEBUG_BCHANNEL, "during deactivation, we reset rx-volume from shift=%d.\n", mISDNport->b_port[i]->p_m_rxvol);
				ph_control(mISDNport->b_addr[i], VOL_CHANGE_RX, 0);
			}
			if (mISDNport->b_port[i]->p_m_conf)
			{
				PDEBUG(DEBUG_BCHANNEL, "during deactivation, we release conference from conf=%d.\n", mISDNport->b_port[i]->p_m_conf);
				ph_control(mISDNport->b_addr[i], CMX_CONF_SPLIT, 0);
			}
			if (mISDNport->b_port[i]->p_m_echo)
			{
				PDEBUG(DEBUG_BCHANNEL, "during deactivation, we reset echo from echo=%d.\n", mISDNport->b_port[i]->p_m_echo);
				ph_control(mISDNport->b_addr[i], CMX_ECHO_OFF, 0);
			}
			if (mISDNport->b_port[i]->p_m_tone)
			{
				PDEBUG(DEBUG_BCHANNEL, "during deactivation, we reset tone from tone=%d.\n", mISDNport->b_port[i]->p_m_tone);
				ph_control(mISDNport->b_addr[i], TONE_PATT_OFF, 0);
			}
			if (mISDNport->b_port[i]->p_m_rxoff)
			{
				PDEBUG(DEBUG_BCHANNEL, "during deactivation, we reset rxoff from rxoff=%d.\n", mISDNport->b_port[i]->p_m_rxoff);
				ph_control(mISDNport->b_addr[i], CMX_RECEIVE_ON, 0);
			}
#if 0
			if (mISDNport->b_port[i]->p_m_txmix)
			{
				PDEBUG(DEBUG_BCHANNEL, "during deactivation, we reset txmix from txmix=%d.\n", mISDNport->b_port[i]->p_m_txmix);
				ph_control(mISDNport->b_addr[i], CMX_MIX_OFF, 0);
			}
#endif
			if (mISDNport->b_port[i]->p_m_dtmf)
			{
				PDEBUG(DEBUG_BCHANNEL, "during deactivation, we reset dtmf from dtmf=%d.\n", mISDNport->b_port[i]->p_m_dtmf);
				ph_control(mISDNport->b_addr[i], DTMF_TONE_STOP, 0);
			}
			if (mISDNport->b_port[i]->p_m_crypt)
			{
				PDEBUG(DEBUG_BCHANNEL, "during deactivation, we reset crypt from crypt=%d.\n", mISDNport->b_port[i]->p_m_dtmf);
				ph_control(mISDNport->b_addr[i], BF_DISABLE, 0);
			}
		}
		/* deactivate bchannel */
		PDEBUG(DEBUG_BCHANNEL, "deactivating bchannel (index %d), because currently active.\n", i);
		dact.prim = DL_RELEASE | REQUEST; 
		dact.addr = mISDNport->b_addr[i] | FLG_MSG_DOWN;
		dact.dinfo = 0;
		dact.len = 0;
		mISDN_write(mISDNdevice, &dact, mISDN_HEADER_LEN+dact.len, TIMEOUT_1SEC);
		mISDNport->b_state[i] = B_STATE_DEACTIVATING;
		return;
	}
}


/*
 * check for available channel and reserve+set it.
 * give channel number or SEL_CHANNEL_ANY or SEL_CHANNEL_NO
 * give exclusiv flag
 * returns -(cause value) or x = channel x or 0 = no channel
 */
denke ans aktivieren und deaktivieren
int PmISDN::seize_bchannel(int channel, int exclusive)
{
	int i;

	/* the channel is what we have */
	if (p_m_b_channel == channel)
		return(channel);

	/* if channel already in use, release it */
	if (p_m_b_channel)
		drop_bchannel();

	/* if CHANNEL_NO */
	if (channel==CHANNEL_NO || channel==0)
		return(0);
	
	/* is channel in range ? */
	if (channel==16
	 || (channel>p_m_mISDNport->b_num && channel<16)
	 || ((channel-1)>p_m_mISDNport->b_num && channel>16)) /* channel-1 because channel 16 is not counted */
		return(-6); /* channel unacceptable */

	/* request exclusive channel */
	if (exclusive && channel>0)
	{
		i = channel-1-(channel>16);
		if (p_m_mISDNport->b_port[i])
			return(-44); /* requested channel not available */
		goto seize;
	}

	/* ask for channel */
	if (channel>0)
	{
		i = channel-1-(channel>16);
		if (p_m_mISDNport->b_port[i] == NULL)
			goto seize;
	}

	/* search for channel */
	i = 0;
	while(i < p_m_mISDNport->b_num)
	{
		if (!p_m_mISDNport->b_port[i])
		{
			channel = i+1+(i>=15);
			goto seize;
		}
		i++;
	}
	return(-34); /* no free channel */

seize:
	/* link Port */
	p_m_mISDNport->b_port[i] = this;
	p_m_b_index = i;
	p_m_b_channel = channel;
	p_m_b_exclusive = exclusive;
	p_m_b_stid = p_m_mISDNport->b_stid[i];
	p_m_b_addr = p_m_mISDNport->b_addr[i];
	p_m_jittercheck = 0;

	/* reserve channel */
	if (p_m_b_reserve == 0) // already reserved
	{
		p_m_b_reserve = 1;
		mISDNport->b_reserved++;
	}

	PDEBUG(DEBUG_BCHANNEL, "PmISDN(%s) seizing bchannel %d (index %d)\n", p_name, channel, i);

	return(channel);
}

/*
 * drop reserved channel and unset it.
 */
void PmISDN::drop_bchannel(void)
{
	/* unreserve channel */
	if (p_m_b_reserve)
		p_m_mISDNport->b_reserved--;
	p_m_b_reserve = 0;

	/* if not in use */
	if (!p_m_b_channel)
		return;

	PDEBUG(DEBUG_BCHANNEL, "PmISDN(%s) dropping bchannel\n", p_name);

	p_m_mISDNport->b_port[p_m_b_index] = NULL;
	p_m_b_index = -1;
	p_m_b_channel = 0;
	p_m_b_exclusive = 0;
	p_m_b_addr = 0;
	p_m_b_stid = 0;
}

/*
 * handler
 */
int PmISDN::handler(void)
{
	struct message *message;

	// NOTE: deletion is done by the child class

	/* handle timeouts */
	if (p_m_timeout)
	{
		if (p_m_timer+p_m_timeout < now_d)
		{
			PDEBUG(DEBUG_ISDN, "(%s) timeout after %d seconds detected (state=%d).\n", p_name, p_m_timeout, p_state);
			p_m_timeout = 0;
			/* send timeout to endpoint */
			message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_TIMEOUT);
			message->param.state = p_state;
			message_put(message);
		}
		return(1);
	}
	
	return(0); /* nothing done */
}


/*
 * whenever we get audio data from bchannel, we process it here
 */
void PmISDN::bchannel_receive(iframe_t *frm)
{
	unsigned char *data_temp;
	unsigned long length_temp;
	struct message *message;
	struct timeval tv;
	struct timezone tz;
	long long jitter_now;
	int newlen;
	unsigned char *p;
	int l;
	unsigned long cont;
//	iframe_t rsp; /* response to possible indication */
#if 0
#warning BCHANNEL-DEBUG
{
	// check if we are part of all ports */
	class Port *port = port_first;
	while(port)
	{
		if (port==this)
			break;
		port=port->next;
	}
	if (!port)
	{
		PERROR_RUNTIME("**************************************************\n");
		PERROR_RUNTIME("*** BCHANNEL-DEBUG: !this! is not in list of ports\n");
		PERROR_RUNTIME("**************************************************\n");
		return;
	}
}
#endif


	if (frm->prim == (PH_CONTROL | INDICATION))
	{
		cont = *((unsigned long *)&frm->data.p);
		// PDEBUG(DEBUG_PORT, "PmISDN(%s) received a PH_CONTROL INDICATION 0x%x\n", p_name, cont);
		if ((cont&(~DTMF_TONE_MASK)) == DTMF_TONE_VAL)
		{
			message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_DTMF);
			message->param.dtmf = cont & DTMF_TONE_MASK;
			PDEBUG(DEBUG_PORT, "PmISDN(%s) PH_CONTROL  DTMF digit '%c'\n", p_name, message->param.dtmf);
			message_put(message);
		}
		switch(cont)
		{
			case BF_REJECT:
			message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_CRYPT);
			message->param.crypt.type = CC_ERROR_IND;
			PDEBUG(DEBUG_PORT, "PmISDN(%s) PH_CONTROL  reject of blowfish.\n", p_name);
			message_put(message);
			break;

			case BF_ACCEPT:
			message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_CRYPT);
			message->param.crypt.type = CC_ACTBF_CONF;
			PDEBUG(DEBUG_PORT, "PmISDN(%s) PH_CONTROL  accept of blowfish.\n", p_name);
			message_put(message);
			break;
		}
		return;
	}

	/* calls will not process any audio data unless
	 * the call is connected OR tones feature is enabled.
	 */
	if (p_state!=PORT_STATE_CONNECT
	 && !p_m_mISDNport->is_tones)
		return;

#if 0
	/* the bearer capability must be audio in order to send and receive
	 * audio prior or after connect.
	 */
	if (!(p_bearerinfo.capability&CLASS_CAPABILITY_AUDIO) && p_state!=PORT_STATE_CONNECT)
		return;
#endif

	/* if rx is off, it may happen that fifos send us pending informations, we just ignore them */
	if (p_m_rxoff)
	{
		PDEBUG(DEBUG_BCHANNEL, "PmISDN(%s) ignoring data, because rx is turned off\n", p_name);
		return;
	}

	/* randomize and listen to crypt message if enabled */
	if (p_m_crypt_listen)
	{
		/* the noisy randomizer */
		p = (unsigned char *)&frm->data.p;
		l = frm->len;
		while(l--)
			mISDN_rand[mISDN_rand_count & 0xff] += *p++;

		cryptman_listen_bch((unsigned char *)&frm->data.p, frm->len);
	}

	/* prevent jitter */
	gettimeofday(&tv, &tz);
	jitter_now = tv.tv_sec;
	jitter_now = jitter_now*8000 + tv.tv_usec/125;
	if (p_m_jittercheck == 0)
	{
		p_m_jittercheck = jitter_now;
		p_m_jitterdropped = 0;
	} else
		p_m_jittercheck += frm->len;
	if (p_m_jittercheck < jitter_now)
	{
//		PERROR("jitter: ignoring slow data\n");
		p_m_jittercheck = jitter_now;
	} else
	if (p_m_jittercheck-ISDN_JITTERLIMIT > jitter_now)
	{
		p_m_jitterdropped += frm->len;
		p_m_jittercheck -= frm->len;
		/* must exit here */
		return;
	} else
	if (p_m_jitterdropped)
	{
		PERROR("jitter: dropping, caused by fast data: %lld\n", p_m_jitterdropped);
		p_m_jitterdropped = 0;
	}

	p = (unsigned char *)&frm->data.p;

	/* send data to epoint */
	if (ACTIVE_EPOINT(p_epointlist)) /* only if we have an epoint object */
	{
//printf("we are port %s and sending to epoint %d\n", p_m_cardname, p_epoint->serial);
		length_temp = frm->len;
		data_temp = p;
		while(length_temp)
		{
			message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_DATA);
			message->param.data.len = (length_temp>sizeof(message->param.data.data))?sizeof(message->param.data.data):length_temp;
			memcpy(message->param.data.data, data_temp, message->param.data.len);
			message->param.data.compressed = 1;
			message->param.data.port_id = p_serial;
			message->param.data.port_type = p_type;
			message_put(message);
			if (length_temp <= sizeof(message->param.data.data))
				break;
			data_temp += sizeof(message->param.data.data);
			length_temp -= sizeof(message->param.data.data);
		}
	}
	/* return the same number of tone data, as we recieved */
	newlen = read_audio(p, frm->len, 1);
	/* send tone data to isdn device only if we have data, otherwhise we send nothing */
	if (newlen>0 && (p_tone_fh>=0 || p_tone_fetched || !p_m_nodata || p_m_crypt_msg_loops))
	{
//printf("jolly: sending.... %d %d %d %d %d\n", newlen, p_tone_fh, p_tone_fetched, p_m_nodata, p_m_crypt_msg_loops);
#if 0
		if (p_m_txmix_on)
		{
			p_m_txmix_on -= newlen;
			if (p_m_txmix_on <= 0)
			{
				p_m_txmix_on = 0;
				p_m_txmix = !p_m_nodata;
				PDEBUG(DEBUG_BCHANNEL, "after sending message, we set txmix to txmix=%d.\n", p_m_txmix);
				if (p_m_txmix)
					ph_control(p_m_b_addr, CMX_MIX_ON, 0);
			}
		}
#endif 
		if (p_m_crypt_msg_loops)
		{
			/* send pending message */
			int tosend;

			tosend = p_m_crypt_msg_len - p_m_crypt_msg_current;
			if (tosend > newlen)
				tosend = newlen;
			memcpy(p, p_m_crypt_msg+p_m_crypt_msg_current, tosend);
			p_m_crypt_msg_current += tosend;
			if (p_m_crypt_msg_current == p_m_crypt_msg_len)
			{
				p_m_crypt_msg_current = 0;
				p_m_crypt_msg_loops--;
#if 0
// we need to disable rxmix some time after sending the loops...
				if (!p_m_crypt_msg_loops && p_m_txmix)
				{
					p_m_txmix_on = 8000; /* one sec */
				}
#endif
			}
		}
		frm->prim = frm->prim & 0xfffffffc | REQUEST; 
		frm->addr = p_m_b_addr | FLG_MSG_DOWN;
		frm->len = newlen;
		mISDN_write(mISDNdevice, frm, mISDN_HEADER_LEN+frm->len, TIMEOUT_1SEC);

		if (p_debug_nothingtosend)
		{
			p_debug_nothingtosend = 0;
			PDEBUG((DEBUG_PORT | DEBUG_BCHANNEL), "PmISDN(%s) start sending, because we have tones and/or remote audio.\n", p_name);
		} 
	} else
	{
		if (!p_debug_nothingtosend)
		{
			p_debug_nothingtosend = 1;
			PDEBUG((DEBUG_PORT | DEBUG_BCHANNEL), "PmISDN(%s) stop sending, because we have only silence.\n", p_name);
		} 
	}
#if 0
	/* response to the data indication */
	rsp.prim = frm->prim & 0xfffffffc | RESPONSE; 
	rsp.addr = frm->addr & INST_ID_MASK | FLG_MSG_DOWN;
	rsp.dinfo = frm->dinfo;
	rsp.len = 0;
	mISDN_write(mISDNdevice, &rsp, mISDN_HEADER_LEN+rsp.len, TIMEOUT_1SEC);
//PDEBUG(DEBUG_ISDN, "written %d bytes.\n", length);
#endif
}


/*
 * set echotest
 */
void PmISDN::set_echotest(int echo)
{
	if (p_m_echo != echo)
	{
		p_m_echo = echo;
		PDEBUG(DEBUG_ISDN, "we set echo to echo=%d.\n", p_m_echo);
		if (p_m_b_channel)
			if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE)
				ph_control(p_m_b_addr, p_m_echo?CMX_ECHO_ON:CMX_ECHO_OFF, 0);
	}
}

/*
 * set tone
 */
void PmISDN::set_tone(char *dir, char *tone)
{
	int id;

	if (!tone)
		tone = "";
	PDEBUG(DEBUG_ISDN, "isdn port now plays tone:'%s'.\n", tone);
	if (!tone[0])
	{
		id = TONE_OFF;
		goto setdsp;
	}

	/* check if we NOT really have to use a dsp-tone */
	if (!options.dsptones)
	{
		nodsp:
		if (p_m_tone)
		if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE)
		{
			PDEBUG(DEBUG_ISDN, "we reset tone from id=%d to OFF.\n", p_m_tone);
			ph_control(p_m_b_addr, TONE_PATT_OFF, 0);
		}
		p_m_tone = 0;
		Port::set_tone(dir, tone);
		return;
	}
	if (p_tone_dir[0])
		goto nodsp;

	/* now we USE dsp-tone, convert name */
	else if (!strcmp(tone, "dialtone"))
	{
		switch(options.dsptones) {
		case DSP_AMERICAN: id = TONE_AMERICAN_DIALTONE; break;
		case DSP_GERMAN: id = TONE_GERMAN_DIALTONE; break;
		case DSP_OLDGERMAN: id = TONE_GERMAN_OLDDIALTONE; break;
		}
	} else if (!strcmp(tone, "dialpbx"))
	{
		switch(options.dsptones) {
		case DSP_AMERICAN: id = TONE_AMERICAN_DIALPBX; break;
		case DSP_GERMAN: id = TONE_GERMAN_DIALPBX; break;
		case DSP_OLDGERMAN: id = TONE_GERMAN_OLDDIALPBX; break;
		}
	} else if (!strcmp(tone, "ringing"))
	{
		switch(options.dsptones) {
		case DSP_AMERICAN: id = TONE_AMERICAN_RINGING; break;
		case DSP_GERMAN: id = TONE_GERMAN_RINGING; break;
		case DSP_OLDGERMAN: id = TONE_GERMAN_OLDRINGING; break;
		}
	} else if (!strcmp(tone, "ringpbx"))
	{
		switch(options.dsptones) {
		case DSP_AMERICAN: id = TONE_AMERICAN_RINGPBX; break;
		case DSP_GERMAN: id = TONE_GERMAN_RINGPBX; break;
		case DSP_OLDGERMAN: id = TONE_GERMAN_OLDRINGPBX; break;
		}
	} else if (!strcmp(tone, "busy"))
	{
		busy:
		switch(options.dsptones) {
		case DSP_AMERICAN: id = TONE_AMERICAN_BUSY; break;
		case DSP_GERMAN: id = TONE_GERMAN_BUSY; break;
		case DSP_OLDGERMAN: id = TONE_GERMAN_OLDBUSY; break;
		}
	} else if (!strcmp(tone, "release"))
	{
		hangup:
		switch(options.dsptones) {
		case DSP_AMERICAN: id = TONE_AMERICAN_HANGUP; break;
		case DSP_GERMAN: id = TONE_GERMAN_HANGUP; break;
		case DSP_OLDGERMAN: id = TONE_GERMAN_OLDHANGUP; break;
		}
	} else if (!strcmp(tone, "cause_10"))
		goto hangup;
	else if (!strcmp(tone, "cause_11"))
		goto busy;
	else if (!strcmp(tone, "cause_22"))
	{
		switch(options.dsptones) {
		case DSP_AMERICAN: id = TONE_SPECIAL_INFO; break;
		case DSP_GERMAN: id = TONE_GERMAN_GASSENBESETZT; break;
		case DSP_OLDGERMAN: id = TONE_GERMAN_OLDBUSY; break;
		}
	} else if (!strncmp(tone, "cause_", 6))
		id = TONE_SPECIAL_INFO;
	else
		id = TONE_OFF;

	/* if we have a tone that is not supported by dsp */
	if (id==TONE_OFF && tone[0])
		goto nodsp;

	setdsp:
	if (p_m_tone != id)
	{
		/* set new tone */
		p_m_tone = id;
		PDEBUG(DEBUG_ISDN, "we set tone to id=%d.\n", p_m_tone);
		if (p_m_b_channel)
			if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE)
				ph_control(p_m_b_addr, p_m_tone?TONE_PATT_ON:TONE_PATT_OFF, p_m_tone);
	}
	/* turn user-space tones off in cases of no tone OR dsp tone */
	Port::set_tone("",NULL);
}


/* MESSAGE_mISDNSIGNAL */
//extern struct message *dddebug;
void PmISDN::message_mISDNsignal(unsigned long epoint_id, int message_id, union parameter *param)
{
	switch(param->mISDNsignal.message)
	{
		case mISDNSIGNAL_VOLUME:
		if (p_m_txvol != param->mISDNsignal.txvol)
		{
			p_m_txvol = param->mISDNsignal.txvol;
			PDEBUG(DEBUG_BCHANNEL, "we change tx-volume to shift=%d.\n", p_m_txvol);
			if (p_m_b_channel)
				if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE)
					ph_control(p_m_b_addr, VOL_CHANGE_TX, p_m_txvol);
		}
		if (p_m_rxvol != param->mISDNsignal.rxvol)
		{
			p_m_rxvol = param->mISDNsignal.rxvol;
			PDEBUG(DEBUG_BCHANNEL, "we change rx-volume to shift=%d.\n", p_m_rxvol);
			if (p_m_b_channel)
				if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE)
					ph_control(p_m_b_addr, VOL_CHANGE_RX, p_m_rxvol);
		}
		break;

		case mISDNSIGNAL_CONF:
//if (dddebug) PDEBUG(DEBUG_ISDN, "dddebug = %d\n", dddebug->type);
//tone		if (!p_m_tone && p_m_conf!=param->mISDNsignal.conf)
		if (p_m_conf != param->mISDNsignal.conf)
		{
			p_m_conf = param->mISDNsignal.conf;
			PDEBUG(DEBUG_BCHANNEL, "we change conference to conf=%d.\n", p_m_conf);
			if (p_m_b_channel)
				if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE)
					ph_control(p_m_b_addr, (p_m_conf)?CMX_CONF_JOIN:CMX_CONF_SPLIT, p_m_conf);
		}
		/* we must set, even if currently tone forbids conf */
		p_m_conf = param->mISDNsignal.conf;
//if (dddebug) PDEBUG(DEBUG_ISDN, "dddebug = %d\n", dddebug->type);
		break;

		case mISDNSIGNAL_CALLDATA:
		if (p_m_calldata != param->mISDNsignal.calldata)
		{
			p_m_calldata = param->mISDNsignal.calldata;
			PDEBUG(DEBUG_BCHANNEL, "we change to calldata=%d.\n", p_m_calldata);
			auch senden
		}
		break;
		
#if 0
		case mISDNSIGNAL_NODATA:
		p_m_nodata = param->mISDNsignal.nodata;
		if (p_m_txmix == p_m_nodata) /* txmix != !nodata */
		{
			p_m_txmix = !p_m_nodata;
			PDEBUG(DEBUG_BCHANNEL, "we change mix mode to txmix=%d (nodata=%d).\n", p_m_txmix, p_m_nodata);
			if (p_m_b_channel)
				if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE)
					ph_control(p_m_b_addr, p_m_txmix?CMX_MIX_ON:CMX_MIX_OFF, 0);
		}
		break;
#endif

#if 0
		case mISDNSIGNAL_RXOFF:
		if (p_m_rxoff != param->mISDNsignal.rxoff)
		{
			p_m_rxoff = param->mISDNsignal.rxoff;
			PDEBUG(DEBUG_BCHANNEL, "we change receive mode to rxoff=%d.\n", p_m_rxoff);
			if (p_m_b_channel)
				if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE)
					ph_control(p_m_b_addr, p_m_rxoff?CMX_RECEIVE_OFF:CMX_RECEIVE_ON, 0);
		}
		break;

		case mISDNSIGNAL_DTMF:
		if (p_m_dtmf != param->mISDNsignal.dtmf)
		{
			p_m_dtmf = param->mISDNsignal.dtmf;
			PDEBUG(DEBUG_BCHANNEL, "we change dtmf mode to dtmf=%d.\n", p_m_dtmf);
			if (p_m_b_channel)
				if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE)
					ph_control(p_m_b_addr, p_m_dtmf?DTMF_TONE_START:DTMF_TONE_STOP, 0);
		}
		break;

#endif
		default:
		PERROR("PmISDN(%s) unsupported signal message %d.\n", p_name, param->mISDNsignal.message);
	}
}

/* MESSAGE_CRYPT */
void PmISDN::message_crypt(unsigned long epoint_id, int message_id, union parameter *param)
{
	struct message *message;

	switch(param->crypt.type)
	{
		case CC_ACTBF_REQ:           /* activate blowfish */
		p_m_crypt = 1;
		p_m_crypt_key_len = param->crypt.len;
		if (p_m_crypt_key_len > (int)sizeof(p_m_crypt_key))
		{
			PERROR("PmISDN(%s) key too long %d > %d\n", p_name, p_m_crypt_key_len, sizeof(p_m_crypt_key));
			message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_CRYPT);
			message->param.crypt.type = CC_ERROR_IND;
			message_put(message);
			break;
		}
		memcpy(p_m_crypt_key, param->crypt.data, p_m_crypt_key_len);
		crypt_off:
		PDEBUG(DEBUG_BCHANNEL, "we set encryption to crypt=%d. (0 means OFF)\n", p_m_crypt);
		if (p_m_b_channel)
			if (p_m_mISDNport->b_state[p_m_b_index] == B_STATE_ACTIVE)
				ph_control_block(p_m_b_addr, p_m_crypt?BF_ENABLE_KEY:BF_DISABLE, p_m_crypt_key, p_m_crypt_key_len);
		break;

		case CC_DACT_REQ:            /* deactivate session encryption */
		p_m_crypt = 0;
		goto crypt_off;
		break;

		case CR_LISTEN_REQ:          /* start listening to messages */
		p_m_crypt_listen = 1;
		p_m_crypt_listen_state = 0;
		break;

		case CR_UNLISTEN_REQ:        /* stop listening to messages */
		p_m_crypt_listen = 0;
		break;

		case CR_MESSAGE_REQ:         /* send message */
		p_m_crypt_msg_len = cryptman_encode_bch(param->crypt.data, param->crypt.len, p_m_crypt_msg, sizeof(p_m_crypt_msg));
		if (!p_m_crypt_msg_len)
		{
			PERROR("PmISDN(%s) message too long %d > %d\n", p_name, param->crypt.len-1, sizeof(p_m_crypt_msg));
			break;
		}
		p_m_crypt_msg_current = 0; /* reset */
		p_m_crypt_msg_loops = 3; /* enable */
#if 0
		/* disable txmix, or we get corrupt data due to audio process */
		if (p_m_txmix)
		{
			PDEBUG(DEBUG_BCHANNEL, "for sending CR_MESSAGE_REQ, we reset txmix from txmix=%d.\n", p_m_txmix);
			ph_control(p_m_b_addr, CMX_MIX_OFF, 0);
		}
#endif
		break;

		default:
		PERROR("PmISDN(%s) unknown crypt message %d\n", p_name, param->crypt.type);
	}

}

/*
 * endpoint sends messages to the port
 */
int PmISDN::message_epoint(unsigned long epoint_id, int message_id, union parameter *param)
{
	if (Port::message_epoint(epoint_id, message_id, param))
		return(1);

	switch(message_id)
	{
		case MESSAGE_mISDNSIGNAL: /* user command */
		PDEBUG(DEBUG_ISDN, "PmISDN(%s) received special ISDN SIGNAL %d.\n", p_name, param->mISDNsignal.message);
		message_mISDNsignal(epoint_id, message_id, param);
		return(1);

		case MESSAGE_CRYPT: /* crypt control command */
		PDEBUG(DEBUG_ISDN, "PmISDN(%s) received encryption command '%d'.\n", p_name, param->crypt.type);
		message_crypt(epoint_id, message_id, param);
		return(1);
	}

	return(0);
}


/*
 * main loop for processing messages from mISDN device
 */
int mISDN_handler(void)
{
	int ret;
	msg_t *msg;
	iframe_t *frm;
	struct mISDNport *mISDNport;
	class PmISDN *isdnport;
	net_stack_t *nst;
	msg_t *dmsg;
	mISDNuser_head_t *hh;
	int i;

	if ((ret = Port::handler()))
		return(ret);

	/* the que avoids loopbacks when replying to stack after receiving
         * from stack. */
	mISDNport = mISDNport_first;
	while(mISDNport)
	{
		/* process turning off rx */
		i = 0;
		while(i < mISDNport->b_num)
		{
			if (mISDNport->b_port[i] && mISDNport->b_state[i] == B_STATE_ACTIVE)
			{
				isdnport=mISDNport->b_port[i];
				if (isdnport->p_tone_name[0] || isdnport->p_tone_fh>=0 || isdnport->p_tone_fetched || !isdnport->p_m_nodata || isdnport->p_m_crypt_msg_loops || isdnport->p_m_crypt_listen || isdnport->p_record)
				{
					/* rx IS required */
					if (isdnport->p_m_rxoff)
					{
						/* turn on RX */
						isdnport->p_m_rxoff = 0;
						PDEBUG(DEBUG_BCHANNEL, "%s: receive data is required, so we turn them on\n");
						ph_control(isdnport->p_m_b_addr, CMX_RECEIVE_ON, 0);
					}
				} else
				{
					/* rx NOT required */
					if (!isdnport->p_m_rxoff)
					{
						/* turn off RX */
						isdnport->p_m_rxoff = 1;
						PDEBUG(DEBUG_BCHANNEL, "%s: receive data is not required, so we turn them off\n");
						ph_control(isdnport->p_m_b_addr, CMX_RECEIVE_OFF, 0);
					}
				}
				isdnport->p_m_jittercheck = 0; /* reset jitter detection check */
			}
			i++;
		}
#if 0
		if (mISDNport->l1timeout && now>mISDNport->l1timeout)
		{
			PDEBUG(DEBUG_ISDN, "the L1 establish timer expired, we release all pending messages.\n", mISDNport->portnum);
			mISDNport->l1timeout = 0;
#endif

		if (mISDNport->l2establish)
		{
			if (now-mISDNport->l2establish > 5)
			{
				if (mISDNport->ntmode)
				{
					PDEBUG(DEBUG_ISDN, "the L2 establish timer expired, we try to establish the link NT portnum=%d.\n", mISDNport->portnum);
					time(&mISDNport->l2establish);
					/* establish */
					dmsg = create_l2msg(DL_ESTABLISH | REQUEST, 0, 0);
					if (mISDNport->nst.manager_l3(&mISDNport->nst, dmsg))
						free_msg(dmsg);
				} else {
					PDEBUG(DEBUG_ISDN, "the L2 establish timer expired, we try to establish the link TE portnum=%d.\n", mISDNport->portnum);
					time(&mISDNport->l2establish);
					/* establish */
					iframe_t act;
					act.prim = DL_ESTABLISH | REQUEST; 
					act.addr = (mISDNport->upper_id & ~LAYER_ID_MASK) | 3 | FLG_MSG_DOWN;
					act.dinfo = 0;
					act.len = 0;
					mISDN_write(mISDNdevice, &act, mISDN_HEADER_LEN+act.len, TIMEOUT_1SEC);
				}
			}
		}
		if ((dmsg = msg_dequeue(&mISDNport->downqueue)))
		{
			if (mISDNport->ntmode)
			{
				hh = (mISDNuser_head_t *)dmsg->data;
				PDEBUG(DEBUG_ISDN, "sending queued NT l3-down-message: prim(0x%x) dinfo(0x%x) msg->len(%d)\n", hh->prim, hh->dinfo, dmsg->len);
				if (mISDNport->nst.manager_l3(&mISDNport->nst, dmsg))
					free_msg(dmsg);
			} else
			{
				frm = (iframe_t *)dmsg->data;
				frm->addr = mISDNport->upper_id | FLG_MSG_DOWN;
				frm->len = (dmsg->len) - mISDN_HEADER_LEN;
				PDEBUG(DEBUG_ISDN, "sending queued TE l3-down-message: prim(0x%x) dinfo(0x%x) msg->len(%d)\n", frm->prim, frm->dinfo, dmsg->len);
				mISDN_write(mISDNdevice, dmsg->data, dmsg->len, TIMEOUT_1SEC);
				free_msg(dmsg);
			}
			return(1);
		}
		mISDNport = mISDNport->next;
	} 

	/* get message from kernel */
	if (!(msg = alloc_msg(MAX_MSG_SIZE)))
		return(1);
	ret = mISDN_read(mISDNdevice, msg->data, MAX_MSG_SIZE, 0);
	if (ret < 0)
	{
		free_msg(msg);
		if (errno == EAGAIN)
			return(0);
		PERROR("FATAL ERROR: failed to do mISDN_read()\n");
		exit(-1); 
	}
	if (!ret)
	{
		free_msg(msg);
//		printf("%s: ERROR: mISDN_read() returns nothing\n");
		return(0);
	}
	msg->len = ret;
	frm = (iframe_t *)msg->data;

	/* global prim */
	switch(frm->prim)
	{
		case MGR_INITTIMER | CONFIRM:
		case MGR_ADDTIMER | CONFIRM:
		case MGR_DELTIMER | CONFIRM:
		case MGR_REMOVETIMER | CONFIRM:
//		if (options.deb & DEBUG_ISDN)
//			PDEBUG(DEBUG_ISDN, "timer-confirm\n");
		free_msg(msg);
		return(1);
	}

	/* find the port */
	mISDNport = mISDNport_first;
	while(mISDNport)
	{
		if ((frm->prim==(MGR_TIMER | INDICATION)) && mISDNport->ntmode)
		{
			itimer_t *it = mISDNport->nst.tlist;

			/* find timer */
			while(it)
			{
				if (it->id == (int)frm->addr)
					break;
				it = it->next;
			}
			if (it)
			{
				mISDN_write_frame(mISDNdevice, msg->data, mISDNport->upper_id | FLG_MSG_DOWN,
					MGR_TIMER | RESPONSE, 0, 0, NULL, TIMEOUT_1SEC);

				PDEBUG(DEBUG_ISDN, "timer-indication %s port %d it=%p\n", (mISDNport->ntmode)?"NT":"TE", mISDNport->portnum, it);
				test_and_clear_bit(FLG_TIMER_RUNING, (long unsigned int *)&it->Flags);
				ret = it->function(it->data);
				break;
			}
			/* we will continue here because we have a timer for a different mISDNport */
		}
//printf("comparing frm->addr %x with upper_id %x\n", frm->addr, mISDNport->upper_id);
		if ((frm->addr&STACK_ID_MASK) == (unsigned int)(mISDNport->upper_id&STACK_ID_MASK))
		{
			/* d-message */
			switch(frm->prim)
			{
				case MGR_SHORTSTATUS | INDICATION:
				case MGR_SHORTSTATUS | CONFIRM:
				switch(frm->dinfo) {
					case SSTATUS_L1_ACTIVATED:
					PDEBUG(DEBUG_ISDN, "Received SSTATUS_L1_ACTIVATED for port %d (%s).\n", mISDNport->portnum, mISDNport->ifport->interface->name);
					goto ss_act;
					case SSTATUS_L1_DEACTIVATED:
					PDEBUG(DEBUG_ISDN, "Received SSTATUS_L1_DEACTIVATED for port %d (%s).\n", mISDNport->portnum, mISDNport->ifport->interface->name);
					goto ss_deact;
					case SSTATUS_L2_ESTABLISHED:
					PDEBUG(DEBUG_ISDN, "Received SSTATUS_L2_ESTABLISHED for port %d (%s).\n", mISDNport->portnum, mISDNport->ifport->interface->name);
					goto ss_estab;
					case SSTATUS_L2_RELEASED:
					PDEBUG(DEBUG_ISDN, "Received SSTATUS_L2_RELEASED for port %d (%s).\n", mISDNport->portnum, mISDNport->ifport->interface->name);
					goto ss_rel;
				}
				break;

				case PH_ACTIVATE | CONFIRM:
				case PH_ACTIVATE | INDICATION:
				PDEBUG(DEBUG_ISDN, "Received PH_ACTIVATED for port %d (%s).\n", mISDNport->portnum, mISDNport->ifport->interface->name);
				if (mISDNport->ntmode)
				{
					mISDNport->l1link = 1;
					setup_queue(mISDNport, 1);
					goto l1_msg;
				}
				ss_act:
				mISDNport->l1link = 1;
				setup_queue(mISDNport, 1);
				break;

				case PH_DEACTIVATE | CONFIRM:
				case PH_DEACTIVATE | INDICATION:
				PDEBUG(DEBUG_ISDN, "Received PH_DEACTIVATED for port %d (%s).\n", mISDNport->portnum, mISDNport->ifport->interface->name);
				if (mISDNport->ntmode)
				{
					mISDNport->l1link = 0;
					setup_queue(mISDNport, 0);
					goto l1_msg;
				}
				ss_deact:
				mISDNport->l1link = 0;
				setup_queue(mISDNport, 0);
				break;

				case PH_CONTROL | CONFIRM:
				case PH_CONTROL | INDICATION:
				PDEBUG(DEBUG_ISDN, "Received PH_CONTROL for port %d (%s).\n", mISDNport->portnum, mISDNport->ifport->interface->name);
				break;

				case DL_ESTABLISH | INDICATION:
				case DL_ESTABLISH | CONFIRM:
//				PDEBUG(DEBUG_ISDN, "addr 0x%x established data link (DL) TE portnum=%d (DL_ESTABLISH)\n", frm->addr, mISDNport->portnum);
				if (!mISDNport->ntmode) break; /* !!!!!!!!!!!!!!!! */
				ss_estab:
//				if (mISDNport->ntmode)
//					break;
				if (mISDNport->l2establish)
				{
					mISDNport->l2establish = 0;
					PDEBUG(DEBUG_ISDN, "the link became active before l2establish timer expiry.\n");
				}
				mISDNport->l2link = 1;
				break;

				case DL_RELEASE | INDICATION:
				case DL_RELEASE | CONFIRM:
//				PDEBUG(DEBUG_ISDN, "addr 0x%x released data link (DL) TE portnum=%d (DL_RELEASE)\n", frm->addr, mISDNport->portnum);
				if (!mISDNport->ntmode) break; /* !!!!!!!!!!!!!!!! */
				ss_rel:
//				if (mISDNport->ntmode)
//					break;
				mISDNport->l2link = 0;
				if (mISDNport->ptp)
				{
					time(&mISDNport->l2establish);
					PDEBUG(DEBUG_ISDN, "because we are ptp, we set a l2establish timer.\n");
				}
				break;

				default:
				l1_msg:
				PDEBUG(DEBUG_STACK, "GOT d-msg from %s port %d prim 0x%x dinfo 0x%x addr 0x%x\n", (mISDNport->ntmode)?"NT":"TE", mISDNport->portnum, frm->prim, frm->dinfo, frm->addr);
				if (frm->dinfo==(signed long)0xffffffff && frm->prim==(PH_DATA|CONFIRM))
				{
					PERROR("SERIOUS BUG, dinfo == 0xffffffff, prim == PH_DATA | CONFIRM !!!!\n");
				}
				/* d-message */
				if (mISDNport->ntmode)
				{
					/* l1-data enters the nt-mode library */
					nst = &mISDNport->nst;
					if (nst->l1_l2(nst, msg))
						free_msg(msg);
					return(1);
				} else
				{
					/* l3-data is sent to pbx */
					if (stack2manager_te(mISDNport, msg))
						free_msg(msg);
					return(1);
				}
			}
			break;
		}
//PDEBUG(DEBUG_ISDN, "flg:%d upper_id=%x addr=%x\n", (frm->addr&FLG_CHILD_STACK), (mISDNport->b_addr[0])&(~IF_CHILDMASK), (frm->addr)&(~IF_CHILDMASK));
		/* check if child, and if parent stack match */
		if ((frm->addr&FLG_CHILD_STACK) && (((unsigned int)(mISDNport->b_addr[0])&(~CHILD_ID_MASK)&STACK_ID_MASK) == ((frm->addr)&(~CHILD_ID_MASK)&STACK_ID_MASK)))
		{
			/* b-message */
			switch(frm->prim)
			{
				/* we don't care about confirms, we use rx data to sync tx */
				case PH_DATA | CONFIRM:
				case DL_DATA | CONFIRM:
				break;

				/* we receive audio data, we respond to it AND we send tones */
				case PH_DATA | INDICATION:
				case DL_DATA | INDICATION:
				case PH_CONTROL | INDICATION:
				i = 0;
				while(i < mISDNport->b_num)
				{
					if ((unsigned int)(mISDNport->b_addr[i]&STACK_ID_MASK) == (frm->addr&STACK_ID_MASK))
						break;
					i++;
				}
				if (i == mISDNport->b_num)
				{
					PERROR("unhandled b-message (address 0x%x).\n", frm->addr);
					break;
				}
				if (mISDNport->b_port[i])
				{
//PERROR("port sech: %s data\n", mISDNport->b_port[i]->p_name);
					mISDNport->b_port[i]->bchannel_receive(frm);
				} else
					PDEBUG(DEBUG_BCHANNEL, "b-channel is not associated to an ISDNPort (address 0x%x), ignoring.\n", frm->addr);
				break;

				case PH_ACTIVATE | INDICATION:
				case DL_ESTABLISH | INDICATION:
				case PH_ACTIVATE | CONFIRM:
				case DL_ESTABLISH | CONFIRM:
				PDEBUG(DEBUG_BCHANNEL, "DL_ESTABLISH confirm: bchannel is now activated (address 0x%x).\n", frm->addr);
				i = 0;
				while(i < mISDNport->b_num)
				{
					if ((unsigned int)(mISDNport->b_addr[i]&STACK_ID_MASK) == (frm->addr&STACK_ID_MASK))
						break;
					i++;
				}
				if (i == mISDNport->b_num)
				{
					PERROR("unhandled b-establish (address 0x%x).\n", frm->addr);
					break;
				}
				mISDNport->b_state[i] = B_STATE_ACTIVE;
				if (!mISDNport->b_port[i])
					bchannel_deactivate(mISDNport, i);
				else
					bchannel_activate(mISDNport, i);
				break;

				case PH_DEACTIVATE | INDICATION:
				case DL_RELEASE | INDICATION:
				case PH_DEACTIVATE | CONFIRM:
				case DL_RELEASE | CONFIRM:
				PDEBUG(DEBUG_BCHANNEL, "DL_RELEASE confirm: bchannel is now de-activated (address 0x%x).\n", frm->addr);
				i = 0;
				while(i < mISDNport->b_num)
				{
					if ((unsigned int)(mISDNport->b_addr[i]&STACK_ID_MASK) == (frm->addr&STACK_ID_MASK))
						break;
					i++;
				}
				if (i == mISDNport->b_num)
				{
					PERROR("unhandled b-release (address 0x%x).\n", frm->addr);
					break;
				}
				mISDNport->b_state[i] = B_STATE_IDLE;
				if (mISDNport->b_port[i])
					bchannel_activate(mISDNport, i);
				else
					bchannel_deactivate(mISDNport, i);
				break;
			}
			break;
		}

		mISDNport = mISDNport->next;
	} 
	if (!mISDNport)
	{
		if (frm->prim == (MGR_TIMER | INDICATION))
			PERROR("unhandled timer indication message: prim(0x%x) addr(0x%x) msg->len(%d)\n", frm->prim, frm->addr, msg->len);
		else
			PERROR("unhandled message: prim(0x%x) addr(0x%x) msg->len(%d)\n", frm->prim, frm->addr, msg->len);
//		PERROR("test: is_child: %x  of stack %x == %x (baddr %x frm %x)\n", (frm->addr&FLG_CHILD_STACK), ((unsigned int)(mISDNport_first->b_addr[0])&(~CHILD_ID_MASK)&STACK_ID_MASK), ((frm->addr)&(~CHILD_ID_MASK)&STACK_ID_MASK), mISDNport_first->b_addr[0], frm->addr);
	}

	free_msg(msg);
	return(1);
}


/*
 * global function to add a new card (port)
 */
struct mISDNport *mISDN_port_open(int port, int ptp, int ptmp)
{
	int ret;
	unsigned char buff[1025];
	iframe_t *frm = (iframe_t *)buff;
	stack_info_t *stinf;
	struct mISDNport *mISDNport, **mISDNportp;
	int i, cnt;
	layer_info_t li;
//	interface_info_t ii;
	net_stack_t *nst;
	manager_t *mgr;
	mISDN_pid_t pid;
	int pri = 0;
	int nt = 0;
	iframe_t dact;

	/* open mISDNdevice if not already open */
	if (mISDNdevice < 0)
	{
		ret = mISDN_open();
		if (ret < 0)
		{
			PERROR("cannot open mISDN device ret=%d errno=%d (%s) Check for mISDN modules!\nAlso did you create \"/dev/mISDN\"? Do: \"mknod /dev/mISDN c 46 0\"\n", ret, errno, strerror(errno));
			return(NULL);
		}
		mISDNdevice = ret;
		PDEBUG(DEBUG_ISDN, "mISDN device opened.\n");

		/* create entity for layer 3 TE-mode */
		mISDN_write_frame(mISDNdevice, buff, 0, MGR_NEWENTITY | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);
		ret = mISDN_read_frame(mISDNdevice, frm, sizeof(iframe_t), 0, MGR_NEWENTITY | CONFIRM, TIMEOUT_1SEC);
		if (ret < (int)mISDN_HEADER_LEN)
		{
			noentity:
			fprintf(stderr, "cannot request MGR_NEWENTITY from mISDN. Exitting due to software bug.");
			exit(-1);
		}
		entity = frm->dinfo & 0xffff;
		if (!entity)
			goto noentity;
		PDEBUG(DEBUG_ISDN, "our entity for l3-processes is %d.\n", entity);
	}

	/* query port's requirements */
	cnt = mISDN_get_stack_count(mISDNdevice);
	if (cnt <= 0)
	{
		PERROR("Found no card. Please be sure to load card drivers.\n");
		return(NULL);
	}
	if (port>cnt || port<1)
	{
		PERROR("Port (%d) given at 'ports' (options.conf) is out of existing port range (%d-%d)\n", port, 1, cnt);
		return(NULL);
	}
	ret = mISDN_get_stack_info(mISDNdevice, port, buff, sizeof(buff));
	if (ret < 0)
	{
		PERROR("Cannot get stack info for port %d (ret=%d)\n", port, ret);
		return(NULL);
	}
	stinf = (stack_info_t *)&frm->data.p;
	switch(stinf->pid.protocol[0] & ~ISDN_PID_FEATURE_MASK)
	{
		case ISDN_PID_L0_TE_S0:
		PDEBUG(DEBUG_ISDN, "TE-mode BRI S/T interface line\n");
		break;
		case ISDN_PID_L0_NT_S0:
		PDEBUG(DEBUG_ISDN, "NT-mode BRI S/T interface port\n");
		nt = 1;
		break;
		case ISDN_PID_L0_TE_U:
		PDEBUG(DEBUG_ISDN, "TE-mode BRI U   interface line\n");
		break;
		case ISDN_PID_L0_NT_U:
		PDEBUG(DEBUG_ISDN, "NT-mode BRI U   interface port\n");
		nt = 1;
		break;
		case ISDN_PID_L0_TE_UP2:
		PDEBUG(DEBUG_ISDN, "TE-mode BRI Up2 interface line\n");
		break;
		case ISDN_PID_L0_NT_UP2:
		PDEBUG(DEBUG_ISDN, "NT-mode BRI Up2 interface port\n");
		nt = 1;
		break;
		case ISDN_PID_L0_TE_E1:
		PDEBUG(DEBUG_ISDN, "TE-mode PRI E1  interface line\n");
		pri = 1;
		break;
		case ISDN_PID_L0_NT_E1:
		PDEBUG(DEBUG_ISDN, "LT-mode PRI E1  interface port\n");
		pri = 1;
		nt = 1;
		break;
		default:
		PERROR("unknown port(%d) type 0x%08x\n", port, stinf->pid.protocol[0]);
		return(NULL);
	}
	if (nt)
	{
		/* NT */
		if (stinf->pid.protocol[1] == 0)
		{
			PERROR("Given port %d: Missing layer 1 NT-mode protocol.\n", port);
			return(NULL);
		}
		if (stinf->pid.protocol[2])
		{
			PERROR("Given port %d: Layer 2 protocol 0x%08x is detected, but not allowed for NT lib.\n", port, stinf->pid.protocol[2]);
			return(NULL);
		}
	} else
	{
		/* TE */
		if (stinf->pid.protocol[1] == 0)
		{
			PERROR("Given port %d: Missing layer 1 protocol.\n", port);
			return(NULL);
		}
		if (stinf->pid.protocol[2] == 0)
		{
			PERROR("Given port %d: Missing layer 2 protocol.\n", port);
			return(NULL);
		}
		if (stinf->pid.protocol[3] == 0)
		{
			PERROR("Given port %d: Missing layer 3 protocol.\n", port);
			return(NULL);
		} else
		{
			switch(stinf->pid.protocol[3] & ~ISDN_PID_FEATURE_MASK)
			{
				case ISDN_PID_L3_DSS1USER:
				break;

				default:
				PERROR("Given port %d: own protocol 0x%08x", port,stinf->pid.protocol[3]);
				return(NULL);
			}
		}
		if (stinf->pid.protocol[4])
		{
			PERROR("Given port %d: Layer 4 protocol not allowed.\n", port);
			return(NULL);
		}
	}

	/* add mISDNport structure */
	mISDNportp = &mISDNport_first;
	while(*mISDNportp)
		mISDNportp = &mISDNport->next;
	mISDNport = (struct mISDNport *)calloc(1, sizeof(struct mISDNport));
	if (!mISDNport)
	{
		PERROR("Cannot alloc mISDNport structure\n");
		return(NULL);
	}
	pmemuse++;
	memset(mISDNport, 0, sizeof(mISDNport));
	*mISDNportp = mISDNport;

	/* allocate ressources of port */
	msg_queue_init(&mISDNport->downqueue);
//	SCPY(mISDNport->name, "noname");
	mISDNport->portnum = port;
	mISDNport->ntmode = nt;
	mISDNport->pri = pri;
	mISDNport->d_stid = stinf->id;
	PDEBUG(DEBUG_ISDN, "d_stid = 0x%x.\n", mISDNport->d_stid);
	mISDNport->b_num = stinf->childcnt;
	PDEBUG(DEBUG_ISDN, "Port has %d b-channels.\n", mISDNport->b_num);
	if ((stinf->pid.protocol[2]&ISDN_PID_L2_DF_PTP) || (nt&&ptp) || pri)
	{
		PDEBUG(DEBUG_ISDN, "Port is point-to-point.\n");
		mISDNport->ptp = ptp = 1;
		if (ptmp && nt)
		{
			PDEBUG(DEBUG_ISDN, "Port is forced to point-to-multipoint.\n");
			mISDNport->ptp = ptp = 0;
		}
	}
	i = 0;
	while(i < stinf->childcnt)
	{
		mISDNport->b_stid[i] = stinf->child[i];
		PDEBUG(DEBUG_ISDN, "b_stid[%d] = 0x%x.\n", i, mISDNport->b_stid[i]);
		i++;
	}
	memset(&li, 0, sizeof(li));
	UCPY(&li.name[0], (nt)?"net l2":"pbx l4");
	li.object_id = -1;
	li.extentions = 0;
	li.pid.protocol[nt?2:4] = (nt)?ISDN_PID_L2_LAPD_NET:ISDN_PID_L4_CAPI20;
	li.pid.layermask = ISDN_LAYER((nt?2:4));
	li.st = mISDNport->d_stid;
	ret = mISDN_new_layer(mISDNdevice, &li);
	if (ret)
	{
		PERROR("Cannot add layer %d of port %d (ret %d)\n", nt?2:4, port, ret);
		closeport:
		mISDNport_close(mISDNport);
		return(NULL);
	}
	mISDNport->upper_id = li.id;
	ret = mISDN_register_layer(mISDNdevice, mISDNport->d_stid, mISDNport->upper_id);
	if (ret)
	{
		PERROR("Cannot register layer %d of port %d\n", nt?2:4, port);
		goto closeport;
	}
	mISDNport->lower_id = mISDN_get_layerid(mISDNdevice, mISDNport->d_stid, nt?1:3); // id of lower layer (nt=1, te=3)
	if (mISDNport->lower_id < 0)
	{
		PERROR("Cannot get layer(%d) id of port %d\n", nt?1:3, port);
		goto closeport;
	}
	mISDNport->upper_id = mISDN_get_layerid(mISDNdevice, mISDNport->d_stid, nt?2:4); // id of uppermost layer (nt=2, te=4)
	if (mISDNport->upper_id < 0)
	{
		PERROR("Cannot get layer(%d) id of port %d\n", nt?2:4, port);
		goto closeport;
	}
	PDEBUG(DEBUG_ISDN, "Layer %d of port %d added.\n", nt?2:4, port);

	/* if ntmode, establish L1 to send the tei removal during start */
	if (mISDNport->ntmode)
	{
		iframe_t act;
		/* L1 */
		act.prim = PH_ACTIVATE | REQUEST; 
		act.addr = mISDNport->upper_id | FLG_MSG_DOWN;
		printf("UPPER ID 0x%x, addr 0x%x\n",mISDNport->upper_id, act.addr);
		act.dinfo = 0;
		act.len = 0;
		mISDN_write(mISDNdevice, &act, mISDN_HEADER_LEN+act.len, TIMEOUT_1SEC);
		usleep(10000); /* to be sure, that l1 is up */
	}

	/* create nst (nt-mode only) */
	if (nt)
	{
		mgr = &mISDNport->mgr;
		nst = &mISDNport->nst;

		mgr->nst = nst;
		nst->manager = mgr;

		nst->l3_manager = stack2manager_nt; /* messages from nt-mode */
		nst->device = mISDNdevice;
		nst->cardnr = port;
		nst->d_stid = mISDNport->d_stid;

		nst->feature = FEATURE_NET_HOLD;
		if (ptp)
			nst->feature |= FEATURE_NET_PTP;
		if (pri)
			nst->feature |= FEATURE_NET_CRLEN2 | FEATURE_NET_EXTCID;
#if 0
		i = 0;
		while(i < mISDNport->b_num)
		{
			nst->b_stid[i] = mISDNport->b_stid[i];
			i++;
		}
#endif
		nst->l1_id = mISDNport->lower_id;
		nst->l2_id = mISDNport->upper_id;

		/* phd */	
		msg_queue_init(&nst->down_queue);

		Isdnl2Init(nst);
		Isdnl3Init(nst);
	}

	/* if te-mode, query state link */
	if (!mISDNport->ntmode)
	{
		iframe_t act;
		/* L2 */
		PDEBUG(DEBUG_ISDN, "sending short status request for port %d.\n", port);
		act.prim = MGR_SHORTSTATUS | REQUEST; 
		act.addr = mISDNport->upper_id | MSG_BROADCAST;
		act.dinfo = SSTATUS_BROADCAST_BIT | SSTATUS_ALL;
		act.len = 0;
		mISDN_write(mISDNdevice, &act, mISDN_HEADER_LEN+act.len, TIMEOUT_1SEC);
	}
	/* if ptp AND te-mode, pull up the link */
	if (mISDNport->ptp && !mISDNport->ntmode)
	{
		iframe_t act;
		/* L2 */
		act.prim = DL_ESTABLISH | REQUEST; 
		act.addr = (mISDNport->upper_id & ~LAYER_ID_MASK) | 4 | FLG_MSG_DOWN;
		act.dinfo = 0;
		act.len = 0;
		mISDN_write(mISDNdevice, &act, mISDN_HEADER_LEN+act.len, TIMEOUT_1SEC);
	}
	/* if ptp AND nt-mode, pull up the link */
	if (mISDNport->ptp && mISDNport->ntmode)
	{
		msg_t *dmsg;
		/* L2 */
		dmsg = create_l2msg(DL_ESTABLISH | REQUEST, 0, 0);
		if (mISDNport->nst.manager_l3(&mISDNport->nst, dmsg))
			free_msg(dmsg);
	}
	/* initially, we assume that the link is down, exept for nt-ptmp */
	mISDNport->l2link = (mISDNport->ntmode && !mISDNport->ptp)?1:0;

	PDEBUG(DEBUG_BCHANNEL, "using 'mISDN_dsp.o' module\n");

	/* add all bchannel layers */
	i = 0;
	while(i < mISDNport->b_num)
	{
		/* create new layer */
		PDEBUG(DEBUG_BCHANNEL, "creating bchannel %d (index %d).\n" , i+1+(i>=15), i);
		memset(&li, 0, sizeof(li));
		memset(&pid, 0, sizeof(pid));
		li.object_id = -1;
		li.extentions = 0;
		li.st = mISDNport->b_stid[i];
		UCPY(li.name, "B L4");
		li.pid.layermask = ISDN_LAYER((4));
		li.pid.protocol[4] = ISDN_PID_L4_B_USER;
		ret = mISDN_new_layer(mISDNdevice, &li);
		if (ret)
		{
			failed_new_layer:
			PERROR("mISDN_new_layer() failed to add bchannel %d (index %d)\n", i+1+(i>=15), i);
			goto closeport;
		}
		mISDNport->b_addr[i] = li.id;
		if (!li.id)
		{
			goto failed_new_layer;
		}
		PDEBUG(DEBUG_BCHANNEL, "new layer (b_addr=0x%x)\n", mISDNport->b_addr[i]);

		/* create new stack */
		pid.protocol[1] = ISDN_PID_L1_B_64TRANS;
		pid.protocol[2] = ISDN_PID_L2_B_TRANS;
		pid.protocol[3] = ISDN_PID_L3_B_DSP;
		pid.protocol[4] = ISDN_PID_L4_B_USER;
		pid.layermask = ISDN_LAYER((1)) | ISDN_LAYER((2)) | ISDN_LAYER((3)) | ISDN_LAYER((4));
		ret = mISDN_set_stack(mISDNdevice, mISDNport->b_stid[i], &pid);
		if (ret)
		{
			stack_error:
			PERROR("mISDN_set_stack() failed (ret=%d) to add bchannel (index %d) stid=0x%x\n", ret, i, mISDNport->b_stid[i]);
			mISDN_write_frame(mISDNdevice, buff, mISDNport->b_addr[i], MGR_DELLAYER | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);
			mISDNport->b_addr[i] = 0;
//			mISDNport->b_addr_low[i] = 0;
			goto closeport;
		}
		ret = mISDN_get_setstack_ind(mISDNdevice, mISDNport->b_addr[i]);
		if (ret)
			goto stack_error;

		/* get layer id */
		mISDNport->b_addr[i] = mISDN_get_layerid(mISDNdevice, mISDNport->b_stid[i], 4);
		if (!mISDNport->b_addr[i])
			goto stack_error;
		/* deactivate bchannel if already enabled due to crash */
		PDEBUG(DEBUG_BCHANNEL, "deactivating bchannel (index %d) as a precaution.\n", i);
		dact.prim = DL_RELEASE | REQUEST; 
		dact.addr = mISDNport->b_addr[i] | FLG_MSG_DOWN;
		dact.dinfo = 0;
		dact.len = 0;
		mISDN_write(mISDNdevice, &dact, mISDN_HEADER_LEN+dact.len, TIMEOUT_1SEC);

		i++;
	}
	PDEBUG(DEBUG_ISDN, "- port %d %s (%s) %d b-channels\n", mISDNport->portnum, (mISDNport->ntmode)?"NT-mode":"TE-mode", (mISDNport->ptp)?"Point-To-Point":"Multipoint", mISDNport->b_num);
	printlog("opening port %d %s (%s) %d b-channels\n", mISDNport->portnum, (mISDNport->ntmode)?"NT-mode":"TE-mode", (mISDNport->ptp)?"Point-To-Point":"Multipoint", mISDNport->b_num);
	return(mISDNport);
}


/*
 * function to free ALL cards (ports)
 */
void mISDNport_close_all(void)
{
	/* free all ports */
	while(mISDNport_first)
		mISDNport_close(mISDNport_first);
}

/*
 * free only one port
 */
void mISDNport_close(struct mISDNport *mISDNport)
{
	struct mISDNport **mISDNportp;
	class Port *port;
	classs PmISDN *pmISDN;
	net_stack_t *nst;
	unsigned char buf[32];
	int i;

	/* remove all port instance that are linked to this mISDNport */
	port = port_first;
	while(port)
	{
		if ((port->p_type&PORT_CLASS_MASK) == PORT_CLASS_mISDN)
		{
			pmISDN = (class PmISDN)*port;
			if (pmISDN->p_m_mISDNport)
			{
				PDEBUG(DEBUG_ISDN, "port %s uses mISDNport %d, destroying it.\n", pnISDN->p_name, mISDNport->portnum);
				delete pmISDN;
			}
		}
		port = port->next;
	}

	printlog("closing port %d\n", mISDNport->portnum);

	/* free bchannels */
	i = 0;
	while(i < mISDNport->b_num)
	{
		bchannel_deactivate(mISDNport, i);
		PDEBUG(DEBUG_BCHANNEL, "freeing %s port %d bchannel (index %d).\n", (mISDNport->ntmode)?"NT":"TE", mISDNport->portnum, i);
		if (mISDNport->b_stid[i])
		{
			mISDN_clear_stack(mISDNdevice, mISDNport->b_stid[i]);
			if (mISDNport->b_addr[i])
				mISDN_write_frame(mISDNdevice, buf, mISDNport->b_addr[i] | FLG_MSG_DOWN, MGR_DELLAYER | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);
		}
		i++;
	}

	/* free ressources of port */
	msg_queue_purge(&mISDNport->downqueue);

	/* free stacks */
	if (mISDNport->ntmode)
	{
		nst = &mISDNport->nst;
		if (nst->manager) /* to see if initialized */
		{
			PDEBUG(DEBUG_STACK, "the following messages are ok: one L3 process always exists (broadcast process) and some L2 instances (broadcast + current telephone's instances)\n");
			cleanup_Isdnl3(nst);
			cleanup_Isdnl2(nst);

			/* phd */
			msg_queue_purge(&nst->down_queue);
			if (nst->phd_down_msg)
				free(nst->phd_down_msg);
		}
	}

	PDEBUG(DEBUG_BCHANNEL, "freeing d-stack.\n");
	if (mISDNport->d_stid)
	{
//		mISDN_clear_stack(mISDNdevice, mISDNport->d_stid);
		if (mISDNport->lower_id)
			mISDN_write_frame(mISDNdevice, buf, mISDNport->lower_id, MGR_DELLAYER | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);
	}

	/* remove from list */
	mISDNportp = &mISDNport_first;
	while(*mISDNportp)
	{
		if (*mISDNportp == mISDNport)
		{
			*mISDNportp = (*mISDNportp)->next;
			break;
		}
		mISDNportp = &((*mISDNportp)->next);
	}

	if (!(*mISDNportp))
	{
		PERROR("software error, mISDNport not in list\n");
		exit(-1);
	}
	
	memset(mISDNport, 0, sizeof(struct mISDNport));
	free(mISDNport);
	pmemuse--;

	/* close mISDNdevice, if no port */
	if (mISDNdevice>=0 && mISDNport_first==NULL)
	{
		/* free entity */
		mISDN_write_frame(mISDNdevice, buf, 0, MGR_DELENTITY | REQUEST, entity, 0, NULL, TIMEOUT_1SEC);
		/* close device */
		mISDN_close(mISDNdevice);
		mISDNdevice = -1;
		PDEBUG(DEBUG_ISDN, "mISDN device closed.\n");
	}
}


/*
 * global function to show all available isdn ports
 */
void mISDN_port_info(void)
{
	int err;
	int i, ii, p;
	int useable, nt, pri;
	unsigned char buff[1025];
	iframe_t *frm = (iframe_t *)buff;
	stack_info_t *stinf;
	int device;

	/* open mISDN */
	if ((device = mISDN_open()) < 0)
	{
		fprintf(stderr, "cannot open mISDN device ret=%d errno=%d (%s) Check for mISDN modules!\nAlso did you create \"/dev/mISDN\"? Do: \"mknod /dev/mISDN c 46 0\"\n", device, errno, strerror(errno));
		exit(-1);
	}

	/* get number of stacks */
	i = 1;
	ii = mISDN_get_stack_count(device);
	printf("\n");
	if (ii <= 0)
	{
		printf("Found no card. Please be sure to load card drivers.\n");
	}

	/* loop the number of cards and get their info */
	while(i <= ii)
	{
		err = mISDN_get_stack_info(device, i, buff, sizeof(buff));
		if (err <= 0)
		{
			fprintf(stderr, "mISDN_get_stack_info() failed: port=%d err=%d\n", i, err);
			break;
		}
		stinf = (stack_info_t *)&frm->data.p;

		nt = pri = 0;
		useable = 1;

		/* output the port info */
		printf("Port %2d: ", i);
		switch(stinf->pid.protocol[0] & ~ISDN_PID_FEATURE_MASK)
		{
			case ISDN_PID_L0_TE_S0:
			printf("TE-mode BRI S/T interface line (for phone lines)");
#if 0
			if (stinf->pid.protocol[0] & ISDN_PID_L0_TE_S0_HFC & ISDN_PID_FEATURE_MASK)
				printf(" HFC multiport card");
#endif
			break;
			case ISDN_PID_L0_NT_S0:
			nt = 1;
			printf("NT-mode BRI S/T interface port (for phones)");
#if 0
			if (stinf->pid.protocol[0] & ISDN_PID_L0_NT_S0_HFC & ISDN_PID_FEATURE_MASK)
				printf(" HFC multiport card");
#endif
			break;
			case ISDN_PID_L0_TE_U:
			printf("TE-mode BRI U   interface line");
			break;
			case ISDN_PID_L0_NT_U:
			nt = 1;
			printf("NT-mode BRI U   interface port");
			break;
			case ISDN_PID_L0_TE_UP2:
			printf("TE-mode BRI Up2 interface line");
			break;
			case ISDN_PID_L0_NT_UP2:
			nt = 1;
			printf("NT-mode BRI Up2 interface port");
			break;
			case ISDN_PID_L0_TE_E1:
			pri = 1;
			printf("TE-mode PRI E1  interface line (for phone lines)");
#if 0
			if (stinf->pid.protocol[0] & ISDN_PID_L0_TE_E1_HFC & ISDN_PID_FEATURE_MASK)
				printf(" HFC-E1 card");
#endif
			break;
			case ISDN_PID_L0_NT_E1:
			nt = 1;
			pri = 1;
			printf("NT-mode PRI E1  interface port (for phones)");
#if 0
			if (stinf->pid.protocol[0] & ISDN_PID_L0_NT_E1_HFC & ISDN_PID_FEATURE_MASK)
				printf(" HFC-E1 card");
#endif
			break;
			default:
			useable = 0;
			printf("unknown type 0x%08x",stinf->pid.protocol[0]);
		}
		printf("\n");

		if (nt)
		{
			if (stinf->pid.protocol[1] == 0)
			{
				useable = 0;
				printf(" -> Missing layer 1 NT-mode protocol.\n");
			}
			p = 2;
			while(p <= MAX_LAYER_NR) {
				if (stinf->pid.protocol[p])
				{
					useable = 0;
					printf(" -> Layer %d protocol 0x%08x is detected, but not allowed for NT lib.\n", p, stinf->pid.protocol[p]);
				}
				p++;
			}
			if (useable)
			{
				if (pri)
					printf(" -> Interface is Point-To-Point (PRI).\n");
				else
					printf(" -> Interface can be Poin-To-Point/Multipoint.\n");
			}
		} else
		{
			if (stinf->pid.protocol[1] == 0)
			{
				useable = 0;
				printf(" -> Missing layer 1 protocol.\n");
			}
			if (stinf->pid.protocol[2] == 0)
			{
				useable = 0;
				printf(" -> Missing layer 2 protocol.\n");
			}
			if (stinf->pid.protocol[2] & ISDN_PID_L2_DF_PTP)
			{
				printf(" -> Interface is Poin-To-Point.\n");
			}
			if (stinf->pid.protocol[3] == 0)
			{
				useable = 0;
				printf(" -> Missing layer 3 protocol.\n");
			} else
			{
				printf(" -> Protocol: ");
				switch(stinf->pid.protocol[3] & ~ISDN_PID_FEATURE_MASK)
				{
					case ISDN_PID_L3_DSS1USER:
					printf("DSS1 (Euro ISDN)");
					break;

					default:
					useable = 0;
					printf("unknown protocol 0x%08x",stinf->pid.protocol[3]);
				}
				printf("\n");
			}
			p = 4;
			while(p <= MAX_LAYER_NR) {
				if (stinf->pid.protocol[p])
				{
					useable = 0;
					printf(" -> Layer %d protocol 0x%08x is detected, but not allowed for TE lib.\n", p, stinf->pid.protocol[p]);
				}
				p++;
			}
		}
		printf("  - %d B-channels\n", stinf->childcnt);

		if (!useable)
			printf(" * Port NOT useable for PBX\n");

		printf("--------\n");

		i++;
	}
	printf("\n");

	/* close mISDN */
	if ((err = mISDN_close(device)))
	{
		fprintf(stderr, "mISDN_close() failed: err=%d '%s'\n", err, strerror(err));
		exit(-1);
	}
}


