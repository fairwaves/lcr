/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** trace functions                                                           **
**                                                                           **
\*****************************************************************************/ 

#include "main.h"

struct trace trace;
char trace_string[MAX_TRACE_ELEMENTS * 100 + 400];

static const char *spaces[11] = {
	"          ",
	"         ",
	"        ",
	"       ",
	"      ",
	"     ",
	"    ",
	"   ",
	"  ",
	" ",
	"",
};

/*
 * initializes a new trace
 * all values will be reset
 */
void _start_trace(const char *__file, int __line, int port, struct interface *interface, const char *caller, const char *dialing, int direction, int category, int serial, const char *name)
{
	if (trace.name[0])
		PERROR("trace already started (name=%s) in file %s line %d\n", trace.name, __file, __line);
	memset(&trace, 0, sizeof(struct trace));
	trace.port = port;
	if (interface)
		SCPY(trace.interface, interface->name);
	if (caller) if (caller[0])
		SCPY(trace.caller, caller);
	if (dialing) if (dialing[0])
		SCPY(trace.dialing, dialing);
	trace.direction = direction;
	trace.category = category;
	trace.serial = serial;
	if (name) if (name[0])
		SCPY(trace.name, name);
	if (!trace.name[0])
		SCPY(trace.name, "<unknown>");
	trace.sec = now_tv.tv_sec;
	trace.usec = now_tv.tv_usec;
}


/*
 * adds a new element to the trace
 * if subelement is given, element will also contain a subelement
 * if multiple subelements belong to same element, name must be equal for all subelements
 */
void _add_trace(const char *__file, int __line, const char *name, const char *sub, const char *fmt, ...)
{
	va_list args;

	if (!trace.name[0])
		PERROR("trace not started in file %s line %d\n", __file, __line);
	
	/* check for required name value */
	if (!name)
		goto nostring;
	if (!name[0])
	{
		nostring:
		PERROR("trace with name=%s gets element with no string\n", trace.name);
		return;
	}
	
	/* write name, sub and value */
	SCPY(trace.element[trace.elements].name, name);
	if (sub) if (sub[0])
		SCPY(trace.element[trace.elements].sub, sub);
	if (fmt) if (fmt[0])
	{
		va_start(args, fmt);
		VUNPRINT(trace.element[trace.elements].value, sizeof(trace.element[trace.elements].value)-1, fmt, args);
		va_end(args);
	}

	/* increment elements */
	trace.elements++;
}


/*
 * prints trace to socket or log
 * detail: 1 = brief, 2=short, 3=long
 */
static char *print_trace(int detail, int port, char *interface, char *caller, char *dialing, int category)
{
	char buffer[256];
	time_t ti = trace.sec;
	struct tm *tm;
	struct mISDNport *mISDNport;
	int i;

	trace_string[0] = '\0'; // always clear string

	if (detail < 1)
		return(NULL);

	/* filter trace */
	if (port && trace.port)
		if (port != trace.port) return(NULL);
	if (interface) if (interface[0] && trace.interface[0])
		if (!!strcasecmp(interface, trace.interface)) return(NULL);
	if (caller) if (caller[0] && trace.caller[0])
		if (!!strncasecmp(caller, trace.caller, strlen(trace.caller))) return(NULL);
	if (dialing) if (dialing[0] && trace.dialing[0])
		if (!!strncasecmp(dialing, trace.dialing, strlen(trace.dialing))) return(NULL);
	if (category && trace.category)
		if (!(category & trace.category)) return(NULL);

	/* head */
	if (detail >= 3)
	{
		SCAT(trace_string, "------------------------------------------------------------------------------\n");
		/* "Port: 1 (BRI PTMP TE)" */
		if (trace.port)
		{
			mISDNport = mISDNport_first;
			while(mISDNport)
			{
				if (mISDNport->portnum == trace.port)
					break;
				mISDNport = mISDNport->next;
			}
			if (mISDNport)
			{
				SPRINT(buffer, "Port: %d (%s %s %s)", trace.port, (mISDNport->pri)?"PRI":"BRI", (mISDNport->ptp)?"PTP":"PTMP", (mISDNport->ntmode)?"NT":"TE");
				/* copy interface, if we have a port */
				if (mISDNport->ifport) if (mISDNport->ifport->interface)
				SCPY(trace.interface, mISDNport->ifport->interface->name);
			} else
				SPRINT(buffer, "Port: %d (does not exist)\n", trace.port);
			SCAT(trace_string, buffer);
		} else
			SCAT(trace_string, "Port: ---");

		if (trace.interface[0])
		{
			/* "  Interface: 'Ext'" */
			SPRINT(buffer, "  Interface: '%s'", trace.interface);
			SCAT(trace_string, buffer);
		} else
			SCAT(trace_string, "  Interface: ---");
			
		if (trace.caller[0])
		{
			/* "  Caller: '021256493'" */
			SPRINT(buffer, "  Caller: '%s'\n", trace.caller);
			SCAT(trace_string, buffer);
		} else
			SCAT(trace_string, "  Caller: ---\n");

		/* "Time: 25.08.73 05:14:39.282" */
		tm = localtime(&ti);
		SPRINT(buffer, "Time: %02d.%02d.%02d %02d:%02d:%02d.%03d", tm->tm_mday, tm->tm_mon+1, tm->tm_year%100, tm->tm_hour, tm->tm_min, tm->tm_sec, trace.usec/1000);
		SCAT(trace_string, buffer);

		if (trace.direction)
		{
			/* "  Direction: out" */
			SPRINT(buffer, "  Direction: %s", (trace.direction==DIRECTION_OUT)?"OUT":"IN");
			SCAT(trace_string, buffer);
		} else
			SCAT(trace_string, "  Direction: ---");

		if (trace.dialing[0])
		{
			/* "  Dialing: '57077'" */
			SPRINT(buffer, "  Dialing: '%s'\n", trace.dialing);
			SCAT(trace_string, buffer);
		} else
			SCAT(trace_string, "  Dialing: ---\n");

		SCAT(trace_string, "------------------------------------------------------------------------------\n");
	}

	if (detail < 3)
	{
		tm = localtime(&ti);
		SPRINT(buffer, "%02d.%02d.%02d %02d:%02d:%02d.%03d ", tm->tm_mday, tm->tm_mon+1, tm->tm_year%100, tm->tm_hour, tm->tm_min, tm->tm_sec, trace.usec/1000);
		SCAT(trace_string, buffer);
	}

	/* "CH(45): CC_SETUP (net->user)" */
	switch (trace.category)
	{	case CATEGORY_CH:
		SCAT(trace_string, "CH");
		break;

		case CATEGORY_EP:
		SCAT(trace_string, "EP");
		break;

		default:
		SCAT(trace_string, "--");
	}
	if (trace.serial)
		SPRINT(buffer, "(%lu): %s", trace.serial, trace.name[0]?trace.name:"<unknown>");
	else
		SPRINT(buffer, ": %s", trace.name[0]?trace.name:"<unknown>");
	SCAT(trace_string, buffer);

	/* elements */
	switch(detail)
	{
		case 1: /* brief */
		if (trace.port)
		{
			SPRINT(buffer, "  port %d", trace.port);
			SCAT(trace_string, buffer);
		}
		i = 0;
		while(i < trace.elements)
		{
			SPRINT(buffer, "  %s", trace.element[i].name);
			if (i) if (!strcmp(trace.element[i].name, trace.element[i-1].name))
				buffer[0] = '\0';
			SCAT(trace_string, buffer);
			if (trace.element[i].sub[0])
				SPRINT(buffer, " %s=", trace.element[i].sub);
			else
				SPRINT(buffer, " ");
			SCAT(trace_string, buffer);
			if (strchr(trace.element[i].value, ' '))
				SPRINT(buffer, "'%s'", trace.element[i].value);
			else
				SPRINT(buffer, "%s", trace.element[i].value);
			SCAT(trace_string, buffer);
			i++;
		}
		SCAT(trace_string, "\n");
		break;

		case 2: /* short */
		case 3: /* long */
		SCAT(trace_string, "\n");
		i = 0;
		while(i < trace.elements)
		{
			SPRINT(buffer, " %s%s", trace.element[i].name, spaces[strlen(trace.element[i].name)]);
			if (i) if (!strcmp(trace.element[i].name, trace.element[i-1].name))
				SPRINT(buffer, "           ");
			SCAT(trace_string, buffer);
			if (trace.element[i].sub[0])
				SPRINT(buffer, " : %s%s = ", trace.element[i].sub, spaces[strlen(trace.element[i].sub)]);
			else
				SPRINT(buffer, " :              ");
			SCAT(trace_string, buffer);
			if (strchr(trace.element[i].value, ' '))
				SPRINT(buffer, "'%s'\n", trace.element[i].value);
			else
				SPRINT(buffer, "%s\n", trace.element[i].value);
			SCAT(trace_string, buffer);
			i++;
		}
		break;
	}

	/* end */
	if (detail >= 3)
		SCAT(trace_string, "\n");
	return(trace_string);
}


/*
 * trace ends
 * this function will put the trace to sockets and logfile, if requested
 */
void _end_trace(const char *__file, int __line)
{
	char *string;
	FILE *fp;
	struct admin_list	*admin;
	struct admin_queue	*response, **responsep;	/* response pointer */

	if (!trace.name[0])
		PERROR("trace not started in file %s line %d\n", __file, __line);
	
	if (options.deb || options.log[0])
	{
		string = print_trace(1, 0, NULL, NULL, NULL, 0);
		if (string)
		{
			/* process debug */
			if (options.deb)
				debug(NULL, 0, "trace", string);
			/* process log */
			if (options.log[0])
			{
				fp = fopen(options.log, "a");
				if (fp)
				{
					fwrite(string, strlen(string), 1, fp);
					fclose(fp);
				}
			}
		}
	}

	/* process admin */
	admin = admin_first;
	while(admin)
	{
		if (admin->trace.detail)
		{
			string = print_trace(admin->trace.detail, admin->trace.port, admin->trace.interface, admin->trace.caller, admin->trace.dialing, admin->trace.category);
			if (string)
			{
				/* seek to end of response list */
				response = admin->response;
				responsep = &admin->response;
				while(response)
				{
					responsep = &response->next;
					response = response->next;
				}

				/* create state response */
				response = (struct admin_queue *)MALLOC(sizeof(struct admin_queue)+sizeof(admin_message));
				memuse++;
				response->num = 1;
				/* message */
				response->am[0].message = ADMIN_TRACE_RESPONSE;
				SCPY(response->am[0].u.trace_rsp.text, string);

				/* attach to response chain */
				*responsep = response;
				responsep = &response->next;
			}
		}
		admin = admin->next;
	}
//	fwrite(string, strlen(string), 1, fp);

	memset(&trace, 0, sizeof(struct trace));
}




