/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** match header file                                                         **
**                                                                           **
\*****************************************************************************/ 


/* memory structure of rulesets */

enum { /* value types */
	VALUE_TYPE_NULL,
	VALUE_TYPE_INTEGER,
	VALUE_TYPE_INTEGER_RANGE,
	VALUE_TYPE_STRING,
	VALUE_TYPE_STRING_RANGE,
};

enum { /* how to parse text file during startup */
	COND_TYPE_NULL,
	COND_TYPE_INTEGER,
	COND_TYPE_TIME,
	COND_TYPE_MDAY,
	COND_TYPE_MONTH,
	COND_TYPE_WDAY,
	COND_TYPE_YEAR,
	COND_TYPE_STRING,
	COND_TYPE_IP,
	COND_TYPE_CAPABILITY,
	COND_TYPE_BMODE,
	COND_TYPE_IFATTR,
};

enum { /* what to check during runtime */
	MATCH_EXTERN,
	MATCH_INTERN,
	MATCH_PORT,
	MATCH_INTERFACE,
	MATCH_CALLERID,
	MATCH_EXTENSION,
	MATCH_DIALING,
	MATCH_ENBLOCK,
	MATCH_OVERLAP,
	MATCH_ANONYMOUS,
	MATCH_VISIBLE,
	MATCH_UNKNOWN,
	MATCH_AVAILABLE,
	MATCH_FAKE,
	MATCH_REAL,
	MATCH_REDIRECTED,
	MATCH_DIRECT,
	MATCH_REDIRID,
	MATCH_TIME,
	MATCH_MDAY,
	MATCH_MONTH,
	MATCH_YEAR,
	MATCH_WDAY,
	MATCH_CAPABILITY,
	MATCH_INFOLAYER1,
	MATCH_HLC,
	MATCH_FILE,
	MATCH_EXECUTE,
	MATCH_DEFAULT,
	MATCH_TIMEOUT,
	MATCH_FREE,
	MATCH_NOTFREE,
	MATCH_DOWN,
	MATCH_UP,
	MATCH_BUSY,
	MATCH_IDLE,
};

enum { /* how to parse text file during startup */
	PARAM_TYPE_NULL,
	PARAM_TYPE_INTEGER,
	PARAM_TYPE_STRING,
	PARAM_TYPE_YESNO,
	PARAM_TYPE_CAPABILITY,
	PARAM_TYPE_BMODE,
	PARAM_TYPE_DIVERSION,
	PARAM_TYPE_DESTIN,
	PARAM_TYPE_PORTS,
	PARAM_TYPE_TYPE,
	PARAM_TYPE_CALLERIDTYPE,
};

/* parameter ID bits */
#define PARAM_PROCEEDING	1LL
#define PARAM_ALERTING		(1LL<<1)
#define PARAM_CONNECT		(1LL<<2)
#define PARAM_EXTENSION		(1LL<<3)
#define PARAM_EXTENSIONS	(1LL<<4)
#define PARAM_PREFIX		(1LL<<5)
#define PARAM_CAPA		(1LL<<6)
#define PARAM_BMODE		(1LL<<7)
#define PARAM_INFO1		(1LL<<8)
#define PARAM_HLC		(1LL<<9)
#define PARAM_EXTHLC		(1LL<<10)
#define PARAM_PRESENT		(1LL<<11)
#define PARAM_DIVERSION		(1LL<<12)
#define PARAM_DEST		(1LL<<13)
#define PARAM_SELECT		(1LL<<14)
#define PARAM_DELAY		(1LL<<15)
#define PARAM_LIMIT		(1LL<<16)
#define PARAM_HOST		(1LL<<17)
#define PARAM_PORT		(1LL<<18)
#define PARAM_INTERFACES	(1LL<<19)
#define PARAM_ADDRESS		(1LL<<20)
#define PARAM_SAMPLE		(1LL<<21)
#define PARAM_ANNOUNCEMENT	(1LL<<22)
#define PARAM_RULESET		(1LL<<23)
#define PARAM_CAUSE		(1LL<<24)
#define PARAM_LOCATION		(1LL<<25)
#define PARAM_DISPLAY		(1LL<<26)
#define PARAM_PORTS		(1LL<<27)
#define PARAM_TPRESET		(1LL<<28)
#define PARAM_FILE		(1LL<<29)
#define PARAM_CONTENT		(1LL<<30)
#define PARAM_APPEND		(1LL<<31)
#define PARAM_EXECUTE		(1LL<<32)
#define PARAM_PARAM		(1LL<<33)
#define PARAM_TYPE		(1LL<<34)
#define PARAM_COMPLETE		(1LL<<35)
#define PARAM_CALLERID		(1LL<<36)
#define PARAM_CALLERIDTYPE	(1LL<<37)
#define PARAM_CALLTO		(1LL<<38)
#define PARAM_ROOM		(1LL<<39)
#define PARAM_TIMEOUT		(1LL<<40)
#define PARAM_NOPASSWORD	(1LL<<41)
#define PARAM_STRIP	(1LL<<42)


/* action index
 * NOTE: The given index is the actual entry number of action_defs[], so add/remove both lists!!!
 */
#define	ACTION_EXTERNAL		0
#define	ACTION_INTERNAL		1
#define	ACTION_OUTDIAL		2
#define	ACTION_CHAN		3
#define	ACTION_VBOX_RECORD	4
#define	ACTION_PARTYLINE	5
#define	ACTION_LOGIN		6
#define	ACTION_CALLERID		7
#define	ACTION_CALLERIDNEXT	8
#define	ACTION_FORWARD		9
#define	ACTION_REDIAL		10
#define	ACTION_REPLY		11
#define	ACTION_POWERDIAL	12	
#define	ACTION_CALLBACK		13
#define	ACTION_ABBREV		14
#define	ACTION_TEST		15
#define	ACTION_PLAY		16
#define	ACTION_VBOX_PLAY	17
#define	ACTION_CALCULATOR	18
#define	ACTION_TIMER		19
#define	ACTION_GOTO		20
#define	ACTION_MENU		21
#define	ACTION_DISCONNECT	22
#define	ACTION_HELP		23
#define ACTION_DEFLECT		24
#define ACTION_SETFORWARD	25
#define ACTION_EXECUTE		26
#define ACTION_FILE		27
#define ACTION_PICK		28
#define	ACTION_PASSWORD		29
#define	ACTION_PASSWORD_WRITE	30
#define	ACTION_NOTHING		31
#define	ACTION_EFI		32

struct route_cond { /* an item */
	struct route_cond	*next;			/* next entry */
	int 			index;			/* index of cond_defs */
	int			match;			/* what is matching (MATCH_*) */
	int			value_type;		/* type of value (VALUE_TYPE_*) */
	int			value_extension;	/* will it be extended? */
	int			integer_value;		/* first integer */
	int			integer_value_to;	/* second integer */
	char			*string_value;		/* first string */
	char			*string_value_to;	/* second string */
	int			comp_string;		/* compare value of strings */
};

struct route_param { /* a parameter */
	struct route_param	*next;			/* next item */
	int			index;			/* index of param_defs */
	unsigned long long	id;			/* what is it (PARAM_*) */
	int			value_type;		/* type of value (VALUE_TYPE_*) */
	int			value_extension;	/* will it be extended? */
	int			integer_value;		/* integer value */
	char			*string_value;		/* string value */
};

struct route_action { /* an action has a list of parameters */
	struct route_action	*next;			/* next item */
	struct route_param	*param_first;		/* link to parameter list */
	int			index;			/* index of action_defs */
	int			timeout;		/* timeout value for action (0 = no timeout) */
	int			line;			/* line parsed from */
};

struct route_rule { /* a rule has a list of items and actions */
	struct route_rule	*next;			/* next item */
	char			file[128];		/* filename */
	int			line;			/* line parsed from */
	struct route_cond	*cond_first;		/* link to condition list */
	struct route_action	*action_first;		/* link to action list */
};

struct route_ruleset { /* the ruleset is a list of rules */
	struct route_ruleset	*next;			/* next item */
	char			file[128];		/* filename */
	int			line;			/* line parsed from */
	char			name[64];		/* name of rule */
	struct route_rule	*rule_first;		/* linke to rule list */
};

struct cond_defs { /* defintion of all conditions */
	char			*name;			/* item's name */
	int			match;			/* what to check */
	int			type;			/* type of value (COND_TYPE) */
	char			*doc;			/* syntax */
	char			*help;			/* short help */
};

struct param_defs { /* definition of all options */
	unsigned long long	id;			/* ID of parameter (just for checking) */
	char			*name;			/* name of parameter */
	int			type;			/* type of value (PARAM_TYPE_*) */
	char			*doc;			/* syntax */
	char			*help;			/* quick help */
};

struct action_defs { /* definition of all actions */
	int			id;			/* ID of parameter (just for checking) */
	char			*name;
	void			(EndpointAppPBX::*init_func)(void);
	void			(EndpointAppPBX::*dialing_func)(void);
	void			(EndpointAppPBX::*hangup_func)(void);
	unsigned long long	 params;
	char			*help;
};



extern struct cond_defs		cond_defs[];
extern struct param_defs	param_defs[];
extern struct action_defs	action_defs[];
extern struct route_ruleset	*ruleset_first;
extern struct route_ruleset	*ruleset_main;
extern struct route_action	action_external;
extern struct route_action	action_internal;
extern struct route_action	action_chan;
extern struct route_action	action_vbox;
extern struct route_action	action_partyline;
extern struct route_action	action_password;
extern struct route_action	action_password_write;
extern struct route_action	action_disconnect;

/* functions */

void doc_rules(const char *action);
void ruleset_free(struct route_ruleset *ruleset_start);
void ruleset_debug(struct route_ruleset *ruleset_start);
extern char ruleset_error[256];
struct route_ruleset *ruleset_parse(void);
struct route_ruleset *getrulesetbyname(char *name);

