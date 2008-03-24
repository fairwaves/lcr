/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** dss1-port header file                                                     **
**                                                                           **
\*****************************************************************************/ 

/* DSS1 port classes */
class Pdss1 : public PmISDN
{
	public:
	Pdss1(int type, struct mISDNport *mISDNport, char *portname, struct port_settings *settings, int channel, int exclusive);
	~Pdss1();
	int p_m_d_l3id;				/* current l3 process id */
	int p_m_d_ces;				/* ntmode: tei&sapi */
	int handler(void);
	int message_epoint(unsigned long epoint_id, int message, union parameter *param);
	void message_isdn(unsigned long prim, unsigned long dinfo, void *data);

	int p_m_d_ntmode;			/* flags the nt-mode */
	struct message *p_m_d_queue;		/* queue for SETUP if link is down */
	struct message *p_m_d_notify_pending;	/* queue for NOTIFY if not connected */

	int p_m_d_collect_cause;		/* collecting cause and location */
	int p_m_d_collect_location;

	void new_state(int state);		/* set new state */
//	void isdn_show_send_message(unsigned long prim, msg_t *msg);
	int received_first_reply_to_setup(unsigned long prim, int channel, int exclusive);
	int hunt_bchannel(int exclusive, int channel);
	void information_ind(unsigned long prim, unsigned long dinfo, void *data);
	void setup_ind(unsigned long prim, unsigned long dinfo, void *data);
	void setup_acknowledge_ind(unsigned long prim, unsigned long dinfo, void *data);
	void proceeding_ind(unsigned long prim, unsigned long dinfo, void *data);
	void alerting_ind(unsigned long prim, unsigned long dinfo, void *data);
	void connect_ind(unsigned long prim, unsigned long dinfo, void *data);
	void disconnect_ind(unsigned long prim, unsigned long dinfo, void *data);
	void release_ind(unsigned long prim, unsigned long dinfo, void *data);
	void release_complete_ind(unsigned long prim, unsigned long dinfo, void *data);
	void disconnect_ind_i(unsigned long prim, unsigned long dinfo, void *data);
	void t312_timeout(unsigned long prim, unsigned long dinfo, void *data);
	void notify_ind(unsigned long prim, unsigned long dinfo, void *data);
	void facility_ind(unsigned long prim, unsigned long dinfo, void *data);
	void hold_ind(unsigned long prim, unsigned long dinfo, void *data);
	void retrieve_ind(unsigned long prim, unsigned long dinfo, void *data);
	void suspend_ind(unsigned long prim, unsigned long dinfo, void *data);
	void resume_ind(unsigned long prim, unsigned long dinfo, void *data);
	void message_information(unsigned long epoint_id, int message_id, union parameter *param);
	void message_setup(unsigned long epoint_id, int message_id, union parameter *param);
	void message_notify(unsigned long epoint_id, int message_id, union parameter *param);
	void message_facility(unsigned long epoint_id, int message_id, union parameter *param);
	void message_overlap(unsigned long epoint_id, int message_id, union parameter *param);
	void message_proceeding(unsigned long epoint_id, int message_id, union parameter *param);
	void message_alerting(unsigned long epoint_id, int message_id, union parameter *param);
	void message_connect(unsigned long epoint_id, int message_id, union parameter *param);
	void message_disconnect(unsigned long epoint_id, int message_id, union parameter *param);
	void message_release(unsigned long epoint_id, int message_id, union parameter *param);

	/* IE conversion */
#ifdef SOCKET_MISDN
	void enc_ie_complete(struct l3_msg *l3m, int complete);
	void dec_ie_complete(struct l3_msg *l3m, int *complete);
	void enc_ie_bearer(struct l3_msg *l3m, int coding, int capability, int mode, int rate, int multi, int user);
	void dec_ie_bearer(struct l3_msg *l3m, int *coding, int *capability, int *mode, int *rate, int *multi, int *user);
	void enc_ie_call_id(struct l3_msg *l3m, unsigned char *callid, int callid_len);
	void dec_ie_call_id(struct l3_msg *l3m, unsigned char *callid, int *callid_len);
	void enc_ie_called_pn(struct l3_msg *l3m, int type, int plan, unsigned char *number);
	void dec_ie_called_pn(struct l3_msg *l3m, int *type, int *plan, unsigned char *number, int number_len);
	void enc_ie_calling_pn(struct l3_msg *l3m, int type, int plan, int present, int screen, unsigned char *number);
	void dec_ie_calling_pn(struct l3_msg *l3m, int *type, int *plan, int *present, int *screen, unsigned char *number, int number_len);
	void enc_ie_connected_pn(struct l3_msg *l3m, int type, int plan, int present, int screen, unsigned char *number);
	void dec_ie_connected_pn(struct l3_msg *l3m, int *type, int *plan, int *present, int *screen, unsigned char *number, int number_len);
	void enc_ie_cause(struct l3_msg *l3m, int location, int cause);
	void dec_ie_cause(struct l3_msg *l3m, int *location, int *cause);
	void enc_ie_channel_id(struct l3_msg *l3m, int exclusive, int channel);
	void dec_ie_channel_id(struct l3_msg *l3m, int *exclusive, int *channel);
	void enc_ie_date(struct l3_msg *l3m, time_t ti, int seconds);
	void enc_ie_display(struct l3_msg *l3m, unsigned char *display);
	void dec_ie_display(struct l3_msg *l3m, unsigned char *display, int display_len);
	void enc_ie_keypad(struct l3_msg *l3m, unsigned char *keypad);
	void dec_ie_keypad(struct l3_msg *l3m, unsigned char *keypad, int keypad_len);
	void enc_ie_notify(struct l3_msg *l3m, int notify);
	void dec_ie_notify(struct l3_msg *l3m, int *notify);
	void enc_ie_progress(struct l3_msg *l3m, int coding, int location, int progress);
	void dec_ie_progress(struct l3_msg *l3m, int *coding, int *location, int *progress);
	void enc_ie_hlc(struct l3_msg *l3m, int coding, int interpretation, int presentation, int hlc, int exthlc);
	void dec_ie_hlc(struct l3_msg *l3m, int *coding, int *interpretation, int *presentation, int *hlc, int *exthlc);
	void enc_ie_redir_nr(struct l3_msg *l3m, int type, int plan, int present, int screen, int reason, unsigned char *number);
	void dec_ie_redir_nr(struct l3_msg *l3m, int *type, int *plan, int *present, int *screen, int *reason, unsigned char *number, int number_len);
	void enc_ie_redir_dn(struct l3_msg *l3m, int type, int plan, int present, unsigned char *number);
	void dec_ie_redir_dn(struct l3_msg *l3m, int *type, int *plan, int *present, unsigned char *number, int number_len);
	void enc_ie_facility(struct l3_msg *l3m, unsigned char *facility, int facility_len);
	void dec_ie_facility(struct l3_msg *l3m, unsigned char *facility, int *facility_len);
	void dec_facility_centrex(struct l3_msg *l3m, unsigned char *cnip, int cnip_len);
	void enc_ie_useruser(struct l3_msg *l3m, int protocol, unsigned char *user, int user_len);
	void dec_ie_useruser(struct l3_msg *l3m, int *protocol, unsigned char *user, int *user_len);
#else
	void enc_ie_complete(unsigned char **ntmode, msg_t *msg, int complete);
	void dec_ie_complete(unsigned char *p, Q931_info_t *qi, int *complete);
	void enc_ie_bearer(unsigned char **ntmode, msg_t *msg, int coding, int capability, int mode, int rate, int multi, int user);
	void dec_ie_bearer(unsigned char *p, Q931_info_t *qi, int *coding, int *capability, int *mode, int *rate, int *multi, int *user);
	void enc_ie_call_id(unsigned char **ntmode, msg_t *msg, unsigned char *callid, int callid_len);
	void dec_ie_call_id(unsigned char *p, Q931_info_t *qi, unsigned char *callid, int *callid_len);
	void enc_ie_called_pn(unsigned char **ntmode, msg_t *msg, int type, int plan, unsigned char *number);
	void dec_ie_called_pn(unsigned char *p, Q931_info_t *qi, int *type, int *plan, unsigned char *number, int number_len);
	void enc_ie_calling_pn(unsigned char **ntmode, msg_t *msg, int type, int plan, int present, int screen, unsigned char *number);
	void dec_ie_calling_pn(unsigned char *p, Q931_info_t *qi, int *type, int *plan, int *present, int *screen, unsigned char *number, int number_len);
	void enc_ie_connected_pn(unsigned char **ntmode, msg_t *msg, int type, int plan, int present, int screen, unsigned char *number);
	void dec_ie_connected_pn(unsigned char *p, Q931_info_t *qi, int *type, int *plan, int *present, int *screen, unsigned char *number, int number_len);
	void enc_ie_cause(unsigned char **ntmode, msg_t *msg, int location, int cause);
	void dec_ie_cause(unsigned char *p, Q931_info_t *qi, int *location, int *cause);
	void enc_ie_channel_id(unsigned char **ntmode, msg_t *msg, int exclusive, int channel);
	void dec_ie_channel_id(unsigned char *p, Q931_info_t *qi, int *exclusive, int *channel);
	void enc_ie_date(unsigned char **ntmode, msg_t *msg, time_t ti, int seconds);
	void enc_ie_display(unsigned char **ntmode, msg_t *msg, unsigned char *display);
	void dec_ie_display(unsigned char *p, Q931_info_t *qi, unsigned char *display, int display_len);
	void enc_ie_keypad(unsigned char **ntmode, msg_t *msg, unsigned char *keypad);
	void dec_ie_keypad(unsigned char *p, Q931_info_t *qi, unsigned char *keypad, int keypad_len);
	void enc_ie_notify(unsigned char **ntmode, msg_t *msg, int notify);
	void dec_ie_notify(unsigned char *p, Q931_info_t *qi, int *notify);
	void enc_ie_progress(unsigned char **ntmode, msg_t *msg, int coding, int location, int progress);
	void dec_ie_progress(unsigned char *p, Q931_info_t *qi, int *coding, int *location, int *progress);
	void enc_ie_hlc(unsigned char **ntmode, msg_t *msg, int coding, int interpretation, int presentation, int hlc, int exthlc);
	void dec_ie_hlc(unsigned char *p, Q931_info_t *qi, int *coding, int *interpretation, int *presentation, int *hlc, int *exthlc);
	void enc_ie_redir_nr(unsigned char **ntmode, msg_t *msg, int type, int plan, int present, int screen, int reason, unsigned char *number);
	void dec_ie_redir_nr(unsigned char *p, Q931_info_t *qi, int *type, int *plan, int *present, int *screen, int *reason, unsigned char *number, int number_len);
	void enc_ie_redir_dn(unsigned char **ntmode, msg_t *msg, int type, int plan, int present, unsigned char *number);
	void dec_ie_redir_dn(unsigned char *p, Q931_info_t *qi, int *type, int *plan, int *present, unsigned char *number, int number_len);
	void enc_ie_facility(unsigned char **ntmode, msg_t *msg, unsigned char *facility, int facility_len);
	void dec_ie_facility(unsigned char *p, Q931_info_t *qi, unsigned char *facility, int *facility_len);
	void dec_facility_centrex(unsigned char *p, Q931_info_t *qi, unsigned char *cnip, int cnip_len);
	void enc_ie_useruser(unsigned char **ntmode, msg_t *msg, int protocol, unsigned char *user, int user_len);
	void dec_ie_useruser(unsigned char *p, Q931_info_t *qi, int *protocol, unsigned char *user, int *user_len);
#endif

};


