
/* GSM port class */
class Premote : public Port
{
	public:
	Premote(int type, char *portname, struct port_settings *settings, struct interface *interface, int remote_id);
	~Premote();

	unsigned int p_r_ref;
	int p_r_remote_id; /* remote instance (socket) */
	char p_r_remote_app[32];
	char p_r_interface_name[64];
	int p_r_tones;

	int message_epoint(unsigned int epoint_id, int message_id, union parameter *param);
	void message_remote(int message_type, union parameter *param);

	int bridge_rx(unsigned char *data, int len);
};


