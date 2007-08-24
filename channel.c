/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** mISDN bchannel access (for Asterisk)                                      **
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

#if 0
#ifndef ISDN_PID_L2_B_USER
#define ISDN_PID_L2_B_USER 0x420000ff
#endif
#ifndef ISDN_PID_L3_B_USER
#define ISDN_PID_L3_B_USER 0x430000ff
#endif
#endif
#ifndef ISDN_PID_L4_B_USER
#define ISDN_PID_L4_B_USER 0x440000ff
#endif

/* used for udevice */
int entity = 0;

/* the device handler and port list */
int mISDNdevice = -1;


/* open mISDN device */
void mISDNdevice_open(void)
{
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
	}
}

/* close mISDN device */
void mISDNdevice_close(void)
{
	if (mISDNdevice > -1)
	{
		mISDN_close();
		PDEBUG(DEBUG_ISDN, "mISDN device closed.\n");
	}
}

/* create bchannel layer */
unsigned long mISDN_createlayer(unsigned long stid)
{
	unsigned long addr;

	/* create new layer */
	PDEBUG(DEBUG_BCHANNEL, "creating new layer for bchannel stid=0%x.\n" , stid);
	memset(&li, 0, sizeof(li));
	memset(&pid, 0, sizeof(pid));
	li.object_id = -1;
	li.extentions = 0;
	li.st = stid;
	UCPY(li.name, "B L4");
	li.pid.layermask = ISDN_LAYER((4));
	li.pid.protocol[4] = ISDN_PID_L4_B_USER;
	ret = mISDN_new_layer(mISDNdevice, &li);
	if (ret)
	{
		failed_new_layer:
		PERROR("mISDN_new_layer() failed to add bchannel stid=0%x.\n", stid);
		goto failed;
	}
	addr = li.id;
	if (!li.id)
	{
		goto failed_new_layer;
	}
	PDEBUG(DEBUG_BCHANNEL, "new layer (addr=0x%x)\n", addr);

	/* create new stack */
	pid.protocol[1] = ISDN_PID_L1_B_64TRANS;
	pid.protocol[2] = ISDN_PID_L2_B_TRANS;
	pid.protocol[3] = ISDN_PID_L3_B_DSP;
	pid.protocol[4] = ISDN_PID_L4_B_USER;
	pid.layermask = ISDN_LAYER((1)) | ISDN_LAYER((2)) | ISDN_LAYER((3)) | ISDN_LAYER((4));
	ret = mISDN_set_stack(mISDNdevice, stid, &pid);
	if (ret)
	{
		stack_error:
		PERROR("mISDN_set_stack() failed (ret=%d) to add bchannel stid=0x%x\n", ret, stid);
		mISDN_write_frame(mISDNdevice, buff, addr, MGR_DELLAYER | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);
		goto failed;
	}
	ret = mISDN_get_setstack_ind(mISDNdevice, addr);
	if (ret)
		goto stack_error;

	/* get layer id */
	addr = mISDN_get_layerid(mISDNdevice, stid, 4);
	if (!addr)
		goto stack_error;
}

/* destroy bchannel layer */
void mISDN_destroylayer(unsigned long stid, unsigned long addr)
{
	/* remove our stack only if set */
	if (addr)
	{
		PDEBUG(DEBUG_BCHANNEL, "free stack (addr=0x%x)\n", addr);
		mISDN_clear_stack(mISDNdevice, stid);
		mISDN_write_frame(mISDNdevice, buff, addr | FLG_MSG_DOWN, MGR_DELLAYER | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);
	}
}

/* do activation and deactivation of bchannel */
static void mISDN_bchannelactivate(unsigned long addr, int activate)
{
	iframe_t act;

	/* activate bchannel */
	act.prim = (activate?DL_ESTABLISH:DL_RELEASE) | REQUEST; 
	act.addr = addr | FLG_MSG_DOWN;
	act.dinfo = 0;
	act.len = 0;
	mISDN_write(mISDNdevice, &act, mISDN_HEADER_LEN+act.len, TIMEOUT_1SEC);
}

/* handle all mISDN messages */
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
	struct chan_bchannel *bchannel;

	/* no device, no read */
	if (mISDNdevice < 0)
		return(0);

	/* get message from kernel */
	if (!(msg = alloc_msg(MAX_MSG_SIZE)))
		return(1);
	ret = mISDN_read(mISDNdevice, msg->data, MAX_MSG_SIZE, 0);
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

	/* look for channel instance, that has the address of this message */
	bchannel = bchannel_first;
	while(bchannel)
	{
		if (frm->addr == bchannel->b_addr)
			break;
		bchannel = chan->next;
	} 
	if (!bchannel)
	{
		PERROR("message belongs to no bchannel: prim(0x%x) addr(0x%x) msg->len(%d)\n", frm->prim, frm->addr, msg->len);
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
		i = 0;
		bchannel_receive(bchannel, frm);
		break;

		case PH_ACTIVATE | INDICATION:
		case DL_ESTABLISH | INDICATION:
		case PH_ACTIVATE | CONFIRM:
		case DL_ESTABLISH | CONFIRM:
		PDEBUG(DEBUG_BCHANNEL, "DL_ESTABLISH confirm: bchannel is now activated (address 0x%x).\n", frm->addr);
		bchannel_event(mISDNport, i, B_EVENT_ACTIVATED);
		break;

		case PH_DEACTIVATE | INDICATION:
		case DL_RELEASE | INDICATION:
		case PH_DEACTIVATE | CONFIRM:
		case DL_RELEASE | CONFIRM:
		PDEBUG(DEBUG_BCHANNEL, "DL_RELEASE confirm: bchannel is now de-activated (address 0x%x).\n", frm->addr);
		bchannel_event(mISDNport, i, B_EVENT_DEACTIVATED);
		break;

		default:
		PERROR("child message not handled: prim(0x%x) addr(0x%x) msg->len(%d)\n", frm->prim, frm->addr, msg->len);
	}

	out:
	free_msg(msg);
	return(1);
}

