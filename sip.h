/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** SIP-port header file                                                      **
**                                                                           **
\*****************************************************************************/ 

#include <sofia-sip/nua.h>

/* SIP port class */
class Psip : public Port
{
	public:
	Psip(int type, char *portname, struct port_settings *settings, struct interface *interface);
	~Psip();
	int message_epoint(unsigned int epoint_id, int message, union parameter *param);
	int message_connect(unsigned int epoint_id, int message, union parameter *param);
	int message_release(unsigned int epoint_id, int message, union parameter *param);
	int message_setup(unsigned int epoint_id, int message, union parameter *param);
	int message_notify(unsigned int epoint_id, int message, union parameter *param);
	int message_information(unsigned int epoint_id, int message, union parameter *param);
	int message_dtmf(unsigned int epoint_id, int message, union parameter *param);
	void i_invite(int status, char const *phrase, nua_t *nua, nua_magic_t *magic, nua_handle_t *nh, nua_hmagic_t *hmagic, sip_t const *sip, tagi_t tags[]);
	void i_bye(int status, char const *phrase, nua_t *nua, nua_magic_t *magic, nua_handle_t *nh, nua_hmagic_t *hmagic, sip_t const *sip, tagi_t tags[]);
	void i_cancel(int status, char const *phrase, nua_t *nua, nua_magic_t *magic, nua_handle_t *nh, nua_hmagic_t *hmagic, sip_t const *sip, tagi_t tags[]);
	void r_bye(int status, char const *phrase, nua_t *nua, nua_magic_t *magic, nua_handle_t *nh, nua_hmagic_t *hmagic, sip_t const *sip, tagi_t tags[]);
	void r_cancel(int status, char const *phrase, nua_t *nua, nua_magic_t *magic, nua_handle_t *nh, nua_hmagic_t *hmagic, sip_t const *sip, tagi_t tags[]);
	void r_invite(int status, char const *phrase, nua_t *nua, nua_magic_t *magic, nua_handle_t *nh, nua_hmagic_t *hmagic, sip_t const *sip, tagi_t tags[]);
	void *p_s_sip_inst;
	struct lcr_work p_s_delete;
	nua_handle_t *p_s_handle;
	nua_magic_t *p_s_magic;
	int p_s_rtp_bridge; /* bridge RTP instead of having a local RTP peer */
	uint8_t p_s_rtp_payload_type;
	unsigned short p_s_rtp_port_local;
	unsigned short p_s_rtp_port_remote;
	unsigned int p_s_rtp_ip_local;
	unsigned int p_s_rtp_ip_remote;
	struct sockaddr_in p_s_rtp_sin_local;
	struct sockaddr_in p_s_rtp_sin_remote;
	struct sockaddr_in p_s_rtcp_sin_local;
	struct sockaddr_in p_s_rtcp_sin_remote;
	struct lcr_fd p_s_rtp_fd;
	struct lcr_fd p_s_rtcp_fd;
	int p_s_rtp_is_connected; /* if RTP session is connected, so we may send frames */
	int p_s_rtp_tx_action;
	uint16_t p_s_rtp_tx_sequence;
	uint32_t p_s_rtp_tx_timestamp;
	uint32_t p_s_rtp_tx_ssrc;
	struct timeval p_s_rtp_tx_last_tv;
	int rtp_open(void);
	int rtp_connect(void);
	void rtp_close(void);
	int rtp_send_frame(unsigned char *data, unsigned int len, uint8_t payload_type);
	int p_s_b_sock; /* SIP bchannel socket */
	struct lcr_fd p_s_b_fd; /* event node */
	int p_s_b_index; /* SIP bchannel socket index to use */
	int p_s_b_active; /* SIP bchannel socket is activated */
	unsigned char p_s_rxdata[160]; /* receive audio buffer */
	int p_s_rxpos; /* position in audio buffer 0..159 */
	int bridge_rx(unsigned char *data, int len);
	int parse_sdp(sip_t const *sip, unsigned int *ip, unsigned short *port, uint8_t payload_type);
	void rtp_shutdown(void);
};

int sip_init_inst(struct interface *interface);
void sip_exit_inst(struct interface *interface);
int sip_init(void);
void sip_exit(void);
void sip_handle(void);
