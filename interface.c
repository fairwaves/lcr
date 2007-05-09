/*****************************************************************************\
**                                                                           **
** LCR                                                                       **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** reading interface.conf file and filling structure                         **
**                                                                           **
\*****************************************************************************/ 

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "main.h"

struct interface *interface_first = NULL; /* first interface is current list */
struct interface *interface_newlist = NULL; /* first interface in new list */


/* set default selchannel */
void default_selchannel(struct interface_port *ifport)
{
	struct select_channel *selchannel, **selchannelp;

	/* channel selection for TE-ports */
	if (!ifport->mISDNport->ntmode)
	{
		selchannel = (struct select_channel *)malloc(sizeof(struct select_channel));
		if (!selchannel)
		{
			PERROR("No memory!");
			return;
		}
		memuse++;
		memset(*selchannelp, 0, sizeof(struct select_channel));
		*selchannelp->channel = SEL_CHANNEL_ANY;
		selchannelp = &ifport->selchannel;
		while(*selchannelp)
			selchannelp = &((*selchannelp)->next);
		*selchannelp = selchannel;
		return(0);
	}

	/* channel selection for NT-ports */
	selchannel = (struct select_channel *)malloc(sizeof(struct select_channel));
	if (!selchannel)
	{
		PERROR("No memory!");
		return;
	}
	memuse++;
	memset(*selchannelp, 0, sizeof(struct select_channel));
	*selchannelp->channel = SEL_CHANNEL_FREE;
	selchannelp = &ifport->selchannel;
	while(*selchannelp)
		selchannelp = &((*selchannelp)->next);
	*selchannelp = selchannel;

	/* additional channel selection for multipoint ports */
	if (!ifport->mISDNport->ptp)
	{
		selchannel = (struct select_channel *)malloc(sizeof(struct select_channel));
		if (!selchannel)
		{
			PERROR("No memory!");
			return;
		}
		memuse++;
		memset(*selchannelp, 0, sizeof(struct select_channel));
		*selchannelp->channel = SEL_CHANNEL_NO; // call waiting
		selchannelp = &ifport->selchannel;
		while(*selchannelp)
			selchannelp = &((*selchannelp)->next);
		*selchannelp = selchannel;
	}
}


/* parse string for a positive number */
static int get_number(char *value)
{
	int val = 0;
	char text[10];

	val = atoi(value);
	
	SPRINT(text, "%d", val);

	if (!strcmp(value, text))
		return(val);

	return(-1);
}


/* remove element from buffer
 * and return pointer to next element in buffer */
static char *get_seperated(char *buffer)
{
	while(*buffer)
	{
		if (*buffer==',' || *buffer<=32) /* seperate */
		{
			*buffer++ = '\0';
			while((*buffer>'\0' && *buffer<=32) || *buffer==',')
				buffer++;
			return(buffer);
		}
		buffer++;
	}
	return(buffer);
}

/*
 * parameter processing
 */
static int inter_block(struct interface *interface, char *filename, int line, char *parameter, char *value)
{
	struct interface_port *ifport;

	/* port in chain ? */
	if (!interface->ifport)
	{
		SPRINT(interface_error, "Error in %s (line %d): parameter '%s' expects previous 'port' definition.\n", filename, line, parameter);
		return(-1);
	}
	/* goto end of chain */
	ifport = interface->ifport;
	while(ifport->next)
		ifport = ifport->next;
	/* add value */
	if (value[0])
	{
		SPRINT(interface_error, "Error in %s (line %d): parameter '%s' expects no value.\n", filename, line, parameter);
		return(-1);
	}
	ifport->block = 1;
	return(0);
}
static int inter_extension(struct interface *interface, char *filename, int line, char *parameter, char *value)
{
	if (value[0])
	{
		SPRINT(interface_error, "Error in %s (line %d): parameter '%s' expects no value.\n", filename, line, parameter);
		return(-1);
	}
	interface->extension = 1;
	return(0);
}
static int inter_ptp(struct interface *interface, char *filename, int line, char *parameter, char *value)
{
	struct interface_port *ifport;

	/* port in chain ? */
	if (!interface->ifport)
	{
		SPRINT(interface_error, "Error in %s (line %d): parameter '%s' expects previous 'port' definition.\n", filename, line, parameter);
		return(-1);
	}
	if (interface->ifport->ptmp)
	{
		SPRINT(interface_error, "Error in %s (line %d): parameter '%s' previously ptmp was given.\n", filename, line, parameter);
		return(-1);
	}
	/* goto end of chain */
	ifport = interface->ifport;
	while(ifport->next)
		ifport = ifport->next;
	/* add value */
	if (value[0])
	{
		SPRINT(interface_error, "Error in %s (line %d): parameter '%s' expects no value.\n", filename, line, parameter);
		return(-1);
	}
	ifport->ptp = 1;
	return(0);
}
static int inter_ptmp(struct interface *interface, char *filename, int line, char *parameter, char *value)
{
	struct interface_port *ifport;

	/* port in chain ? */
	if (!interface->ifport)
	{
		SPRINT(interface_error, "Error in %s (line %d): parameter '%s' expects previous 'port' definition.\n", filename, line, parameter);
		return(-1);
	}
	if (interface->ifport->ptp)
	{
		SPRINT(interface_error, "Error in %s (line %d): parameter '%s' previously ptp was given.\n", filename, line, parameter);
		return(-1);
	}
	/* goto end of chain */
	ifport = interface->ifport;
	while(ifport->next)
		ifport = ifport->next;
	/* add value */
	if (value[0])
	{
		SPRINT(interface_error, "Error in %s (line %d): parameter '%s' expects no value.\n", filename, line, parameter);
		return(-1);
	}
	ifport->ptmp = 1;
	return(0);
}
static int inter_tones(struct interface *interface, char *filename, int line, char *parameter, char *value)
{
	if (!strcasecmp(value, "yes"))
	{
		interface->is_tones = IS_YES;
	} else
	if (!strcasecmp(value, "no"))
	{
		interface->is_tones = IS_NO;
	} else
	{
		SPRINT(interface_error, "Error in %s (line %d): parameter '%s' expects value 'yes' or 'no'.\n", filename, line, parameter);
		return(-1);
	}
	return(0);
}
static int inter_earlyb(struct interface *interface, char *filename, int line, char *parameter, char *value)
{
	if (!strcasecmp(value, "yes"))
	{
		interface->is_earlyb = IS_YES;
	} else
	if (!strcasecmp(value, "no"))
	{
		interface->is_earlyb = IS_NO;
	} else
	{
		SPRINT(interface_error, "Error in %s (line %d): parameter '%s' expects value 'yes' or 'no'.\n", filename, line, parameter);
		return(-1);
	}
	return(0);
}
static int inter_hunt(struct interface *interface, char *filename, int line, char *parameter, char *value)
{
	if (!strcasecmp(value, "linear"))
	{
		interface->hunt = HUNT_LINEAR;
	} else
	if (!strcasecmp(value, "roundrobin"))
	{
		interface->hunt = HUNT_ROUNDROBIN;
	} else
	{
		SPRINT(interface_error, "Error in %s (line %d): parameter '%s' expects value 'linear' or 'roundrobin'.\n", filename, line, parameter);
		return(-1);
	}
	return(0);
}
static int inter_port(struct interface *interface, char *filename, int line, char *parameter, char *value)
{
	struct interface_port *ifport, **ifportp;
	struct interface *searchif;
	struct interface_port *searchport;
	int val;

	val = get_number(value);
	if (val == -1)
	{
		SPRINT(interface_error, "Error in %s (line %d): parameter '%s' expects one numeric value.\n", filename, line, parameter);
		return(-1);
	}
	/* check for port already assigned */
	searchif = interface_newlist;
	while(searchif)
	{
		searchport = searchif->ifport;
		while(ifport)
		{
			if (ifport->portnum == val)
			{
				SPRINT(interface_error, "Error in %s (line %d): port '%d' already used above.\n", filename, line, val);
				return(-1);
			}
			ifport = ifport->next;
		}
		searchif = searchif->next;
	}
	/* alloc port substructure */
	ifport = (struct interface_port *)malloc(sizeof(struct interface_port));
	if (!ifport)
	{
		SPRINT(interface_error, "No memory!");
		return(-1);
	}
	memuse++;
	memset(ifport, 0, sizeof(struct interface_port));
	ifport->interface = interface;
	/* set value */
	ifport->portnum = val;
	/* tail port */
	ifportp = &interface->ifport;
	while(*ifportp)
		ifportp = &((*ifportp)->next);
	*ifportp = ifport;
	return(0);
}
static int inter_channel_out(struct interface *interface, char *filename, int line, char *parameter, char *value)
{
	struct interface_port *ifport;
	struct select_channel *selchannel, **selchannelp;
	int val;
	char *p, *el;

	/* port in chain ? */
	if (!interface->ifport)
	{
		SPRINT(interface_error, "Error in %s (line %d): parameter '%s' expects previous 'port' definition.\n", filename, line, parameter);
		return(-1);
	}
	/* goto end of chain */
	ifport = interface->ifport;
	while(ifport->next)
		ifport = ifport->next;
	p = value;
	while(*p)
	{
		el = p;
		p = get_seperated(p);
		if (!strcasecmp(el, "force"))
		{
			ifport->channel_force = 1;
			if (ifport->selchannel)
			{
				SPRINT(interface_error, "Error in %s (line %d): value 'force' may only appear as first element in list.\n", filename, line);
				return(-1);
			}
		} else
		if (!strcasecmp(el, "any"))
		{
			val = SEL_CHANNEL_ANY;
			goto selchannel;
		} else
		if (!strcasecmp(el, "free"))
		{
			val = SEL_CHANNEL_FREE;
			goto selchannel;
		} else
		if (!strcasecmp(el, "no"))
		{
			val = SEL_CHANNEL_NO;
			goto selchannel;
		} else
		{
			val = get_number(el);
			if (val == -1)
			{
				SPRINT(interface_error, "Error in %s (line %d): parameter '%s' expects a comma seperated list of 'force', 'any', 'free', 'no' and any channel number.\n", filename, line, parameter);
				return(-1);
			}

			if (val<1 || val==16 || val>126)
			{
				SPRINT(interface_error, "Error in %s (line %d): channel '%d' out of range.\n", filename, line, val);
				return(-1);
			}
			selchannel:
			/* add to select-channel list */
			selchannel = (struct select_channel *)malloc(sizeof(struct select_channel));
			if (!selchannel)
			{
				SPRINT(interface_error, "No memory!");
				return(-1);
			}
			memuse++;
			memset(selchannel, 0, sizeof(struct select_channel));
			/* set value */
			selchannel->channel = val;
			/* tail port */
			selchannelp = &ifport->selchannel;
			while(*selchannelp)
				selchannelp = &((*selchannelp)->next);
			*selchannelp = selchannel;
		}
	}
	return(0);
}
static int inter_channel_in(struct interface *interface, char *filename, int line, char *parameter, char *value)
{
	struct interface_port *ifport;
	struct select_channel *selchannel, **selchannelp;
	int val;
	char *p, *el;

	/* port in chain ? */
	if (!interface->ifport)
	{
		SPRINT(interface_error, "Error in %s (line %d): parameter '%s' expects previous 'port' definition.\n", filename, line, parameter);
		return(-1);
	}
	/* goto end of chain */
	ifport = interface->ifport;
	while(ifport->next)
		ifport = ifport->next;
	p = value;
	while(*p)
	{
		el = p;
		p = get_seperated(p);
		if (ifport->in_select) if (ifport->in_select->channel == SEL_CHANNEL_FREE)
		{
			SPRINT(interface_error, "Error in %s (line %d): parameter '%s' has values behind 'free' keyword. They has no effect.\n", filename, line, parameter);
				return(-1);
		}
		if (!strcasecmp(el, "free"))
		{
			val = SEL_CHANNEL_FREE;
			goto selchannel;
		} else
		{
			val = get_number(el);
			if (val == -1)
			{
				SPRINT(interface_error, "Error in %s (line %d): parameter '%s' expects a comma seperated list of channel numbers and 'free'.\n", filename, line, parameter);
				return(-1);
			}

			if (val<1 || val==16 || val>126)
			{
				SPRINT(interface_error, "Error in %s (line %d): channel '%d' out of range.\n", filename, line, val);
				return(-1);
			}
			selchannel:
			/* add to select-channel list */
			selchannel = (struct select_channel *)malloc(sizeof(struct select_channel));
			if (!selchannel)
			{
				SPRINT(interface_error, "No memory!");
				return(-1);
			}
			memuse++;
			memset(selchannel, 0, sizeof(struct select_channel));
			/* set value */
			selchannel->channel = val;
			/* tail port */
			selchannelp = &ifport->in_select;
			while(*selchannelp)
				selchannelp = &((*selchannelp)->next);
			*selchannelp = selchannel;
		}
	}
	return(0);
}
static int inter_msn(struct interface *interface, char *filename, int line, char *parameter, char *value)
{
	struct interface_msn *ifmsn, **ifmsnp;
	char *p, *el;

	if (!value[0])
	{
		SPRINT(interface_error, "Error in %s (line %d): parameter '%s' expects one MSN number or a list.\n", filename, line, parameter);
		return(-1);
	}
	if (interface->ifscreen_in)
	{
		SPRINT(interface_error, "Error in %s (line %d): parameter '%s' not allowed with 'screen_in' parameter.\n", filename, line, parameter);
		return(-1);
	}

	/* process list */
	p = value;
	while(*p)
	{
		el = p;
		p = get_seperated(p);
		/* add MSN to list */
		ifmsn = (struct interface_msn *)malloc(sizeof(struct interface_msn));
		if (!ifmsn)
		{
			SPRINT(interface_error, "No memory!");
			return(-1);
		}
		memuse++;
		memset(ifmsn, 0, sizeof(struct interface_msn));
		/* set value */
		SCPY(ifmsn->msn, el);
		/* tail port */
		ifmsnp = &interface->ifmsn;
		while(*ifmsnp)
			ifmsnp = &((*ifmsnp)->next);
		*ifmsnp = ifmsn;
	}
	return(0);
}
static int inter_screen(struct interface_screen **ifscreenp, struct interface *interface, char *filename, int line, char *parameter, char *value)
{
	struct interface_screen *ifscreen;
	char *p, *el;

	if (!value[0])
	{
		SPRINT(interface_error, "Error in %s (line %d): parameter '%s' expects old caller ID and new caller ID.\n", filename, line, parameter);
		return(-1);
	}
	p = value;
	el = p;
	p = get_seperated(p);
	/* add screen entry to list*/
	ifscreen = (struct interface_screen *)malloc(sizeof(struct interface_screen));
	if (!ifscreen)
	{
		SPRINT(interface_error, "No memory!");
		return(-1);
	}
	memuse++;
	memset(ifscreen, 0, sizeof(struct interface_screen));
#warning handle unchanged as unchanged!!
	ifscreen->result_type = -1; /* unchanged */
	ifscreen->result_present = -1; /* unchanged */
	/* tail port */
	while(*ifscreenp)
		ifscreenp = &((*ifscreenp)->next);
	*ifscreenp = ifscreen;
	/* get match */
	while(*p)
	{
		el = p;
		p = get_seperated(p);
		if (!strcasecmp(el, "unknown"))
		{
			if (ifscreen->match_type != -1)
			{
				typeerror:
				SPRINT(interface_error, "Error in %s (line %d): number type already set earlier.\n", filename, line, parameter);
				return(-1);
			}
			ifscreen->match_type = INFO_NTYPE_UNKNOWN;
		} else
		if (!strcasecmp(el, "subscriber"))
		{
			if (ifscreen->match_type != -1)
				goto typeerror;
			ifscreen->match_type = INFO_NTYPE_SUBSCRIBER;
		} else
		if (!strcasecmp(el, "national"))
		{
			if (ifscreen->match_type != -1)
				goto typeerror;
			ifscreen->match_type = INFO_NTYPE_NATIONAL;
		} else
		if (!strcasecmp(el, "international"))
		{
			if (ifscreen->match_type != -1)
				goto typeerror;
			ifscreen->match_type = INFO_NTYPE_INTERNATIONAL;
		} else
		if (!strcasecmp(el, "allowed"))
		{
			if (ifscreen->match_present != -1)
			{
				presenterror:
				SPRINT(interface_error, "Error in %s (line %d): presentation type already set earlier.\n", filename, line, parameter);
				return(-1);
			}
			ifscreen->match_present = INFO_PRESENT_ALLOWED;
		} else
		if (!strcasecmp(el, "restricted"))
		{
			if (ifscreen->match_present != -1)
				goto presenterror;
			ifscreen->match_present = INFO_PRESENT_RESTRICTED;
		} else {
			SCPY(ifscreen->match, el);
			break;
		}
	}
	if (ifscreen->match[0] == '\0')
	{
		SPRINT(interface_error, "Error in %s (line %d): parameter '%s' expects old caller ID.\n", filename, line, parameter);
		return(-1);
	}
	/* get result */
	while(*p)
	{
		el = p;
		p = get_seperated(p);
		if (!strcasecmp(el, "unknown"))
		{
			if (ifscreen->result_type != -1)
				goto typeerror;
			ifscreen->result_type = INFO_NTYPE_UNKNOWN;
		} else
		if (!strcasecmp(el, "subscriber"))
		{
			if (ifscreen->result_type != -1)
				goto typeerror;
			ifscreen->result_type = INFO_NTYPE_SUBSCRIBER;
		} else
		if (!strcasecmp(el, "national"))
		{
			if (ifscreen->result_type != -1)
				goto typeerror;
			ifscreen->result_type = INFO_NTYPE_NATIONAL;
		} else
		if (!strcasecmp(el, "international"))
		{
			if (ifscreen->result_type != -1)
				goto typeerror;
			ifscreen->result_type = INFO_NTYPE_INTERNATIONAL;
		} else
		if (!strcasecmp(el, "allowed"))
		{
			if (ifscreen->result_present != -1)
				goto presenterror;
			ifscreen->result_present = INFO_PRESENT_ALLOWED;
		} else
		if (!strcasecmp(el, "restricted"))
		{
			if (ifscreen->result_present != -1)
				goto presenterror;
			ifscreen->result_present = INFO_PRESENT_RESTRICTED;
		} else {
			SCPY(ifscreen->result, el);
			break;
		}
	}
	if (ifscreen->result[0] == '\0')
	{
		SPRINT(interface_error, "Error in %s (line %d): parameter '%s' expects new caller ID.\n", filename, line, parameter);
		return(-1);
	}
	return(0);
}
static int inter_screen_in(struct interface *interface, char *filename, int line, char *parameter, char *value)
{
	if (interface->ifmsn)
	{
		SPRINT(interface_error, "Error in %s (line %d): parameter '%s' not allowed with 'msn' parameter.\n", filename, line, parameter);
		return(-1);
	}

	return(inter_screen(&interface->ifscreen_in, interface, filename, line, parameter, value));
}
static int inter_screen_out(struct interface *interface, char *filename, int line, char *parameter, char *value)
{
	return(inter_screen(&interface->ifscreen_out, interface, filename, line, parameter, value));
}
static int inter_filter(struct interface *interface, char *filename, int line, char *parameter, char *value)
{
#warning filter to be done
	return(0);
}


/*
 * structure of parameters
 */
struct interface_param interface_param[] = {
	{ "extension", &inter_extension, "",
	"If keyword is given, calls to interface are handled as internal extensions."},
	{"tones", &inter_tones, "yes | no",
	"Interface generates tones during call setup and release, or not.\nBy default only NT-mode interfaces generate tones."},

	{"earlyb", &inter_earlyb, "yes | no",
	"Interface receives and bridges tones during call setup and release, or not.\nBy default only TE-mode interfaces receive tones."},

	{"hunt", &inter_hunt, "linear | roundrobin",
	"Select the algorithm for selecting port with free channel."},

	{"port", &inter_port, "<number>",
	"Give exactly one port for this interface.\nTo give multiple ports, add more lines with port parameters."},

	{"block", &inter_block, "",
	"If keyword is given, calls on this interface are blocked.\n"
	"This parameter must follow a 'port' parameter."},

	{"ptp", &inter_ptp, "",
	"The given port above is opened as point-to-point.\n"
	"This is required on NT-mode ports that are multipoint by default.\n"
	"This parameter must follow a 'port' parameter."},

	{"ptmp", &inter_ptmp, "",
	"The given port above is opened as point-to-multipoint.\n"
	"This is required on PRI NT-mode ports that are point-to-point by default.\n"
	"This parameter must follow a 'port' parameter."},

	{"channel_out", &inter_channel_out, "[force,][<number>][,...][,free][,any][,no]",
	"Channel selection list for all outgoing calls to the interface.\n"
	"A free channels is searched in order of appearance.\n"
	"This parameter must follow a 'port' parameter.\n"
	" force - Forces the selected port with no acceptable alternative (see DSS1).\n"
	" <number>[,...] - List of channels to search.\n"
	" free - Select any free channel\n"
	" any - On outgoing calls, signal 'any channel acceptable'. (see DSS1)\n"
	" no - Signal 'no channel available' aka 'call waiting'. (see DSS1)"},

	{"channel_in", &inter_channel_in, "[force,][<number>][,...][,free][,any][,no]",
	"Channel selection list for all incomming calls from the interface.\n"
	"A free channels is accepted if in the list.\n"
	"If no channel was requested, the first free channel found is selected.\n"
	"This parameter must follow a 'port' parameter.\n"
	" <number>[,...] - List of channels to accept.\n"
	" free - Accept any free channel\n"

	{"msn", &inter_msn, "<default MSN>,[<additional MSN>[,...]]",
	"Incomming caller ID is checked against given MSN numbers.\n"
	"If the caller ID is not found in this list, it is overwritten by the first MSN"},

	{"screen-in", &inter_screen_in, "[options] <old caller ID>[%%] [options] <new caller ID>[%%]",
	"Adds an entry for incomming calls to the caller ID screen list.\n"
	"If the given 'old caller ID' matches, it is replaced by the 'new caller ID'\n"
	"If '%%' is given after old caller ID, it matches even if caller ID has\n"
	"additional digits.\n"
	"If '%%' is given after mew caller ID, additinal digits of the 'old caller ID'\n"
	"are added.\n"
	"Options can be:\n"
	" unknown | subsciber | national | international - Change caller ID type.\n"
	" present | restrict - Change presentation of caller ID."},
		
	{"screen-out", &inter_screen_out, "<old caller ID> <new caller ID> [options]",
	"Adds an entry for outgoing calls to the caller ID screen list.\n"
	"See 'screen-in' for help."},

	{"filter", &inter_filter, "<filter> [parameters]",
	"Adds/appends a filter. Filters are ordered in transmit direction.\n"
	"gain <tx-volume> <rx-volume> - Changes volume (-8 .. 8)\n"
	"blowfish <key> - Adds encryption. Key must be 4-56 bytes (8-112 hex characters."},

	{NULL, NULL, NULL, NULL}
};

/* read interfaces
 *
 * read settings from interface.conf
 */
char interface_error[256];
struct interface *read_interfaces(void)
{
	FILE			*fp = NULL;
	char			filename[128];
	char			*p;
	unsigned int		line, i;
	char			buffer[256];
	struct interface	*interface = NULL, /* in case no interface */
				**interfacep = &interface_newlist;
	char			parameter[128];
	char			value[256];
	int			expecting = 1; /* expecting new interface */
	struct interface_param	*ifparam;

	if (interface_newlist != NULL)
	{
		PERROR("software error, list is not empty.\n");
		exit(-1);
	}
	interface_error[0] = '\0';
	SPRINT(filename, "%s/interface.conf", INSTALL_DATA);

	if (!(fp = fopen(filename,"r")))
	{
		SPRINT(interface_error, "Cannot open '%s'\n", filename);
		goto error;
	}

	line=0;
	while((fgets(buffer,sizeof(buffer),fp)))
	{
		buffer[sizeof(buffer)-1]=0;
		if (buffer[0]) buffer[strlen(buffer)-1]=0;
		p=buffer;
		line++;

		while(*p <= 32) /* skip spaces */
		{
			if (*p == 0)
				break;
			p++;
		}
		if (*p==0 || *p=='#') /* ignore comments and empty line */
			continue;

		parameter[0]=0;
		value[0]=0;
		i=0; /* read parameter */
		while(*p > 32)
		{
			if (i+1 >= sizeof(parameter))
			{
				SPRINT(interface_error, "Error in %s (line %d): parameter name too long.\n",filename,line);
				goto error;
			}
			parameter[i+1] = '\0';
			parameter[i++] = *p++;
		}

		while(*p <= 32) /* skip spaces */
		{
			if (*p == 0)
				break;
			p++;
		}

		if (*p!=0 && *p!='#') /* missing name */
		{
			i=0; /* read until end */
			while(*p!=0 && *p!='#')
			{
				if (i+1 >= sizeof(value))
				{
					SPRINT(interface_error, "Error in %s (line %d): value too long.\n", filename, line);
					goto error;
				}
				value[i+1] = '\0';
				value[i++] = *p++;
			}

			/* remove trailing spaces from value */
			while(i)
			{
				if (value[i-1]==0 || value[i-1]>32)
					break;
				value[i-1] = '\0';
				i--;
			}
		}

		/* check for interface name as first statement */
		if (expecting && parameter[0]!='[')
		{
			SPRINT(interface_error, "Error in %s (line %d): expecting interface name inside [ and ], but got: '%s'.\n", filename, line, parameter);
			goto error;
		}
		expecting = 0;

		/* check for new interface */
		if (parameter[0] == '[')
		{
			if (parameter[strlen(parameter)-1] != ']')
			{
				SPRINT(interface_error, "Error in %s (line %d): expecting interface name inside [ and ], but got: '%s'.\n", filename, line, parameter);
				goto error;
			}
			parameter[strlen(parameter)-1] = '\0';

			/* check if interface name already exists */
			interface = interface_first;
			while(interface)
			{
				if (!strcasecmp(interface->name, parameter+1))
				{
					SPRINT(interface_error, "Error in %s (line %d): interface name '%s' already defined above.\n", filename, line, parameter+1);
					goto error;
				}
				interface = interface->next;
			}

			/* append interface to new list */
			interface = (struct interface *)malloc(sizeof(struct interface));
			if (!interface)
			{
				SPRINT(interface_error, "No memory!");
				goto error;
			}
			memuse++;
			memset(interface, 0, sizeof(struct interface));

			/* name interface */
			SCPY(interface->name, parameter+1);

			/* attach */
			*interfacep = interface;
			interfacep = &interface->next;

			continue;
		}

		ifparam = interface_param;
		while(ifparam->name)
		{
			if (!strcasecmp(parameter, ifparam->name))
			{
				if (ifparam->func(interface, filename, line, parameter, value))
					goto error;
				continue;
			}
			ifparam++;
		}

		SPRINT(interface_error, "Error in %s (line %d): unknown parameter: '%s'.\n", filename, line, parameter);
		goto error;
	}

	if (fp) fclose(fp);
	return(interface_newlist);
error:
	PERROR_RUNTIME("%s", interface_error);
	if (fp) fclose(fp);
	free_interfaces(interface_newlist);
	interface_newlist = NULL;
	return(NULL);
}


/*
 * freeing chain of interfaces
 */
void free_interfaces(struct interface *interface)
{
	void *temp;
	struct interface_port *ifport;
	struct select_channel *selchannel;
	struct interface_msn *ifmsn;
	struct interface_screen *ifscreen;
	struct interface_filter *iffilter;

	while(interface)
	{
		ifport = interface->ifport;
		while(ifport)
		{
			selchannel = ifport->in_channel;
			while(selchannel)
			{
				temp = selchannel;
				selchannel = selchannel->next;
				memset(temp, 0, sizeof(struct select_channel));
				free(temp);
				memuse--;
			}
			selchannel = ifport->out_channel;
			while(selchannel)
			{
				temp = selchannel;
				selchannel = selchannel->next;
				memset(temp, 0, sizeof(struct select_channel));
				free(temp);
				memuse--;
			}
			temp = ifport;
			ifport = ifport->next;
			memset(temp, 0, sizeof(struct interface_port));
			free(temp);
			memuse--;
		}
		ifmsn = interface->ifmsn;
		while(ifmsn)
		{
			temp = ifmsn;
			ifmsn = ifmsn->next;
			memset(temp, 0, sizeof(struct interface_msn));
			free(temp);
			memuse--;
		}
		ifscreen = interface->ifscreen_in;
		while(ifscreen)
		{
			temp = ifscreen;
			ifscreen = ifscreen->next;
			memset(temp, 0, sizeof(struct interface_screen));
			free(temp);
			memuse--;
		}
		ifscreen = interface->ifscreen_out;
		while(ifscreen)
		{
			temp = ifscreen;
			ifscreen = ifscreen->next;
			memset(temp, 0, sizeof(struct interface_screen));
			free(temp);
			memuse--;
		}
		iffilter = interface->iffilter;
		while(iffilter)
		{
			temp = iffilter;
			iffilter = iffilter->next;
			memset(temp, 0, sizeof(struct interface_filter));
			free(temp);
			memuse--;
		}
		temp = interface;
		interface = interface->next;
		memset(temp, 0, sizeof(struct interface));
		free(temp);
		memuse--;
	}
}


/*
 * all links between mISDNport and interface are made
 * unused mISDNports are closed, new mISDNports are opened
 * also set default select_channel lists
 */
void relink_interfaces(void)
{
	struct mISDNport *mISDNport;
	struct interface *interface;
	struct interface_port *ifport;

	/* unlink all mISDNports */
	mISDNport = mISDNport_first;
	while(mISDNport)
	{
		mISDNport->ifport = NULL;
		mISDNport = mISDNport->next;
	}

	/* relink existing mISDNports */
	interface = interface_newlist;
	while(interface)
	{
		ifport = interface->ifport;
		while(ifport)
		{
			mISDNport = mISDNport_first;
			while(mISDNport)
			{
				if (mISDNport->portnum == ifport->portnum)
				{
					ifport->mISDNport = mISDNport;
					mISDNport->ifport = ifport;
				}
				mISDNport = mISDNport->next;
			}
			ifport = ifport->next;
		}
		interface = interface->next;
	}

	/* close unused mISDNports */
	closeagain:
	mISDNport = mISDNport_first;
	while(mISDNport)
	{
		if (mISDNport->ifport == NULL)
		{
			PDEBUG(DEBUG_ISDN, "Port %d is not used anymore and will be closed\n", mISDNport->portnum);
			/* remove all port objects and destroy port */
			mISDNport_close(mISDNport);
			goto closeagain;
		}
		mISDNport = mISDNport->next;
	}

	/* open and link new mISDNports */
	interface = interface_newlist;
	while(interface)
	{
		ifport = interface->ifport;
		while(ifport)
		{
			if (!ifport->mISDNport)
			{
				/* open new port */
				mISDNport = mISDNport_open(ifport->portnum, ifport->ptp, ifport->ptmp);
				if (mISDNport)
				{
					ifport->mISDNport = mISDNport;
					mISDNport->ifport = ifport;
				}
			}
			if (ifport->mISDNport)
			{
				/* default channel selection list */
				if (!ifport->selchannel)
					default_selchannel(ifport);
				/* default is_tones */
				if (ifport->interface->is_tones)
					ifport->mISDNport->is_tones = (ifport->interface->is_tones==IS_YES);
				else
					ifport->mISDNport->is_tones = (ifport->mISDNport->ntmode)?1:0;
				/* default is_earlyb */
				if (ifport->interface->is_earlyb)
					ifport->mISDNport->is_earlyb = (ifport->interface->is_earlyb==IS_YES);
				else
					ifport->mISDNport->is_earlyb = (ifport->mISDNport->ntmode)?0:1;
			}
		}
		interface = interface->next;
	}

}

/*
 * give summary of interface syntax
 */
void doc_interface(void)
{
	struct interface_param *ifparam;
	
	printf("Syntax overview\n");
	printf("---------------\n\n");

	printf("[<name>]\n");
	ifparam = interface_param;
	while(ifparam->name)
	{
		printf("%s %s\n", ifparam->name, ifparam->usage);
		ifparam++;
	}

	ifparam = interface_param;
	while(ifparam->name)
	{
		printf("\nParameter: %s %s\n", ifparam->name, ifparam->usage);
		printf("%s\n", ifparam->help);
		ifparam++;
	}
}


