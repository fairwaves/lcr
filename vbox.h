/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** vbox-port header file                                                     **
**                                                                           **
\*****************************************************************************/ 

/* answering machine port class */
class VBoxPort : public Port
{
	public:
	VBoxPort(int type, struct port_settings *settings);
	~VBoxPort();
	int message_epoint(unsigned int epoint_id, int message, union parameter *param);
	void send_announcement(void);

	private:
	struct EndpointAppPBX *p_vbox_apppbx;		/* pbx application */
	unsigned int p_vbox_timeout;			/* timeout for recording */
	char p_vbox_extension[32];			/* current extension */

//	int p_vbox_recording;				/* if currently recording */
	int p_vbox_announce_fh;				/* the announcement filehandler */
	int p_vbox_announce_codec;			/* the announcement codec */
	signed int p_vbox_announce_left;		/* the number of bytes left of announcement sample */
	signed int p_vbox_announce_size;		/* size of current announcement (in bytes) */
	int p_vbox_mode;				/* type of recording VBOX_MODE_* */
	long long p_vbox_audio_start;			/* time stamp of starting of audio (0 == not yet started) */ 
	unsigned int p_vbox_audio_transferred;		/* number of samples sent to endpoint */
	struct lcr_timer p_vbox_announce_timer;		/* timer for sending announcement */
	struct lcr_timer p_vbox_record_timeout;		/* timer for recording limit */
	signed int p_vbox_record_limit;		/* limit for recording */

	struct extension p_vbox_ext;			/* save settings of extension */
};

