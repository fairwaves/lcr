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
#ifdef SOCKET_MISDN
#include <netinet/udp.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#include <pthread.h>
#else
#include "mISDNlib.h"
#include <linux/mISDNif.h>
#endif
#include "bchannel.h"

#ifndef ISDN_PID_L4_B_USER
#define ISDN_PID_L4_B_USER 0x440000ff
#endif

#define PERROR(arg...) fprintf(stderr, ##arg)
#define PDEBUG(arg...) while(0)

pid_t	bchannel_pid;

enum {
	BSTATE_IDLE,
	BSTATE_ACTIVATING,
	BSTATE_ACTIVE,
};

#ifdef MISDN_SOCKET
int bchannel_socket = -1;

int bchannel_initialize(void)
{
	/* try to open raw socket to check kernel */
	bchannel_socket = socket(PF_ISDN, SOCK_RAW, ISDN_P_BASE);
	if (bchannel_socket < 0)
	{
		PERROR("Cannot open mISDN due to %s. (Does your Kernel support socket based mISDN?)\n", strerror(errno));
		return(-1);
	}

	mISDN_debug_init(global_debug, NULL, NULL, NULL);

	bchannel_pid = get_pid();

	/* init mlayer3 */
	init_layer3(4); // buffer of 4

	return(0);
}

void bchannel_deinitialize(void)
{
	cleanup_layer3();

	mISDN_debug_close();

	if (bchannel_socket > -1)
		close(bchannel_socket);
}
#else
int bchannel_entity = 0; /* used for udevice */
int bchannel_device = -1; /* the device handler and port list */

int bchannel_initialize(void)
{
	char debug_log[128];
	unsigned char buff[1025];
	iframe_t *frm = (iframe_t *)buff;
	int ret;

	/* open mISDNdevice if not already open */
	if (bchannel_device < 0)
	{
		ret = mISDN_open();
		if (ret < 0)
		{
			PERROR("cannot open mISDN device ret=%d errno=%d (%s) Check for mISDN modules!\nAlso did you create \"/dev/mISDN\"? Do: \"mknod /dev/mISDN c 46 0\"\n", ret, errno, strerror(errno));
			return(-1);
		}
		bchannel_device = ret;
		PDEBUG("mISDN device opened.\n");

		/* create entity for layer 3 TE-mode */
		mISDN_write_frame(bchannel_device, buff, 0, MGR_NEWENTITY | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);
		ret = mISDN_read_frame(bchannel_device, frm, sizeof(iframe_t), 0, MGR_NEWENTITY | CONFIRM, TIMEOUT_1SEC);
		if (ret < (int)mISDN_HEADER_LEN)
		{
			noentity:
			PERROR("Cannot request MGR_NEWENTITY from mISDN. Exitting due to software bug.");
			return(-1);
		}
		bchannel_entity = frm->dinfo & 0xffff;
		if (!bchannel_entity)
			goto noentity;
	}
	return(0);
}

void bchannel_deinitialize(void)
{
	unsigned char buff[1025];

	if (bchannel_device >= 0)
	{
		/* free entity */
		mISDN_write_frame(bchannel_device, buff, 0, MGR_DELENTITY | REQUEST, bchannel_entity, 0, NULL, TIMEOUT_1SEC);
		/* close device */
		mISDN_close(bchannel_device);
		bchannel_device = -1;
	}
}
#endif

/*
 * send control information to the channel (dsp-module)
 */
static void ph_control(unsigned long handle, unsigned long c1, unsigned long c2, char *trace_name, int trace_value)
{
#ifdef SOCKET_MISDN
	unsigned char buffer[MISDN_HEADER_LEN+sizeof(int)+sizeof(int)];
	struct mISDNhead *ctrl = (struct mISDNhead *)buffer;
	unsigned long *d = buffer+MISDN_HEADER_LEN;
	int ret;

	ctrl->prim = PH_CONTROL_REQ;
	ctrl->id = 0;
	*d++ = c1;
	*d++ = c2;
	ret = sendto(handle, buffer, MISDN_HEADER_LEN+sizeof(int)*2, 0, NULL, 0);
	if (!ret)
		PERROR("Failed to send to socket %d\n", handle);
#else
	unsigned char buffer[mISDN_HEADER_LEN+sizeof(int)+sizeof(int)];
	iframe_t *ctrl = (iframe_t *)buffer; 
	unsigned long *d = (unsigned long *)&ctrl->data.p;

	ctrl->prim = PH_CONTROL | REQUEST;
	ctrl->addr = handle | FLG_MSG_DOWN;
	ctrl->dinfo = 0;
	ctrl->len = sizeof(int)*2;
	*d++ = c1;
	*d++ = c2;
	mISDN_write(bchannel_device, ctrl, mISDN_HEADER_LEN+ctrl->len, TIMEOUT_1SEC);
#endif
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
#ifdef SOCKET_MISDN
	unsigned char buffer[MISDN_HEADER_LEN+sizeof(int)+c2_len];
	struct mISDNhead *ctrl = (struct mISDNhead *)buffer;
	unsigned long *d = buffer+MISDN_HEADER_LEN;
	int ret;

	ctrl->prim = PH_CONTROL_REQ;
	ctrl->id = 0;
	*d++ = c1;
	memcpy(d, c2, c2_len);
	ret = sendto(handle, buffer, MISDN_HEADER_LEN+sizeof(int)+c2_len, 0, NULL, 0);
	if (!ret)
		PERROR("Failed to send to socket %d\n", handle);
#else
	unsigned char buffer[mISDN_HEADER_LEN+sizeof(int)+c2_len];
	iframe_t *ctrl = (iframe_t *)buffer;
	unsigned long *d = (unsigned long *)&ctrl->data.p;

	ctrl->prim = PH_CONTROL | REQUEST;
	ctrl->addr = handle | FLG_MSG_DOWN;
	ctrl->dinfo = 0;
	ctrl->len = sizeof(int)+c2_len;
	*d++ = c1;
	memcpy(d, c2, c2_len);
	mISDN_write(bchannel_device, ctrl, mISDN_HEADER_LEN+ctrl->len, TIMEOUT_1SEC);
#endif
#if 0
	chan_trace_header(mISDNport, isdnport, "BCHANNEL control", DIRECTION_OUT);
	add_trace(trace_name, NULL, "%d", trace_value);
	end_trace();
#endif
}


/*
 * create stack
 */
int bchannel_create(struct bchannel *channel)
{
	unsigned char buff[1024];
	int ret;
#ifdef SOCKET_MISDN
	unsigned long on = 1;
	struct sockadd_mISDN addr;

	if (channel->b_sock)
	{
		PERROR("Error: Socket already created for handle %d\n", channel->handle);
		return(0);
	}

	/* open socket */
	channel->b_sock = socket(PF_ISDN, SOCK_DGRAM, ISDN_P_B_L2DSP);
	if (channel->b_sock < 0)
	{
		PERROR("Error: Failed to open bchannel-socket for handle %d with mISDN-DSP layer. Did you load mISDNdsp.ko?\n", channel->handle);
		return(0);
	}
	
	/* set nonblocking io */
	ret = ioctl(channel->b_sock, FIONBIO, &on);
	if (ret < 0)
	{
		PERROR("Error: Failed to set bchannel-socket handle %d into nonblocking IO\n", channel->handle);
		close(channel->b_sock);
		channel->b_sock = -1;
		return(0);
	}

	/* bind socket to bchannel */
	addr.family = AF_ISDN;
	addr.dev = (channel->handle>>8)-1;
	addr.channel = channel->handle && 0xff;
	ret = bind(di->bchan, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0)
	{
		PERROR("Error: Failed to bind bchannel-socket for handle %d with mISDN-DSP layer. Did you load mISDNdsp.ko?\n", channel->handle);
		close(channel->b_sock);
		channel->b_sock = -1;
		return(0);
	}

#if 0
	chan_trace_header(mISDNport, mISDNport->b_port[i], "BCHANNEL create socket", DIRECTION_OUT);
	add_trace("channel", NULL, "%d", i+1+(i>=15));
	add_trace("socket", NULL, "%d", mISDNport->b_socket[i]);
	end_trace();
#endif
#else
	layer_info_t li;
	mISDN_pid_t pid;

	if (channel->b_stid)
	{
		PERROR("Error: stack already created for address 0x%x\n", channel->b_stid);
		return(0);
	}

	if (channel->b_addr)
	{
		PERROR("Error: stack already created for address 0x%x\n", channel->b_addr);
		return(0);
	}

	/* create new layer */
	PDEBUG("creating new layer for stid 0x%x.\n" , channel->handle);
	memset(&li, 0, sizeof(li));
	memset(&pid, 0, sizeof(pid));
	li.object_id = -1;
	li.extentions = 0;
	li.st = channel->handle;
	strcpy(li.name, "B L4");
	li.pid.layermask = ISDN_LAYER((4));
	li.pid.protocol[4] = ISDN_PID_L4_B_USER;
	ret = mISDN_new_layer(bchannel_device, &li);
	if (ret)
	{
		failed_new_layer:
		PERROR("mISDN_new_layer() failed to add bchannel for stid 0x%x.\n", channel->handle);
		goto failed;
	}
	if (!li.id)
	{
		goto failed_new_layer;
	}
	channel->b_stid = channel->handle;
	channel->b_addr = li.id;
	PDEBUG("new layer (b_addr=0x%x)\n", channel->b_addr);

	/* create new stack */
	pid.protocol[1] = ISDN_PID_L1_B_64TRANS;
	pid.protocol[2] = ISDN_PID_L2_B_TRANS;
	pid.protocol[3] = ISDN_PID_L3_B_DSP;
	pid.protocol[4] = ISDN_PID_L4_B_USER;
	pid.layermask = ISDN_LAYER((1)) | ISDN_LAYER((2)) | ISDN_LAYER((3)) | ISDN_LAYER((4));
	ret = mISDN_set_stack(bchannel_device, channel->b_stid, &pid);
	if (ret)
	{
		stack_error:
		PERROR("mISDN_set_stack() failed (ret=%d) to add bchannel stid=0x%x\n", ret, channel->b_stid);
		mISDN_write_frame(bchannel_device, buff, channel->b_addr, MGR_DELLAYER | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);
		goto failed;
	}
	ret = mISDN_get_setstack_ind(bchannel_device, channel->b_addr);
	if (ret)
		goto stack_error;

	/* get layer id */
	channel->b_addr = mISDN_get_layerid(bchannel_device, channel->b_stid, 4);
	if (!channel->b_addr)
		goto stack_error;
#if 0
	chan_trace_header(mISDNport, mISDNport->b_port[i], "BCHANNEL create stack", DIRECTION_OUT);
	add_trace("channel", NULL, "%d", i+1+(i>=15));
	add_trace("stack", "id", "0x%08x", mISDNport->b_stid[i]);
	add_trace("stack", "address", "0x%08x", mISDNport->b_addr[i]);
	end_trace();
#endif
#endif

	return(1);

failed:
	channel->b_stid = 0;
	channel->b_addr = 0;
	return(0);
}


/*
 * activate / deactivate request
 */
void bchannel_activate(struct bchannel *channel, int activate)
{
#ifdef SOCKET_MISDN
	struct mISDNhead act;
	int ret;

	act.prim = (activate)?DL_ESTABLISH_REQ:DL_RELEASE_REQ; 
	act.id = 0;
	ret = sendto(channel->b_sock, &act, MISDN_HEADER_LEN, 0, NULL, 0);
	if (!ret)
		PERROR("Failed to send to socket %d\n", channel->b_sock);
#else
	iframe_t act;

	/* activate bchannel */
	act.prim = (activate?DL_ESTABLISH:DL_RELEASE) | REQUEST; 
	act.addr = channel->b_addr | FLG_MSG_DOWN;
	act.dinfo = 0;
	act.len = 0;
	mISDN_write(bchannel_device, &act, mISDN_HEADER_LEN+act.len, TIMEOUT_1SEC);
#endif

	channel->b_state = BSTATE_ACTIVATING;
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
static void bchannel_activated(struct bchannel *channel)
{
#ifdef SOCKET_MISDN
	int handle;

	handle = channel->b_sock;
#else
	unsigned long handle;

	handle = channel->b_addr;
#endif

	/* set dsp features */
	if (channel->b_txdata)
		ph_control(handle, (channel->b_txdata)?CMX_TXDATA_ON:CMX_TXDATA_OFF, 0, "DSP-TXDATA", channel->b_txdata);
	if (channel->b_delay)
		ph_control(handle, CMX_DELAY, channel->b_delay, "DSP-DELAY", channel->b_delay);
	if (channel->b_tx_dejitter)
		ph_control(handle, (channel->b_tx_dejitter)?CMX_TX_DEJITTER:CMX_TX_DEJ_OFF, 0, "DSP-DELAY", channel->b_tx_dejitter);
	if (channel->b_tx_gain)
		ph_control(handle, VOL_CHANGE_TX, channel->b_tx_gain, "DSP-TX_GAIN", channel->b_tx_gain);
	if (channel->b_rx_gain)
		ph_control(handle, VOL_CHANGE_RX, channel->b_rx_gain, "DSP-RX_GAIN", channel->b_rx_gain);
	if (channel->b_pipeline[0])
		ph_control_block(handle, PIPELINE_CFG, channel->b_pipeline, strlen(channel->b_pipeline)+1, "DSP-PIPELINE", 0);
	if (channel->b_conf)
		ph_control(handle, CMX_CONF_JOIN, channel->b_conf, "DSP-CONF", channel->b_conf);
	if (channel->b_echo)
		ph_control(handle, CMX_ECHO_ON, 0, "DSP-ECHO", 1);
	if (channel->b_tone)
		ph_control(handle, TONE_PATT_ON, channel->b_tone, "DSP-TONE", channel->b_tone);
	if (channel->b_rxoff)
		ph_control(handle, CMX_RECEIVE_OFF, 0, "DSP-RXOFF", 1);
//	if (channel->b_txmix)
//		ph_control(handle, CMX_MIX_ON, 0, "DSP-MIX", 1);
	if (channel->b_dtmf)
		ph_control(handle, DTMF_TONE_START, 0, "DSP-DTMF", 1);
	if (channel->b_crypt_len)
		ph_control_block(handle, BF_ENABLE_KEY, channel->b_crypt_key, channel->b_crypt_len, "DSP-CRYPT", channel->b_crypt_len);

	channel->b_state = BSTATE_ACTIVE;
}

/*
 * destroy stack
 */
static void bchannel_destroy(struct bchannel *channel)
{
#ifdef SOCKET_MISDN
#if 0
	chan_trace_header(mISDNport, mISDNport->b_port[i], "BCHANNEL remove socket", DIRECTION_OUT);
	add_trace("channel", NULL, "%d", i+1+(i>=15));
	add_trace("socket", NULL, "%d", mISDNport->b_socket[i]);
	end_trace();
#endif
	if (channel->b_sock > -1)
	{
		close(channel->b_sock);
		channel->b_sock = -1;
	}
#else
	unsigned char buff[1024];

#if 0
	chan_trace_header(mISDNport, mISDNport->b_port[i], "BCHANNEL remove stack", DIRECTION_OUT);
	add_trace("channel", NULL, "%d", i+1+(i>=15));
	add_trace("stack", "id", "0x%08x", mISDNport->b_stid[i]);
	add_trace("stack", "address", "0x%08x", mISDNport->b_addr[i]);
	end_trace();
#endif
	/* remove our stack only if set */
	if (channel->b_addr)
	{
		PDEBUG("free stack (b_addr=0x%x)\n", channel->b_addr);
		mISDN_clear_stack(bchannel_device, channel->b_stid);
		mISDN_write_frame(bchannel_device, buff, channel->b_addr | FLG_MSG_DOWN, MGR_DELLAYER | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);
		channel->b_stid = 0;
		channel->b_addr = 0;
	}
#endif
	channel->b_state = BSTATE_IDLE;
}


/*
 * whenever we get audio data from bchannel, we process it here
 */
static void bchannel_receive(struct bchannel *channel, unsigned long prim, unsigned long dinfo, unsigned char *data, int len)
{
	unsigned long cont = *((unsigned long *)data);
	unsigned char *data_temp;
	unsigned long length_temp;
	unsigned char *p;
	int l;

	if (prim == (PH_CONTROL | INDICATION))
	{
		if (len < 4)
		{
			PERROR("SHORT READ OF PH_CONTROL INDICATION\n");
			return;
		}
		if ((cont&(~DTMF_TONE_MASK)) == DTMF_TONE_VAL)
		{
#if 0
			chan_trace_header(p_m_mISDNport, this, "BCHANNEL control", DIRECTION_IN);
			add_trace("DTMF", NULL, "%c", cont & DTMF_TONE_MASK);
			end_trace();
#endif
			if (channel->rx_dtmf)
				channel->rx_dtmf(channel, cont & DTMF_TONE_MASK);
			return;
		}
		switch(cont)
		{
			case BF_REJECT:
#if 0
			chan_trace_header(p_m_mISDNport, this, "BCHANNEL control", DIRECTION_IN);
			add_trace("DSP-CRYPT", NULL, "error");
			end_trace();
#endif
			break;

			case BF_ACCEPT:
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
	if (prim == (PH_SIGNAL | INDICATION))
	{
		switch(dinfo)
		{
			case CMX_TX_DATA:
			if (!channel->b_txdata)
			{
				/* if tx is off, it may happen that fifos send us pending informations, we just ignore them */
				PDEBUG("PmISDN(%s) ignoring tx data, because 'txdata' is turned off\n", p_name);
				return;
			}
			break;

			default:
#if 0
			chan_trace_header(p_m_mISDNport, this, "BCHANNEL signal", DIRECTION_IN);
			add_trace("unknown", NULL, "0x%x", frm->dinfo);
			end_trace();
#else
			;
#endif
		}
		return;
	}
	if (prim != PH_DATA_IND && prim != DL_DATA_IND)
	{
		PERROR("Bchannel received unknown primitve: 0x%x\n", prim);
		return;
	}
	/* calls will not process any audio data unless
	 * the call is connected OR interface features audio during call setup.
	 */

	/* if rx is off, it may happen that fifos send us pending informations, we just ignore them */
	if (channel->b_rxoff)
	{
		PDEBUG("PmISDN(%s) ignoring data, because rx is turned off\n", p_name);
		return;
	}

	if (channel->rx_data)
		channel->rx_data(channel, data, len);
}


/*
 * transmit data to bchannel
 */
void bchannel_transmit(struct bchannel *channel, unsigned char *data, int len)
{
	unsigned char buff[1025];
	iframe_t *frm = (iframe_t *)buff;

	if (channel->b_state != BSTATE_ACTIVE)
		return;
#ifdef SOCKET_MISDN
	frm->prim = DL_DATA_REQ;
	frm->id = 0;
	ret = sendto(channel->b_sock, data, len, 0, NULL, 0);
	if (!ret)
		PERROR("Failed to send to socket %d\n", channel->b_sock);
#else
	frm->prim = DL_DATA | REQUEST; 
	frm->addr = channel->b_addr | FLG_MSG_DOWN;
	frm->dinfo = 0;
	frm->len = len;
	if (frm->len)
		mISDN_write(bchannel_device, frm, mISDN_HEADER_LEN+frm->len, TIMEOUT_1SEC);
#endif
}


/*
 * join bchannel
 */
void bchannel_join(struct bchannel *channel, unsigned short id)
{
#ifdef SOCKET_MISDN
	int handle;

	handle = channel->b_sock;
#else
	unsigned long handle;

	handle = channel->b_addr;
#endif
	if (id)
		channel->b_conf = (id<<16) + bchannel_pid;
	else
		channel->b_conf = 0;
	if (channel->b_state == BSTATE_ACTIVE)
		ph_control(handle, CMX_CONF_JOIN, channel->b_conf, "DSP-CONF", channel->b_conf);
}


/*
 * main loop for processing messages from mISDN
 */
#ifdef SOCKET_MISDN
int bchannel_handle(void)
{
	int ret, work = 0;
	struct bchannel *channel;
	int i;
	char buffer[2048+MISDN_HEADER_LEN];
	struct mISDNhead *hh = (struct mISDNhead *)buffer;

	/* process all bchannels */
	channel = bchannel_first;
	while(channel)
	{
		/* handle message from bchannel */
		if (channel->b_sock > -1)
		{
			ret = recv(channel->b_sock, buffer, sizeof(buffer), 0);
			if (ret >= MISDN_HEADER_LEN)
			{
				work = 1;
				switch(hh->prim)
				{
					/* we don't care about confirms, we use rx data to sync tx */
					case PH_DATA_CONF:
					case DL_DATA_CONF:
					break;

					/* we receive audio data, we respond to it AND we send tones */
					case PH_DATA_IND:
					case DL_DATA_IND:
					case PH_SIGNAL_IND:
					case PH_CONTROL | INDICATION:
					bchannel_receive(channel, hh->prim, hh->dinfo, buffer+MISDN_HEADER_LEN, ret-MISDN_HEADER_LEN);
					break;

					case PH_ACTIVATE_IND:
					case DL_ESTABLISH_IND:
					case PH_ACTIVATE_CONF:
					case DL_ESTABLISH_CONF:
					PDEBUG("DL_ESTABLISH confirm: bchannel is now activated (socket %d).\n", channel->b_sock);
					bchannel_activated(channel);
					break;

					case PH_DEACTIVATE_IND:
					case DL_RELEASE_IND:
					case PH_DEACTIVATE_CONF:
					case DL_RELEASE_CONF:
					PDEBUG("DL_RELEASE confirm: bchannel is now de-activated (socket %d).\n", channel->b_sock);
//					bchannel_deactivated(channel);
					break;

					default:
					PERROR("child message not handled: prim(0x%x) socket(%d) msg->len(%d)\n", hh->prim, channel->b_sock, msg->len);
				}
			} else
			{
				if (ret < 0 && errno != EWOULDBLOCK)
					PERROR("Read from socket %d failed with return code %d\n", channel->b_sock, ret);
			}
		}
		channel = channel->next;
	}

	/* if we received at least one b-frame, we will return 1 */
	return(work);
}
#else
int bchannel_handle(void)
{
	int ret;
	int i;
	struct bchannel *channel;
	msg_t *msg;
	iframe_t *frm;
	msg_t *dmsg;
	mISDNuser_head_t *hh;
	net_stack_t *nst;

	/* no device, no read */
	if (bchannel_device < 0)
		return(0);

	/* get message from kernel */
	if (!(msg = alloc_msg(MAX_MSG_SIZE)))
		return(1);
	ret = mISDN_read(bchannel_device, msg->data, MAX_MSG_SIZE, 0);
	if (ret < 0)
	{
		free_msg(msg);
		if (errno == EAGAIN)
			return(0);
		FATAL("Failed to do mISDN_read()\n");
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
		case MGR_DELLAYER | CONFIRM:
		case MGR_INITTIMER | CONFIRM:
		case MGR_ADDTIMER | CONFIRM:
		case MGR_DELTIMER | CONFIRM:
		case MGR_REMOVETIMER | CONFIRM:
		free_msg(msg);
		return(1);
	}

	/* find the mISDNport that belongs to the stack */
	channel = bchannel_first;
	while(channel)
	{
		if (frm->addr == channel->b_addr)
			break;
		channel = channel->next;
	} 
	if (!channel)
	{
		PERROR("message belongs to no channel: prim(0x%x) addr(0x%x) msg->len(%d)\n", frm->prim, frm->addr, msg->len);
		goto out;
	}

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
		case PH_SIGNAL | INDICATION:
		bchannel_receive(channel, frm->prim, frm->dinfo, frm->data.p, frm->len);
		break;

		case PH_ACTIVATE | INDICATION:
		case DL_ESTABLISH | INDICATION:
		case PH_ACTIVATE | CONFIRM:
		case DL_ESTABLISH | CONFIRM:
		PDEBUG( "DL_ESTABLISH confirm: bchannel is now activated (address 0x%x).\n", frm->addr);
		bchannel_activated(channel);
		break;

		case PH_DEACTIVATE | INDICATION:
		case DL_RELEASE | INDICATION:
		case PH_DEACTIVATE | CONFIRM:
		case DL_RELEASE | CONFIRM:
		PDEBUG("DL_RELEASE confirm: bchannel is now de-activated (address 0x%x).\n", frm->addr);
//		bchannel_deactivated(channel);
		break;

		default:
		PERROR("message not handled: prim(0x%x) addr(0x%x) msg->len(%d)\n", frm->prim, frm->addr, msg->len);
	}

	out:
	free_msg(msg);
	return(1);
}
#endif


/*
 * bchannel channel handling
 */
struct bchannel *bchannel_first = NULL;
struct bchannel *find_bchannel_handle(unsigned long handle)
{
	struct bchannel *channel = bchannel_first;

	while(channel)
	{
		if (channel->handle == handle)
			break;
		channel = channel->next;
	}
	return(channel);
}

struct bchannel *find_bchannel_ref(unsigned long ref)
{
	struct bchannel *channel = bchannel_first;

	while(channel)
	{
		if (channel->ref == ref)
			break;
		channel = channel->next;
	}
	return(channel);
}

struct bchannel *alloc_bchannel(unsigned long handle)
{
	struct chan_bchannel **channelp = &bchannel_first;

	while(*channelp)
		channelp = &((*channelp)->next);

	*channelp = (struct chan_bchannel *)malloc(sizeof(struct chan_bchannel));
	if (!*channelp)
		return(NULL);
	channel->handle = handle;
	channel->b_state = BSTATE_IDLE;
		
	return(*channelp);
}

void free_bchannel(struct bchannel *channel)
{
	struct bchannel **temp = &bchannel_first;

	while(*temp)
	{
		if (*temp == channel)
		{
			*temp = (*temp)->next;
#ifdef SOCKET_MISDN
			if (channel->b_sock > -1)
#else
			if (channel->b_stid)
#endif
				bchannel_destroy(channel);
			free(channel);
			return;
		}
		temp = &((*temp)->next);
	}
}


