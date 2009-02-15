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
	Pdss1(int type, struct mISDNport *mISDNport, char *portname, struct port_settings *settings, int channel, int exclusive, int mode);
	~Pdss1();
	unsigned int p_m_d_l3id;		/* current l3 process id */
	void message_isdn(unsigned int cmd, unsigned int pid, struct l3_msg *l3m);
	int p_m_d_ces;				/* ntmode: tei&sapi */
	int handler(void);
	int message_epoint(unsigned int epoint_id, int message, union parameter *param);

	int p_m_d_ntmode;			/* flags the nt-mode */
	int p_m_d_tespecial;			/* special te-mode with all nt-mode IEs */
	char p_m_d_queue[64];			/* queue for dialing information (if larger than setup allows) */
	struct lcr_msg *p_m_d_notify_pending;	/* queue for NOTIFY if not connected */

	int p_m_d_collect_cause;		/* collecting cause and location */
	int p_m_d_collect_location;

	void new_state(int state);		/* set new state */
//	void isdn_show_send_message(unsigned int prim, msg_t *msg);
	int hunt_bchannel(int exclusive, int channel);
	int received_first_reply_to_setup(unsigned int cmd, int channel, int exclusive);
	void information_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m);
	void setup_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m);
	void setup_acknowledge_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m);
	void proceeding_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m);
	void alerting_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m);
	void connect_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m);
	void disconnect_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m);
	void release_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m);
	void restart_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m);
	void release_complete_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m);
	void disconnect_ind_i(unsigned int cmd, unsigned int pid, struct l3_msg *l3m);
	void t312_timeout_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m);
	void notify_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m);
	void facility_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m);
	void hold_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m);
	void retrieve_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m);
	void suspend_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m);
	void resume_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m);
	void message_information(unsigned int epoint_id, int message_id, union parameter *param);
	void message_setup(unsigned int epoint_id, int message_id, union parameter *param);
	void message_notify(unsigned int epoint_id, int message_id, union parameter *param);
	void message_facility(unsigned int epoint_id, int message_id, union parameter *param);
	void message_overlap(unsigned int epoint_id, int message_id, union parameter *param);
	void message_proceeding(unsigned int epoint_id, int message_id, union parameter *param);
	void message_alerting(unsigned int epoint_id, int message_id, union parameter *param);
	void message_connect(unsigned int epoint_id, int message_id, union parameter *param);
	void message_disconnect(unsigned int epoint_id, int message_id, union parameter *param);
	void message_release(unsigned int epoint_id, int message_id, union parameter *param);

	/* IE conversion */
	void enc_ie_complete(struct l3_msg *l3m, int complete);
	void dec_ie_complete(struct l3_msg *l3m, int *complete);
	void enc_ie_bearer(struct l3_msg *l3m, int coding, int capability, int mode, int rate, int multi, int user);
	void dec_ie_bearer(struct l3_msg *l3m, int *coding, int *capability, int *mode, int *rate, int *multi, int *user);
	void enc_ie_call_id(struct l3_msg *l3m, unsigned char *callid, int callid_len);
	void dec_ie_call_id(struct l3_msg *l3m, unsigned char *callid, int *callid_len);
	void enc_ie_called_pn(struct l3_msg *l3m, int type, int plan, unsigned char *number, int number_len);
	void dec_ie_called_pn(struct l3_msg *l3m, int *type, int *plan, unsigned char *number, int number_len);
	void enc_ie_calling_pn(struct l3_msg *l3m, int type, int plan, int present, int screen, unsigned char *number, int type2, int plan2, int present2, int screen2, unsigned char *number2);
	void dec_ie_calling_pn(struct l3_msg *l3m, int *type, int *plan, int *present, int *screen, unsigned char *number, int number_len, int *type2, int *plan2, int *present2, int *screen2, unsigned char *number2, int number_len2);
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

};


