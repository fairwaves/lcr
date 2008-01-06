/*****************************************************************************\
**                                                                           **
** LCR                                                                       **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** interface header file                                                     **
**                                                                           **
\*****************************************************************************/ 

#define FLAG_PORT_USE		1
#define FLAG_PORT_PTP		(1<<1)

enum {	/* interface type */
	IF_TYPE_DIRECT,
	IF_TYPE_EXTENSION,
};

	/* channel selection -1 is reserved for "no ie" */
#define CHANNEL_NO		-2
#define CHANNEL_ANY		-3
#define CHANNEL_FREE		-100

	/* port selection */
enum {	HUNT_LINEAR = 0,
	HUNT_ROUNDROBIN,
};

	/* filters */
enum {	FILTER_GAIN,
	FILTER_CANCEL,
	FILTER_BLOWFISH,
};

enum {	IS_DEFAULT = 0,
	IS_YES,
	IS_NO,
};

struct select_channel {
	struct select_channel	*next;
	int			channel;
};

struct interface_port {
	struct interface_port	*next;
	struct interface	*interface; /* link to interface */
	struct mISDNport	*mISDNport; /* link to port */
	int			portnum; /* port number */
	int			ptp; /* force load stack in PTP mode */
	int			ptmp; /* force load stack in PTP mode */
	int			nt; /* load stack in NT-mode */
	int			channel_force; /* forces channel by protocol */
	int			nodtmf; /* disables DTMF */
	struct select_channel	*out_channel; /* list of channels to select */
	struct select_channel	*in_channel; /* the same for incoming channels */
	int			block; /* set if interface is blocked */
};

struct interface_msn {
	struct interface_msn	*next;
	char			msn[64]; /* msn */
};

struct interface_screen {
	struct interface_screen	*next;
	char			match[64]; /* what caller id to match */
	int			match_type; /* number type */
	int			match_present; /* presentation type */
	char			result[64]; /* what caller id to change to */
	int			result_type; /* number type */
	int			result_present; /* presentation type */
};

struct interface {
	struct interface	*next;
	char			name[64]; /* name of interface */
	int			extension; /* calls are handled as extension */
	int			is_tones; /* generate tones */
	int			is_earlyb; /* bridge tones during call setup */
	int			hunt; /* select algorithm */
	int			hunt_next; /* ifport index to start hunt */
	struct interface_port	*ifport; /* link to interface port list */
	struct interface_msn	*ifmsn; /* link to interface msn list */
	struct interface_screen *ifscreen_in; /* link to screening list */
	struct interface_screen *ifscreen_out; /* link to screening list */
	int			gain_tx, gain_rx; /* filter gain */
	char			pipeline[256]; /* filter pipeline */
	unsigned char		bf_key[56]; /* filter blowfish */
	int			bf_len; /* filter length of blowfish */
};

struct interface_param {
	char			*name;
/*      return value		(pointer of function)(args ...) */
	int			(*func)(struct interface *, char *, int, char *, char*);
	char			*usage;
	char			*help;
};


extern struct interface *interface_first;
extern struct interface *interface_newlist;

extern char interface_error[256];
struct interface *read_interfaces(void);
void free_interfaces(struct interface *interface_start);
void relink_interfaces(void);
void load_port(struct interface_port *ifport);
void doc_interface(void);
void do_screen(int out, char *id, int idsize, int *type, int *present, struct interface *interface);

