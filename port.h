/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** port header file                                                          **
**                                                                           **
\*****************************************************************************/ 

#ifndef PORT_HEADER
#define PORT_HEADER

/* type of port */
#define	PORT_TYPE_NULL		0x0000
#define PORT_CLASS_mISDN	0x0100
#define PORT_CLASS_MASK		0xff00
#define PORT_CLASS_mISDN_DSS1	0x0110
#define PORT_CLASS_mISDN_MASK	0xfff0
	/* nt-mode */
#define	PORT_TYPE_DSS1_NT_IN	0x0111
#define	PORT_TYPE_DSS1_NT_OUT	0x0112
	/* te-mode */
#define	PORT_TYPE_DSS1_TE_IN	0x0113
#define	PORT_TYPE_DSS1_TE_OUT	0x0114
	/* answering machine */
#define	PORT_TYPE_VBOX_OUT	0x0311


enum { /* states of call */
	PORT_STATE_IDLE,	/* no call */
	PORT_STATE_IN_SETUP,	/* incoming connection */
	PORT_STATE_OUT_SETUP,	/* outgoing connection */
	PORT_STATE_IN_OVERLAP,	/* more informatiopn needed */
	PORT_STATE_OUT_OVERLAP,	/* more informatiopn needed */
	PORT_STATE_IN_PROCEEDING,/* call is proceeding */
	PORT_STATE_OUT_PROCEEDING,/* call is proceeding */
	PORT_STATE_IN_ALERTING,	/* call is ringing */
	PORT_STATE_OUT_ALERTING,/* call is ringing */
	PORT_STATE_CONNECT_WAITING,/* connect is sent to the network, waiting for acknowledge */
	PORT_STATE_CONNECT,	/* call is connected and transmission is enabled */
	PORT_STATE_IN_DISCONNECT,/* incoming disconnected */
	PORT_STATE_OUT_DISCONNECT,/* outgoing disconnected */
	PORT_STATE_RELEASE,	/* call released */
};

#define PORT_STATE_NAMES \
static char *state_name[] = { \
	"PORT_STATE_IDLE", \
	"PORT_STATE_IN_SETUP", \
	"PORT_STATE_OUT_SETUP", \
	"PORT_STATE_IN_OVERLAP", \
	"PORT_STATE_OUT_OVERLAP", \
	"PORT_STATE_IN_PROCEEDING", \
	"PORT_STATE_OUT_PROCEEDING", \
	"PORT_STATE_IN_ALERTING", \
	"PORT_STATE_OUT_ALERTING", \
	"PORT_STATE_CONNECT_WAITING", \
	"PORT_STATE_CONNECT", \
	"PORT_STATE_IN_DISCONNECT", \
	"PORT_STATE_OUT_DISCONNECT", \
	"PORT_STATE_RELEASE", \
};


enum { /* event list from listening to tty */
	TTYI_EVENT_nodata,	/* no data was received nor processed */
	TTYI_EVENT_NONE,	/* nothing happens */
	TTYI_EVENT_CONNECT,	/* a connection is made */
	TTYI_EVENT_RING,	/* incoming call */
	TTYI_EVENT_CALLER,	/* caller id information */
	TTYI_EVENT_INFO,	/* dialing information */
	TTYI_EVENT_OVERLAP,	/* setup complete, awaiting more dialing info */
	TTYI_EVENT_PROC,	/* proceeding */
	TTYI_EVENT_ALRT,	/* alerting */
	TTYI_EVENT_CONN,	/* connect */
	TTYI_EVENT_DISC,	/* disconnect */
	TTYI_EVENT_RELE,	/* release signal */
	TTYI_EVENT_BUSY,	/* channel unavailable */
};

#define RECORD_BUFFER_LENGTH	1024 // must be a binary border & must be greater 256, because 256 will be written if buffer overflows
#define RECORD_BUFFER_MASK	1023

/* structure of epoint_list */
struct epoint_list {
	struct epoint_list	*next;
	unsigned long		epoint_id;
	int			active;
};

inline unsigned long ACTIVE_EPOINT(struct epoint_list *epointlist)
{
	while(epointlist)
	{
		if (epointlist->active)
			return(epointlist->epoint_id);
		epointlist = epointlist->next;
	}
	return(0);
}

inline unsigned long INACTIVE_EPOINT(struct epoint_list *epointlist)
{
	while(epointlist)
	{
		if (!epointlist->active)
			return(epointlist->epoint_id);
		epointlist = epointlist->next;
	}
	return(0);
}


/* structure of port settings */
struct port_settings {
	char tones_dir[256];			/* directory of current tone */
        int tout_setup;
        int tout_dialing;
        int tout_proceeding;
        int tout_alerting;
        int tout_disconnect;
//	int tout_hold;
//	int tout_park;
	int no_seconds;				/* don't send seconds with time information element */
};

/* generic port class */
class Port
{
	public:
	/* methods */
	Port(int type, char *portname, struct port_settings *settings);
	virtual ~Port();
	class Port *next;			/* next port in list */
	int p_type;				/* type of port */
	virtual int handler(void);
	virtual int message_epoint(unsigned long epoint_id, int message, union parameter *param);
	virtual void set_echotest(int echotest);
	virtual void set_tone(char *dir, char *name);
	virtual int read_audio(unsigned char *buffer, int length);

	struct port_settings p_settings;
	
	/* tone */
	char p_tone_dir[256];			/* name of current directory */
	char p_tone_name[256];			/* name of current tone */
	char p_tone_fh;				/* file descriptor of current tone or -1 if not open */
	void *p_tone_fetched;			/* pointer to fetched data */
	int p_tone_codec;			/* codec that the tone is made of */
	long p_tone_size, p_tone_left;		/* size of tone in bytes (not samples), bytes left */
	long p_tone_eof;			/* flag that makes the use of eof message */
	long p_tone_counter;			/* flag that makes the use of counter message */
	long p_tone_speed;			/* speed of current tone, 1=normal, may also be negative */
//	char p_knock_fh;			/* file descriptor of knocking tone or -1 if not open */
//	void *p_knock_fetched;			/* pointer to fetched data */
//	int p_knock_codec;
//	long p_knock_size, p_knock_left;
	void set_vbox_tone(char *dir, char *name);/* tone of answering machine */
	void set_vbox_play(char *name, int offset); /* sample of answ. */
	void set_vbox_speed(int speed);	/* speed of answ. */

	/* identification */
	unsigned long p_serial;			/* serial unique id of port */
	char p_name[128];			/* name of port or token (h323) */

	/* endpoint relation */
	struct epoint_list *p_epointlist;	/* endpoint relation */

	/* state */
	int p_state;				/* state of port */
	void new_state(int state);		/* set new state */
	struct caller_info p_callerinfo;	/* information about the caller */
	struct dialing_info p_dialinginfo;	/* information about dialing */
	struct connect_info p_connectinfo;	/* information about connected line */
	struct redir_info p_redirinfo;		/* info on redirection (to the calling user) */
	struct capa_info p_capainfo;	/* info on l2,l3 capacity */
	int p_echotest;				/* set to echo audio data FROM port back to port's mixer */

	/* recording */
	int open_record(int type, int mode, int skip, char *terminal, int anon_ignore, char *vbox_email, int vbox_email_file);
	void close_record(int beep);
	void record(unsigned char *data, int length, int dir_fromup);
	FILE *p_record;				/* recording fp: if not NULL, recording is enabled */
	int p_record_type;			/* codec to use: RECORD_MONO, RECORD_STEREO, ... */
	int p_record_skip;			/* skip bytes before writing the sample */
	unsigned long p_record_length;		/* size of what's written so far */

	signed short p_record_buffer[RECORD_BUFFER_LENGTH];
	unsigned long p_record_buffer_readp;
	unsigned long p_record_buffer_writep;
	int p_record_buffer_dir;		/* current direction in buffer */

	char p_record_filename[256];		/* record filename */
	int p_record_vbox;			/* 0= normal recording, 1= announcement, 2= record to vbox dir */
	int p_record_vbox_year;			/* time when vbox recording started */
	int p_record_vbox_mon;
	int p_record_vbox_mday;
	int p_record_vbox_hour;
	int p_record_vbox_min;
	char p_record_extension[32];		/* current name (digits) of extension */
	int p_record_anon_ignore;
	char p_record_vbox_email[128];
	int p_record_vbox_email_file;

	void free_epointlist(struct epoint_list *epointlist);
	void free_epointid(unsigned long epoint_id);
	struct epoint_list *epointlist_new(unsigned long epoint_id);
};


extern Port *port_first;
extern unsigned long port_serial;

class Port *find_port_with_token(char *name);
class Port *find_port_id(unsigned long port_id);


#endif // PORT_HEADER


