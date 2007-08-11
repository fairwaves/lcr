/*****************************************************************************\ 
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** match processing of routing configuration                                 **
**                                                                           **
\*****************************************************************************/ 

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "main.h"


struct route_ruleset	*ruleset_first;		/* first entry */
struct route_ruleset	*ruleset_main;		/* pointer to main ruleset */

struct cond_defs cond_defs[] = {
	{ "extern",	MATCH_EXTERN,	COND_TYPE_NULL,
	  "extern", "Matches if call is from external port (no extension)."},
	{ "intern",	MATCH_INTERN,COND_TYPE_NULL,
	  "intern", "Matches if call is from an extension."},
	{ "port",	MATCH_PORT,	COND_TYPE_INTEGER,
	  "port=<number>[-<number>][,...]", "Matches if call is received from given port(s). NOT INTERFACE!"},
	{ "interface",	MATCH_INTERFACE,COND_TYPE_STRING,
	  "interface=<interface>[,...]", "Matches if call is received from given interface(s). NOT PORTS!"},
	{ "callerid",	MATCH_CALLERID,	COND_TYPE_STRING,
	  "callerid=<digits>[-<digits>][,...]", "Matches if caller ID matches or begins with the given (range(s) of) prefixes(s)."},
	{ "extension",	MATCH_EXTENSION,COND_TYPE_STRING,
	  "extension=<digits>[-<digits>][,...]", "Matches if caller calls from given (range(s) of) extension(s)."},
	{ "dialing",	MATCH_DIALING,	COND_TYPE_STRING,
	  "dialing=<digits>[-<digits>][,...]", "Matches if caller has dialed the given (range(s) of) digits at least."},
	{ "enblock",	MATCH_ENBLOCK,	COND_TYPE_NULL,
	  "enblock", "Matches if caller dialed en block. (Dial the number before pick up.)"},
	{ "overlap",	MATCH_OVERLAP,	COND_TYPE_NULL,
	  "overlap", "Matches if caller dialed digit by digit. (Dial the number after pick up.)"},
	{ "anonymous",	MATCH_ANONYMOUS,COND_TYPE_NULL,
	  "anonymous", "Matches if caller uses restricted caller ID or if not available."},
	{ "visible",	MATCH_VISIBLE,	COND_TYPE_NULL,
	  "visible", "Matches if caller ID is presented and if available."},
	{ "unknown",	MATCH_UNKNOWN,	COND_TYPE_NULL,
	  "unknown", "Matches if no ID is available from caller."},
	{ "available",	MATCH_AVAILABLE,COND_TYPE_NULL,
	  "available", "Matches if ID is available from caller."},
	{ "fake",	MATCH_FAKE,	COND_TYPE_NULL,
	  "fake", "Matches if caller ID is not screened and may be faked by caller."},
	{ "real",	MATCH_REAL,	COND_TYPE_NULL,
	  "real", "Matches if caller ID is screend and so it is the real caller's ID."},
	{ "redirected",	MATCH_REDIRECTED,COND_TYPE_NULL,
	  "redirected", "Matches if caller has been redirected."},
	{ "direct",	MATCH_DIRECT	,COND_TYPE_NULL,
	  "direct", "Matches if caller did not come from an redirection."},
	{ "redirid",	MATCH_REDIRID	,COND_TYPE_STRING,
	  "redirid=<digits>[-<digits>][,...]", "Matches if the caller has been redirected by the given (range(s) of) ID(s) or prefix(es))"},
	{ "time",	MATCH_TIME,	COND_TYPE_TIME,
	  "time=<minutes>[-<minutes>][,...]", "Matches if the caller calls within the given (range(s) of) time(s). (e.g. 0700-1900)"},
	{ "mday",	MATCH_MDAY,	COND_TYPE_MDAY,
	  "mday=<day>[-<day>][,...]", "Matches if the caller calls within the given (range(s) of) day(s) of the month. (1..31)"},
	{ "month",	MATCH_MONTH,	COND_TYPE_MONTH,
	  "month=<month>[-<month>][,...]", "Matches if the caller calls within the given (range(s) of) month(s). (1=January..12=December)"},
	{ "year",	MATCH_YEAR,	COND_TYPE_YEAR,
	  "year=<year>[-<year>][,...]", "Matches if the caller calls within the given (range(s) of) year(s). (1970..2106)"},
	{ "wday",	MATCH_WDAY,	COND_TYPE_WDAY,
	  "wday=<day>[-<day>][,...]", "Matches if the caller calls within the given (range(s) of) weekday(s). (1=Monday..7=Sunday)"},
	{ "capability",	MATCH_CAPABILITY, COND_TYPE_CAPABILITY,
	  "capability=speech|audio|video|digital-restricted|digital-unrestricted|digital-unrestricted-tones[,...]", "Matches the given bearer capability(s)."},
	{ "infolayer1",	MATCH_INFOLAYER1, COND_TYPE_INTEGER,
	  "infolayer1=<value>[,...]", "Matches the given information layer 1. (2=u-Law, 3=a-law, see info layer 1 in bearer capability.)"},
	{ "hlc",	MATCH_HLC,	COND_TYPE_INTEGER,
	  "hlc=<value>[,...]", "Matches the high layer capability(s)."},
	{ "file",	MATCH_FILE,	COND_TYPE_STRING,
	  "file=<path>[,...]", "Mathes is the given file exists and if the first character is '1'."},
	{ "execute",	MATCH_EXECUTE,	COND_TYPE_STRING,
	  "execute=<command>[,...]","Matches if the return value of the given command is greater 0."},
	{ "default",	MATCH_DEFAULT,	COND_TYPE_NULL,
	  "default","Matches if no further dialing could match."},
	{ "timeout",	MATCH_TIMEOUT,	COND_TYPE_INTEGER,
	  "timeout=<seconds>","Matches if the ruleset was entered AFTER given seconds."},
	{ "free",	MATCH_FREE,	COND_TYPE_IFATTR,
	  "free=<interface>:<channel>","Matches if the given minimum of channels are free."},
	{ "notfree",	MATCH_NOTFREE,	COND_TYPE_IFATTR,
	  "notfree=<interface>:<channel>","Matches if NOT the given minimum of channels are free."},
	{ "blocked",	MATCH_DOWN,	COND_TYPE_STRING,
	  "blocked=<interfaces>[,...]","Matches if all of the given interfaces are blocked."},
	{ "idle",	MATCH_UP,	COND_TYPE_STRING,
	  "idle=<interface>[,...]","Matches if any of the given interfaces is idle."},
	{ "busy",	MATCH_BUSY,	COND_TYPE_STRING,
	  "busy=<extension>[,...]","Matches if any of the given extension is busy."},
	{ "notbusy",	MATCH_IDLE,	COND_TYPE_STRING,
	  "notbusy=<extension>[,...]","Matches if any of the given extension is not busy."},
	{ "remote",	MATCH_REMOTE,	COND_TYPE_STRING,
	  "remote=<application name>","Matches if remote application is running."},
	{ "notremote",	MATCH_NOTREMOTE,COND_TYPE_NULL,
	  "notremote=<application name>","Matches if remote application is not running."},
	{ NULL, 0, 0, NULL}
};

struct param_defs param_defs[] = {
	{ PARAM_PROCEEDING,
	  "proceeding",	PARAM_TYPE_NULL,
	  "proceeding", "Will set the call into 'proceeding' state to prevent dial timeout."},
	{ PARAM_ALERTING,
	  "alerting",	PARAM_TYPE_NULL,
	  "alerting", "Will set the call into 'altering' state."},
	{ PARAM_CONNECT,
	  "connect",	PARAM_TYPE_NULL,
	  "connect", "Will complete the call before processing the action. Audio path for external calls will be established."},
	{ PARAM_EXTENSION,
	  "extension",	PARAM_TYPE_STRING,
	  "extension=<digits>", "Give extension name (digits) to relate this action to."},
	{ PARAM_EXTENSIONS,
	  "extensions",	PARAM_TYPE_STRING,
	  "extensions=<extension>[,<extension>[,...]]", "One or more extensions may be given."},
	{ PARAM_PREFIX,
	  "prefix",	PARAM_TYPE_STRING,
	  "prefix=<digits>", "Add prefix in front of the dialed number."},
	{ PARAM_CAPA,
	  "capability",	PARAM_TYPE_CAPABILITY,
	  "capability=speech|audio|video|digital-restricted|digital-unrestricted|digital-unrestricted-tones", "Alter the service type of the call."},
	{ PARAM_BMODE,
	  "bmode",	PARAM_TYPE_BMODE,
	  "bmode=transparent|hdlc", "Alter the bchannel mode of the call. Use hdlc for data calls."},
	{ PARAM_INFO1,
	  "infolayer1",	PARAM_TYPE_INTEGER,
	  "infolayer1=<value>", "Alter the layer 1 information of a call. Use 3 for ALAW or 2 for uLAW."},
	{ PARAM_HLC,
	  "hlc",	PARAM_TYPE_INTEGER,
	  "hlc=<value>", "Alter the HLC identification. Use 1 for telephony or omit."},
	{ PARAM_EXTHLC,
	  "exthlc",	PARAM_TYPE_INTEGER,
	  "exthlc=<value>", "Alter extended HLC value. (Mainenance only, don't use it.)"},
	{ PARAM_PRESENT,
	  "present",	PARAM_TYPE_YESNO,
	  "present=yes|no", "Allow or restrict caller ID regardless what the caller wants."},
	{ PARAM_DIVERSION,
	  "diversion",	PARAM_TYPE_DIVERSION,
	  "diversion=cfu|cfnr|cfb|cfp", "Set diversion type."},
	{ PARAM_DEST,
	  "dest",	PARAM_TYPE_DESTIN,
	  "dest=<string>", "Destination number to divert to. Use 'vbox' to divert to vbox. (cfu,cfnr,cfb only)"},
	{ PARAM_SELECT,
	  "select",	PARAM_TYPE_NULL,
	  "select", "Lets the caller select the history of calls using keys '1'/'3' or '*'/'#'."},
	{ PARAM_DELAY,
	  "delay",	PARAM_TYPE_INTEGER,
	  "delay=<seconds>", "Number of seconds to delay."},
	{ PARAM_LIMIT,
	  "limit",	PARAM_TYPE_INTEGER,
	  "limit=<retries>", "Number of maximum retries."},
	{ PARAM_HOST,
	  "host",	PARAM_TYPE_STRING,
	  "host=<string>", "Name of remote VoIP host."},
	{ PARAM_PORT,
	  "port",	PARAM_TYPE_STRING,
	  "port=<value>", "Alternate port to use if 'host' is given."},
	{ PARAM_INTERFACES,
	  "interfaces",	PARAM_TYPE_STRING,
	  "interfaces=<interface>[,<interface>[,...]]", "Give one or a list of Interfaces to select a free channel from."},
	{ PARAM_ADDRESS,
	  "address",	PARAM_TYPE_STRING,
	  "address=<string>", "Complete VoIP address. ( [user@]host[:port] )"},
	{ PARAM_SAMPLE,
	  "sample",	PARAM_TYPE_STRING,
	  "sample=<file prefix>", "Filename of sample (current tone's dir) or full path to sample. ('.wav'/'.wave'/'.isdn' is added automatically."},
	{ PARAM_ANNOUNCEMENT,
	  "announcement",PARAM_TYPE_STRING,
	  "announcement=<file prefix>", "Filename of announcement (inside vbox recording dir) or full path to sample. ('.wav'/'.wave'/'.isdn' is added automatically."},
	{ PARAM_RULESET,
	  "ruleset",	PARAM_TYPE_STRING,
	  "ruleset=<name>", "Ruleset to go to."},
	{ PARAM_CAUSE,
	  "cause",	PARAM_TYPE_INTEGER,
	  "cause=<cause value>", "Cause value when disconnecting. (21=reject 1=unassigned 63=service not available)"},
	{ PARAM_LOCATION,
	  "location",	PARAM_TYPE_INTEGER,
	  "location=<location value>", "Location of cause value when disconnecting. (0=user 1=private network sering local user)"},
	{ PARAM_DISPLAY,
	  "display",	PARAM_TYPE_STRING,
	  "display=<text>", "Message to display on the caller's telephone. (internal only)"},
	{ PARAM_PORTS,
	  "ports",	PARAM_TYPE_INTEGER,
	  "ports=<port>[,<port>[,...]]", "ISDN port[s] to use."},
	{ PARAM_TPRESET,
	  "tpreset",	PARAM_TYPE_INTEGER,
	  "tpreset=<seconds>", "Preset of countdown timer."},
	{ PARAM_FILE,
	  "file",	PARAM_TYPE_STRING,
	  "file=<full path>", "Full path to file name."},
	{ PARAM_CONTENT,
	  "content",	PARAM_TYPE_STRING,
	  "content=<string>", "Content to write into file."},
	{ PARAM_APPEND,
	  "append",	PARAM_TYPE_NULL,
	  "append", "Will append to given file, rather than overwriting it."},
	{ PARAM_EXECUTE,
	  "execute",	PARAM_TYPE_STRING,
	  "execute=<full path>", "Full path to script/command name. (Dialed digits are the argument 1.)"},
	{ PARAM_PARAM,
	  "param",	PARAM_TYPE_STRING,
	  "param=<string>", "Optionally this parameter can be inserted as argument 1, others are shifted."},
	{ PARAM_TYPE,
	  "type",	PARAM_TYPE_TYPE,
	  "type=unknown|subscriber|national|international", "Type of number to dial, default is 'unknown'."},
	{ PARAM_COMPLETE,
	  "complete",	PARAM_TYPE_NULL,
	  "complete", "Indicates complete number as given by prefix. Proceeding of long distance calls may be faster."},
	{ PARAM_CALLERID,
	  "callerid",	PARAM_TYPE_STRING,
	  "callerid=<digits>", "Change caller ID to given string."},
	{ PARAM_CALLERIDTYPE,
	  "calleridtype",PARAM_TYPE_CALLERIDTYPE,
	  "calleridtype=[unknown|subscriber|national|international]", "Type of caller ID. For normal MSN use 'unknown'"},
	{ PARAM_CALLTO,
	  "callto",	PARAM_TYPE_STRING,
	  "callto=<digits>", "Where to call back. By default the caller ID is used."},
	{ PARAM_ROOM,
	  "room",	PARAM_TYPE_INTEGER,
	  "room=<digits>", "Conference room number, must be greater 0, as in real life."},
	{ PARAM_TIMEOUT,
	  "timeout",	PARAM_TYPE_INTEGER,
	  "timeout=<seconds>", "Timeout before continue with next action."},
	{ PARAM_NOPASSWORD,
	  "nopassword",	PARAM_TYPE_NULL,
	  "nopassword", "Don't ask for password. Be sure to authenticate right via real caller ID."},
	{ PARAM_STRIP,
	  "strip",	PARAM_TYPE_NULL,
	  "strip", "Remove digits that were required to match this rule."},
	{ PARAM_APPLICATION,
	  "application",PARAM_TYPE_STRING,
	  "application", "Name of remote application to make call to."},
	{ 0, NULL, 0, NULL, NULL}
};

struct action_defs action_defs[] = {
	{ ACTION_EXTERNAL,
	  "extern",	&EndpointAppPBX::action_init_call, &EndpointAppPBX::action_dialing_external, &EndpointAppPBX::action_hangup_call,
	  PARAM_CONNECT | PARAM_PREFIX | PARAM_COMPLETE | PARAM_TYPE | PARAM_CAPA | PARAM_BMODE | PARAM_INFO1 | PARAM_HLC | PARAM_EXTHLC | PARAM_PRESENT | PARAM_INTERFACES | PARAM_CALLERID | PARAM_CALLERIDTYPE | PARAM_TIMEOUT,
	  "Call is routed to extern number as dialed."},
	{ ACTION_INTERNAL,
	  "intern",	&EndpointAppPBX::action_init_call, &EndpointAppPBX::action_dialing_internal, &EndpointAppPBX::action_hangup_call,
	  PARAM_CONNECT | PARAM_EXTENSION | PARAM_TYPE | PARAM_CAPA | PARAM_BMODE | PARAM_INFO1 | PARAM_HLC | PARAM_EXTHLC | PARAM_PRESENT | PARAM_TIMEOUT,
	  "Call is routed to intern extension as given by the dialed number or specified by option."},
	{ ACTION_OUTDIAL,
	  "outdial",	&EndpointAppPBX::action_init_call, &EndpointAppPBX::action_dialing_external, &EndpointAppPBX::action_hangup_call,
	  PARAM_CONNECT | PARAM_PREFIX | PARAM_COMPLETE | PARAM_TYPE | PARAM_CAPA | PARAM_BMODE | PARAM_INFO1 | PARAM_HLC | PARAM_EXTHLC | PARAM_PRESENT | PARAM_INTERFACES | PARAM_CALLERID | PARAM_CALLERIDTYPE | PARAM_TIMEOUT,
	  "Same as 'extern'"},
	{ ACTION_REMOTE,
	  "remote",	&EndpointAppPBX::action_init_remote, &EndpointAppPBX::action_dialing_remote, &EndpointAppPBX::action_hangup_call,
	  PARAM_CONNECT | PARAM_APPLICATION | PARAM_TIMEOUT,
	  "Call is routed to Remote application, like Asterisk."},
	{ ACTION_VBOX_RECORD,
	  "vbox-record",&EndpointAppPBX::action_init_call, &EndpointAppPBX::action_dialing_vbox_record, &EndpointAppPBX::action_hangup_call,
	  PARAM_CONNECT | PARAM_EXTENSION | PARAM_ANNOUNCEMENT | PARAM_TIMEOUT,
	  "Caller is routed to the voice box of given extension."},
	{ ACTION_PARTYLINE,
	  "partyline",&EndpointAppPBX::action_init_partyline, NULL, &EndpointAppPBX::action_hangup_call,
	  PARAM_ROOM,
	  "Caller is participating the conference with the given room number."},
	{ ACTION_LOGIN,
	  "login",	NULL, &EndpointAppPBX::action_dialing_login, NULL,
	  PARAM_CONNECT | PARAM_EXTENSION | PARAM_NOPASSWORD,
	  "Log into the given extension. Password required."},
	{ ACTION_CALLERID,
	  "callerid",	&EndpointAppPBX::action_init_change_callerid, &EndpointAppPBX::action_dialing_callerid, NULL,
	  PARAM_CONNECT | PARAM_CALLERID | PARAM_CALLERIDTYPE | PARAM_PRESENT,
	  "Caller changes the caller ID for all calls."},
	{ ACTION_CALLERIDNEXT,
	  "calleridnext",&EndpointAppPBX::action_init_change_callerid, &EndpointAppPBX::action_dialing_calleridnext, NULL,
	  PARAM_CONNECT | PARAM_CALLERID | PARAM_CALLERIDTYPE | PARAM_PRESENT,
	  "Caller changes the caller ID for the next call."},
	{ ACTION_FORWARD,
	  "forward",	&EndpointAppPBX::action_init_change_forward, &EndpointAppPBX::action_dialing_forward, NULL,
	  PARAM_CONNECT | PARAM_DIVERSION | PARAM_DEST | PARAM_DELAY,
	  "Caller changes the diversion of given type to the given destination or voice box."},
	{ ACTION_REDIAL,
	  "redial",	&EndpointAppPBX::action_init_redial_reply, &EndpointAppPBX::action_dialing_redial, NULL,
	  PARAM_CONNECT | PARAM_SELECT,
	  "Caller redials. (last outgoing call(s))"},
	{ ACTION_REPLY,
	  "reply",	&EndpointAppPBX::action_init_redial_reply, &EndpointAppPBX::action_dialing_reply, NULL,
	  PARAM_CONNECT | PARAM_SELECT,
	  "Caller replies. (last incomming call(s))"},
	{ ACTION_POWERDIAL,
	  "powerdial",	NULL, &EndpointAppPBX::action_dialing_powerdial, NULL,
	  PARAM_CONNECT | PARAM_DELAY | PARAM_LIMIT | PARAM_TIMEOUT,
	  "Caller redials using powerdialing."},
	{ ACTION_CALLBACK,
	  "callback",	NULL, &EndpointAppPBX::action_dialing_callback, &EndpointAppPBX::action_hangup_callback,
	  PARAM_PROCEEDING | PARAM_ALERTING | PARAM_CONNECT | PARAM_EXTENSION | PARAM_DELAY | PARAM_CALLTO | PARAM_PREFIX,
	  "Caller will use the callback service. After disconnecting, the callback is triggered."},
	{ ACTION_ABBREV,
	  "abbrev",	NULL, &EndpointAppPBX::action_dialing_abbrev, NULL,
	  PARAM_CONNECT,
	  "Caller dials abbreviation."},
	{ ACTION_TEST,
	  "test",	NULL, &EndpointAppPBX::action_dialing_test, NULL,
	  PARAM_CONNECT | PARAM_PREFIX | PARAM_TIMEOUT,
	  "Caller dials test mode."},
	{ ACTION_PLAY,
	  "play",	&EndpointAppPBX::action_init_play, NULL, NULL,
	  PARAM_PROCEEDING | PARAM_ALERTING | PARAM_CONNECT | PARAM_SAMPLE | PARAM_TIMEOUT,
	  "Plays the given sample."},
	{ ACTION_VBOX_PLAY,
	  "vbox-play",	&EndpointAppPBX::action_init_vbox_play, &EndpointAppPBX::action_dialing_vbox_play, NULL,
	  PARAM_EXTENSION,
	  "Caller listens to her voice box or to given extension."},
	{ ACTION_CALCULATOR,
	  "calculator",	NULL, &EndpointAppPBX::action_dialing_calculator, NULL,
	  PARAM_CONNECT,
	  "Caller calls the calculator."},
	{ ACTION_TIMER,
	  "timer",	NULL, &EndpointAppPBX::action_dialing_timer, NULL,
	  PARAM_CONNECT | PARAM_TPRESET | PARAM_TIMEOUT,
	  NULL},
//	  "Caller calls the timer."},
	{ ACTION_GOTO,
	  "goto",	NULL, &EndpointAppPBX::action_dialing_goto, NULL,
	  PARAM_CONNECT | PARAM_RULESET | PARAM_STRIP | PARAM_SAMPLE,
	  "Jump to given ruleset and optionally play sample. Dialed digits are not flushed."},
	{ ACTION_MENU,
	  "menu",	NULL, &EndpointAppPBX::action_dialing_menu, NULL,
	  PARAM_CONNECT | PARAM_RULESET | PARAM_SAMPLE,
	  "Same as 'goto', but flushes all digits dialed so far."},
	{ ACTION_DISCONNECT,
	  "disconnect",	NULL, &EndpointAppPBX::action_dialing_disconnect, NULL,
	  PARAM_CONNECT | PARAM_CAUSE | PARAM_LOCATION | PARAM_SAMPLE | PARAM_DISPLAY,
	  "Caller gets disconnected optionally with given cause and given sample and given display text."},
	{ ACTION_DEFLECT,
	  "deflect",	NULL, &EndpointAppPBX::action_dialing_deflect, NULL,
	  PARAM_DEST,
	  NULL},
//	  "External call is deflected to the given destination within the telephone network."},
	{ ACTION_SETFORWARD,
	  "setforward",	NULL, &EndpointAppPBX::action_dialing_setforward, NULL,
	  PARAM_CONNECT | PARAM_DIVERSION | PARAM_DEST | PARAM_PORT,
	  NULL},
//	  "The call forward is set within the telephone network of the external line."},
	{ ACTION_EXECUTE,
	  "execute",	NULL, NULL, &EndpointAppPBX::action_hangup_execute,
	  PARAM_CONNECT | PARAM_EXECUTE | PARAM_PARAM,
	  "Executes the given script file. The file must terminate quickly, because it will halt the PBX."},
	{ ACTION_FILE,
	  "file",	NULL, NULL, &EndpointAppPBX::action_hangup_file,
	  PARAM_CONNECT | PARAM_FILE | PARAM_CONTENT | PARAM_APPEND,
	  "Writes givent content to given file. If content is not given, the dialed digits are written."},
	{ ACTION_PICK,
	  "pick",	&EndpointAppPBX::action_init_pick, NULL, NULL,
	  PARAM_EXTENSIONS,
	  "Pick up a call that is ringing on any phone. Extensions may be given to limit the picking ability."},
	{ ACTION_PASSWORD,
	  "password",	NULL, &EndpointAppPBX::action_dialing_password, NULL,
	  0,
	  NULL},
	{ ACTION_PASSWORD_WRITE,
	  "password_wr",NULL, &EndpointAppPBX::action_dialing_password_wr, NULL,
	  0,
	  NULL},
	{ ACTION_NOTHING,
	  "nothing",	NULL, NULL, NULL,
	  PARAM_PROCEEDING | PARAM_ALERTING | PARAM_CONNECT | PARAM_TIMEOUT,
	  "does nothing. Usefull to wait for calls to be released completely, by giving timeout value."},
	{ ACTION_EFI,
	  "efi",	&EndpointAppPBX::action_init_efi, NULL, NULL,
	  PARAM_PROCEEDING | PARAM_ALERTING | PARAM_CONNECT,
	  "Elektronische Fernsprecher Identifikation - announces caller ID."},
	{ -1,
	  NULL, NULL, NULL, NULL, 0, NULL}
};


/* display documentation of rules */

void doc_rules(const char *name)
{
	int i, j;

	if (name)
	{
		i = 0;
		while(action_defs[i].name)
		{
			if (!strcasecmp(action_defs[i].name, name))
				break;
			i++;
		}
		if (!action_defs[i].name)
		{
			fprintf(stderr, "Given action '%s' unknown.\n", name);
			return;
		}
		name = action_defs[i].name;
	}

	printf("Syntax overview:\n");
	printf("----------------\n\n");
	printf("[ruleset]\n");
	printf("<condition> ...   : <action> [parameter ...]   [timeout=X : <action> ...]\n");
	printf("...\n");
	printf("Please refer to the documentation for description on rule format.\n\n");

	if (!name)
	{
		printf("Available conditions to match:\n");
		printf("------------------------------\n\n");
		i = 0;
		while(cond_defs[i].name)
		{
			printf("Usage: %s\n", cond_defs[i].doc);
			printf("%s\n\n", cond_defs[i].help);
			i++;
		}

		printf("Available actions with their parameters:\n");
		printf("----------------------------------------\n\n");
	} else
	{
		printf("Detailes parameter description of action:\n");
		printf("-----------------------------------------\n\n");
	}
	i = 0;
	while(action_defs[i].name)
	{
		if (name && !!strcmp(action_defs[i].name,name)) /* not selected */
		{
			i++;
			continue;
		}
		if (!action_defs[i].help) /* not internal actions */
		{
			i++;
			continue;
		}
		printf("Usage: %s", action_defs[i].name);
		j = 0;
		while(j < 64)
		{
			if ((1LL<<j) & action_defs[i].params)
				printf(" [%s]", param_defs[j].doc);
			j++;
		}
		printf("\n%s\n\n", action_defs[i].help);
		if (name) /* only show parameter help for specific action */
		{
			j = 0;
			while(j < 64)
			{
				if ((1LL<<j) & action_defs[i].params)
					printf("%s:\n\t%s\n", param_defs[j].doc, param_defs[j].help);
				j++;
			}
			printf("\n");
		}
		i++;
	}
}

void ruleset_free(struct route_ruleset *ruleset_start)
{
	struct route_ruleset *ruleset;
	struct route_rule *rule;
	struct route_cond *cond;
	struct route_action *action;
	struct route_param *param;

	while(ruleset_start)
	{
		ruleset = ruleset_start;
		ruleset_start = ruleset->next;
		while(ruleset->rule_first)
		{
			rule = ruleset->rule_first;
			ruleset->rule_first = rule->next;
			while(rule->cond_first)
			{
				cond = rule->cond_first;
				if (cond->string_value)
				{
					FREE(cond->string_value, 0);
					rmemuse--;
				}
				if (cond->string_value_to)
				{
					FREE(cond->string_value_to, 0);
					rmemuse--;
				}
				rule->cond_first = cond->next;
				FREE(cond, sizeof(struct route_cond));
				rmemuse--;
			}
			while(rule->action_first)
			{
				action = rule->action_first;
				rule->action_first = action->next;
				while(action->param_first)
				{
					param = action->param_first;
					action->param_first = param->next;
					if (param->string_value)
					{
						FREE(param->string_value, 0);
						rmemuse--;
					}
					FREE(param, sizeof(struct route_param));
					rmemuse--;
				}
				FREE(action, sizeof(struct route_action));
				rmemuse--;
			}
			FREE(rule, sizeof(struct route_rule));
			rmemuse--;
		}
		FREE(ruleset, sizeof(struct route_ruleset));
		rmemuse--;
	}
}

void ruleset_debug(struct route_ruleset *ruleset_start)
{
	struct route_ruleset	*ruleset;
	struct route_rule	*rule;
	struct route_cond	*cond;
	struct route_action	*action;
	struct route_param	*param;
	int			first;

	ruleset = ruleset_start;
	while(ruleset)
	{
		printf("Ruleset: '%s'\n", ruleset->name);
		rule = ruleset->rule_first;
		while(rule)
		{
			/* CONDITION */
			first = 1;
			cond = rule->cond_first;
			while(cond)
			{
				if (first)
					printf("    Condition:");
				else
					printf("    and       ");
				first = 0;
				printf(" %s", cond_defs[cond->index].name);
				if (cond->value_type != VALUE_TYPE_NULL)
					printf(" = ");
				next_cond_value:
				switch(cond->value_type)
				{
					case VALUE_TYPE_NULL:
					break;

					case VALUE_TYPE_INTEGER:
					printf("%d", cond->integer_value);
					break;

					case VALUE_TYPE_INTEGER_RANGE:
					printf("%d-%d", cond->integer_value, cond->integer_value_to);
					break;

					case VALUE_TYPE_STRING:
					printf("'%s'", cond->string_value);
					break;

					case VALUE_TYPE_STRING_RANGE:
					printf("'%s'-'%s'", cond->string_value, cond->string_value_to);
					break;

					default:
					printf("Software error: VALUE_TYPE_* %d not known in function '%s' line=%d", cond->value_type, __FUNCTION__, __LINE__);
				}
				if (cond->value_extension && cond->next)
				{
					cond = cond->next;
					printf(" or ");
					goto next_cond_value;
				}

				cond = cond->next;
				printf("\n");
			}

			/* ACTION */
			action = rule->action_first;
			while(action)
			{
				printf("    Action: %s\n", action_defs[action->index].name);
				/* PARAM */
				first = 1;
				param = action->param_first;
				while(param)
				{
					if (first)
						printf("    Param:");
					else
						printf("          ");
					first = 0;
					printf(" %s", param_defs[param->index].name);
					if (param->value_type != VALUE_TYPE_NULL)
						printf(" = ");
					switch(param->value_type)
					{
						case VALUE_TYPE_NULL:
						break;

						case VALUE_TYPE_INTEGER:
						if (param_defs[param->index].type == PARAM_TYPE_CALLERIDTYPE)
						{
							switch(param->integer_value)
							{
								case INFO_NTYPE_UNKNOWN:
								printf("unknown");
								break;
								case INFO_NTYPE_SUBSCRIBER:
								printf("subscriber");
								break;
								case INFO_NTYPE_NATIONAL:
								printf("national");
								break;
								case INFO_NTYPE_INTERNATIONAL:
								printf("international");
								break;
								default:
								printf("unknown(%d)", param->integer_value);
							}
							break;
						}
						if (param_defs[param->index].type == PARAM_TYPE_CAPABILITY)
						{
							switch(param->integer_value)
							{
								case INFO_BC_SPEECH:
								printf("speech");
								break;
								case INFO_BC_AUDIO:
								printf("audio");
								break;
								case INFO_BC_VIDEO:
								printf("video");
								break;
								case INFO_BC_DATARESTRICTED:
								printf("digital-restricted");
								break;
								case INFO_BC_DATAUNRESTRICTED:
								printf("digital-unrestricted");
								break;
								case INFO_BC_DATAUNRESTRICTED_TONES:
								printf("digital-unrestricted-tones");
								break;
								default:
								printf("unknown(%d)", param->integer_value);
							}
							break;
						}
						if (param_defs[param->index].type == PARAM_TYPE_DIVERSION)
						{
							switch(param->integer_value)
							{
								case INFO_DIVERSION_CFU:
								printf("cfu");
								break;
								case INFO_DIVERSION_CFNR:
								printf("cfnr");
								break;
								case INFO_DIVERSION_CFB:
								printf("cfb");
								break;
								case INFO_DIVERSION_CFP:
								printf("cfp");
								break;
								default:
								printf("unknown(%d)", param->integer_value);
							}
							break;
						}
						if (param_defs[param->index].type == PARAM_TYPE_TYPE)
						{
							switch(param->integer_value)
							{
								case INFO_NTYPE_UNKNOWN:
								printf("unknown");
								break;
								case INFO_NTYPE_SUBSCRIBER:
								printf("subscriber");
								break;
								case INFO_NTYPE_NATIONAL:
								printf("national");
								break;
								case INFO_NTYPE_INTERNATIONAL:
								printf("international");
								break;
								default:
								printf("unknown(%d)", param->integer_value);
							}
							break;
						}
						if (param_defs[param->index].type == PARAM_TYPE_YESNO)
						{
							switch(param->integer_value)
							{
								case 1:
								printf("yes");
								break;
								case 0:
								printf("no");
								break;
								default:
								printf("unknown(%d)", param->integer_value);
							}
							break;
						}
						if (param_defs[param->index].type == PARAM_TYPE_NULL)
						{
							break;
						}
						printf("%d", param->integer_value);
						break;

						case VALUE_TYPE_STRING:
						printf("'%s'", param->string_value);
						break;

						default:
						printf("Software error: VALUE_TYPE_* %d not known in function '%s' line=%d", param->value_type, __FUNCTION__, __LINE__);
					}
					param = param->next;
					printf("\n");
				}
				/* TIMEOUT */
				if (action->timeout)
					printf("    Timeout: %d\n", action->timeout);
				action = action->next;
			}
			printf("\n");
			rule = rule->next;
		}
		printf("\n");
		ruleset = ruleset->next;
	}
}


/*
 * parse ruleset
 */
static char *read_string(char *p, char *key, int key_size, char *special)
{
	key[0] = 0;

	if (*p == '\"')
	{
		p++;
		/* quote */
		while(*p)
		{
			if (*p == '\"')
			{
				p++;
				*key = '\0';
				return(p);
			}
			if (*p == '\\')
			{
				p++;
				if (*p == '\0')
				{
					break;
				}
			}
			if (--key_size == 0)
			{
				UPRINT(key, "\001String too long.");
				return(p);
			}
			*key++ = *p++;
		}
		UPRINT(key, "\001Unexpected end of line inside quotes.");
		return(p);
	}

	/* no quote */
	while(*p)
	{
		if (strchr(special, *p))
		{
			*key = '\0';
			return(p);
		}
		if (*p == '\\')
		{
			p++;
			if (*p == '\0')
			{
				UPRINT(key, "\001Unexpected end of line.");
				return(p);
			}
		}
		if (--key_size == 0)
		{
			UPRINT(key, "\001String too long.");
			return(p);
		}
		*key++ = *p++;
	}
	*key = '\0';
	return(p);
}
char ruleset_error[256];
struct route_ruleset *ruleset_parse(void)
{
//	char			from[128];
//	char			to[128];
	int			i;
	unsigned long long	j;
//	int			a,
//				b;
	#define			MAXNESTING 8
	FILE			*fp[MAXNESTING];
	char			filename[MAXNESTING][256];
	int			line[MAXNESTING];
	int			nesting = -1;
	char			buffer[1024],
				key[1024],
				key_to[1024],
				pointer[1024+1],
				*p;
	int			expecting = 1; /* 1 = expecting ruleset */
	int			index,
				value_type,
				integer,
				integer_to; /* condition index, .. */
	struct route_ruleset	*ruleset_start = NULL, *ruleset;
	struct route_ruleset	**ruleset_pointer = &ruleset_start;
	struct route_rule	*rule;
	struct route_rule	**rule_pointer = NULL;
	struct route_cond	*cond;
	struct route_cond	**cond_pointer = NULL;
	struct route_action	*action;
	struct route_action	**action_pointer = NULL;
	struct route_param	*param;
	struct route_param	**param_pointer = NULL;
	char			failure[256];
	unsigned long long	allowed_params;

	/* check the integrity of IDs for ACTION_* and PARAM_* */
	i = 0;
	while(action_defs[i].name)
	{
		if (action_defs[i].id != i)
		{
			PERROR("Software Error action '%s' must have id of %d, but has %d.\n",
				action_defs[i].name, i, action_defs[i].id);
			goto openerror;
		}
		i++;
	}
	i = 0; j = 1;
	while(param_defs[i].name)
	{
		if (param_defs[i].id != j)
		{
			PERROR("Software Error param '%s' must have id of 0x%llx, but has 0x%llx.\n",
				param_defs[i].name, j, param_defs[i].id);
			goto openerror;
		}
		i++;
		j<<=1;
	}

        SPRINT(filename[0], "%s/routing.conf", INSTALL_DATA);

        if (!(fp[0]=fopen(filename[0],"r")))
        {
                PERROR("Cannot open %s\n",filename[0]);
               	goto openerror;
        }
	nesting++;
	fduse++;

	go_leaf:
        line[nesting]=0;
	go_root:
        while((fgets(buffer,sizeof(buffer),fp[nesting])))
        {
                line[nesting]++;
                buffer[sizeof(buffer)-1]=0;
                if (buffer[0]) buffer[strlen(buffer)-1]=0;
                p = buffer;

		/* remove tabs */
		while(*p) {
			if (*p < 32)
				*p = 32;
			p++;
		} 
		p = buffer;

                /* skip spaces, if any */
                while(*p == 32)
                {
                        if (*p == 0)
                                break;
                        p++;
		}

		/* skip comments */
		if (*p == '#')
		{
			p++;
			/* don't skip "define" */
			if (!!strncmp(p, "define", 6))
				continue;
			p+=6;
			if (*p != 32)
				continue;
			/* skip spaces */
			while(*p == 32)
			{
				if (*p == 0)
					break;
				p++;
			}
			p++;
			p = read_string(p, key, sizeof(key), " ");
			if (key[0] == 1) /* error */
			{
				SPRINT(failure, "Parsing Filename failed: %s", key+1);
				goto parse_error;
			}
			if (nesting == MAXNESTING-1)
			{
				SPRINT(failure, "'include' is nesting too deep.\n");
				goto parse_error;
			}
			if (key[0] == '/')
				SCPY(filename[nesting+1], key);
			else
	        		SPRINT(filename[nesting+1], "%s/%s", INSTALL_DATA, key);
		        if (!(fp[nesting+1]=fopen(filename[nesting+1],"r")))
			{
				PERROR("Cannot open %s\n", filename[nesting+1]);
				goto parse_error;
			}
			fduse++;
			nesting++;
			goto go_leaf;
		}
		if (*p == '/') if (p[1] == '/')
			continue;

		/* skip empty lines */
		if (*p == 0)
			continue;

		/* expecting ruleset */
		if (expecting)
		{
			new_ruleset:
			/* expecting [ */
			if (*p != '[')
			{
				SPRINT(failure, "Expecting ruleset name starting with '['.");
				goto parse_error;
			}
			p++;

			/* reading ruleset name text */
			i = 0;
			while(*p>' ' && *p<127 && *p!=']')
			{
				if (*p>='A' && *p<='Z') *p = *p-'A'+'a'; /* lower case */
				key[i++] = *p++;
				if (i == sizeof(key)) i--; /* limit */
			}
			key[i] = 0;
			if (key[0] == '\0') {
				SPRINT(failure, "Missing ruleset name after '['.");
				goto parse_error;
			}

			/* expecting ] and nothing more */
			if (*p != ']') {
				SPRINT(failure, "Expecting ']' after ruleset name.");
				goto parse_error;
			}
			p++;
			if (*p != 0) {
				SPRINT(failure, "Unexpected character after ruleset name.");
				goto parse_error;
			}

			/* check for duplicate rulesets */
			ruleset = ruleset_start;
			while(ruleset)
			{
				if (!strcmp(ruleset->name, key))
				{
					SPRINT(failure, "Duplicate ruleset '%s', already defined in file '%s' line %d.", key, ruleset->file, ruleset->line);
					goto parse_error;
				}
				ruleset = ruleset->next;
			}

			/* create ruleset */
			ruleset = (struct route_ruleset *)MALLOC(sizeof(struct route_ruleset));
			rmemuse++;
			*ruleset_pointer = ruleset;
			ruleset_pointer = &(ruleset->next);
			SCPY(ruleset->name, key);
			SCPY(ruleset->file, filename[nesting]);
			ruleset->line = line[nesting];
			rule_pointer = &(ruleset->rule_first);
			expecting = 0;
			continue;
		}

		/* for new ruleset [ */
		if (*p == '[')
		{
			goto new_ruleset;
		}

		/* Alloc memory for rule */
		rule = (struct route_rule *)MALLOC(sizeof(struct route_rule));
		rmemuse++;
		*rule_pointer = rule;
		rule_pointer = &(rule->next);
		cond_pointer = &(rule->cond_first);
		action_pointer = &(rule->action_first);
		SCPY(rule->file, filename[nesting]);
		rule->line = line[nesting];

		/* loop CONDITIONS */
		while(*p!=':' && *p!='\0')
		{
			/* read item text */
			i = 0;
			while((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') || (*p>='0' && *p<='9'))
			{
				if (*p>='A' && *p<='Z') *p = *p-'A'+'a'; /* lower case */
				key[i++] = *p++;
				if (i == sizeof(key)) i--; /* limit */
			}
			key[i] = 0;
			if (key[0] == '\0')
		       	{
				SPRINT(failure, "Expecting condition item name or ':' for end of condition list.");
				goto parse_error;
			}
			if (*p!=' ' && *p!='=')
			{
				SPRINT(failure, "Illegal character '%c' after condition name '%s'. Expecting '=' for equation or ' ' to seperate condition items.", *p, key);
				goto parse_error;
			}

			/* check if condition exists */
			index = 0;
			while(cond_defs[index].name)
			{
				if (!strcmp(cond_defs[index].name, key))
					break;
				index++;
			}
			if (cond_defs[index].name == NULL)
			{
				SPRINT(failure, "Unknown condition item name '%s'.", key);
				goto parse_error;
			}

			/* items without values must not have any parameter */
			if (cond_defs[index].type == COND_TYPE_NULL)
			{
				if (*p == '=')
				{
					SPRINT(failure, "Condition item '%s' must not have any value. Don't use '=' for this type of condition.", key);
					goto parse_error;
				}
				if (*p != ' ')
				{
					SPRINT(failure, "Condition item '%s' must not have any value. Expecting ' ' or tab after item name.", key);
					goto parse_error;
				}
//				p++;
			} else
			{
				if (*p == ' ')
				{
					SPRINT(failure, "Condition item '%s' must have at least one value, '=' expected, and not a space.", key);
					goto parse_error;
				}
				if (*p != '=')
				{
					SPRINT(failure, "Condition item '%s' must have at least one value, '=' expected.", key);
					goto parse_error;
				}
				p++;
			}

			/* check for duplicate condition */
			cond = rule->cond_first;
			while(cond)
			{
				if (cond->index == index)
				{
					SPRINT(failure, "Duplicate condition '%s', use ',' to give multiple values.", key);
					goto parse_error;
				}
				cond = cond->next;
			}

			nextcondvalue:
			/* Alloc memory for item */
			cond = (struct route_cond *)MALLOC(sizeof(struct route_cond));
			rmemuse++;
			*cond_pointer = cond;
			cond_pointer = &(cond->next);
			cond->index = index;
			cond->match = cond_defs[index].match;
			switch(cond_defs[index].type)
			{
				case COND_TYPE_NULL:
				if (*p=='=')
				{
					SPRINT(failure, "Expecting no value.");
					goto parse_error;
				}
				value_type = VALUE_TYPE_NULL;
				break;

				/* parse all integer values/ranges */
				case COND_TYPE_INTEGER:
				case COND_TYPE_TIME:
				case COND_TYPE_MDAY:
				case COND_TYPE_MONTH:
				case COND_TYPE_WDAY:
				case COND_TYPE_YEAR:
				integer = integer_to = 0;
				if (*p==',' || *p==' ' || *p=='\0')
				{
					SPRINT(failure, "Missing integer value.");
					goto parse_error;
				}
				while(*p>='0' && *p<='9')
				{
					integer = integer*10 + *p-'0';
					p++;
				}
				value_type = VALUE_TYPE_INTEGER;
				if (*p == '-')
				{
					p++;
					if (*p==',' || *p==' ' || *p=='\0')
					{
						SPRINT(failure, "Missing integer value.");
						goto parse_error;
					}
					while(*p>='0' && *p<='9')
					{
						integer_to = integer_to*10 + *p-'0';
						p++;
					}
					value_type = VALUE_TYPE_INTEGER_RANGE;
				}
				if (cond_defs[index].type == COND_TYPE_TIME)
				{
					// Simon: i store the time as decimal, later i compare it correctly:
					// hours * 100 + minutes
					if (integer == 2400)
						integer = 0;
					if (integer >= 2400)
					{
						timeoutofrange1:
						SPRINT(failure, "Given time '%d' not in range 0000..2359 (or 2400 for 0000)", integer);
						goto parse_error;
					}
					if (integer%100 >= 60)
						goto timeoutofrange1;
					if (value_type == VALUE_TYPE_INTEGER)
						goto integer_done;
					if (integer_to == 2400)
						integer_to = 0;
					if (integer_to >= 2400)
					{
						timeoutofrange2:
						SPRINT(failure, "Given time '%d' not in range 0000..2359 (or 2400 for 0000)", integer_to);
						goto parse_error;
					}
					if (integer_to%100 >= 60)
						goto timeoutofrange2;
				}
				if (cond_defs[index].type == COND_TYPE_MDAY)
				{
					if (integer<1 || integer>31)
					{
						SPRINT(failure, "Given day-of-month '%d' not in range 1..31", integer);
						goto parse_error;
					} 
					if (value_type == VALUE_TYPE_INTEGER)
						goto integer_done;
					if (integer_to<1 || integer_to>31)
					{
						SPRINT(failure, "Given day-of-month '%d' not in range 1..31", integer_to);
						goto parse_error;
					} 
				}
				if (cond_defs[index].type == COND_TYPE_WDAY)
				{
					if (integer<1 || integer>7)
					{
						SPRINT(failure, "Given day-of-week '%d' not in range 1..7", integer);
						goto parse_error;
					} 
					if (value_type == VALUE_TYPE_INTEGER)
						goto integer_done;
					if (integer_to<1 || integer_to>7)
					{
						SPRINT(failure, "Given day-of-week '%d' not in range 1..7", integer_to);
						goto parse_error;
					} 
				}
				if (cond_defs[index].type == COND_TYPE_MONTH)
				{
					if (integer<1 || integer>12)
					{
						SPRINT(failure, "Given month '%d' not in range 1..12", integer);
						goto parse_error;
					} 
					if (value_type == VALUE_TYPE_INTEGER)
						goto integer_done;
					if (integer_to<1 || integer_to>12)
					{
						SPRINT(failure, "Given month '%d' not in range 1..12", integer_to);
						goto parse_error;
					} 
				}
				if (cond_defs[index].type == COND_TYPE_YEAR)
				{
					if (integer<1970 || integer>2106)
					{
						SPRINT(failure, "Given year '%d' not in range 1970..2106", integer);
						goto parse_error;
					} 
					if (value_type == VALUE_TYPE_INTEGER)
						goto integer_done;
					if (integer_to<1970 || integer_to>2106)
					{
						SPRINT(failure, "Given year '%d' not in range 1970..2106", integer_to);
						goto parse_error;
					} 
				}
				integer_done:
				cond->integer_value = integer;
				cond->integer_value_to = integer_to;
				cond->value_type = value_type;
				break;

				/* parse all string values/ranges */
				case COND_TYPE_STRING:
				key[0] = key_to[0] = '\0';
				if (*p==',' || *p==' ' || *p=='\0')
				{
					SPRINT(failure, "Missing string value, use \"\" for empty string.");
					goto parse_error;
				}
				p = read_string(p, key, sizeof(key), "-, ");
				if (key[0] == 1) /* error */
				{
					SPRINT(failure, "Parsing String failed: %s", key+1);
					goto parse_error;
				}
				value_type = VALUE_TYPE_STRING;
				if (*p == '-')
				{
					p++;
					if (*p==',' || *p==' ' || *p=='\0')
					{
						SPRINT(failure, "Missing string value, use \"\" for empty string.");
						goto parse_error;
					}
					p = read_string(p, key_to, sizeof(key_to), "-, ");
					if (key_to[0] == 1) /* error */
					{
						SPRINT(failure, "Parsing string failed: %s", key_to+1);
						goto parse_error;
					}
					value_type = VALUE_TYPE_STRING_RANGE;
					if (strlen(key) != strlen(key_to))
					{
						SPRINT(failure, "Given range of strings \"%s\"-\"%s\" have unequal length.", key, key_to);
						goto parse_error;
					}
					if (key[0] == '\0')
					{
						SPRINT(failure, "Given range has no length.");
						goto parse_error;
					}
				}
				alloc_string:
				cond->string_value = (char *)MALLOC(strlen(key)+1);
				rmemuse++;
				UCPY(cond->string_value, key);
				if (value_type == VALUE_TYPE_STRING_RANGE)
				{
					cond->string_value_to = (char *)MALLOC(strlen(key_to)+1);
					rmemuse++;
					UCPY(cond->string_value_to, key_to);
					cond->comp_string = strcmp(key, key_to);
				}
				cond->value_type = value_type;
				break;

				/* parse service value */
				case COND_TYPE_CAPABILITY:
				if (!strncasecmp("speech", p, 6))
					cond->integer_value = INFO_BC_SPEECH;
				else if (!strncasecmp("audio", p, 5))
					cond->integer_value = INFO_BC_AUDIO;
				else if (!strncasecmp("video", p, 5))
					cond->integer_value = INFO_BC_VIDEO;
				else if (!strncasecmp("digital-restricted", p, 18))
					cond->integer_value = INFO_BC_DATARESTRICTED;
				else if (!strncasecmp("digital-unrestricted", p, 20))
					cond->integer_value = INFO_BC_DATAUNRESTRICTED;
				else if (!strncasecmp("digital-unrestricted-tones", p, 26))
					cond->integer_value = INFO_BC_DATAUNRESTRICTED_TONES;
				else
				{
					SPRINT(failure, "Given service type is invalid or misspelled.");
					goto parse_error;
				}
				cond->value_type = VALUE_TYPE_INTEGER;
				break;

				/* parse bmode value */
				case COND_TYPE_BMODE:
				if (!strncasecmp("transparent", p, 11))
					cond->integer_value = INFO_BMODE_CIRCUIT;
				else if (!strncasecmp("hdlc", p, 4))
					cond->integer_value = INFO_BMODE_PACKET;
				else
				{
					SPRINT(failure, "Given bchannel mode is invalid or misspelled.");
					goto parse_error;
				}
				cond->value_type = VALUE_TYPE_INTEGER;
				break;

				/* parse interface attribute <if>:<value> */
				case COND_TYPE_IFATTR:
				key[0] = key_to[0] = '\0';
				if (*p==':' || *p==',' || *p==' ' || *p=='\0')
				{
					SPRINT(failure, "Missing interface name.");
					goto parse_error;
				}
				p = read_string(p, key, sizeof(key), ":-, ");
				if (key[0] == 1) /* error */
				{
					SPRINT(failure, "Parsing interface failed: %s", key+1);
					goto parse_error;
				}
				if (*p != ':')
				{
					SPRINT(failure, "Expeciting kolon to seperate value behind interface name.");
					goto parse_error;
				}
				SCCAT(key, *p++);
				while(*p>='0' && *p<='9')
				{
					SCCAT(key, *p++);
				}
				if (*p!=',' && *p!=' ' && *p!='\0')
				{
					SPRINT(failure, "Invalid characters behind value.");
					goto parse_error;
				}
				value_type = VALUE_TYPE_STRING;
				goto alloc_string;
				break;

				default:
				SPRINT(failure, "Software error: COND_TYPE_* %d not parsed in function '%s'", cond_defs[index].type, __FUNCTION__);
				goto parse_error;
			}
			/* if we have another value for that item, we attach it */
			if (*p == ',')
			{
				p++;
				/* next item */
				cond->value_extension = 1;
				goto nextcondvalue;
			}
			/* to seperate the items, a space is required */
			if (*p != ' ')
			{
				SPRINT(failure, "Character '%c' not expected here. Use ',' to seperate multiple possible values.", *p);
				goto parse_error;
			}
                	/* skip spaces */
	                while(*p == 32)
	                {
	                        if (*p == 0)
	                                break;
	                        p++;
			}
		}

		/* we are done with CONDITIONS, so we expect the ACTION */
		if (*p != ':')
		{
			SPRINT(failure, "Expecting ':' after condition item(s).");
			goto parse_error;
		}
		p++;

		nextaction:
                /* skip spaces, if any */
                while(*p == 32)
                {
                        if (*p == 0)
                                break;
                        p++;
		}

		/* read action name */
		i = 0;
		while((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') || (*p>='0' && *p<='9') || *p == '-')
		{
			if (*p>='A' && *p<='Z') *p = *p-'A'+'a'; /* lower case */
			key[i++] = *p++;
			if (i == sizeof(key)) i--; /* limit */
		}
		key[i] = 0;
		if (key[0] == '\0') {
			SPRINT(failure, "Expecting action name.");
			goto parse_error;
		}

		/* check if item exists */
		index = 0;
		while(action_defs[index].name)
		{
			if (!action_defs[index].help) /* not internal actions */
			{
				index++;
				continue;
			}
			if (!strcmp(action_defs[index].name, key))
				break;
			index++;
		}
		if (action_defs[index].name == NULL)
		{
			SPRINT(failure, "Unknown action name '%s'.", key);
			goto parse_error;
		}
		allowed_params = action_defs[index].params;

		/* alloc memory for action */
		action = (struct route_action *)MALLOC(sizeof(struct route_action));
		rmemuse++;
		*action_pointer = action;
		action_pointer = &(action->next);
		param_pointer = &(action->param_first);
		action->index = index;
		action->line = line[nesting];

               	/* skip spaces after param name */
	        while(*p == 32)
	        {
	        	if (*p == 0)
				break;
			p++;
		}

		/* loop PARAMS */
		while(*p != 0)
		{
			/* read param text */
			i = 0;
			while((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') || (*p>='0' && *p<='9'))
			{
				if (*p>='A' && *p<='Z') *p = *p-'A'+'a'; /* lower case */
				key[i++] = *p++;
				if (i == sizeof(key)) i--; /* limit */
			}
			key[i] = 0;
			if (key[0] == '\0') {
				SPRINT(failure, "Expecting parameter name.");
				goto parse_error;
			}

			/* check if item exists */
			index = 0;
			while(param_defs[index].name)
			{
				if (!strcmp(param_defs[index].name, key))
					break;
				index++;
			}
			if (param_defs[index].name == NULL)
			{
				SPRINT(failure, "Unknown param name '%s'.", key);
				goto parse_error;
			}

			/* check if item is allowed for the action */
			if (!(param_defs[index].id & allowed_params))
			{
				SPRINT(failure, "Param name '%s' exists, but not for this action.", key);
				goto parse_error;
			}

			/* params without values must not have any parameter */
			if (param_defs[index].type == PARAM_TYPE_NULL)
			{
				if (*p!=' ' && *p!='\0')
				{
					SPRINT(failure, "Parameter '%s' must not have any value.", key);
					goto parse_error;
				}
			} else
			{
				if (*p == ' ')
				{
					SPRINT(failure, "Parameter '%s' must have at least one value, '=' expected and not a space.", key);
					goto parse_error;
				}
				if (*p != '=')
				{
					SPRINT(failure, "Parameter '%s' must have at least one value, '=' expected.", key);
					goto parse_error;
				}
				p++;
			}

			/* special timeout value */
			if (!strcmp("timeout", key))
			{
				if (action->timeout)
				{
					SPRINT(failure, "Duplicate timeout value.");
					goto parse_error;
				}
				if (*p==',' || *p==' ' || *p=='\0')
				{
					SPRINT(failure, "Missing integer value.");
					goto parse_error;
				}
				integer = 0;
				while(*p>='0' && *p<='9')
				{
					integer = integer*10 + *p-'0';
					p++;
				}
				if (integer < 1)
				{
					SPRINT(failure, "Expecting timeout value greater 0.");
					goto parse_error;
				}
				if (*p!=' ' && *p!='\0')
				{
					SPRINT(failure, "Character '%c' not expected here. Use ' ' to seperate parameters.", *p);
					goto parse_error;
				}
				/* skip spaces */
				while(*p == 32)
				{
					if (*p == 0)
						break;
					p++;
				}
				action->timeout = integer;
				/* check for next ACTION */
				if (*p == ':')
				{
					p++;
					goto nextaction;
				}
				continue;
			}

			/* check for duplicate parameters */
			param = action->param_first;
			while(param)
			{
				if (param->index == index)
				{
					SPRINT(failure, "Duplicate parameter '%s', use ',' to give multiple values.", key);
					goto parse_error;
				}
				param = param->next;
			}

			nextparamvalue:
			/* Alloc memory for param */
			param = (struct route_param *)MALLOC(sizeof(struct route_param));
			rmemuse++;
			*param_pointer = param;
			param_pointer = &(param->next);
			param->index = index;
			param->id = param_defs[index].id;

			switch(param_defs[index].type)
			{
				/* parse null value */
				case PARAM_TYPE_NULL:
				param->value_type = VALUE_TYPE_NULL;
				break;

				/* parse integer value */
				case PARAM_TYPE_INTEGER:
				integer = 0;
				if (*p==',' || *p==' ' || *p=='\0')
				{
					SPRINT(failure, "Missing integer value.");
					goto parse_error;
				}
				while(*p>='0' && *p<='9')
				{
					integer = integer*10 + *p-'0';
					p++;
				}
				param->integer_value = integer;
				param->value_type = VALUE_TYPE_INTEGER;
				break;

#if 0
				/* parse ports value */
				case PARAM_TYPE_PORTS:
				key[0] = '\0';
				if (*p==',' || *p==' ' || *p=='\0')
				{
					SPRINT(failure, "Missing port number, omit parameter or give port number.");
					goto parse_error;
				}
				i = 0;
				nextport:
				integer = 0;
				while(*p>='0' && *p<='9')
				{
					if (i < (int)sizeof(key)-1)
					{
						key[i] = *p;
						key[i++] = '\0';
					}
					integer = integer*10 + *p-'0';
					p++;
				}
				if (integer > 255)
				{
					SPRINT(failure, "Port number too high.");
					goto parse_error;
				}
				if (*p==',')
				{
					if (i < (int)sizeof(key)-1)
					{
						key[i] = *p;
						key[i++] = '\0';
					}
					p++;
					goto nextport;
				}
				goto mallocstring;
#endif

				/* parse string value */
				case PARAM_TYPE_STRING:
				case PARAM_TYPE_CALLERIDTYPE:
				case PARAM_TYPE_CAPABILITY:
				case PARAM_TYPE_BMODE:
				case PARAM_TYPE_DIVERSION:
				case PARAM_TYPE_DESTIN:
				case PARAM_TYPE_TYPE:
				case PARAM_TYPE_YESNO:
				key[0] = '\0';
				if (*p==',' || *p==' ' || *p=='\0')
				{
					SPRINT(failure, "Missing string value, use \"\" for empty string.");
					goto parse_error;
				}
				p = read_string(p, key, sizeof(key), " ");
				if (key[0] == 1) /* error */
				{
					SPRINT(failure, "Parsing string failed: %s", key+1);
					goto parse_error;
				}
				if (param_defs[index].type == PARAM_TYPE_CALLERIDTYPE)
				{
					param->value_type = VALUE_TYPE_INTEGER;
					if (!strcasecmp(key, "unknown"))
					{
						param->integer_value = INFO_NTYPE_UNKNOWN;
						break;
					}
					if (!strcasecmp(key, "subscriber"))
					{
						param->integer_value = INFO_NTYPE_SUBSCRIBER;
						break;
					}
					if (!strcasecmp(key, "national"))
					{
						param->integer_value = INFO_NTYPE_NATIONAL;
						break;
					}
					if (!strcasecmp(key, "international"))
					{
						param->integer_value = INFO_NTYPE_INTERNATIONAL;
						break;
					}
					SPRINT(failure, "Caller ID type '%s' unknown.", key);
					goto parse_error;
				}
				if (param_defs[index].type == PARAM_TYPE_CAPABILITY)
				{
					param->value_type = VALUE_TYPE_INTEGER;
					if (!strcasecmp(key, "speech"))
					{
						param->integer_value = INFO_BC_SPEECH;
						break;
					}
					if (!strcasecmp(key, "audio"))
					{
						param->integer_value = INFO_BC_AUDIO;
						break;
					}
					if (!strcasecmp(key, "video"))
					{
						param->integer_value = INFO_BC_VIDEO;
						break;
					}
					if (!strcasecmp(key, "digital-restricted"))
					{
						param->integer_value = INFO_BC_DATARESTRICTED;
						break;
					}
					if (!strcasecmp(key, "digital-unrestricted"))
					{
						param->integer_value = INFO_BC_DATAUNRESTRICTED;
						break;
					}
					if (!strcasecmp(key, "digital-unrestricted-tones"))
					{
						param->integer_value = INFO_BC_DATAUNRESTRICTED_TONES;
						break;
					}
					SPRINT(failure, "Service type '%s' unknown.", key);
					goto parse_error;
				}
				if (param_defs[index].type == PARAM_TYPE_BMODE)
				{
					param->value_type = VALUE_TYPE_INTEGER;
					if (!strcasecmp(key, "transparent"))
					{
						param->integer_value = INFO_BMODE_CIRCUIT;
						break;
					}
					if (!strcasecmp(key, "hdlc"))
					{
						param->integer_value = INFO_BMODE_PACKET;
						break;
					}
					SPRINT(failure, "Bchannel mode '%s' unknown.", key);
					goto parse_error;
				}
				if (param_defs[index].type == PARAM_TYPE_DIVERSION)
				{
					param->value_type = VALUE_TYPE_INTEGER;
					if (!strcasecmp(key, "cfu"))
					{
						param->integer_value = INFO_DIVERSION_CFU;
						break;
					}
					if (!strcasecmp(key, "cfb"))
					{
						param->integer_value = INFO_DIVERSION_CFB;
						break;
					}
					if (!strcasecmp(key, "cfnr"))
					{
						param->integer_value = INFO_DIVERSION_CFNR;
						break;
					}
					if (!strcasecmp(key, "cfp"))
					{
						param->integer_value = INFO_DIVERSION_CFP;
						break;
					}
					SPRINT(failure, "Diversion type '%s' unknown.", key);
					goto parse_error;
				}
				if (param_defs[index].type == PARAM_TYPE_TYPE)
				{
					param->value_type = VALUE_TYPE_INTEGER;
					if (!strcasecmp(key, "unknown"))
					{
						param->integer_value = INFO_NTYPE_UNKNOWN;
						break;
					}
					if (!strcasecmp(key, "subscriber"))
					{
						param->integer_value = INFO_NTYPE_SUBSCRIBER;
						break;
					}
					if (!strcasecmp(key, "national"))
					{
						param->integer_value = INFO_NTYPE_NATIONAL;
						break;
					}
					if (!strcasecmp(key, "international"))
					{
						param->integer_value = INFO_NTYPE_INTERNATIONAL;
						break;
					}
					SPRINT(failure, "Number type '%s' unknown.", key);
					goto parse_error;
				}
				if (param_defs[index].type == PARAM_TYPE_YESNO)
				{
					param->value_type = VALUE_TYPE_INTEGER;
					if (!strcasecmp(key, "yes"))
					{
						param->integer_value = 1;
						break;
					}
					if (!strcasecmp(key, "no"))
					{
						param->integer_value = 0;
						break;
					}
					SPRINT(failure, "Value '%s' unknown. ('yes' or 'no')", key);
					goto parse_error;
				}
				param->string_value = (char *)MALLOC(strlen(key)+1);
				rmemuse++;
				UCPY(param->string_value, key);
				param->value_type = VALUE_TYPE_STRING;
				break;

				default:
				SPRINT(failure, "Software error: PARAM_TYPE_* %d not parsed in function '%s'", param_defs[index].type, __FUNCTION__);
				goto parse_error;
			}

			if (*p == ',')
			{
				p++;
				/* next item */
				param->value_extension = 1;
				goto nextparamvalue;
			}

			/* end of line */
			if (*p == '\0')
				break;

			/* to seperate the items, a space is required */
			if (*p != ' ')
			{
				SPRINT(failure, "Character '%c' not expected here. Use ' ' to seperate parameters, or ',' for multiple values.", *p);
				goto parse_error;
			}
                	/* skip spaces */
	                while(*p == 32)
	                {
	                        if (*p == 0)
	                                break;
	                        p++;
			}

			/* check for next ACTION */
			if (*p == ':')
			{
				p++;
				goto nextaction;
			}
		}
	}

	fclose(fp[nesting--]);
	fduse--;

	if (nesting >= 0)
		goto go_root;

	if (!ruleset_start)
	{
		SPRINT(failure, "No ruleset defined.");
	}
	return(ruleset_start);

	parse_error:
	printf("While parsing %s, an error occurred in line %d:\n", filename[nesting], line[nesting]);
	printf("-> %s\n", buffer);
	memset(pointer, ' ', sizeof(pointer));
	pointer[p-buffer] = '^';
	pointer[p-buffer+1] = '\0';
	printf("   %s\n", pointer);
	printf("%s\n", failure);
	SPRINT(ruleset_error, "Error in file %s, line %d: %s",  filename[nesting], line[nesting], failure);

	openerror:
	while(nesting >= 0)
	{
		fclose(fp[nesting--]);
		fduse--;
	}

	ruleset_free(ruleset_start);
	return(NULL);
}

/*
 * return ruleset by name
 */
struct route_ruleset *getrulesetbyname(char *name)
{
	struct route_ruleset *ruleset = ruleset_first;

	while(ruleset)
	{
		if (!strcasecmp(name, ruleset->name))
		{
			break;
		}
		ruleset = ruleset->next;
	}
	PDEBUG(DEBUG_ROUTE, "ruleset %s %s.\n", name, ruleset?"found":"not found");
	return(ruleset);
}

/*
 * parses the current ruleset and returns action
 */
struct route_action *EndpointAppPBX::route(struct route_ruleset *ruleset)
{
	int			match,
				couldmatch = 0, /* any rule could match */
				istrue,
				couldbetrue,
				condition,
				dialing_required,
				avail,
				any;
	struct route_rule	*rule = ruleset->rule_first;
	struct route_cond	*cond;
	struct route_action	*action = NULL;
	unsigned long		comp_len;
	int			j, jj;
	char			callerid[64],	redirid[64];
	int			integer;
	char			*string;
	FILE			*tfp;
	double			timeout;
	struct mISDNport	*mISDNport;
	struct admin_list	*admin;

	/* reset timeout action */
	e_match_timeout = 0; /* no timeout */
	e_match_to_action = NULL;

	SCPY(callerid, numberrize_callerinfo(e_callerinfo.id, e_callerinfo.ntype));
	SCPY(redirid, numberrize_callerinfo(e_redirinfo.id, e_redirinfo.ntype));
	
	PDEBUG(DEBUG_ROUTE, "parsing ruleset '%s'\n", ruleset->name);
	while(rule)
	{
		PDEBUG(DEBUG_ROUTE, "checking rule in line %d\n", rule->line);
		match = 1; /* this rule matches */
		dialing_required = 0;
		timeout = 0; /* timeout time */
		cond = rule->cond_first;
		while(cond)
		{
			condition = 0; /* any condition element is true (1) or could be true (2) */
			checkextension:
			istrue = 0; /* this condition-element is true */
			couldbetrue = 0; /* this conditions-element could be true */
			switch(cond->match)
			{
				case MATCH_EXTERN:
				if (!e_ext.number[0])
					istrue = 1;	 
				break;

				case MATCH_INTERN:
				if (e_ext.number[0])
					istrue = 1;	 
				break;

				case MATCH_PORT:
				if (ea_endpoint->ep_portlist)
				if ((ea_endpoint->ep_portlist->port_type & PORT_CLASS_mISDN_MASK) != PORT_CLASS_mISDN_DSS1)
					break;
				integer = e_callerinfo.isdn_port;
				goto match_integer;

				case MATCH_INTERFACE:
				if (!e_callerinfo.interface[0])
					break;
				string = e_callerinfo.interface;
				goto match_string_prefix;

				case MATCH_CALLERID:
				string = callerid;
				goto match_string_prefix;

				case MATCH_EXTENSION:
				string = e_ext.name;
				goto match_string;

				case MATCH_DIALING:
				string = e_dialinginfo.id;
				goto match_string_prefix;

				case MATCH_ENBLOCK:
				if (!e_overlap)
					istrue = 1;
				break;

				case MATCH_OVERLAP:
				if (e_overlap)
					istrue = 1;
				break;

				case MATCH_ANONYMOUS:
				if (e_callerinfo.present != INFO_PRESENT_ALLOWED)
					istrue = 1;
				break;

				case MATCH_VISIBLE:
				if (e_callerinfo.present == INFO_PRESENT_ALLOWED)
					istrue = 1;
				break;

				case MATCH_UNKNOWN:
				if (e_callerinfo.present == INFO_PRESENT_NOTAVAIL)
					istrue = 1;
				break;

				case MATCH_AVAILABLE:
				if (e_callerinfo.present != INFO_PRESENT_NOTAVAIL)
					istrue = 1;
				break;

				case MATCH_FAKE:
				if (e_callerinfo.screen == INFO_SCREEN_USER)
					istrue = 1;
				break;

				case MATCH_REAL:
				if (e_callerinfo.screen != INFO_SCREEN_USER)
					istrue = 1;
				break;

				case MATCH_REDIRECTED:
				if (e_redirinfo.present != INFO_PRESENT_NULL)
					istrue = 1;
				break;

				case MATCH_DIRECT:
				if (e_redirinfo.present == INFO_PRESENT_NULL)
					istrue = 1;
				break;

				case MATCH_REDIRID:
				string = redirid;
				goto match_string_prefix;

				case MATCH_TIME:
				integer = now_tm->tm_hour*100 + now_tm->tm_min;
				goto match_integer;

				case MATCH_MDAY:
				integer = now_tm->tm_mday;
				goto match_integer;

				case MATCH_MONTH:
				integer = now_tm->tm_mon+1;
				goto match_integer;

				case MATCH_YEAR:
				integer = now_tm->tm_year + 1900;
				goto match_integer;

				case MATCH_WDAY:
				integer = now_tm->tm_wday;
				integer = integer?integer:7; /* correct sunday */
				goto match_integer;

				case MATCH_CAPABILITY:
				integer = e_capainfo.bearer_capa;
				goto match_integer;
	
				case MATCH_INFOLAYER1:
				integer = e_capainfo.bearer_info1;
				goto match_integer;

				case MATCH_HLC:
				integer = e_capainfo.hlc;
				goto match_integer;

				case MATCH_FILE:
				tfp = fopen(cond->string_value, "r");
				if (!tfp)
				{
					break;
				}
				if (fgetc(tfp) == '1')
					istrue = 1;
				fclose(tfp);
				break;

				case MATCH_EXECUTE:
				if (system(cond->string_value) == 0)
					istrue = 1;
				break;

				case MATCH_DEFAULT:
				if (!couldmatch)
					istrue = 1;
				break;

				case MATCH_TIMEOUT:
				timeout = now_d + cond->integer_value;
				istrue = 1;
				break;

				case MATCH_FREE:
				case MATCH_NOTFREE:
				if (!(comp_len = (unsigned long)strchr(cond->string_value, ':')))
					break;
				comp_len = comp_len-(unsigned long)cond->string_value;
				avail = 0;
				mISDNport = mISDNport_first;
				while(mISDNport)
				{
					if (mISDNport->ifport)
					if (strlen(mISDNport->ifport->interface->name) == comp_len)
					if (!strncasecmp(mISDNport->ifport->interface->name, cond->string_value, comp_len)) 
					if (!mISDNport->ptp || mISDNport->l2link)
					{
						j = 0;
						jj = mISDNport->b_num;
						avail += jj;
						while(j < jj)
						{
							if (mISDNport->b_state[j])
								avail--;
							j++;
						}
					}
					mISDNport = mISDNport->next;
				}
				if (cond->match == MATCH_FREE)
				{
					if (avail >= atoi(cond->string_value + comp_len + 1))
						istrue = 1;
				} else
				{
					if (avail < atoi(cond->string_value + comp_len + 1))
						istrue = 1;
				}
				break;


				case MATCH_DOWN:
				mISDNport = mISDNport_first;
				while(mISDNport)
				{
					if (mISDNport->ifport)
					if (!strcasecmp(mISDNport->ifport->interface->name, cond->string_value)) 
					if (!mISDNport->ptp || mISDNport->l2link) /* break if one is up */
						break;
					mISDNport = mISDNport->next;
				}
				if (!mISDNport) /* all down */
					istrue = 1;
				break;

				case MATCH_UP:
				mISDNport = mISDNport_first;
				while(mISDNport)
				{
					if (mISDNport->ifport)
					if (!strcasecmp(mISDNport->ifport->interface->name, cond->string_value)) 
					if (!mISDNport->ptp || mISDNport->l2link) /* break if one is up */
						break;
					
					mISDNport = mISDNport->next;
				}
				if (mISDNport) /* one link at least */
					istrue = 1;
				break;

				case MATCH_BUSY:
				case MATCH_IDLE:
				any = 0;
				mISDNport = mISDNport_first;
				while(mISDNport)
				{
					if (mISDNport->ifport)
					if (!strcasecmp(mISDNport->ifport->interface->name, cond->string_value)) 
					if (mISDNport->use) /* break if in use */
						break;
					mISDNport = mISDNport->next;
				}
				if (mISDNport && cond->match==MATCH_BUSY)
					istrue = 1;
				if (!mISDNport && cond->match==MATCH_IDLE)
					istrue = 1;
				break;

				case MATCH_REMOTE:
				case MATCH_NOTREMOTE:
				admin = admin_first;
				while(admin)
				{
					if (admin->remote_name[0] && !strcmp(cond->string_value, admin->remote_name))
						break;
					admin = admin->next;
				}
				if (admin && cond->match==MATCH_REMOTE)
					istrue = 1;
				if (!admin && cond->match==MATCH_NOTREMOTE)
					istrue = 1;
				break;

				default:
				PERROR("Software error: MATCH_* %d not parsed in function '%s'", cond->match, __FUNCTION__);
				break;

				match_integer:
				if (cond->value_type == VALUE_TYPE_INTEGER)
				{
					if (integer != cond->integer_value)
						break;
					istrue = 1;
					break;
				}
				if (cond->value_type == VALUE_TYPE_INTEGER_RANGE)
				{
					/* check if negative range (2100 - 700 o'clock) */
					if (cond->integer_value > cond->integer_value_to)
					{
						if (integer>=cond->integer_value && integer<=cond->integer_value_to)
							istrue = 1;
						break;
					}
					/* range is positive */
					if (integer>=cond->integer_value && integer<=cond->integer_value_to)
						istrue = 1;
					break;
				}
				break;

				match_string:
				if (strlen(cond->string_value) != strlen(string))
					break;
				/* fall through */
				match_string_prefix:
				comp_len = strlen(cond->string_value); /* because we must reach value's length */
				/* we must have greater or equal length to values */
				if ((unsigned long)strlen(string) < comp_len)
				{
					/* special case for unfinished dialing */
					if (cond->match == MATCH_DIALING)
					{
						couldbetrue = 1; /* could match */
						comp_len = strlen(string);
					} else
					{
						break;
					}
				}
				/* on single string match */
				if (cond->value_type == VALUE_TYPE_STRING)
				{
					if (!strncmp(string, cond->string_value, comp_len))
					{
						istrue = 1;
						/* must be set for changing 'e_extdialing' */
						if (cond->match == MATCH_DIALING)
							dialing_required = comp_len;
						break;
					}
					break;
				}
				/* on range match */
				if (cond->value_type == VALUE_TYPE_STRING_RANGE)
				{
					/* check if negative range ("55"-"22") */
					if (cond->comp_string > 0)
					{
						if (strncmp(string, cond->string_value, comp_len) >= 0)
						{
							istrue = 1;
							/* must be set for changing 'e_extdialing' */
							if (cond->match == MATCH_DIALING)
								dialing_required = comp_len;
							break;
						}
						if (strncmp(string, cond->string_value_to, comp_len) <= 0)
						{
							/* must be set for changing 'e_extdialing' */
							istrue = 1;
							if (cond->match == MATCH_DIALING)
								dialing_required = comp_len;
							break;
						}
						break;
					}
					/* range is positive */
					if (strncmp(string, cond->string_value, comp_len) < 0)
						break;
					if (strncmp(string, cond->string_value_to, comp_len) > 0)
						break;
					istrue = 1;
					if (cond->match == MATCH_DIALING)
						dialing_required = comp_len;
					break;
				}
				break;
			}

			/* set current condition */
			if (istrue && !couldbetrue)
				condition = 1; /* element matches, so condition matches */
			if (istrue && couldbetrue && !condition)
				condition = 2; /* element could match and other elements don't match, so condition could match */

			/* if not matching or could match */
			if (condition != 1)
			{
				/* if we have more values to check */
				if (cond->value_extension && cond->next)
				{
					cond = cond->next;
					goto checkextension;
				}
				match = condition;
				break;
			}
			
			/* skip exteded values, beacuse we already have one matching value */
			while(cond->value_extension && cond->next)
				cond = cond->next;

			cond = cond->next;
		}
		if (timeout>now_d && match==1) /* the matching rule with timeout in the future */
		if (e_match_timeout<1 || timeout<e_match_timeout) /* first timeout or lower */
		{
			/* set timeout in the furture */
			e_match_timeout = timeout;
			e_match_to_action = rule->action_first;
			e_match_to_extdialing = e_dialinginfo.id + dialing_required;
			match = 0; /* matches in the future */
		}
		if (match == 1)
		{
			/* matching, we return first action */
			action = rule->action_first;
			e_match_timeout = 0; /* no timeout */
			e_match_to_action = NULL;
			e_extdialing = e_dialinginfo.id + dialing_required;
			break;
		}
		if (match == 2)
		{
			/* rule could match if more is dialed */
			couldmatch = 1;
		}
		rule = rule->next;
	}
	return(action);
}

/*
 * parses the current action's parameters and return them
 */
struct route_param *EndpointAppPBX::routeparam(struct route_action *action, unsigned long long id)
{
	struct route_param *param = action->param_first;

	while(param)
	{
		if (param->id == id)
			break;
		param = param->next;
	}
	return(param);
}


/*
 * internal rules that are not defined by route.conf
 */
struct route_action action_password = {
	NULL,
	NULL,
	ACTION_PASSWORD,
	0,
	0,
};

struct route_action action_password_write = {
	NULL,
	NULL,
	ACTION_PASSWORD_WRITE,
	0,
	0,
};

struct route_action action_external = {
	NULL,
	NULL,
	ACTION_EXTERNAL,
	0,
	0,
};

struct route_action action_internal = {
	NULL,
	NULL,
	ACTION_INTERNAL,
	0,
	0,
};

struct route_action action_remote = {
	NULL,
	NULL,
	ACTION_REMOTE,
	0,
	0,
};

struct route_action action_vbox = {
	NULL,
	NULL,
	ACTION_VBOX_RECORD,
	0,
	0,
};

struct route_action action_partyline = {
	NULL,
	NULL,
	ACTION_PARTYLINE,
	0,
	0,
};

struct route_action action_disconnect = {
	NULL,
	NULL,
	ACTION_DISCONNECT,
	0,
	0,
};


