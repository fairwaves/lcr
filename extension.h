/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** extension header file                                                     **
**                                                                           **
\*****************************************************************************/ 

/* display of callerid on internal numbers */

enum {
	DISPLAY_CID_ASIS,			/* with type as defined */
	DISPLAY_CID_NUMBER,			/* "5551212" */
	DISPLAY_CID_ABBREVIATION,		/* "05" */
	DISPLAY_CID_NAME,			/* "Axel" */
	DISPLAY_CID_NAME_NUMBER,		/* "Axel 5551212" */
	DISPLAY_CID_NUMBER_NAME,		/* "5551212 Axel" */
	DISPLAY_CID_ABBREV_NUMBER,		/* "05 5551212" */
	DISPLAY_CID_ABBREV_NAME,		/* "05 Axel" */
	DISPLAY_CID_ABBREV_NUMBER_NAME,		/* "05 5551212 Axel" */
	DISPLAY_CID_ABBREV_NAME_NUMBER,		/* "05 Axel 5551212" */
};
enum {
	DISPLAY_CID_INTERNAL_OFF,		/* "20" */
	DISPLAY_CID_INTERNAL_ON,		/* "Intern 20" */
};

/* display of clear causes using display messages */

enum {
	DISPLAY_CAUSE_NONE,
	DISPLAY_CAUSE_ENGLISH,		/* "34 - no channel" */
	DISPLAY_CAUSE_GERMAN,		/* "34 - kein Kanal" */
	DISPLAY_LOCATION_ENGLISH,	/* "34 - Network (Remote)" */
	DISPLAY_LOCATION_GERMAN,	/* "34 - Vermittlung (Gegenstelle)" */
	DISPLAY_CAUSE_NUMBER,		/* "Cause 34" */
};

/* clip */

enum {
	CLIP_ASIS,			/* use colp as presented by caller */
	CLIP_HIDE,			/* use extension's caller id */
};

/* colp */

enum {
	COLP_ASIS,			/* use colp as presented by called */
	COLP_HIDE,			/* use extension's caller id */
	COLP_FORCE,			/* use colp even if called dosn't provide or allow */
};

/* codec to use */

enum {
	CODEC_OFF,			/* record wave off */
	CODEC_MONO,			/* record wave mono */
	CODEC_STEREO,			/* record wave stereo */
	CODEC_8BIT,			/* record wave mono 8bit */
	CODEC_LAW,			/* record LAW */
};

/* VBOX mode */

enum {
	VBOX_MODE_NORMAL,		/* normal mode: send announcement, then record */
	VBOX_MODE_PARALLEL,		/* parallel mode: send announcement and record during announcement */
	VBOX_MODE_ANNOUNCEMENT,		/* announcement mode: send announcement and disconnect */
};

/* VBOX display */

enum {
	VBOX_DISPLAY_BRIEF,		/* parallel mode: send announcement and record during announcement */
	VBOX_DISPLAY_DETAILED,		/* announcement mode: send announcement and disconnect */
	VBOX_DISPLAY_OFF,		/* normal mode: send announcement, then record */
};

/* VBOX language */

enum {
	VBOX_LANGUAGE_ENGLISH,		/* display and announcements are in english */
	VBOX_LANGUAGE_GERMAN,		/* display and announcements are in german */
};

/* dsptones */

enum {
	DSP_NONE,
	DSP_AMERICAN,
	DSP_GERMAN,
	DSP_OLDGERMAN,
};


/* extensions
 *
 * extensions are settings saved at <extensions_dir>/<extension>/settings
 * they carry all information and permissions about an extension
 * they will be loaded when needed and saved when changed
 */

struct extension {
	char number[32];	/* number of extension */
	char name[32];
	char prefix[32];
	char next[32];		/* next number to dial when pickup (temp prefix) */
	char alarm[32];
	char cfb[256];
	char cfu[256];
	char cfnr[256];
	int cfnr_delay;
	int change_forward;
	char cfp[256];
	char interfaces[128];
	char callerid[32];
	int callerid_type;
	int callerid_present;
	char id_next_call[32];
	int id_next_call_type;
	int id_next_call_present;
	int change_callerid;
	int clip;		/* how to present caller id on forwarded calls */
	int colp;		/* how to present called line id on forwarded calls */
	char clip_prefix[32];	/* prefix for screening incomming clip */
	int keypad;		/* support keypad for call control */
	int centrex;		/* present name of caller/called on internal extension */
	int anon_ignore;	/* ignore anonymouse calls */
	int rights;
	int delete_ext;		/* delete function for external dialing */
	int noknocking;		/* deny knocking of incoming call */
	char last_out[MAX_REMEMBER][64];	/* numbers to redail */
	char last_in[MAX_REMEMBER][64];	/* numbers to reply */
	int txvol;
	int rxvol;
	int display_cause; 	/* clear cause using display message */
	int display_ext;	/* display external caller ids */
	int display_int;	/* display internal caller ids */
	int display_fake; 	/* display fake caller ids */
	int display_anon; 	/* display anonymouse caller ids */
	int display_menu; 	/* display menu */
	int display_dialing;	/* display interpreted digits while dialing */
	int display_name;	/* display caller's name if available (CNIP) */
	char tones_dir[64];	/* directory of all tones/patterns */
	int record;		/* SEE RECORD_* */
	char password[64];	/* callback / login password */

	int vbox_mode;		/* see VBOX_MODE_* */
	int vbox_codec;		/* see CODEC_* */
	int vbox_time;		/* time to recorde, 0=infinite */
	int vbox_display;	/* see VBOX_DISPLAY_* */
	int vbox_language;	/* see VBOX_LANGUAGE_* */
	char vbox_email[128];	/* send mail if given */
	int vbox_email_file;	/* set, if also the audio fille will be attached */
	int vbox_free;		/* if vbox shall connect after announcment */
	
	int tout_setup;
	int tout_dialing;
	int tout_proceeding;
	int tout_alerting;
	int tout_disconnect;
//	int tout_hold;
//	int tout_park;
	int own_setup;
	int own_proceeding;
	int own_alerting;
	int own_cause;

	int facility;		/* must be set to forward facility to terminal */
	int datacall;		/* data calls are handled as voice calls */
	int no_seconds;		/* don't include seconds in the connect message */
};

int read_extension(struct extension *ext, char *number);
int write_extension(struct extension *ext, char *number);
int write_log(char *number, char *callerid, char *calledid, time_t start, time_t stop, int aoce, int cause, int location);
int parse_phonebook(char *number, char **abbrev_pointer, char **phone_pointer, char **name_pointer);
int parse_secrets(char *number, char *remote_id, char **auth_pointer, char **crypt_pointer, char **key_pointer);
char *parse_directory(char *number, int type);
int parse_callbackauth(char *number, struct caller_info *callerinfo);
void append_callbackauth(char *number, struct caller_info *callerinfo);


