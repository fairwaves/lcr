/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** message types and parameters                                              **
**                                                                           **
\*****************************************************************************/ 

enum { /* interface types */
	INFO_ITYPE_ISDN,
	INFO_ITYPE_ISDN_EXTENSION, /* call from internal extension */
	INFO_ITYPE_CHAN,
	INFO_ITYPE_VBOX,
};

enum { /* number types */
	INFO_NTYPE_UNKNOWN,
	INFO_NTYPE_SUBSCRIBER,
	INFO_NTYPE_NATIONAL,
	INFO_NTYPE_INTERNATIONAL,
};

enum { /* number presentation */
	INFO_PRESENT_NULL,
	INFO_PRESENT_ALLOWED,
	INFO_PRESENT_RESTRICTED,
	INFO_PRESENT_NOTAVAIL,
};

enum { /* number presentation */
	INFO_SCREEN_USER, /* user provided */
	INFO_SCREEN_NETWORK, /* network provided */
};

enum { /* redirection reason */
	INFO_REDIR_UNKNOWN,
	INFO_REDIR_BUSY,
	INFO_REDIR_NORESPONSE,
	INFO_REDIR_UNCONDITIONAL,
	INFO_REDIR_OUTOFORDER,
	INFO_REDIR_CALLDEFLECT,
};

#define	INFO_NOTIFY_NONE			0x00
#define	INFO_NOTIFY_USER_SUSPENDED		0x80	
#define	INFO_NOTIFY_USER_RESUMED		0x81
#define	INFO_NOTIFY_BEARER_SERVICE_CHANGED	0x82
#define	INFO_NOTIFY_CALL_COMPLETION_DELAY	0x83
#define	INFO_NOTIFY_CONFERENCE_ESTABLISHED	0xc2
#define	INFO_NOTIFY_CONFERENCE_DISCONNECTED	0xc3
#define INFO_NOTIFY_OTHER_PARTY_ADDED		0xc4
#define INFO_NOTIFY_ISOLATED			0xc5
#define INFO_NOTIFY_REATTACHED			0xc6
#define INFO_NOTIFY_OTHER_PARTY_ISOLATED	0xc7
#define INFO_NOTIFY_OTHER_PARTY_REATTACHED	0xc8
#define INFO_NOTIFY_OTHER_PARTY_SPLIT		0xc9
#define INFO_NOTIFY_OTHER_PARTY_DISCONNECTED	0xca
#define INFO_NOTIFY_CONFERENCE_FLOATING		0xcb
#define INFO_NOTIFY_CONFERENCE_DISCONNECTED_P	0xcc /* preemted */
#define INFO_NOTIFY_CONFERENCE_FLOATING_S_U_P	0xcf /* served user preemted */
#define INFO_NOTIFY_CALL_IS_A_WAITING_CALL	0xe0
#define INFO_NOTIFY_DIVERSION_ACTIVATED		0xe8
#define INFO_NOTIFY_RESERVED_CT_1		0xe9
#define INFO_NOTIFY_RESERVED_CT_2		0xea
#define INFO_NOTIFY_REVERSE_CHARGING		0xee
#define INFO_NOTIFY_REMOTE_HOLD			0xf9
#define INFO_NOTIFY_REMOTE_RETRIEVAL		0xfa
#define INFO_NOTIFY_CALL_IS_DIVERTING		0xfb

enum { /* diversion types */
	INFO_DIVERSION_CFU,
	INFO_DIVERSION_CFNR,
	INFO_DIVERSION_CFB,
	INFO_DIVERSION_CFP,
};

/* bearer capabilities */
#define	INFO_BC_SPEECH					0x00
#define	INFO_BC_DATAUNRESTRICTED			0x08
#define	INFO_BC_DATARESTRICTED				0x09
#define	INFO_BC_AUDIO					0x10
#define	INFO_BC_DATAUNRESTRICTED_TONES			0x11
#define	INFO_BC_VIDEO					0x18

/* bearer mode */
#define	INFO_BMODE_CIRCUIT				0
#define	INFO_BMODE_PACKET				2

/* bearer user l1 */
#define	INFO_INFO1_NONE					0x00
#define	INFO_INFO1_V110					0x81
#define	INFO_INFO1_ULAW					0x82
#define	INFO_INFO1_ALAW					0x83
#define	INFO_INFO1_G721					0x84
#define	INFO_INFO1_H221H242				0x85
#define	INFO_INFO1_NONCCITT				0x87
#define	INFO_INFO1_V120					0x88
#define	INFO_INFO1_X31HDLC				0x89

/* hlc */
#define INFO_HLC_NONE					0x00
#define INFO_HLC_TELEPHONY				0x81
#define INFO_HLC_FAXG2G3				0x84
#define INFO_HLC_FAXG4					0xa1
#define INFO_HLC_TELETEX1				0xa4
#define INFO_HLC_TELETEX2				0xa8
#define INFO_HLC_TELETEX3				0xb1
#define INFO_HLC_VIDEOTEX1				0xb2
#define INFO_HLC_VIDEOTEX2				0xb3
#define INFO_HLC_TELEX					0xb5
#define INFO_HLC_MHS					0xb8
#define INFO_HLC_OSI					0xc1
#define INFO_HLC_MAINTENANCE				0xde
#define INFO_HLC_MANAGEMENT				0xdf
#define INFO_HLC_AUDIOVISUAL				0xe0

enum { /* isdnsignal */
	mISDNSIGNAL_VOLUME,		/* change volume */
	mISDNSIGNAL_CONF,		/* joint/split conference */
	mISDNSIGNAL_JOINDATA,		/* data required by join instance */
	mISDNSIGNAL_ECHO,		/* enable/disable echoe */
	mISDNSIGNAL_DELAY,		/* use delay or adaptive jitter */
};

enum { /* bchannel assignment */
	BCHANNEL_REQUEST,		/* application requests bchannel */
	BCHANNEL_ASSIGN,		/* bchannel assigned by LCR */
	BCHANNEL_ASSIGN_ACK,		/* application acknowledges */
	BCHANNEL_REMOVE,		/* bchannel removed by LCR */
	BCHANNEL_REMOVE_ACK,		/* application acknowledges */
};
enum {
	B_STATE_IDLE,		/* not open */
	B_STATE_ACTIVATING,	/* DL_ESTABLISH sent */
	B_STATE_ACTIVE,		/* channel active */
	B_STATE_DEACTIVATING,	/* DL_RELEASE sent */
	B_STATE_EXPORTING,	/* BCHANNEL_ASSIGN sent */
	B_STATE_REMOTE,		/* bchannel assigned to remote application */
	B_STATE_IMPORTING,	/* BCHANNEL_REMOVE sent */
};
enum {
	B_EVENT_USE,		/* activate/export bchannel */
	B_EVENT_EXPORTREQUEST,	/* remote app requests bchannel */
	B_EVENT_ACTIVATED,	/* DL_ESTABLISH received */
	B_EVENT_DROP,		/* deactivate/re-import bchannel */
	B_EVENT_DEACTIVATED,	/* DL_RELEASE received */
	B_EVENT_EXPORTED,	/* BCHANNEL_ASSIGN received */
	B_EVENT_IMPORTED,	/* BCHANNEL_REMOVE received */
};


/* call-info structure CALLER */
struct caller_info {
	char id[32];			/* id of caller (user number) */
	char extension[32];		/* internal id */
	char name[16];
	int isdn_port;			/* internal/external port (if call is isdn) */
	char interface[32];		/* interface name the call was from */
	int itype;			/* type of interface */
	int ntype;			/* type of number */
	int present;			/* presentation */
	int screen;			/* who provided the number */
	char display[84];		/* display information */
};

/* call-info structure DIALING */
struct dialing_info {
	char id[256];			/* number dialing (so far) */
	char interfaces[128];		/* interfaces for extenal calls */
	int itype;			/* type of interface */
	int ntype;			/* type of number */
	int sending_complete;		/* end of dialing */
};

/* call-info structure CONNECT */
struct connect_info {
	char id[32];			/* id of caller (user number) */
	char extension[32];		/* internal id */
	char name[16];
	int isdn_port;			/* internal/external port (if call is isdn) */
	char interface[128];		/* interface for extenal calls */
	int itype;			/* type of interface */
	int ntype;			/* type of number */
	int present;			/* presentation */
	int screen;			/* who provided the number */
	char display[84];		/* display information */
};

/* call-info structure DISCONNECT */
struct disconnect_info {
	int cause;			/* reason for disconnect */
	int location;			/* disconnect location */
	char display[84];		/* optional display information */
};

/* call-info structure REDIR */
struct redir_info {
	char id[32];			/* id of caller (user number) */
	char extension[32];		/* internal id */
	int isdn_port;			/* internal/external port (if call is isdn) */
	int itype;			/* type of interface */
	int ntype;			/* type of number */
	int present;			/* presentation */
	int screen;			/* who provided the number */
	int reason;			/* reason for redirecing */
};

/* call-info structure capability */
struct capa_info {
	int bearer_capa;		/* capability */
	int bearer_mode;		/* circuit/packet */
	int bearer_info1;		/* alaw,ulaw,... */
	int hlc;			/* hlc capability */
	int exthlc;			/* extendet hlc */
};

/* call-info structure NOTIFY */
struct notify_info {
	int notify;			/* notifications (see INFO_NOTIFY_*) */
	char id[32];			/* redirection id (user number) */
	char extension[32];		/* internal id */
	int isdn_port;			/* internal/external port (if call is isdn) */
	int itype;			/* type of interface */
	int ntype;			/* type of number */
	int present;			/* redirection presentation */
	char display[84];		/* display information */
	int local;			/* if set, endpoints gets information about audio channel (open/close) */
};

/* call-info structure FACILITY */
struct facility_info {
	char data[256];			/* data info about facility */
	int len;			/* length of facility content */
};

/* call-info structure USERUSER */
struct useruser_info {
	int protocol;
	int len;
	unsigned char data[128];	/* user-user info (not a sting!)*/
};

/* call-info structure SETUP */ 
struct message_setup {
	int isdn_port; /* card number 1...n (only on calls from isdn port) */
	int port_type; /* type of port (only required if message is port -> epoint) */
	int dtmf; /* used to enabled dtmf dialing at setup state */
	int partyline; /* if set, call will be a conference room */
	struct caller_info callerinfo;		/* information about the caller */
	struct dialing_info dialinginfo;	/* information about dialing */
	struct redir_info redirinfo;		/* info on redirection (to the calling user) */
	struct capa_info capainfo;		/* info on l2,l3 capability */
	struct useruser_info useruser;		/* user-user */
};

/* call-info structure PARK */
struct park_info {
	char callid[8];
	int len;
};

/* DATA */
struct param_data {
	unsigned char data[512]; /* audio/hdlc data */
	int len; /* audio/hdlc data */
};

struct param_play {
	char file[512]; /* file name */
	int offset; /* offset to start file at (in seconds) */
};

struct param_tone {
	char dir[128]; /* directory */
	char name[128]; /* file name */
};

struct param_counter {
	int current; /* current counter in seconds */
	int max; /* total size of file (0=no info) */
};

struct param_mISDNsignal {
	int message;
	int rxvol;
	int txvol;
	int conf;
	int joindata;
	int tone;
	int echo;
	int delay;
};

/* encryption control structure CRYPT */
struct param_crypt {
	int type; /* see messages in crypt.h */
	int len;
	unsigned char data[512+32]; /* a block of 512 byte + some overhead */
};

struct param_hello {
	char application[32]; /* name of remote application */
};

struct param_bchannel {
	int type; /* BCHANNEL_* */
	unsigned long addr; /* bchannel stack address */
};

/* structure of message parameter */
union parameter {
	struct param_tone tone; /* MESSAGE_TONE */
	char dtmf; /* MESSAGE_DTMF */
	struct message_setup setup; /* MESSAGE_SETUP */
	struct dialing_info information; /* MESSAGE_INFO */
	struct connect_info connectinfo; /* CONNECT INFO */
	struct disconnect_info disconnectinfo; /* DISCONNECT INFO */
	struct notify_info notifyinfo; /* some notifications */
	struct facility_info facilityinfo; /* some notifications */
	struct park_info parkinfo; /* MESSAGE_SUSPEND, MESSAGE_RESUME */
	int state; /* MESSAGE_TIMEOUT */
	int knock; /* MESSAGE_KNOCK 0=off !0=on */
	int audiopath; /* MESSAGE_audiopath see RELATION_CHANNEL_* (join.h) */
	struct param_data data; /* MESSAGE_DATA */
	struct param_play play; /* MESSAGE_VBOX_PLAY */
	int speed; /* MESSAGE_VBOX_PLAY_SPEED */
	struct param_counter counter; /* MESSAGE_TONE_COUNTER */
	struct param_mISDNsignal mISDNsignal; /* MESSAGE_mISDNSIGNAL */
	struct extension ext; /* tell port about extension information */
	struct param_crypt crypt; /* MESSAGE_CRYPT */
	struct param_hello hello; /* MESSAGE_HELLO */
	struct param_bchannel bchannel; /* MESSAGE_BCHANNEL */
};

enum { /* message flow */
	PORT_TO_EPOINT,
	EPOINT_TO_JOIN,
	JOIN_TO_EPOINT,
	EPOINT_TO_PORT,
};

/* message structure */
struct message {
	struct message *next;
	int type; /* type of message */
	int flow; /* from where to where */
	unsigned long id_from; /* in case of flow==PORT_TO_EPOINT: id_from is the port's serial, id_to is the epoint's serial */
	unsigned long id_to;
	int keep;
	union parameter param;
};

enum { /* messages between entities */
	MESSAGE_NONE,		/* no message */
	MESSAGE_TONE,		/* set information tone (to isdn port) */
	MESSAGE_DTMF,		/* dtmf digit (from isdn port) */
	MESSAGE_mISDNSIGNAL,	/* special mixer command (down to isdn port) */

	MESSAGE_SETUP,		/* setup message */
	MESSAGE_INFORMATION,	/* additional digit information */
	MESSAGE_OVERLAP,	/* call accepted, send more information */
	MESSAGE_PROCEEDING,	/* proceeding */
	MESSAGE_ALERTING,	/* ringing */
	MESSAGE_CONNECT,	/* connect */
	MESSAGE_DISCONNECT,	/* disconnect with cause */
	MESSAGE_RELEASE,	/* release with cause */
	MESSAGE_TIMEOUT,	/* protocol state has timed out (port->epoint) */
	MESSAGE_NOTIFY,		/* used to send progress and notify infos */
	MESSAGE_FACILITY,	/* used to facility infos, like aocd */

	MESSAGE_SUSPEND,	/* suspend port */
	MESSAGE_RESUME,		/* resume port */

	MESSAGE_AUDIOPATH,	/* set status of audio path to endpoint (to call, audio is also set) */
//	MESSAGE_REMOTE_AUDIO,	/* tell remote to set audio status */
	MESSAGE_PATTERN,	/* pattern information tones available */
	MESSAGE_NOPATTERN,	/* pattern information tones unavailable */
	MESSAGE_CRYPT,		/* encryption message */

	MESSAGE_DATA,		/* audio/hdlc data */

	MESSAGE_VBOX_PLAY,	/* play recorded file */
	MESSAGE_VBOX_PLAY_SPEED,/* change speed of file */
	MESSAGE_VBOX_TONE,	/* set answering VBOX tone */
	MESSAGE_TONE_COUNTER,	/* tone counter (for VBOX tone use) */
	MESSAGE_TONE_EOF,	/* tone is end of file */
	MESSAGE_BCHANNEL,	/* request/assign/remove bchannel */
	MESSAGE_HELLO,		/* hello message for remote application */
	MESSAGE_NEWREF,		/* special message to create and inform ref */
};

#define MESSAGES static const char *messages_txt[] = { \
	"MESSAGE_NONE", \
	"MESSAGE_TONE", \
	"MESSAGE_DTMF", \
	"MESSAGE_mISDNSIGNAL", \
	"MESSAGE_SETUP", \
	"MESSAGE_INFORMATION", \
	"MESSAGE_OVERLAP", \
	"MESSAGE_PROCEEDING", \
	"MESSAGE_ALERTING", \
	"MESSAGE_CONNECT", \
	"MESSAGE_DISCONNECT", \
	"MESSAGE_RELEASE", \
	"MESSAGE_TIMEOUT", \
	"MESSAGE_NOTIFY", \
	"MESSAGE_FACILITY", \
	"MESSAGE_SUSPEND", \
	"MESSAGE_RESUME", \
	"MESSAGE_AUDIOPATH", \
/*	"MESSAGE_REMOTE_AUDIO",*/ \
	"MESSAGE_PATTERN", \
	"MESSAGE_NOPATTERN", \
	"MESSAGE_CRYPT", \
	"MESSAGE_DATA", \
	"MESSAGE_VBOX_PLAY", \
	"MESSAGE_VBOX_PLAY_SPEED", \
	"MESSAGE_VBOX_TONE", \
	"MESSAGE_TONE_COUNTER", \
	"MESSAGE_TONE_EOF", \
	"MESSAGE_BCHANNEL", \
	"MESSAGE_HELLO", \
	"MESSAGE_NEWREF", \
};


struct message *message_create(int id_from, int id_to, int flow, int type);
void message_put(struct message *message);
struct message *message_forward(int id_from, int id_to, int flow, union parameter *param);
struct message *message_get(void);
void message_free(struct message *message);



