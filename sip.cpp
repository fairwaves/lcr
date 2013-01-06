/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** SIP port                                                                  **
**                                                                           **
\*****************************************************************************/ 

#include "main.h"
#include <sofia-sip/sip_status.h>
#include <sofia-sip/su_log.h>
#include <sofia-sip/sdp.h>
#include <sofia-sip/sip_header.h>

#undef NUTAG_AUTO100

unsigned char flip[256];

int any_sip_interface = 0;

//pthread_mutex_t mutex_msg;
su_home_t	sip_home[1];

struct sip_inst {
	char			interface_name[64];
	char			local_peer[32];
	char			remote_peer[32];
	su_root_t		*root;
	nua_t			*nua;
};

static int delete_event(struct lcr_work *work, void *instance, int index);
static int load_timer(struct lcr_timer *timer, void *instance, int index);

/*
 * initialize SIP port
 */
Psip::Psip(int type, char *portname, struct port_settings *settings, struct interface *interface) : Port(type, portname, settings, interface)
{
	p_s_rtp_bridge = 0;
	if (interface->rtp_bridge)
		p_s_rtp_bridge = 1;
	p_s_sip_inst = interface->sip_inst;
	memset(&p_s_delete, 0, sizeof(p_s_delete));
	add_work(&p_s_delete, delete_event, this, 0);
	p_s_handle = 0;
	p_s_magic = 0;
	memset(&p_s_rtp_fd, 0, sizeof(p_s_rtp_fd));
	memset(&p_s_rtcp_fd, 0, sizeof(p_s_rtcp_fd));
	memset(&p_s_rtp_sin_local, 0, sizeof(p_s_rtp_sin_local));
	memset(&p_s_rtcp_sin_local, 0, sizeof(p_s_rtcp_sin_local));
	memset(&p_s_rtp_sin_remote, 0, sizeof(p_s_rtp_sin_remote));
	memset(&p_s_rtcp_sin_remote, 0, sizeof(p_s_rtcp_sin_remote));
	p_s_rtp_ip_local = 0;
	p_s_rtp_ip_remote = 0;
	p_s_rtp_port_local = 0;
	p_s_rtp_port_remote = 0;
	p_s_b_sock = -1;
	p_s_b_index = -1;
	p_s_b_active = 0;
	p_s_rxpos = 0;
	p_s_rtp_tx_action = 0;

	/* audio */
	memset(&p_s_loadtimer, 0, sizeof(p_s_loadtimer));
	add_timer(&p_s_loadtimer, load_timer, this, 0);
	p_s_next_tv_sec = 0;

	PDEBUG(DEBUG_SIP, "Created new Psip(%s).\n", portname);
	if (!p_s_sip_inst)
		FATAL("No SIP instance for interface\n");
}


/*
 * destructor
 */
Psip::~Psip()
{
	PDEBUG(DEBUG_SIP, "Destroyed SIP process(%s).\n", p_name);

	del_timer(&p_s_loadtimer);
	del_work(&p_s_delete);

	rtp_close();
}

static const char *media_type2name(uint8_t media_type) {
	switch (media_type) {
	case MEDIA_TYPE_ULAW:
		return "PCMU";
	case MEDIA_TYPE_ALAW:
		return "PCMA";
	case MEDIA_TYPE_GSM:
		return "GSM";
	case MEDIA_TYPE_GSM_HR:
		return "GSM-HR";
	case MEDIA_TYPE_GSM_EFR:
		return "GSM-EFR";
	case MEDIA_TYPE_AMR:
		return "AMR";
	}

	return "UKN";
}

static void sip_trace_header(class Psip *sip, const char *message, int direction)
{
	/* init trace with given values */
	start_trace(-1,
		    NULL,
		    sip?numberrize_callerinfo(sip->p_callerinfo.id, sip->p_callerinfo.ntype, options.national, options.international):NULL,
		    sip?sip->p_dialinginfo.id:NULL,
		    direction,
		    CATEGORY_CH,
		    sip?sip->p_serial:0,
		    message);
}

/*
 * RTP
 */

/* according to RFC 3550 */
struct rtp_hdr {
#if __BYTE_ORDER == __LITTLE_ENDIAN
	uint8_t  csrc_count:4,
		  extension:1,
		  padding:1,
		  version:2;
	uint8_t  payload_type:7,
		  marker:1;
#elif __BYTE_ORDER == __BIG_ENDIAN
	uint8_t  version:2,
		  padding:1,
		  extension:1,
		  csrc_count:4;
	uint8_t  marker:1,
		  payload_type:7;
#endif
	uint16_t sequence;
	uint32_t timestamp;
	uint32_t ssrc;
} __attribute__((packed));

struct rtp_x_hdr {
	uint16_t by_profile;
	uint16_t length;
} __attribute__((packed));

#define RTP_VERSION	2

#define PAYLOAD_TYPE_ULAW 0
#define PAYLOAD_TYPE_ALAW 8
#define PAYLOAD_TYPE_GSM 3

/* decode an rtp frame  */
static int rtp_decode(class Psip *psip, unsigned char *data, int len)
{
	struct rtp_hdr *rtph = (struct rtp_hdr *)data;
	struct rtp_x_hdr *rtpxh;
	uint8_t *payload;
	int payload_len;
	int x_len;
	unsigned char *from, *to;
	int n;

	if (len < 12) {
		PDEBUG(DEBUG_SIP, "received RTP frame too short (len = %d)\n", len);
		return -EINVAL;
	}
	if (rtph->version != RTP_VERSION) {
		PDEBUG(DEBUG_SIP, "received RTP version %d not supported.\n", rtph->version);
		return -EINVAL;
	}
	payload = data + sizeof(struct rtp_hdr) + (rtph->csrc_count << 2);
	payload_len = len - sizeof(struct rtp_hdr) - (rtph->csrc_count << 2);
	if (payload_len < 0) {
		PDEBUG(DEBUG_SIP, "received RTP frame too short (len = %d, "
			"csrc count = %d)\n", len, rtph->csrc_count);
		return -EINVAL;
	}
	if (rtph->extension) {
		if (payload_len < (int)sizeof(struct rtp_x_hdr)) {
			PDEBUG(DEBUG_SIP, "received RTP frame too short for "
				"extension header\n");
			return -EINVAL;
		}
		rtpxh = (struct rtp_x_hdr *)payload;
		x_len = ntohs(rtpxh->length) * 4 + sizeof(struct rtp_x_hdr);
		payload += x_len;
		payload_len -= x_len;
		if (payload_len < 0) {
			PDEBUG(DEBUG_SIP, "received RTP frame too short, "
				"extension header exceeds frame length\n");
			return -EINVAL;
		}
	}
	if (rtph->padding) {
		if (payload_len < 0) {
			PDEBUG(DEBUG_SIP, "received RTP frame too short for "
				"padding length\n");
			return -EINVAL;
		}
		payload_len -= payload[payload_len - 1];
		if (payload_len < 0) {
			PDEBUG(DEBUG_SIP, "received RTP frame with padding "
				"greater than payload\n");
			return -EINVAL;
		}
	}

	switch (rtph->payload_type) {
#if 0
we only support alaw and ulaw!
	case RTP_PT_GSM_FULL:
		if (payload_len != 33) {
			PDEBUG(DEBUG_SIP, "received RTP full rate frame with "
				"payload length != 33 (len = %d)\n",
				payload_len);
			return -EINVAL;
		}
		break;
	case RTP_PT_GSM_EFR:
		if (payload_len != 31) {
			PDEBUG(DEBUG_SIP, "received RTP full rate frame with "
				"payload length != 31 (len = %d)\n",
				payload_len);
			return -EINVAL;
		}
		break;
	case RTP_PT_GSM_HALF:
		if (payload_len != 14) {
			PDEBUG(DEBUG_SIP, "received RTP half rate frame with "
				"payload length != 14 (len = %d)\n",
				payload_len);
			return -EINVAL;
		}
		break;
#endif
	case PAYLOAD_TYPE_ALAW:
		if (options.law != 'a') {
			PDEBUG(DEBUG_SIP, "received Alaw, but we don't do Alaw\n");
			return -EINVAL;
		}
		break;
	case PAYLOAD_TYPE_ULAW:
		if (options.law == 'a') {
			PDEBUG(DEBUG_SIP, "received Ulaw, but we don't do Ulaw\n");
			return -EINVAL;
		}
		break;
	default:
		PDEBUG(DEBUG_SIP, "received RTP frame with unknown payload "
			"type %d\n", rtph->payload_type);
		return -EINVAL;
	}

	if (payload_len <= 0) {
		PDEBUG(DEBUG_SIP, "received RTP payload is too small: %d\n", payload_len);
		return 0;
	}

	/* record audio */
	if (psip->p_record)
		psip->record(payload, payload_len, 0); // from down
	if (psip->p_tap)
		psip->tap(payload, payload_len, 0); // from down

	n = payload_len;
	from = payload;
	to = payload;
	if (psip->p_echotest) {
		/* echo rtp data we just received */
		psip->rtp_send_frame(from, n, (options.law=='a')?PAYLOAD_TYPE_ALAW:PAYLOAD_TYPE_ULAW);
		return 0;
	}
	while(n--)
		*to++ = flip[*from++];
	psip->bridge_tx(payload, payload_len);

	return 0;
}

static int rtp_sock_callback(struct lcr_fd *fd, unsigned int what, void *instance, int index)
{
	class Psip *psip = (class Psip *) instance;
	int len;
	unsigned char buffer[256];
	int rc = 0;

	if ((what & LCR_FD_READ)) {
		len = read(fd->fd, &buffer, sizeof(buffer));
		if (len <= 0) {
			PDEBUG(DEBUG_SIP, "read result=%d\n", len);
//			psip->rtp_close();
//			psip->rtp_shutdown();
			return len;
		}
		if (psip->p_s_rtp_is_connected)
			rc = rtp_decode(psip, buffer, len);
	}

	return rc;
}

static int rtcp_sock_callback(struct lcr_fd *fd, unsigned int what, void *instance, int index)
{
//	class Psip *psip = (class Psip *) instance;
	int len;
	unsigned char buffer[256];

	if ((what & LCR_FD_READ)) {
		len = read(fd->fd, &buffer, sizeof(buffer));
		if (len <= 0) {
			PDEBUG(DEBUG_SIP, "read result=%d\n", len);
//			psip->rtp_close();
//			psip->rtp_shutdown();
			return len;
		}
		PDEBUG(DEBUG_SIP, "rtcp!\n");
	}

	return 0;
}

#define RTP_PORT_BASE	30000
#define RTP_PORT_MAX	39998
static unsigned short next_udp_port = RTP_PORT_BASE;

static int rtp_sub_socket_bind(int fd, struct sockaddr_in *sin_local, uint32_t ip, uint16_t port)
{
	int rc;
	socklen_t alen = sizeof(*sin_local);

	sin_local->sin_family = AF_INET;
	sin_local->sin_addr.s_addr = htonl(ip);
	sin_local->sin_port = htons(port);

	rc = bind(fd, (struct sockaddr *) sin_local, sizeof(*sin_local));
	if (rc < 0)
		return rc;

	/* retrieve the address we actually bound to, in case we
	 * passed INADDR_ANY as IP address */
	return getsockname(fd, (struct sockaddr *) sin_local, &alen);
}

static int rtp_sub_socket_connect(int fd, struct sockaddr_in *sin_local, struct sockaddr_in *sin_remote, uint32_t ip, uint16_t port)
{
	int rc;
	socklen_t alen = sizeof(*sin_local);

	sin_remote->sin_family = AF_INET;
	sin_remote->sin_addr.s_addr = htonl(ip);
	sin_remote->sin_port = htons(port);

	rc = connect(fd, (struct sockaddr *) sin_remote, sizeof(*sin_remote));
	if (rc < 0) {
		PERROR("failed to connect to ip %08x port %d rc=%d\n", ip, port, rc);
		return rc;
	}

	return getsockname(fd, (struct sockaddr *) sin_local, &alen);
}

int Psip::rtp_open(void)
{
	int rc, rc2;
	struct in_addr ia;
	unsigned int ip;
	unsigned short start_port;

	/* create socket */
	rc = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (rc < 0) {
		rtp_close();
		return -EIO;
	}
	p_s_rtp_fd.fd = rc;
	register_fd(&p_s_rtp_fd, LCR_FD_READ, rtp_sock_callback, this, 0);

	rc = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (rc < 0) {
		rtp_close();
		return -EIO;
	}
	p_s_rtcp_fd.fd = rc;
	register_fd(&p_s_rtcp_fd, LCR_FD_READ, rtcp_sock_callback, this, 0);

	/* bind socket */
	ip = htonl(INADDR_ANY);
	ia.s_addr = ip;
	start_port = next_udp_port;
	while (1) {
		rc = rtp_sub_socket_bind(p_s_rtp_fd.fd, &p_s_rtp_sin_local, ip, next_udp_port);
		if (rc != 0)
			goto try_next_port;

		rc = rtp_sub_socket_bind(p_s_rtcp_fd.fd, &p_s_rtcp_sin_local, ip, next_udp_port + 1);
		if (rc == 0) {
			p_s_rtp_port_local = next_udp_port;
			next_udp_port = (next_udp_port + 2 > RTP_PORT_MAX) ? RTP_PORT_BASE : next_udp_port + 2;
			break;
		}
		/* reopen rtp socket and try again with next udp port */
		unregister_fd(&p_s_rtp_fd);
		close(p_s_rtp_fd.fd);
		p_s_rtp_fd.fd = 0;
		rc2 = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (rc2 < 0) {
			rtp_close();
			return -EIO;
		}
		p_s_rtp_fd.fd = rc2;
		register_fd(&p_s_rtp_fd, LCR_FD_READ, rtp_sock_callback, this, 0);

try_next_port:
		next_udp_port = (next_udp_port + 2 > RTP_PORT_MAX) ? RTP_PORT_BASE : next_udp_port + 2;
		if (next_udp_port == start_port)
			break;
		/* we must use rc2, in order to preserve rc */
	}
	if (rc < 0) {
		PDEBUG(DEBUG_SIP, "failed to find port\n");
		rtp_close();
		return rc;
	}
	p_s_rtp_ip_local = ntohl(p_s_rtp_sin_local.sin_addr.s_addr);
	PDEBUG(DEBUG_SIP, "local ip %08x port %d\n", p_s_rtp_ip_local, p_s_rtp_port_local);
	PDEBUG(DEBUG_SIP, "remote ip %08x port %d\n", p_s_rtp_ip_remote, p_s_rtp_port_remote);

	return p_s_rtp_port_local;
}

int Psip::rtp_connect(void)
{
	int rc;
	struct in_addr ia;

	ia.s_addr = htonl(p_s_rtp_ip_remote);
	PDEBUG(DEBUG_SIP, "rtp_connect(ip=%s, port=%u)\n", inet_ntoa(ia), p_s_rtp_port_remote);

	rc = rtp_sub_socket_connect(p_s_rtp_fd.fd, &p_s_rtp_sin_local, &p_s_rtp_sin_remote, p_s_rtp_ip_remote, p_s_rtp_port_remote);
	if (rc < 0)
		return rc;

	rc = rtp_sub_socket_connect(p_s_rtcp_fd.fd, &p_s_rtcp_sin_local, &p_s_rtcp_sin_remote, p_s_rtp_ip_remote, p_s_rtp_port_remote + 1);
	if (rc < 0)
		return rc;

	p_s_rtp_ip_local = ntohl(p_s_rtp_sin_local.sin_addr.s_addr);
	PDEBUG(DEBUG_SIP, "local ip %08x port %d\n", p_s_rtp_ip_local, p_s_rtp_port_local);
	PDEBUG(DEBUG_SIP, "remote ip %08x port %d\n", p_s_rtp_ip_remote, p_s_rtp_port_remote);
	p_s_rtp_is_connected = 1;

	return 0;
}
void Psip::rtp_close(void)
{
	if (p_s_rtp_fd.fd > 0) {
		unregister_fd(&p_s_rtp_fd);
		close(p_s_rtp_fd.fd);
		p_s_rtp_fd.fd = 0;
	}
	if (p_s_rtcp_fd.fd > 0) {
		unregister_fd(&p_s_rtcp_fd);
		close(p_s_rtcp_fd.fd);
		p_s_rtcp_fd.fd = 0;
	}
	if (p_s_rtp_is_connected) {
		PDEBUG(DEBUG_SIP, "rtp closed\n");
		p_s_rtp_is_connected = 0;
	}
}

/* "to - from" */
void tv_difference(struct timeval *diff, const struct timeval *from,
			  const struct timeval *__to)
{
	struct timeval _to = *__to, *to = &_to;

	if (to->tv_usec < from->tv_usec) {
		to->tv_sec -= 1;
		to->tv_usec += 1000000;
	}

	diff->tv_usec = to->tv_usec - from->tv_usec;
	diff->tv_sec = to->tv_sec - from->tv_sec;
}

/* encode and send a rtp frame */
int Psip::rtp_send_frame(unsigned char *data, unsigned int len, uint8_t payload_type)
{
	struct rtp_hdr *rtph;
	int payload_len;
	int duration; /* in samples */
	unsigned char buffer[256];

	/* record audio */
	if (p_record)
		record(data, len, 1); // from up
	if (p_tap)
		tap(data, len, 1); // from up

	if (!p_s_rtp_is_connected) {
		/* drop silently */
		return 0;
	}

	if (!p_s_rtp_tx_action) {
		/* initialize sequences */
		p_s_rtp_tx_action = 1;
		p_s_rtp_tx_ssrc = rand();
		p_s_rtp_tx_sequence = random();
		p_s_rtp_tx_timestamp = random();
		memset(&p_s_rtp_tx_last_tv, 0, sizeof(p_s_rtp_tx_last_tv));
	}

	switch (payload_type) {
#if 0
we only support alaw and ulaw!
	case RTP_PT_GSM_FULL:
		payload_len = 33;
		duration = 160;
		break;
	case RTP_PT_GSM_EFR:
		payload_len = 31;
		duration = 160;
		break;
	case RTP_PT_GSM_HALF:
		payload_len = 14;
		duration = 160;
		break;
#endif
	case PAYLOAD_TYPE_ALAW:
	case PAYLOAD_TYPE_ULAW:
		payload_len = len;
		duration = len;
		break;
	default:
		PERROR("unsupported message type %d\n", payload_type);
		return -EINVAL;
	}

#if 0
	{
		struct timeval tv, tv_diff;
		long int usec_diff, frame_diff;

		gettimeofday(&tv, NULL);
		tv_difference(&tv_diff, &p_s_rtp_tx_last_tv, &tv);
		p_s_rtp_tx_last_tv = tv;

		usec_diff = tv_diff.tv_sec * 1000000 + tv_diff.tv_usec;
		frame_diff = (usec_diff / 20000);

		if (abs(frame_diff) > 1) {
			long int frame_diff_excess = frame_diff - 1;

			PDEBUG(DEBUG_SIP, "Correcting frame difference of %ld frames\n", frame_diff_excess);
			p_s_rtp_tx_sequence += frame_diff_excess;
			p_s_rtp_tx_timestamp += frame_diff_excess * duration;
		}
	}
#endif

	rtph = (struct rtp_hdr *) buffer;
	rtph->version = RTP_VERSION;
	rtph->padding = 0;
	rtph->extension = 0;
	rtph->csrc_count = 0;
	rtph->marker = 0;
	rtph->payload_type = payload_type;
	rtph->sequence = htons(p_s_rtp_tx_sequence++);
	rtph->timestamp = htonl(p_s_rtp_tx_timestamp);
	p_s_rtp_tx_timestamp += duration;
	rtph->ssrc = htonl(p_s_rtp_tx_ssrc);
	memcpy(buffer + sizeof(struct rtp_hdr), data, payload_len);

	if (p_s_rtp_fd.fd > 0) {
		len = write(p_s_rtp_fd.fd, &buffer, sizeof(struct rtp_hdr) + payload_len);
		if (len != sizeof(struct rtp_hdr) + payload_len) {
			PDEBUG(DEBUG_SIP, "write result=%d\n", len);
//			rtp_close();
//			rtp_shutdown();
			return -EIO;
		}
	}

	return 0;
}

/* receive from remote */
int Psip::bridge_rx(unsigned char *data, int len)
{
	/* don't bridge, if tones are provided */
	if (p_tone_name[0])
		return -EBUSY;

	/* write to rx buffer */
	while(len--) {
		p_s_rxdata[p_s_rxpos++] = flip[*data++];
		if (p_s_rxpos == 160) {
			p_s_rxpos = 0;

			/* transmit data via rtp */
			rtp_send_frame(p_s_rxdata, 160, (options.law=='a')?PAYLOAD_TYPE_ALAW:PAYLOAD_TYPE_ULAW);
		}
	}

	return 0;
}

/* taken from freeswitch */
/* map sip responses to QSIG cause codes ala RFC4497 section 8.4.4 */
static int status2cause(int status)
{
	switch (status) {
	case 200:
		return 16; //SWITCH_CAUSE_NORMAL_CLEARING;
	case 401:
	case 402:
	case 403:
	case 407:
	case 603:
		return 21; //SWITCH_CAUSE_CALL_REJECTED;
	case 404:
		return 1; //SWITCH_CAUSE_UNALLOCATED_NUMBER;
	case 485:
	case 604:
		return 3; //SWITCH_CAUSE_NO_ROUTE_DESTINATION;
	case 408:
	case 504:
		return 102; //SWITCH_CAUSE_RECOVERY_ON_TIMER_EXPIRE;
	case 410:
		return 22; //SWITCH_CAUSE_NUMBER_CHANGED;
	case 413:
	case 414:
	case 416:
	case 420:
	case 421:
	case 423:
	case 505:
	case 513:
		return 127; //SWITCH_CAUSE_INTERWORKING;
	case 480:
		return 180; //SWITCH_CAUSE_NO_USER_RESPONSE;
	case 400:
	case 481:
	case 500:
	case 503:
		return 41; //SWITCH_CAUSE_NORMAL_TEMPORARY_FAILURE;
	case 486:
	case 600:
		return 17; //SWITCH_CAUSE_USER_BUSY;
	case 484:
		return 28; //SWITCH_CAUSE_INVALID_NUMBER_FORMAT;
	case 488:
	case 606:
		return 88; //SWITCH_CAUSE_INCOMPATIBLE_DESTINATION;
	case 502:
		return 38; //SWITCH_CAUSE_NETWORK_OUT_OF_ORDER;
	case 405:
		return 63; //SWITCH_CAUSE_SERVICE_UNAVAILABLE;
	case 406:
	case 415:
	case 501:
		return 79; //SWITCH_CAUSE_SERVICE_NOT_IMPLEMENTED;
	case 482:
	case 483:
		return 25; //SWITCH_CAUSE_EXCHANGE_ROUTING_ERROR;
	case 487:
		return 31; //??? SWITCH_CAUSE_ORIGINATOR_CANCEL;
	default:
		return 31; //SWITCH_CAUSE_NORMAL_UNSPECIFIED;
	}
}

static int cause2status(int cause, int location, const char **st)
{
	int s;

	switch (cause) {
	case 1:
		s = 404; *st = sip_404_Not_found;
		break;
	case 2:
		s = 404; *st = sip_404_Not_found;
		break;
	case 3:
		s = 404; *st = sip_404_Not_found;
		break;
	case 17:
		s = 486; *st = sip_486_Busy_here;
		break;
	case 18:
		s = 408; *st = sip_408_Request_timeout;
		break;
	case 19:
		s = 480; *st = sip_480_Temporarily_unavailable;
		break;
	case 20:
		s = 480; *st = sip_480_Temporarily_unavailable;
		break;
	case 21:
		if (location == LOCATION_USER) {
			s = 603; *st = sip_603_Decline;
		} else {
			s = 403; *st = sip_403_Forbidden;
		}
		break;
	case 22:
		//s = 301; *st = sip_301_Moved_permanently;
		s = 410; *st = sip_410_Gone;
		break;
	case 23:
		s = 410; *st = sip_410_Gone;
		break;
	case 27:
		s = 502; *st = sip_502_Bad_gateway;
		break;
	case 28:
		s = 484; *st = sip_484_Address_incomplete;
		break;
	case 29:
		s = 501; *st = sip_501_Not_implemented;
		break;
	case 31:
		s = 480; *st = sip_480_Temporarily_unavailable;
		break;
	case 34:
		s = 503; *st = sip_503_Service_unavailable;
		break;
	case 38:
		s = 503; *st = sip_503_Service_unavailable;
		break;
	case 41:
		s = 503; *st = sip_503_Service_unavailable;
		break;
	case 42:
		s = 503; *st = sip_503_Service_unavailable;
		break;
	case 47:
		s = 503; *st = sip_503_Service_unavailable;
		break;
	case 55:
		s = 403; *st = sip_403_Forbidden;
		break;
	case 57:
		s = 403; *st = sip_403_Forbidden;
		break;
	case 58:
		s = 503; *st = sip_503_Service_unavailable;
		break;
	case 65:
		s = 488; *st = sip_488_Not_acceptable;
		break;
	case 69:
		s = 501; *st = sip_501_Not_implemented;
		break;
	case 70:
		s = 488; *st = sip_488_Not_acceptable;
		break;
	case 79:
		s = 501; *st = sip_501_Not_implemented;
		break;
	case 87:
		s = 403; *st = sip_403_Forbidden;
		break;
	case 88:
		s = 503; *st = sip_503_Service_unavailable;
		break;
	case 102:
		s = 504; *st = sip_504_Gateway_time_out;
		break;
	default:
		s = 468; *st = sip_486_Busy_here;
	}

	return s;
}

/*
 * endpoint sends messages to the SIP port
 */

int Psip::message_connect(unsigned int epoint_id, int message_id, union parameter *param)
{
	char sdp_str[256];
	struct in_addr ia;
	struct lcr_msg *message;
	int media_type;
	unsigned char payload_type;

	if (param->connectinfo.rtpinfo.port) {
		PDEBUG(DEBUG_SIP, "RTP info given by remote, forward that\n");
		p_s_rtp_bridge = 1;
		media_type = param->connectinfo.rtpinfo.media_types[0];
		payload_type = param->connectinfo.rtpinfo.payload_types[0];
		p_s_rtp_ip_local = param->connectinfo.rtpinfo.ip;
		p_s_rtp_port_local = param->connectinfo.rtpinfo.port;
		PDEBUG(DEBUG_SIP, "payload type %d\n", payload_type);
		PDEBUG(DEBUG_SIP, "local ip %08x port %d\n", p_s_rtp_ip_local, p_s_rtp_port_local);
		PDEBUG(DEBUG_SIP, "remote ip %08x port %d\n", p_s_rtp_ip_remote, p_s_rtp_port_remote);
	} else {
		PDEBUG(DEBUG_SIP, "RTP info not given by remote, so we do our own RTP\n");
		media_type = (options.law=='a') ? MEDIA_TYPE_ALAW : MEDIA_TYPE_ULAW;
		payload_type = (options.law=='a') ? PAYLOAD_TYPE_ALAW : PAYLOAD_TYPE_ULAW;
		/* open local RTP peer (if not bridging) */
		if (!p_s_rtp_is_connected && rtp_connect() < 0) {
			nua_cancel(p_s_handle, TAG_END());
			nua_handle_destroy(p_s_handle);
			p_s_handle = NULL;
			sip_trace_header(this, "CANCEL", DIRECTION_OUT);
			add_trace("reason", NULL, "failed to connect RTP/RTCP sockts");
			end_trace();
			message = message_create(p_serial, epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
			message->param.disconnectinfo.cause = 41;
			message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
			message_put(message);
			new_state(PORT_STATE_RELEASE);
			trigger_work(&p_s_delete);
			return 0;
		}
	}

	ia.s_addr = htonl(p_s_rtp_ip_local);

	SPRINT(sdp_str,
		"v=0\n"
		"o=LCR-Sofia-SIP 0 0 IN IP4 %s\n"
		"s=SIP Call\n"
		"c=IN IP4 %s\n"
		"t=0 0\n"
		"m=audio %d RTP/AVP %d\n"
		"a=rtpmap:%d %s/8000\n"
		, inet_ntoa(ia), inet_ntoa(ia), p_s_rtp_port_local, payload_type, payload_type, media_type2name(media_type));
	PDEBUG(DEBUG_SIP, "Using SDP response: %s\n", sdp_str);

	nua_respond(p_s_handle, SIP_200_OK,
		NUTAG_MEDIA_ENABLE(0),
		SIPTAG_CONTENT_TYPE_STR("application/sdp"),
		SIPTAG_PAYLOAD_STR(sdp_str), TAG_END());
	new_state(PORT_STATE_CONNECT);
	sip_trace_header(this, "RESPOND", DIRECTION_OUT);
	add_trace("respond", "value", "200 OK");
	add_trace("reason", NULL, "call connected");
	add_trace("rtp", "ip", "%s", inet_ntoa(ia));
	add_trace("rtp", "port", "%d,%d", p_s_rtp_port_local, p_s_rtp_port_local + 1);
	add_trace("rtp", "payload", "%s:%d", media_type2name(media_type), payload_type);
	end_trace();

	return 0;
}

int Psip::message_release(unsigned int epoint_id, int message_id, union parameter *param)
{
	struct lcr_msg *message;
	char cause_str[128] = "";
	int cause = param->disconnectinfo.cause;
	int location = param->disconnectinfo.cause;
	int status;
	const char *status_text;

	if (cause > 0 && cause <= 127) {
		SPRINT(cause_str, "Q.850;cause=%d;text=\"%s\"", cause, isdn_cause[cause].english);
	}

	switch (p_state) {
	case PORT_STATE_OUT_SETUP:
	case PORT_STATE_OUT_PROCEEDING:
	case PORT_STATE_OUT_ALERTING:
		PDEBUG(DEBUG_SIP, "RELEASE/DISCONNECT will cancel\n");
		sip_trace_header(this, "CANCEL", DIRECTION_OUT);
		if (cause_str[0])
			add_trace("cause", "value", "%d", cause);
		end_trace();
		nua_cancel(p_s_handle, TAG_IF(cause_str[0], SIPTAG_REASON_STR(cause_str)), TAG_END());
		break;
	case PORT_STATE_IN_SETUP:
	case PORT_STATE_IN_PROCEEDING:
	case PORT_STATE_IN_ALERTING:
		PDEBUG(DEBUG_SIP, "RELEASE/DISCONNECT will respond\n");
		status = cause2status(cause, location, &status_text);
		sip_trace_header(this, "RESPOND", DIRECTION_OUT);
		if (cause_str[0])
			add_trace("cause", "value", "%d", cause);
		add_trace("respond", "value", "%d %s", status, status_text);
		end_trace();
		nua_respond(p_s_handle, status, status_text, TAG_IF(cause_str[0], SIPTAG_REASON_STR(cause_str)), TAG_END());
		nua_handle_destroy(p_s_handle);
		p_s_handle = NULL;
		trigger_work(&p_s_delete);
		break;
	default:
		PDEBUG(DEBUG_SIP, "RELEASE/DISCONNECT will perform nua_bye\n");
		sip_trace_header(this, "BYE", DIRECTION_OUT);
		if (cause_str[0])
			add_trace("cause", "value", "%d", cause);
		end_trace();
		nua_bye(p_s_handle, TAG_IF(cause_str[0], SIPTAG_REASON_STR(cause_str)), TAG_END());
	}

	if (message_id == MESSAGE_DISCONNECT) {
		while(p_epointlist) {
			message = message_create(p_serial, p_epointlist->epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
			message->param.disconnectinfo.cause = CAUSE_NORMAL;
			message->param.disconnectinfo.location = LOCATION_BEYOND;
			message_put(message);
			/* remove epoint */
			free_epointlist(p_epointlist);
		}
	}

	new_state(PORT_STATE_RELEASE);

	return(0);
}

int Psip::message_setup(unsigned int epoint_id, int message_id, union parameter *param)
{
	struct sip_inst *inst = (struct sip_inst *) p_s_sip_inst;
	char from[128];
	char to[128];
	const char *local = inst->local_peer;
	char local_ip[16];
	const char *remote = inst->remote_peer;
	char sdp_str[512], pt_str[32];
	struct in_addr ia;
	struct epoint_list *epointlist;
	sip_cseq_t *cseq = NULL;
	struct lcr_msg *message;
	int lcr_media = { (options.law=='a') ? MEDIA_TYPE_ALAW : MEDIA_TYPE_ULAW };
	unsigned char lcr_payload = { (options.law=='a') ? PAYLOAD_TYPE_ALAW : PAYLOAD_TYPE_ULAW };
	int *media_types;
	unsigned char *payload_types;
	int payloads = 0;
	int i;

	PDEBUG(DEBUG_SIP, "Doing Setup (inst %p)\n", inst);

	memcpy(&p_dialinginfo, &param->setup.dialinginfo, sizeof(p_dialinginfo));
	memcpy(&p_callerinfo, &param->setup.callerinfo, sizeof(p_callerinfo));
	memcpy(&p_redirinfo, &param->setup.redirinfo, sizeof(p_redirinfo));

	if (param->setup.rtpinfo.port) {
		PDEBUG(DEBUG_SIP, "RTP info given by remote, forward that\n");
		p_s_rtp_bridge = 1;
		media_types = param->setup.rtpinfo.media_types;
		payload_types = param->setup.rtpinfo.payload_types;
		payloads = param->setup.rtpinfo.payloads;
		p_s_rtp_ip_local = param->setup.rtpinfo.ip;
		p_s_rtp_port_local = param->setup.rtpinfo.port;
		PDEBUG(DEBUG_SIP, "local ip %08x port %d\n", p_s_rtp_ip_local, p_s_rtp_port_local);
		PDEBUG(DEBUG_SIP, "remote ip %08x port %d\n", p_s_rtp_ip_remote, p_s_rtp_port_remote);
	} else {
		PDEBUG(DEBUG_SIP, "RTP info not given by remote, so we do our own RTP\n");
		p_s_rtp_bridge = 0;
		media_types = &lcr_media;
		payload_types = &lcr_payload;
		payloads = 1;

		/* open local RTP peer (if not bridging) */
		if (rtp_open() < 0) {
			PERROR("Failed to open RTP sockets\n");
			/* send release message to endpoit */
			message = message_create(p_serial, epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
			message->param.disconnectinfo.cause = 41;
			message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
			message_put(message);
			new_state(PORT_STATE_RELEASE);
			trigger_work(&p_s_delete);
			return 0;
		}
	}

	p_s_handle = nua_handle(inst->nua, NULL, TAG_END());
	if (!p_s_handle) {
		PERROR("Failed to create handle\n");
		/* send release message to endpoit */
		message = message_create(p_serial, epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = 41;
		message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		message_put(message);
		new_state(PORT_STATE_RELEASE);
		trigger_work(&p_s_delete);
		return 0;
	}
	/* apply handle */
	sip_trace_header(this, "NEW handle", DIRECTION_IN);
	add_trace("handle", "new", "0x%x", p_s_handle);
	end_trace();

	if (!p_s_rtp_ip_local) {
		char *p;

		/* extract IP from local peer */
		SCPY(local_ip, local);
		p = strchr(local_ip, ':');
		if (p)
			*p = '\0';
		PDEBUG(DEBUG_SIP, "RTP local IP not known, so we use our local SIP ip %s\n", local_ip);
		inet_pton(AF_INET, local_ip, &p_s_rtp_ip_local);
		p_s_rtp_ip_local = ntohl(p_s_rtp_ip_local);
	}
	ia.s_addr = htonl(p_s_rtp_ip_local);
	SPRINT(sdp_str,
		"v=0\n"
		"o=LCR-Sofia-SIP 0 0 IN IP4 %s\n"
		"s=SIP Call\n"
		"c=IN IP4 %s\n"
		"t=0 0\n"
		"m=audio %d RTP/AVP"
		, inet_ntoa(ia), inet_ntoa(ia), p_s_rtp_port_local);
	for (i = 0; i < payloads; i++) {
		SPRINT(pt_str, " %d", payload_types[i]);
		SCAT(sdp_str, pt_str);
	}
	SCAT(sdp_str, "\n");
	for (i = 0; i < payloads; i++) {
		SPRINT(pt_str, "a=rtpmap:%d %s/8000\n", payload_types[i], media_type2name(media_types[i]));
		SCAT(sdp_str, pt_str);
	}
	PDEBUG(DEBUG_SIP, "Using SDP for invite: %s\n", sdp_str);

	SPRINT(from, "sip:%s@%s", param->setup.callerinfo.id, local);
	SPRINT(to, "sip:%s@%s", param->setup.dialinginfo.id, remote);

	sip_trace_header(this, "INVITE", DIRECTION_OUT);
	add_trace("from", "uri", "%s", from);
	add_trace("to", "uri", "%s", to);
	add_trace("rtp", "ip", "%s", inet_ntoa(ia));
	add_trace("rtp", "port", "%d,%d", p_s_rtp_port_local, p_s_rtp_port_local + 1);
	for (i = 0; i < payloads; i++)
		add_trace("rtp", "payload", "%s:%d", media_type2name(media_types[i]), payload_types[i]);
	end_trace();

//	cseq = sip_cseq_create(sip_home, 123, SIP_METHOD_INVITE);

	nua_invite(p_s_handle,
		TAG_IF(from[0], SIPTAG_FROM_STR(from)),
		TAG_IF(to[0], SIPTAG_TO_STR(to)),
		TAG_IF(cseq, SIPTAG_CSEQ(cseq)),
		NUTAG_MEDIA_ENABLE(0),
		SIPTAG_CONTENT_TYPE_STR("application/sdp"),
		SIPTAG_PAYLOAD_STR(sdp_str), TAG_END());
	new_state(PORT_STATE_OUT_SETUP);

#if 0
	PDEBUG(DEBUG_SIP, "do overlap\n");
	new_state(PORT_STATE_OUT_OVERLAP);
	message = message_create(p_serial, epoint_id, PORT_TO_EPOINT, MESSAGE_OVERLAP);
	message_put(message);
#else
	PDEBUG(DEBUG_SIP, "do proceeding\n");
	new_state(PORT_STATE_OUT_PROCEEDING);
	message = message_create(p_serial, epoint_id, PORT_TO_EPOINT, MESSAGE_PROCEEDING);
	message_put(message);
#endif

	/* attach only if not already */
	epointlist = p_epointlist;
	while(epointlist) {
		if (epointlist->epoint_id == epoint_id)
			break;
		epointlist = epointlist->next;
	}
	if (!epointlist)
		epointlist_new(epoint_id);

	return 0;
}
	
int Psip::message_notify(unsigned int epoint_id, int message_id, union parameter *param)
{
//	char sdp_str[256];
//	struct in_addr ia;

	switch (param->notifyinfo.notify) {
	case INFO_NOTIFY_REMOTE_HOLD:
#if 0
		SPRINT(sdp_str,
			"v=0\n"
			"o=LCR-Sofia-SIP 0 0 IN IP4 0.0.0.0\n"
			"s=SIP Call\n"
			"c=IN IP4 0.0.0.0\n"
			"t=0 0\n"
			);
		PDEBUG(DEBUG_SIP, "Using SDP for hold: %s\n", sdp_str);
		nua_info(p_s_handle,
//			TAG_IF(from[0], SIPTAG_FROM_STR(from)),
//			TAG_IF(to[0], SIPTAG_TO_STR(to)),
//			TAG_IF(cseq, SIPTAG_CSEQ(cseq)),
			NUTAG_MEDIA_ENABLE(0),
			SIPTAG_CONTENT_TYPE_STR("application/sdp"),
			SIPTAG_PAYLOAD_STR(sdp_str), TAG_END());
#endif
		break;
	case INFO_NOTIFY_REMOTE_RETRIEVAL:
#if 0
		ia.s_addr = htonl(p_s_rtp_ip_local);
		SPRINT(sdp_str,
			"v=0\n"
			"o=LCR-Sofia-SIP 0 0 IN IP4 %s\n"
			"s=SIP Call\n"
			"c=IN IP4 %s\n"
			"t=0 0\n"
			"m=audio %d RTP/AVP %d\n"
			"a=rtpmap:%d %s/8000\n"
			, inet_ntoa(ia), inet_ntoa(ia), p_s_rtp_port_local, p_s_rtp_payload_type, p_s_rtp_payload_type, media_type2name(p_s_rtp_media_type));
		PDEBUG(DEBUG_SIP, "Using SDP for rertieve: %s\n", sdp_str);
		nua_info(p_s_handle,
//			TAG_IF(from[0], SIPTAG_FROM_STR(from)),
//			TAG_IF(to[0], SIPTAG_TO_STR(to)),
//			TAG_IF(cseq, SIPTAG_CSEQ(cseq)),
			NUTAG_MEDIA_ENABLE(0),
			SIPTAG_CONTENT_TYPE_STR("application/sdp"),
			SIPTAG_PAYLOAD_STR(sdp_str), TAG_END());
#endif
		break;
	}

	return 0;
}

int Psip::message_dtmf(unsigned int epoint_id, int message_id, union parameter *param)
{
	char dtmf_str[64];
	
	/* prepare DTMF info payload */
	SPRINT(dtmf_str,
		"Signal=%c\n"
		"Duration=160\n"
		, param->dtmf);

	/* start invite to handle DTMF */
	nua_info(p_s_handle,
		NUTAG_MEDIA_ENABLE(0),
		SIPTAG_CONTENT_TYPE_STR("application/dtmf-relay"),
		SIPTAG_PAYLOAD_STR(dtmf_str), TAG_END());
	
	return 0;
}

/* NOTE: incomplete and not working */
int Psip::message_information(unsigned int epoint_id, int message_id, union parameter *param)
{
	char dtmf_str[64];
	
	/* prepare DTMF info payload */
	SPRINT(dtmf_str,
		"Signal=%s\n"
		"Duration=160\n"
		, param->information.id);

	/* start invite to handle DTMF */
	nua_info(p_s_handle,
		NUTAG_MEDIA_ENABLE(0),
		SIPTAG_CONTENT_TYPE_STR("application/dtmf-relay"),
		SIPTAG_PAYLOAD_STR(dtmf_str), TAG_END());
	
	return 0;
}

int Psip::message_epoint(unsigned int epoint_id, int message_id, union parameter *param)
{
	if (Port::message_epoint(epoint_id, message_id, param))
		return 1;

	switch(message_id) {
		case MESSAGE_ALERTING: /* call is ringing on LCR side */
		if (p_state != PORT_STATE_IN_SETUP
		 && p_state != PORT_STATE_IN_PROCEEDING)
			return 0;
		nua_respond(p_s_handle, SIP_180_RINGING, TAG_END());
		sip_trace_header(this, "RESPOND", DIRECTION_OUT);
		add_trace("respond", "value", "180 Ringing");
		end_trace();
		new_state(PORT_STATE_IN_ALERTING);
		return 1;

		case MESSAGE_CONNECT: /* call is connected on LCR side */
		if (p_state != PORT_STATE_IN_SETUP
		 && p_state != PORT_STATE_IN_PROCEEDING
		 && p_state != PORT_STATE_IN_ALERTING)
			return 0;
		message_connect(epoint_id, message_id, param);
		return 1;

		case MESSAGE_DISCONNECT: /* call has been disconnected */
		case MESSAGE_RELEASE: /* call has been released */
		message_release(epoint_id, message_id, param);
		return 1;

		case MESSAGE_SETUP: /* dial-out command received from epoint */
		message_setup(epoint_id, message_id, param);
		return 1;

		case MESSAGE_INFORMATION: /* overlap dialing */
		if (p_state != PORT_STATE_OUT_OVERLAP)
			return 0;
		message_information(epoint_id, message_id, param);
		return 1;

		case MESSAGE_DTMF: /* DTMF info to be transmitted via INFO transaction */
		if (p_state == PORT_STATE_CONNECT)
			message_dtmf(epoint_id, message_id, param);
		case MESSAGE_NOTIFY: /* notification about remote hold/retrieve */
		if (p_state == PORT_STATE_CONNECT)
			message_notify(epoint_id, message_id, param);
		return(1);

		default:
		PDEBUG(DEBUG_SIP, "PORT(%s) SP port with (caller id %s) received an unsupported message: %d\n", p_name, p_callerinfo.id, message_id);
	}

	return 0;
}

int Psip::parse_sdp(sip_t const *sip, unsigned int *ip, unsigned short *port, uint8_t *payload_types, int *media_types, int *payloads, int max_payloads)
{
	*payloads = 0;

	if (!sip->sip_payload) {
		PDEBUG(DEBUG_SIP, "no payload given\n");
		return 0;
	}

	sdp_parser_t *parser;
	sdp_session_t *sdp;
	sdp_media_t *m;
	sdp_attribute_t *attr;
	sdp_rtpmap_t *map;
	sdp_connection_t *conn;

	PDEBUG(DEBUG_SIP, "payload given: %s\n", sip->sip_payload->pl_data);

	parser = sdp_parse(NULL, sip->sip_payload->pl_data, (int) strlen(sip->sip_payload->pl_data), 0);
	if (!parser) {
		return 400;
	}
	if (!(sdp = sdp_session(parser))) {
		sdp_parser_free(parser);
		return 400;
	}
	for (m = sdp->sdp_media; m; m = m->m_next) {
		if (m->m_proto != sdp_proto_rtp)
			continue;
		if (m->m_type != sdp_media_audio)
			continue;
		PDEBUG(DEBUG_SIP, "RTP port:'%u'\n", m->m_port);
		*port = m->m_port;
		for (attr = m->m_attributes; attr; attr = attr->a_next) {
			PDEBUG(DEBUG_SIP, "ATTR: name:'%s' value='%s'\n", attr->a_name, attr->a_value);
		}
		if (m->m_connections) {
			conn = m->m_connections;
			PDEBUG(DEBUG_SIP, "CONN: address:'%s'\n", conn->c_address);
			inet_pton(AF_INET, conn->c_address, ip);
			*ip = ntohl(p_s_rtp_ip_remote);
		} else {
			char *p = sip->sip_payload->pl_data;
			char addr[16];

			PDEBUG(DEBUG_SIP, "sofia cannot find connection tag, so we try ourself\n");
			p = strstr(p, "c=IN IP4 ");
			if (!p) {
				PDEBUG(DEBUG_SIP, "missing c-tag with internet address\n");
				sdp_parser_free(parser);
				return 400;
			}
			SCPY(addr, p + 9);
			if ((p = strchr(addr, '\n'))) *p = '\0';
			if ((p = strchr(addr, '\r'))) *p = '\0';
			PDEBUG(DEBUG_SIP, "CONN: address:'%s'\n", addr);
			inet_pton(AF_INET, addr, ip);
			*ip = ntohl(p_s_rtp_ip_remote);
		}
		for (map = m->m_rtpmaps; map; map = map->rm_next) {
			int media_type = 0;

			PDEBUG(DEBUG_SIP, "RTPMAP: coding:'%s' rate='%d' pt='%d'\n", map->rm_encoding, map->rm_rate, map->rm_pt);
			/* append to payload list, if there is space */
			add_trace("rtp", "payload", "%s:%d", map->rm_encoding, map->rm_pt);
			if (map->rm_pt == PAYLOAD_TYPE_ALAW)
				media_type = MEDIA_TYPE_ALAW;
			else if (map->rm_pt == PAYLOAD_TYPE_ULAW)
				media_type = MEDIA_TYPE_ULAW;
			else if (map->rm_pt == PAYLOAD_TYPE_GSM)
				media_type = MEDIA_TYPE_GSM;
			else if (!strcmp(map->rm_encoding, "GSM-EFR"))
				media_type = MEDIA_TYPE_GSM_EFR;
			else if (!strcmp(map->rm_encoding, "AMR"))
				media_type = MEDIA_TYPE_AMR;
			else if (!strcmp(map->rm_encoding, "GSM-HR"))
				media_type = MEDIA_TYPE_GSM_HR;
			if (media_type && *payloads <= max_payloads) {
				*payload_types++ = map->rm_pt;
				*media_types++ = media_type;
				(*payloads)++;
			}
		}
	}

	sdp_parser_free(parser);

	return 0;
}

void Psip::i_invite(int status, char const *phrase, nua_t *nua, nua_magic_t *magic, nua_handle_t *nh, nua_hmagic_t *hmagic, sip_t const *sip, tagi_t tagss[])
{
	struct sip_inst *inst = (struct sip_inst *) p_s_sip_inst;
	const char *from = "", *to = "", *name = "";
	char imsi[16] = "";
	int ret;
	class Endpoint *epoint;
	struct lcr_msg *message;
	struct interface *interface;
	int media_types[32];
	uint8_t payload_types[32];
	int payloads = 0;
	int media_type;

	interface = getinterfacebyname(inst->interface_name);
	if (!interface) {
		PERROR("Cannot find interface %s.\n", inst->interface_name);
		return;
	}

	if (sip->sip_from) {
		if (sip->sip_from->a_url)
			from = sip->sip_from->a_url->url_user;
		if (sip->sip_from->a_display) {
			name = sip->sip_from->a_display;
			if (!strncmp(name, "\"IMSI", 5)) {
				strncpy(imsi, name + 5, 15);
				imsi[15] = '\0';
				name = "";
			}
		}
	}
	if (sip->sip_to) {
		if (sip->sip_to->a_url)
			to = sip->sip_to->a_url->url_user;
	}
	PDEBUG(DEBUG_SIP, "invite received (%s->%s)\n", from, to);

	sip_trace_header(this, "Payload received", DIRECTION_NONE);
	ret = parse_sdp(sip, &p_s_rtp_ip_remote, &p_s_rtp_port_remote, payload_types, media_types, &payloads, sizeof(payload_types));
	if (!ret) {
		/* if no RTP bridge, we must support LAW codec, otherwise we forward what we have */
		if (!p_s_rtp_bridge) {
			int i;

			/* check if supported payload type exists */
			for (i = 0; i < payloads; i++) {
				if (media_types[i] == ((options.law=='a') ? MEDIA_TYPE_ALAW : MEDIA_TYPE_ULAW))
					break;
			}
			if (i == payloads) {
				add_trace("error", NULL, "Expected LAW payload type (not bridged)");
				ret = 415;
			}
		}
	}
	end_trace();
	if (ret) {
		if (ret == 400)
			nua_respond(nh, SIP_400_BAD_REQUEST, TAG_END());
		else
			nua_respond(nh, SIP_415_UNSUPPORTED_MEDIA, TAG_END());
		nua_handle_destroy(nh);
		p_s_handle = NULL;
		sip_trace_header(this, "RESPOND", DIRECTION_OUT);
		if (ret == 400)
			add_trace("respond", "value", "415 Unsupported Media");
		else
			add_trace("respond", "value", "400 Bad Request");
		add_trace("reason", NULL, "offered codec does not match");
		end_trace();
		new_state(PORT_STATE_RELEASE);
		trigger_work(&p_s_delete);
		return;
	}

	/* open local RTP peer (if not bridging) */
	if (!p_s_rtp_bridge && rtp_open() < 0) {
		nua_respond(nh, SIP_500_INTERNAL_SERVER_ERROR, TAG_END());
		nua_handle_destroy(nh);
		p_s_handle = NULL;
		sip_trace_header(this, "RESPOND", DIRECTION_OUT);
		add_trace("respond", "value", "500 Internal Server Error");
		add_trace("reason", NULL, "failed to open RTP/RTCP sockts");
		end_trace();
		new_state(PORT_STATE_RELEASE);
		trigger_work(&p_s_delete);
		return;
	}

	/* apply handle */
	sip_trace_header(this, "NEW handle", DIRECTION_IN);
	add_trace("handle", "new", "0x%x", nh);
	p_s_handle = nh;
	end_trace();

	sip_trace_header(this, "INVITE", DIRECTION_IN);
	add_trace("rtp", "port", "%d", p_s_rtp_port_remote);
	/* caller information */
	if (!from[0]) {
		p_callerinfo.present = INFO_PRESENT_NOTAVAIL;
		p_callerinfo.ntype = INFO_NTYPE_NOTPRESENT;
		add_trace("calling", "present", "unavailable");
	} else {
		p_callerinfo.present = INFO_PRESENT_ALLOWED;
		add_trace("calling", "present", "allowed");
		p_callerinfo.screen = INFO_SCREEN_NETWORK;
		p_callerinfo.ntype = INFO_NTYPE_UNKNOWN;
		SCPY(p_callerinfo.id, from);
		add_trace("calling", "number", "%s", from);
		SCPY(p_callerinfo.name, name);
		if (name[0])
			add_trace("calling", "name", "%s", name);
		SCPY(p_callerinfo.imsi, imsi);
		if (imsi[0])
			add_trace("calling", "imsi", "%s", imsi);
	}
	SCPY(p_callerinfo.interface, inst->interface_name);
	/* dialing information */
	if (to[0]) {
		p_dialinginfo.ntype = INFO_NTYPE_UNKNOWN;
		SCAT(p_dialinginfo.id, to);
		add_trace("dialing", "number", "%s", to);
	}
	/* redir info */
	/* bearer capability */
	p_capainfo.bearer_capa = INFO_BC_SPEECH;
	p_capainfo.bearer_info1 = (options.law=='a')?3:2;
	p_capainfo.bearer_mode = INFO_BMODE_CIRCUIT;
	add_trace("bearer", "capa", "speech");
	add_trace("bearer", "mode", "circuit");
	/* if packet mode works some day, see dss1.cpp for conditions */
	p_capainfo.source_mode = B_MODE_TRANSPARENT;

	end_trace();

	/* create endpoint */
	if (p_epointlist)
		FATAL("Incoming call but already got an endpoint.\n");
	if (!(epoint = new Endpoint(p_serial, 0)))
		FATAL("No memory for Endpoint instance\n");
	epoint->ep_app = new_endpointapp(epoint, 0, interface->app); //incoming
	epointlist_new(epoint->ep_serial);

#ifdef NUTAG_AUTO100
	/* send trying (proceeding) */
	nua_respond(nh, SIP_100_TRYING, TAG_END());
	sip_trace_header(this, "RESPOND", DIRECTION_OUT);
	add_trace("respond", "value", "100 Trying");
	end_trace();
#endif

	new_state(PORT_STATE_IN_PROCEEDING);

	/* send setup message to endpoit */
	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_SETUP);
	message->param.setup.port_type = p_type;
//	message->param.setup.dtmf = 0;
	memcpy(&message->param.setup.dialinginfo, &p_dialinginfo, sizeof(struct dialing_info));
	memcpy(&message->param.setup.callerinfo, &p_callerinfo, sizeof(struct caller_info));
	memcpy(&message->param.setup.capainfo, &p_capainfo, sizeof(struct capa_info));
//	SCPY((char *)message->param.setup.useruser.data, useruser.info);
//	message->param.setup.useruser.len = strlen(mncc->useruser.info);
//	message->param.setup.useruser.protocol = mncc->useruser.proto;
	if (p_s_rtp_bridge) {
		int i;

		PDEBUG(DEBUG_SIP, "sending setup with RTP info\n");
		message->param.setup.rtpinfo.ip = p_s_rtp_ip_remote;
		message->param.setup.rtpinfo.port = p_s_rtp_port_remote;
		/* add codecs to setup message */
		for (i = 0; i < payloads; i++) {
			message->param.setup.rtpinfo.media_types[i] = media_types[i];
			message->param.setup.rtpinfo.payload_types[i] = payload_types[i];
			if (i == sizeof(message->param.setup.rtpinfo.payload_types))
				break;
		}
		message->param.setup.rtpinfo.payloads = i;
	}
	message_put(message);

	/* send progress, if tones are available and if we don't bridge */
	if (!p_s_rtp_bridge && interface->is_tones == IS_YES) {
		char sdp_str[256];
		struct in_addr ia;
		unsigned char payload_type;

		PDEBUG(DEBUG_SIP, "Connecting audio, since we have tones available\n");
		media_type = (options.law=='a') ? MEDIA_TYPE_ALAW : MEDIA_TYPE_ULAW;
		payload_type = (options.law=='a') ? PAYLOAD_TYPE_ALAW : PAYLOAD_TYPE_ULAW;
		/* open local RTP peer (if not bridging) */
		if (rtp_connect() < 0) {
			nua_respond(nh, SIP_500_INTERNAL_SERVER_ERROR, TAG_END());
			nua_handle_destroy(nh);
			p_s_handle = NULL;
			sip_trace_header(this, "RESPOND", DIRECTION_OUT);
			add_trace("respond", "value", "500 Internal Server Error");
			add_trace("reason", NULL, "failed to connect RTP/RTCP sockts");
			end_trace();
			new_state(PORT_STATE_RELEASE);
			trigger_work(&p_s_delete);
			message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_RELEASE);
			message->param.disconnectinfo.cause = 41;
			message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
			message_put(message);
			new_state(PORT_STATE_RELEASE);
			trigger_work(&p_s_delete);
			return;
		}

		ia.s_addr = htonl(p_s_rtp_ip_local);

		SPRINT(sdp_str,
			"v=0\n"
			"o=LCR-Sofia-SIP 0 0 IN IP4 %s\n"
			"s=SIP Call\n"
			"c=IN IP4 %s\n"
			"t=0 0\n"
			"m=audio %d RTP/AVP %d\n"
			"a=rtpmap:%d %s/8000\n"
			, inet_ntoa(ia), inet_ntoa(ia), p_s_rtp_port_local, payload_type, payload_type, media_type2name(media_type));
		PDEBUG(DEBUG_SIP, "Using SDP response: %s\n", sdp_str);

		nua_respond(p_s_handle, SIP_183_SESSION_PROGRESS,
			NUTAG_MEDIA_ENABLE(0),
			SIPTAG_CONTENT_TYPE_STR("application/sdp"),
			SIPTAG_PAYLOAD_STR(sdp_str), TAG_END());
		sip_trace_header(this, "RESPOND", DIRECTION_OUT);
		add_trace("respond", "value", "183 SESSION PROGRESS");
		add_trace("reason", NULL, "audio available");
		add_trace("rtp", "ip", "%s", inet_ntoa(ia));
		add_trace("rtp", "port", "%d,%d", p_s_rtp_port_local, p_s_rtp_port_local + 1);
		add_trace("rtp", "payload", "%s:%d", media_type2name(media_type), payload_type);
		end_trace();
	}
}

void Psip::i_bye(int status, char const *phrase, nua_t *nua, nua_magic_t *magic, nua_handle_t *nh, nua_hmagic_t *hmagic, sip_t const *sip, tagi_t tagss[])
{
	struct lcr_msg *message;
	int cause = 0;

	PDEBUG(DEBUG_SIP, "bye received\n");

	sip_trace_header(this, "BYE", DIRECTION_IN);
	if (sip->sip_reason && sip->sip_reason->re_protocol && !strcasecmp(sip->sip_reason->re_protocol, "Q.850") && sip->sip_reason->re_cause) {
		cause = atoi(sip->sip_reason->re_cause);
		add_trace("cause", "value", "%d", cause);
	}
	end_trace();

// let stack do bye automaticall, since it will not accept our response for some reason
//	nua_respond(nh, SIP_200_OK, TAG_END());
	sip_trace_header(this, "RESPOND", DIRECTION_OUT);
	add_trace("respond", "value", "200 OK");
	end_trace();
//	nua_handle_destroy(nh);
	p_s_handle = NULL;

	rtp_close();

	while(p_epointlist) {
		/* send setup message to endpoit */
		message = message_create(p_serial, p_epointlist->epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = cause ? : 16;
		message->param.disconnectinfo.location = LOCATION_BEYOND;
		message_put(message);
		/* remove epoint */
		free_epointlist(p_epointlist);
	}
	new_state(PORT_STATE_RELEASE);
	trigger_work(&p_s_delete);
}

void Psip::i_cancel(int status, char const *phrase, nua_t *nua, nua_magic_t *magic, nua_handle_t *nh, nua_hmagic_t *hmagic, sip_t const *sip, tagi_t tagss[])
{
	struct lcr_msg *message;

	PDEBUG(DEBUG_SIP, "cancel received\n");

	sip_trace_header(this, "CANCEL", DIRECTION_IN);
	end_trace();

	nua_handle_destroy(nh);
	p_s_handle = NULL;

	rtp_close();

	while(p_epointlist) {
		/* send setup message to endpoit */
		message = message_create(p_serial, p_epointlist->epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = 16;
		message->param.disconnectinfo.location = LOCATION_BEYOND;
		message_put(message);
		/* remove epoint */
		free_epointlist(p_epointlist);
	}
	new_state(PORT_STATE_RELEASE);
	trigger_work(&p_s_delete);
}

void Psip::r_bye(int status, char const *phrase, nua_t *nua, nua_magic_t *magic, nua_handle_t *nh, nua_hmagic_t *hmagic, sip_t const *sip, tagi_t tagss[])
{
	PDEBUG(DEBUG_SIP, "bye response received\n");

	nua_handle_destroy(nh);
	p_s_handle = NULL;

	rtp_close();

	trigger_work(&p_s_delete);
}

void Psip::r_cancel(int status, char const *phrase, nua_t *nua, nua_magic_t *magic, nua_handle_t *nh, nua_hmagic_t *hmagic, sip_t const *sip, tagi_t tagss[])
{
	PDEBUG(DEBUG_SIP, "cancel response received\n");

	nua_handle_destroy(nh);
	p_s_handle = NULL;

	rtp_close();

	trigger_work(&p_s_delete);
}

void Psip::r_invite(int status, char const *phrase, nua_t *nua, nua_magic_t *magic, nua_handle_t *nh, nua_hmagic_t *hmagic, sip_t const *sip, tagi_t tagss[])
{
	struct lcr_msg *message;
	int cause = 0, location = 0;
	int media_types[32];
	uint8_t payload_types[32];
	int payloads = 0;

	PDEBUG(DEBUG_SIP, "response to invite received (status = %d)\n", status);

	sip_trace_header(this, "RESPOND", DIRECTION_OUT);
	add_trace("respond", "value", "%d", status);
	end_trace();

	/* connect audio */
	if (status == 183 || (status >= 200 && status <= 299)) {
		int ret;

		sip_trace_header(this, "Payload received", DIRECTION_NONE);
		ret = parse_sdp(sip, &p_s_rtp_ip_remote, &p_s_rtp_port_remote, payload_types, media_types, &payloads, sizeof(payload_types));
		if (!ret) {
			if (payloads != 1)
				ret = 415;
			else if (!p_s_rtp_bridge) {
				if (media_types[0] != ((options.law=='a') ? MEDIA_TYPE_ALAW : MEDIA_TYPE_ULAW)) {
					add_trace("error", NULL, "Expected LAW payload type (not bridged)");
					ret = 415;
				}
			}
		}
		end_trace();
		if (ret) {
			nua_cancel(nh, TAG_END());
			sip_trace_header(this, "CANCEL", DIRECTION_OUT);
			add_trace("reason", NULL, "accepted codec does not match");
			end_trace();
			cause = 88;
			location = LOCATION_PRIVATE_LOCAL;
			goto release_with_cause;
		}

		/* connect to remote RTP (if not bridging) */
		if (!p_s_rtp_bridge && rtp_connect() < 0) {
			nua_cancel(nh, TAG_END());
			sip_trace_header(this, "CANCEL", DIRECTION_OUT);
			add_trace("reason", NULL, "failed to open RTP/RTCP sockts");
			end_trace();
			cause = 31;
			location = LOCATION_PRIVATE_LOCAL;
			goto release_with_cause;
		}
	}

	/* process 1xx */
	switch (status) {
	case 100:
#if 0
		PDEBUG(DEBUG_SIP, "do proceeding\n");
		new_state(PORT_STATE_OUT_PROCEEDING);
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_PROCEEDING);
		message_put(message);
#endif
		return;
	case 180:
		PDEBUG(DEBUG_SIP, "do alerting\n");
		new_state(PORT_STATE_OUT_ALERTING);
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_ALERTING);
		message_put(message);
		return;
	case 183:
		PDEBUG(DEBUG_SIP, "do progress\n");
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_PROGRESS);
		message->param.progressinfo.progress = 8;
		message->param.progressinfo.location = 10;
		if (p_s_rtp_bridge) {
			message->param.progressinfo.rtpinfo.ip = p_s_rtp_ip_remote;
			message->param.progressinfo.rtpinfo.port = p_s_rtp_port_remote;
			message->param.progressinfo.rtpinfo.media_types[0] = media_types[0];
			message->param.progressinfo.rtpinfo.payload_types[0] = payload_types[0];
			message->param.progressinfo.rtpinfo.payloads = 1;
		}
		message_put(message);
		return;
	default:
		if (status < 100 || status > 199)
			break;
		PDEBUG(DEBUG_SIP, "skipping 1xx message\n");

		return;
	}

	/* process 2xx */
	if (status >= 200 && status <= 299) {
		PDEBUG(DEBUG_SIP, "do connect\n");
		nua_ack(nh, TAG_END());
		new_state(PORT_STATE_CONNECT);
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_CONNECT);
		if (p_s_rtp_bridge) {
			message->param.connectinfo.rtpinfo.ip = p_s_rtp_ip_remote;
			message->param.connectinfo.rtpinfo.port = p_s_rtp_port_remote;
			message->param.connectinfo.rtpinfo.media_types[0] = media_types[0];
			message->param.connectinfo.rtpinfo.payload_types[0] = payload_types[0];
			message->param.connectinfo.rtpinfo.payloads = 1;
		}
		message_put(message);
		return;
	}
	cause = status2cause(status);
	location = LOCATION_BEYOND;

release_with_cause:
	PDEBUG(DEBUG_SIP, "do release (cause %d)\n", cause);

	while(p_epointlist) {
		/* send setup message to endpoit */
		message = message_create(p_serial, p_epointlist->epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = cause;
		message->param.disconnectinfo.location = location;
		message_put(message);
		/* remove epoint */
		free_epointlist(p_epointlist);
	}

	new_state(PORT_STATE_RELEASE);

	rtp_close();

	trigger_work(&p_s_delete);
}

static void sip_callback(nua_event_t event, int status, char const *phrase, nua_t *nua, nua_magic_t *magic, nua_handle_t *nh, nua_hmagic_t *hmagic, sip_t const *sip, tagi_t tags[])
{
	struct sip_inst *inst = (struct sip_inst *) magic;
	class Port *port;
	class Psip *psip = NULL;

	PDEBUG(DEBUG_SIP, "Event %d from stack received (handle=%p)\n", event, nh);
	if (!nh)
		return;

	/* create or find port instance */
	if (event == nua_i_invite)
	{
		char name[64];
		struct interface *interface = interface_first;

		/* create call instance */
		SPRINT(name, "%s-%d-in", inst->interface_name, 0);
		while (interface) {
			if (!strcmp(interface->name, inst->interface_name))
				break;
			interface = interface->next;
		}
		if (!interface) {
			PERROR("Cannot find interface %s.\n", inst->interface_name);
			return;
		}
		if (!(psip = new Psip(PORT_TYPE_SIP_IN, name, NULL, interface)))
			FATAL("Cannot create Port instance.\n");
	} else {
		port = port_first;
		while(port) {
			if ((port->p_type & PORT_CLASS_mISDN_MASK) == PORT_CLASS_SIP) {
				psip = (class Psip *)port;
				if (psip->p_s_handle == nh) {
					break;
				}
			}
			port = port->next;
		}
	}
	if (!psip) {
		PERROR("no SIP Port found for handel %p\n", nh);
		nua_respond(nh, SIP_500_INTERNAL_SERVER_ERROR, TAG_END());
		nua_handle_destroy(nh);
		return;
	}

	switch (event) {
	case nua_r_set_params:
		PDEBUG(DEBUG_SIP, "setparam response\n");
		break;
	case nua_i_error:
		PDEBUG(DEBUG_SIP, "error received\n");
		break;
	case nua_i_state:
		PDEBUG(DEBUG_SIP, "state change received\n");
		break;
	case nua_i_register:
		PDEBUG(DEBUG_SIP, "register received\n");
		break;
	case nua_i_invite:
		psip->i_invite(status, phrase, nua, magic, nh, hmagic, sip, tags);
		break;
	case nua_i_ack:
		PDEBUG(DEBUG_SIP, "ack received\n");
		break;
	case nua_i_active:
		PDEBUG(DEBUG_SIP, "active received\n");
		break;
	case nua_i_bye:
		psip->i_bye(status, phrase, nua, magic, nh, hmagic, sip, tags);
		break;
	case nua_i_cancel:
		psip->i_cancel(status, phrase, nua, magic, nh, hmagic, sip, tags);
		break;
	case nua_r_bye:
		psip->r_bye(status, phrase, nua, magic, nh, hmagic, sip, tags);
		break;
	case nua_r_cancel:
		psip->r_cancel(status, phrase, nua, magic, nh, hmagic, sip, tags);
		break;
	case nua_r_invite:
		psip->r_invite(status, phrase, nua, magic, nh, hmagic, sip, tags);
		break;
	case nua_i_terminated:
		PDEBUG(DEBUG_SIP, "terminated received\n");
		break;
	default:
		PDEBUG(DEBUG_SIP, "Event %d not handled\n", event);
	}
}

/* received shutdown due to termination of RTP */
void Psip::rtp_shutdown(void)
{
	struct lcr_msg *message;

	PDEBUG(DEBUG_SIP, "RTP stream terminated\n");

	sip_trace_header(this, "RTP terminated", DIRECTION_IN);
	end_trace();

	nua_handle_destroy(p_s_handle);
	p_s_handle = NULL;

	while(p_epointlist) {
		/* send setup message to endpoit */
		message = message_create(p_serial, p_epointlist->epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = 16;
		message->param.disconnectinfo.location = LOCATION_BEYOND;
		message_put(message);
		/* remove epoint */
		free_epointlist(p_epointlist);
	}
	new_state(PORT_STATE_RELEASE);
	trigger_work(&p_s_delete);
}

int sip_init_inst(struct interface *interface)
{
	struct sip_inst *inst = (struct sip_inst *) MALLOC(sizeof(*inst));
	char local[64];

	interface->sip_inst = inst;
	SCPY(inst->interface_name, interface->name);
	SCPY(inst->local_peer, interface->sip_local_peer);
	SCPY(inst->remote_peer, interface->sip_remote_peer);

	/* init root object */
	inst->root = su_root_create(inst);
	if (!inst->root) {
		PERROR("Failed to create SIP root\n");
		sip_exit_inst(interface);
		return -EINVAL;
	}

	SPRINT(local, "sip:%s",inst->local_peer);
	if (!strchr(inst->local_peer, ':'))
		SCAT(local, ":5060");
	inst->nua = nua_create(inst->root, sip_callback, inst, NUTAG_URL(local), TAG_END());
	if (!inst->nua) {
		PERROR("Failed to create SIP stack object\n");
		sip_exit_inst(interface);
		return -EINVAL;
	}
	nua_set_params(inst->nua,
		SIPTAG_ALLOW_STR("INVITE,ACK,BYE,CANCEL,OPTIONS,NOTIFY,INFO"),
		NUTAG_APPL_METHOD("INVITE"),
		NUTAG_APPL_METHOD("ACK"),
//		NUTAG_APPL_METHOD("BYE"), /* we must reply to BYE */
		NUTAG_APPL_METHOD("CANCEL"),
		NUTAG_APPL_METHOD("OPTIONS"),
		NUTAG_APPL_METHOD("NOTIFY"),
		NUTAG_APPL_METHOD("INFO"),
		NUTAG_AUTOACK(0),
#ifdef NUTAG_AUTO100
		NUTAG_AUTO100(0),
#endif
		NUTAG_AUTOALERT(0),
		NUTAG_AUTOANSWER(0),
		TAG_NULL());

	PDEBUG(DEBUG_SIP, "SIP interface created (inst=%p)\n", inst);

	any_sip_interface = 1;

	return 0;
}

void sip_exit_inst(struct interface *interface)
{
	struct sip_inst *inst = (struct sip_inst *) interface->sip_inst;

	if (!inst)
		return;
	if (inst->root)
		su_root_destroy(inst->root);
	if (inst->nua) {
		nua_destroy(inst->nua);
	}
	FREE(inst, sizeof(*inst));
	interface->sip_inst = NULL;

	PDEBUG(DEBUG_SIP, "SIP interface removed\n");

	/* check if there is any other SIP interface left */
	interface = interface_first;
	while (interface) {
		if (interface->sip_inst)
			break;
		interface = interface->next;
	}
	if (!interface)
		any_sip_interface = 0;
}

extern su_log_t su_log_default[];
extern su_log_t nua_log[];
//extern su_log_t soa_log[];

int sip_init(void)
{
	int i;

	/* init SOFIA lib */
	su_init();
	su_home_init(sip_home);

	if (options.deb & DEBUG_SIP) {
		su_log_set_level(su_log_default, 9);
		su_log_set_level(nua_log, 9);
		//su_log_set_level(soa_log, 9);
	}

	for (i = 0; i < 256; i++)
		flip[i] = ((i & 1) << 7) + ((i & 2) << 5) + ((i & 4) << 3) + ((i & 8) << 1) + ((i & 16) >> 1) + ((i & 32) >> 3) + ((i & 64) >> 5) + ((i & 128) >> 7);

	PDEBUG(DEBUG_SIP, "SIP globals initialized\n");

	return 0;
}

void sip_exit(void)
{
	su_home_deinit(sip_home);
	su_deinit();

	PDEBUG(DEBUG_SIP, "SIP globals de-initialized\n");
}

void sip_handle(void)
{
	struct interface *interface = interface_first;
	struct sip_inst *inst;

	while (interface) {
		if (interface->sip_inst) {
			inst = (struct sip_inst *) interface->sip_inst;
			su_root_step(inst->root, 0);
		}
		interface = interface->next;
	}
}

/* deletes when back in event loop */
static int delete_event(struct lcr_work *work, void *instance, int index)
{
	class Psip *psip = (class Psip *)instance;

	delete psip;

	return 0;
}


/*
 * generate audio, if no data is received from bridge
 */

void Psip::set_tone(const char *dir, const char *tone)
{
	Port::set_tone(dir, tone);

	update_load();
}

void Psip::update_load(void)
{
	/* don't trigger load event if event already active */
	if (p_s_loadtimer.active)
		return;

	/* don't start timer if ... */
	if (!p_tone_name[0])
		return;

	p_s_next_tv_sec = 0;
	schedule_timer(&p_s_loadtimer, 0, 0); /* no delay the first time */
}

static int load_timer(struct lcr_timer *timer, void *instance, int index)
{
	class Psip *psip = (class Psip *)instance;

	/* stop timer if ... */
	if (!psip->p_tone_name[0])
		return 0;

	psip->load_tx();

	return 0;
}

#define SEND_SIP_LEN 160

void Psip::load_tx(void)
{
	int diff;
	struct timeval current_time;
	int tosend = SEND_SIP_LEN, i;
	unsigned char buf[SEND_SIP_LEN], *p = buf;

	/* get elapsed */
	gettimeofday(&current_time, NULL);
	if (!p_s_next_tv_sec) {
		/* if timer expired the first time, set next expected timeout 160 samples in advance */
		p_s_next_tv_sec = current_time.tv_sec;
		p_s_next_tv_usec = current_time.tv_usec + SEND_SIP_LEN * 125;
		if (p_s_next_tv_usec >= 1000000) {
			p_s_next_tv_usec -= 1000000;
			p_s_next_tv_sec++;
		}
		schedule_timer(&p_s_loadtimer, 0, SEND_SIP_LEN * 125);
	} else {
		diff = 1000000 * (current_time.tv_sec - p_s_next_tv_sec)
			+ (current_time.tv_usec - p_s_next_tv_usec);
		if (diff < -SEND_SIP_LEN * 125 || diff > SEND_SIP_LEN * 125) {
			/* if clock drifts too much, set next timeout event to current timer + 160 */
			diff = 0;
			p_s_next_tv_sec = current_time.tv_sec;
			p_s_next_tv_usec = current_time.tv_usec + SEND_SIP_LEN * 125;
			if (p_s_next_tv_usec >= 1000000) {
				p_s_next_tv_usec -= 1000000;
				p_s_next_tv_sec++;
			}
		} else {
			/* if diff is positive, it took too long, so next timeout will be earlier */
			p_s_next_tv_usec += SEND_SIP_LEN * 125;
			if (p_s_next_tv_usec >= 1000000) {
				p_s_next_tv_usec -= 1000000;
				p_s_next_tv_sec++;
			}
		}
		schedule_timer(&p_s_loadtimer, 0, SEND_SIP_LEN * 125 - diff);
	}

	/* copy tones */
	if (p_tone_name[0]) {
		tosend -= read_audio(p, tosend);
	}
	if (tosend) {
		PERROR("buffer is not completely filled\n");
		return;
	}

	p = buf;
	for (i = 0; i < SEND_SIP_LEN; i++) {
		*p = flip[*p];
		p++;
	}
	/* transmit data via rtp */
	rtp_send_frame(buf, SEND_SIP_LEN, (options.law=='a')?PAYLOAD_TYPE_ALAW:PAYLOAD_TYPE_ULAW);
}

