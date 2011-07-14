/*****************************************************************************\
**                                                                           **
** LCR                                                                       **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** loopback interface functions                                              **
**                                                                           **
\*****************************************************************************/ 

#include "main.h"

struct mISDNloop mISDNloop = { -1, 0 };

void mISDNloop_close(void)
{
	if (mISDNloop.sock > -1)
		close(mISDNloop.sock);
	mISDNloop.sock = -1;
}

int mISDNloop_open()
{
	int ret;
	int cnt;
	unsigned long on = 1;
	struct sockaddr_mISDN addr;
	struct mISDN_devinfo devinfo;
	int pri, bri;

	/* already open */
	if (mISDNloop.sock > -1)
		return 0;

	PDEBUG(DEBUG_PORT, "Open external interface of loopback.\n");

	/* check port counts */
	ret = ioctl(mISDNsocket, IMGETCOUNT, &cnt);
	if (ret < 0) {
		fprintf(stderr, "Cannot get number of mISDN devices. (ioctl IMGETCOUNT failed ret=%d)\n", ret);
		return(ret);
	}

	if (cnt <= 0) {
		PERROR_RUNTIME("Found no card. Please be sure to load card drivers.\n");
		return -EIO;
	}
	mISDNloop.port = mISDN_getportbyname(mISDNsocket, cnt, options.loopback_ext);
	if (mISDNloop.port < 0) {
		PERROR_RUNTIME("Port name '%s' not found, did you load loopback interface?.\n", options.loopback_ext);
		return mISDNloop.port;
	}
	/* get protocol */
	bri = pri = 0;
	devinfo.id = mISDNloop.port;
	ret = ioctl(mISDNsocket, IMGETDEVINFO, &devinfo);
	if (ret < 0) {
		PERROR_RUNTIME("Cannot get device information for port %d. (ioctl IMGETDEVINFO failed ret=%d)\n", mISDNloop.port, ret);
		return ret;
	}
	if (devinfo.Dprotocols & (1 << ISDN_P_TE_S0)) {
		bri = 1;
	}
	if (devinfo.Dprotocols & (1 << ISDN_P_TE_E1)) {
		pri = 1;
	}
	if (!bri && !pri) {
		PERROR_RUNTIME("loop port %d does not support TE PRI or TE BRI.\n", mISDNloop.port);
	}
	/* open socket */
	if ((mISDNloop.sock = socket(PF_ISDN, SOCK_DGRAM, (pri)?ISDN_P_TE_E1:ISDN_P_TE_S0)) < 0) {
		PERROR_RUNTIME("loop port %d failed to open socket.\n", mISDNloop.port);
		mISDNloop_close();
		return mISDNloop.sock;
	}
	/* set nonblocking io */
	if ((ret = ioctl(mISDNloop.sock, FIONBIO, &on)) < 0) {
		PERROR_RUNTIME("loop port %d failed to set socket into nonblocking io.\n", mISDNloop.port);
		mISDNloop_close();
		return ret;
	}
	/* bind socket to dchannel */
	memset(&addr, 0, sizeof(addr));
	addr.family = AF_ISDN;
	addr.dev = mISDNloop.port;
	addr.channel = 0;
	if ((ret = bind(mISDNloop.sock, (struct sockaddr *)&addr, sizeof(addr))) < 0) {
		PERROR_RUNTIME("loop port %d failed to bind socket. (name = %s errno=%d)\n", mISDNloop.port, options.loopback_ext, errno);
		mISDNloop_close();
		return (ret);
	}

	return 0;
}

int loop_hunt_bchannel(class PmISDN *port, struct mISDNport *mISDNport)
{
	int channel;
	int i;
	char map[mISDNport->b_num];
	struct interface *interface;
	struct interface_port *ifport;

	chan_trace_header(mISDNport, port, "CHANNEL SELECTION (setup)", DIRECTION_NONE);
	add_trace("channel", "reserved", "%d", mISDNport->b_reserved);
	if (mISDNport->b_reserved >= mISDNport->b_num) { // of out chan..
		add_trace("conclusion", NULL, "all channels are reserved");
		end_trace();
		return(-34); // no channel
	}

	/* map all used ports of shared loopback interface */
	memset(map, 0, sizeof(map));
	interface = interface_first;
	while(interface) {
		ifport = interface->ifport;
		while(ifport) {
			if (!strcmp(ifport->portname, options.loopback_lcr)) {
				i = 0;
				while(i < mISDNport->b_num) {
					if (mISDNport->b_port[i])
						map[i] = 1;
					i++;
				}
			}
			ifport = ifport->next;
		}
		interface = interface->next;
	}

	/* find channel */
	i = 0;
	channel = 0;
	while(i < mISDNport->b_num) {
		if (!map[i]) {
			channel = i+1+(i>=15);
			break;
		}
		i++;
	}
	if (!channel) {
		add_trace("conclusion", NULL, "no channel available");
		end_trace();
		return(-6); // channel unacceptable
	}
	add_trace("conclusion", NULL, "channel available");
	add_trace("connect", "channel", "%d", channel);
	end_trace();
	return(channel);
}


