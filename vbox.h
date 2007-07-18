/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** port header file                                                          **
**                                                                           **
\*****************************************************************************/ 

/* answerin machine port class */
class VBoxPort : public Port
{
	public:
	VBoxPort(int type, struct port_settings *settings);
	~VBoxPort();
	int message_epoint(unsigned long epoint_id, int message, union parameter *param);
	int handler(void);

	private:
	struct EndpointAppPBX *p_vbox_apppbx;		/* pbx application */
	unsigned long p_vbox_timeout;			/* timeout for recording */
	char p_vbox_extension[32];			/* current extension */

//	int p_vbox_recording;				/* if currently recording */
	int p_vbox_announce_fh;				/* the announcement filehandler */
	int p_vbox_announce_codec;			/* the announcement codec */
	signed long p_vbox_announce_left;		/* the number of bytes left of announcement sample */
	signed long p_vbox_announce_size;		/* size of current announcement (in bytes) */
	int p_vbox_mode;				/* type of recording VBOX_MODE_* */
	double p_vbox_audio_start;			/* time stamp of starting of audio (<1 == not yet started) */ 
	unsigned long p_vbox_audio_transferred;		/* number of samples sent to endpoint */
	signed long p_vbox_record_start;		/* start for recording */
	signed long p_vbox_record_limit;		/* limit for recording */

	struct extension p_vbox_ext;			/* save settings of extension */
};

