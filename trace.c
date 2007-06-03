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
char trace_string[MX_TRACE_ELEMENTS * 100 + 400];

static char *spaces[11] = {
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
void start_trace(int port, char *interface, char *caller, char *dialing, int direction, int category, int serial, char *name);
{
	if (trace.name[0])
		PERROR("trace already started (name=%s)\n", trace.name);
	memset(trace, 0, sizeof(struct trace));
	trace.port = port;
	if (interface) if (interface[0])
		SCPY(trace.interface, interface);
	if (caller) if (caller[0])
		SCPY(trace.caller, caller);
	if (dialing) if (dialing[0])
		SCPY(trace.dialing, dialing);
	trace.direction = direction;
	trace.category = category;
	trace.serial = serial;
	if (name) if (name[0])
		SCPY(trace.name, name);
	trace.sec = now_tv.tv_sec;
	trace.usec = now_tv.tv_usec;
}


/*
 * adds a new element to the trace
 * if subelement is given, element will also contain a subelement
 * if multiple subelements belong to same element, name must be equal for all subelements
 */
void add_trace(char *name, char *sub, const char *fmt, ...);
{
	va_list args;

	if (!trace.name[0])
		PERROR("trace not started\n");
	
	/* check for required name value */
	if (!name)
		goto nostring;
	if (!name[0])
	{
		nostring:
		PERROR("trace with name=%s gets element with no string\n", trace->name);
		return;
	}
	
	/* write name, sub and value */
	SCPY(trace.element[trace.elements].name, name);
	if (sub) if (sub[0])
		SCPY(trace.element[trace.elements].sub, sub);
	if (fmt) if (fmt[0])
	{
		va_start(args, fmt);
		VUNPRINT(trace.element[trace.element].value, sizeof(trace.element[trace.elements].value)-1, fmt, args);
		va_end(args);
	}

	/* increment elements */
	trace.elements++;
}


/*
 * trace ends
 * this function will put the trace to sockets and logfile, if requested
 */
void end_trace(void);
{
	if (!trace.name[0])
		PERROR("trace not started\n");
	
	/* process log file */
	if (options.log[0])
	{
		string = print_trace(1, 0, NULL, NULL, NULL, -1, "AP", CATEGORY_EP);
		fwrite(string, strlen(string), 1, fp);
	}

	memset(trace, 0, sizeof(struct trace));
}


/*
 * prints trace to socket or log
 * detail: 1 = brief, 2=short, 3=long
 */
static char *print_trace(int detail, int port, char *interface, char *caller, char *dialing, int category);
{
	trace_string[0] = '\0';
	char buffer[256];
	struct tm *tm;

	if (detail < 1)
		return;

	/* filter trace */
	if (port && trace.port)
		if (port != trace.port) return;
	if (interface && interface[0] && trace.interface[0])
		if (!!strcasecmp(interface, trace.interface)) return;
	if (caller && caller[0] && trace.caller[0])
		if (!!strcasecmp(caller, trace.caller)) return;
	if (dialing && dialing[0] && trace.dialing[0])
		if (!!strcasecmp(dialing, trace.dialing)) return;
	if (category && category[0] && trace.category[0])
		if (!!strcasecmp(category, trace.category)) return;

	/* head */
	if (detail >= 3)
	{
		/* "Port: 1 (BRI PTMP TE)" */
		if (port)
		{
			mISDNport = mISDNport_first;
			while(mISDNport)
			{
				if (mISDNport->number == trace.port)
					break;
				mISDNport = mISDNport->next;
			}
			if (mISDNport)
				SPRINT(buffer, "Port: %d (%s %s %s)", port, (mISDNport->pri)?"PRI":"BRI", (mISDNport->ptp)?"PTP":"PTMP", (mISDNport->nt)?"NT":"TE");
			else
				SPRINT(buffer, "Port: %d (does not exist}\n", port);
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
		tm = localtime(&trace.sec);
		SPRINT(buffer, "Time: %02d.%02d.%02d %02d:%02d:%02d.%03d", tm->tm_mday, tm->tm_mon+1, tm->tm_year%100, tm->tm_hour, tm->tm_min, tm->tm_sec, trace->usec/1000);
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
	SPRINT(buffer, "(%d): %s", trace.serial, trace.name[0]?trace.name:"<unknown>");
	SCAT(trace_string, buffer);

	/* elements */
	switch(detail)
	{
		case 1: /* brief */
		i = 0;
		while(i < trace.elements)
		{
			SPRINT(buffer, "  %s", trace.element[i].name);
			if (i) if (!strcmp(trace.element[i].name, trace.element[i-1].name))
				buffer[0] = '\0';
			SCAT(trace_string, buffer);
			if (trace.element[i].sub[0])
				SPRINT(buffer, " %s=", trace.element[i].sub, value);
			else
				SPRINT(buffer, " ", value);
			SCAT(trace_string, buffer);
			if (strchr(value, ' '))
				SPRINT(buffer, "'%s'", value);
			else
				SPRINT(buffer, "%s", value);
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
				SPRINT(buffer, " : %s%s = ", trace.element[i].sub, spaces[strlen(trace.element[i].sub)], value);
			else
				SPRINT(buffer, " :              ", value);
			SCAT(trace_string, buffer);
			if (strchr(value, ' '))
				SPRINT(buffer, "'%s'\n", value);
			else
				SPRINT(buffer, "%s\n", value);
			SCAT(trace_string, buffer);
			i++;
		}
		break;
	}

	/* end */
	if (detail >= 3)
		SCAT(trace_string, "\n");
}



^todo:
socket
file open


