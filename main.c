/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** Main function                                                             **
**                                                                           **
\*****************************************************************************/ 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include "main.h"

MESSAGES

double now_d;
time_t now;
struct tm *now_tm;
struct timeval now_tv;
struct timezone now_tz;
#define GET_NOW() \
	{ \
		gettimeofday(&now_tv, &now_tz); \
		now_d = ((double)(now_tv.tv_usec))/1000000 + now_tv.tv_sec; \
		now = now_tv.tv_sec; \
		now_tm = localtime(&now); \
	}
//#define DEBUG_DURATION

int global_debug = 0;
int quit=0;

pthread_mutex_t mutexd; // debug output mutex
//pthread_mutex_t mutext; // trace output mutex
pthread_mutex_t mutexe; // error output mutex

int memuse = 0;
int mmemuse = 0;
int cmemuse = 0;
int ememuse = 0;
int pmemuse = 0;
int amemuse = 0;
int rmemuse = 0;
int classuse = 0;
int fduse = 0;
int fhuse = 0;

char *debug_prefix = 0;
int debug_count = 0;
int last_debug = 0;
int debug_newline = 1;
int nooutput = 0;

static void debug(const char *function, int line, char *prefix, char *buffer)
{
	/* if we have a new debug count, we add a mark */
	if (last_debug != debug_count)
	{
		last_debug = debug_count;
		if (!nooutput)
			printf("\033[34m--------------------- %04d.%02d.%02d %02d:%02d:%02d %06d\033[36m\n", now_tm->tm_year+1900, now_tm->tm_mon+1, now_tm->tm_mday, now_tm->tm_hour, now_tm->tm_min, now_tm->tm_sec, debug_count%1000000);
		if (options.deb&DEBUG_LOG && global_debug)
			dprint(DBGM_MAN, 0, "--------------------- %04d.%02d.%02d %02d:%02d:%02d %06d\n", now_tm->tm_year+1900, now_tm->tm_mon+1, now_tm->tm_mday, now_tm->tm_hour, now_tm->tm_min, now_tm->tm_sec, debug_count%1000000);
	}

	if (!nooutput)
	{
		if (debug_newline)
			printf("\033[32m%06d %s\033[37m%s", debug_count%1000000, prefix?prefix:"", prefix?" ":"");
		if (function)
			printf("(in %s() line %d): %s", function, line, buffer);
		else
			printf("%s", buffer);
	}

	if (options.deb&DEBUG_LOG && global_debug)
	{
		if (debug_newline)
		{
			if (function)
				dprint(DBGM_MAN, 0, "%s%s(in %s() line %d): %s", prefix?prefix:"", prefix?" ":"", function, line, buffer);
			else
				dprint(DBGM_MAN, 0, "%s%s: %s", prefix?prefix:"", prefix?" ":"", buffer);
		}
	}

	debug_newline = 0;
	if (buffer[0])
		if (buffer[strlen(buffer)-1] == '\n')
			debug_newline = 1;
}


void _printdebug(const char *function, int line, unsigned long mask, const char *fmt, ...)
{
	char buffer[4096];
	va_list args;

	if (!(options.deb & mask))
		return;
	pthread_mutex_lock(&mutexd);

	va_start(args,fmt);
	VUNPRINT(buffer,sizeof(buffer)-1,fmt,args);
	buffer[sizeof(buffer)-1]=0;
	va_end(args);

	debug(function, line, debug_prefix, buffer);

	pthread_mutex_unlock(&mutexd);
}

void _printerror(const char *function, int line, const char *fmt, ...)
{
	char buffer[4096];
	va_list args;

	pthread_mutex_lock(&mutexe);

	va_start(args,fmt);
	VUNPRINT(buffer,sizeof(buffer)-1,fmt,args);
	buffer[sizeof(buffer)-1]=0;
	va_end(args);

	if (options.deb)
		debug(function, line, "ERROR", buffer);
	else /* only if we do not debug */
	{
		if (function)
			fprintf(stderr, "ERROR (in %s() line %d) %s", function, line, buffer);
		else
			fprintf(stderr, "ERROR %s", buffer);
	}

	pthread_mutex_unlock(&mutexe);
}


void sighandler(int sigset)
{
	struct sched_param schedp;

	if (sigset == SIGHUP)
		return;
	if (sigset == SIGPIPE)
		return;
	if (!quit)
	{
		quit=1;
		/* set scheduler & priority */
		if (options.schedule > 1)
		{
			memset(&schedp, 0, sizeof(schedp));
			schedp.sched_priority = 0;
			sched_setscheduler(0, SCHED_OTHER, &schedp);
		}
		fprintf(stderr, "PBX: Signal received: %d\n", sigset);
		PERROR("Signal received: %d\n", sigset);
	}
}


/*
 * the main
 */
#ifdef VOIP
#define ARGC (args.GetCount()+1)
#define ARGV(a) (args[a-1])
void PBXMain::Main(void)
{
	PArgList &args = GetArguments();
#else
#define ARGC (argc)
#define ARGV(a) (argv[a])
int main(int argc, char *argv[])
{
#endif
	int			ret = -1;
	int			lockfd = -1; /* file lock */
	struct message		*message;
	class Port		*port;
	class Endpoint		*epoint;
	class Call		*call;
	int			i;
	int			all_idle;
	char			prefix_string[64];
	struct sched_param	schedp;
	char 			*debug_prefix = "alloc";
	int			created_mutexd = 0,/* created_mutext = 0,*/ created_mutexe = 0,
        			created_lock = 0, created_signal = 0, created_debug = 0;
#ifdef DEBUG_DURATION
	time_t			durationupdate;
	double			idle_duration, isdn_duration, port_duration, epoint_duration, call_duration, message_duration, admin_duration;
	double			start_d;
#endif
	int			idletime = 0, idlecheck = 0;
	char			debug_log[128];

	/* current time */
	GET_NOW();

	/* show version */
	printf("\n** %s  Version %s\n\n", NAME, VERSION_STRING);

	/* show options */
	if (ARGC <= 1)
	{
		usage:
		printf("\n");
		printf("Usage: pbx (query | start | fork | rules | route)\n");
		printf("query     = Show available isdn ports.\n");
		printf("start     = Run pbx normally, abort with CTRL+C.\n");
		printf("fork      = Do daemon fork and run as background process.\n");
		printf("interface = Get help of available interface syntax.\n");
		printf("rules     = Get help of available routing rule syntax.\n");
		printf("rules [action] = Get individual help for given action.\n");
//		printf("route = Show current routing as it is parsed.\n");
		printf("\n");
		ret = 0;
		goto free;
	}

	/* init crc */
	crc_init();

	/* check for root (real or effective) */
	if (getuid() && geteuid())
	{
		fprintf(stderr, "Please run %s as super-user.\n", NAME);
		goto free;
	}

	/* the mutex init */
	if (pthread_mutex_init(&mutexd, NULL))
	{
		fprintf(stderr, "cannot create 'PDEBUG' mutex\n");
		goto free;
	}
	created_mutexd = 1;
//	if (pthread_mutex_init(&mutext, NULL))
//	{
//		fprintf(stderr, "cannot create 'trace' mutex\n");
//		goto free;
//	}
//	created_mutext = 1;
	if (pthread_mutex_init(&mutexe, NULL))
	{
		fprintf(stderr, "cannot create 'PERROR' mutex\n");
		goto free;
	}
	created_mutexe = 1;

	/* show interface */
	if (!(strcasecmp(ARGV(1),"interface")))
	{
		doc_interface();
		ret = 0;
		goto free;
	}

	/* show rules */
	if (!(strcasecmp(ARGV(1),"rules")))
	{
		if (ARGC <= 2)
			doc_rules(NULL);
		else
			doc_rules(ARGV(2));
		ret = 0;
		goto free;
	}

	/* query available isdn ports */
	if (!(strcasecmp(ARGV(1),"query")))
	{
		mISDN_port_info();
		ret = 0;
		goto free;
	}

	/* read options */
	if (read_options() == 0)
		goto free;

	/* initialize stuff of the NT lib */
	if (options.deb & DEBUG_STACK)
	{
		global_debug = 0xffffffff & ~DBGM_MSG;
//		global_debug = DBGM_L3DATA;
	} else
		global_debug = DBGM_MAN;
	SPRINT(debug_log, "%s/debug.log", INSTALL_DATA);
	if (options.deb & DEBUG_LOG)
		debug_init(global_debug, debug_log, debug_log, debug_log);
	else
		debug_init(global_debug, NULL, NULL, NULL);
	created_debug = 1;

	msg_init();

	/* read ruleset(s) */
	if (!(ruleset_first = ruleset_parse()))
		goto free;

	/* set pointer to main ruleset */
	ruleset_main = getrulesetbyname("main");
	if (!ruleset_main)
	{
		fprintf(stderr, "\n***\n -> Missing 'main' ruleset, causing ALL calls to be disconnected.\n***\n\n");
		PDEBUG(DEBUG_LOG, "Missing 'main' ruleset, causing ALL calls to be disconnected.\n");
		sleep(2);
	}

#if 0
	/* query available isdn ports */
	if (!(strcasecmp(ARGV(1),"route")))
	{
		ruleset_debug(ruleset_first);
		ret = 0;
		goto free;
	}
#endif

	/* do fork in special cases */
	if (!(strcasecmp(ARGV(1),"fork")))
	{
		pid_t pid;

		/* do daemon fork */
		pid = fork();

		if (pid < 0)
		{
			fprintf(stderr, "Cannot fork!\n");
			goto free;
		}
		if (pid != 0)
		{
			exit(0);
		}
		usleep(200000);
		printf("\n");
		
		/* do second fork */
		pid = fork();

		if (pid < 0)
		{
			fprintf(stderr, "Cannot fork!\n");
			goto free;
		}
		if (pid != 0)
		{
			printf("PBX: Starting daemon.\n");
			exit(0);
		}
		nooutput = 1;
	} else
	/* if not start */
	if (!!strcasecmp(ARGV(1),"start"))
	{
		goto usage;
	}

	/* create lock and lock! */
	if ((lockfd = open("/var/run/pbx.lock", O_CREAT, 0)) < 0)
	{
		fprintf(stderr, "Cannot create lock file: /var/run/pbx.lock\n");
		goto free;
	}
	if (flock(lockfd, LOCK_EX|LOCK_NB) < 0)
	{
		if (errno == EWOULDBLOCK)
			fprintf(stderr, "PBX: Another PBX process is running. Please kill the other one.\n");
		else	fprintf(stderr, "Locking process failed: errno=%d\n", errno);
		goto free;
	}
	created_lock = 1;

	/* initialize admin socket */
	if (admin_init())
	{
		fprintf(stderr, "Unable to initialize admin socket.\n");
		goto free;
	}

	/* generate alaw / ulaw tables */
	generate_tables(options.law);

	/* load tones (if requested) */
	if (fetch_tones() == 0)
	{
		fprintf(stderr, "Unable to fetch tones into memory.\n");
		goto free;
	}

	/* read interfaces and open ports */
	if (!read_interfaces())
	{
		PERROR_RUNTIME("No interfaces specified or failed to parse interface.conf.\n");
		fprintf(stderr, "No interfaces specified or failed to parse interface.conf.\n");
		goto free;
	}
	relink_interfaces();
	interface_first = interface_newlist;
	free_interfaces(interface_newlist);
	interface_newlist = NULL;
	
	/* locking memory paging */
	i = 0;
	while(i < 10)
	{
		if (mlockall(MCL_CURRENT | MCL_FUTURE) >= 0)
			break;
		usleep(200000);
		i++;
	}
	if (i == 10)
	{
		switch(errno)
		{
			case ENOMEM:
			fprintf(stderr, "Not enough memory to lock paging, exitting...\n");
			break;
			case EPERM:
			fprintf(stderr, "No permission to lock paging, exitting...\n");
			break;
			case EFAULT:
			fprintf(stderr, "'Bad address' while locking paging, exitting...\n");
			break;
			default:
			fprintf(stderr, "Unknown error %d while locking paging, exitting...\n", errno);
		}
		goto free;
	}

	/* set real time scheduler & priority */
	if (options.schedule > 1)
	{
		memset(&schedp, 0, sizeof(schedp));
		schedp.sched_priority = options.schedule;
		ret = sched_setscheduler(0, SCHED_RR, &schedp);
		if (ret < 0)
		{
			PERROR("Scheduling failed with given priority %d (errno = %d).\nCheck options.conf 'schedule', exitting...\n", options.schedule, errno);
			goto free;
		}
	}

	/* signal handlers */	
	signal(SIGINT,sighandler);
	signal(SIGHUP,sighandler);
	signal(SIGTERM,sighandler);
	signal(SIGPIPE,sighandler);
	created_signal = 1;

	/*** main loop ***/
	printf("%s %s started, waiting for calls...\n", NAME, VERSION_STRING);
	GET_NOW();
#ifdef DEBUG_DURATION
	start_d = now_d;
	durationupdate = now;
	idle_duration = isdn_duration = port_duration = epoint_duration = call_duration = message_duration = admin_duration = 0;
#endif
	quit = 0;
	while(!quit)
	{
		/* all loops must be counted from the beginning since nodes might get freed during handler */
		all_idle = 1;

		/* handle mISDN messages from kernel */
		debug_prefix = "ISDN";
		if (mISDN_handler())
			all_idle = 0;
#ifdef DEBUG_DURATION
		GET_NOW();
		isdn_duration += (now_d - start_d);
		start_d = now_d;
#endif
BUDETECT

		/* loop through all port ports and call their handler */
		port_again:
		port = port_first;
		while(port)
		{
			debug_prefix = port->p_name;
			debug_count++;
			ret = port->handler();
			if (ret)
				all_idle = 0;
			if (ret < 0) /* port has been destroyed */
				goto port_again;
			port = port->next;
		}
#ifdef DEBUG_DURATION
		GET_NOW();
		port_duration += (now_d - start_d);
		start_d = now_d;
#endif

		/* loop through all epoint and call their handler */
		epoint_again:
		epoint = epoint_first;
		while(epoint)
		{
			debug_prefix = prefix_string;
			SPRINT(prefix_string, "ep%ld", epoint->ep_serial);
			debug_count++;
			ret = epoint->handler();
			if (ret)
				all_idle = 0;
			if (ret < 0) /* epoint has been destroyed */
				goto epoint_again;
			epoint = epoint->next;
		}
#ifdef DEBUG_DURATION
		GET_NOW();
		epoint_duration += (now_d - start_d);
		start_d = now_d;
#endif

		/* loop through all calls and call their handler */
		call_again:
		call = call_first;
		while(call)
		{
			debug_prefix = "call";
			debug_count++;
			ret = call->handler();
			if (ret)
				all_idle = 0;
			if (ret < 0) /* call has been destroyed */
				goto call_again;
			call = call->next;
		}
#ifdef DEBUG_DURATION
		GET_NOW();
		call_duration += (now_d - start_d);
		start_d = now_d;
#endif

		debug_prefix = 0;

		/* process any message */
		debug_count++;
		debug_prefix = "message";
		while ((message = message_get()))
		{
			all_idle = 0;
			switch(message->flow)
			{
				case PORT_TO_EPOINT:
				debug_prefix = "msg port->epoint";
				epoint = find_epoint_id(message->id_to);
				if (epoint)
				{
					if (epoint->ep_app)
					{
						epoint->ep_app->ea_message_port(message->id_from, message->type, &message->param);
					} else
					{
						PDEBUG(DEBUG_MSG, "Warning: message %s from port %d to endpoint %d. endpoint doesn't have an application.\n", messages_txt[message->type], message->id_from, message->id_to);
					}
				} else
				{
					PDEBUG(DEBUG_MSG, "Warning: message %s from port %d to endpoint %d. endpoint doesn't exist anymore.\n", messages_txt[message->type], message->id_from, message->id_to);
				}
				break;

				case EPOINT_TO_CALL:
				debug_prefix = "msg epoint->call";
				call = find_call_id(message->id_to);
				if (call)
				{
					call->message_epoint(message->id_from, message->type, &message->param);
				} else
				{
					PDEBUG(DEBUG_MSG, "Warning: message %s from endpoint %d to call %d. call doesn't exist anymore\n", messages_txt[message->type], message->id_from, message->id_to);
				}
				break;

				case CALL_TO_EPOINT:
				debug_prefix = "msg call->epoint";
				epoint = find_epoint_id(message->id_to);
				if (epoint)
				{
					if (epoint->ep_app)
					{
						epoint->ep_app->ea_message_call(message->id_from, message->type, &message->param);
					} else
					{
						PDEBUG(DEBUG_MSG, "Warning: message %s from call %d to endpoint %d. endpoint doesn't have an application.\n", messages_txt[message->type], message->id_from, message->id_to);
					}
				} else
				{
					PDEBUG(DEBUG_MSG, "Warning: message %s from call %d to endpoint %d. endpoint doesn't exist anymore.\n", messages_txt[message->type], message->id_from, message->id_to);
				}
				break;

				case EPOINT_TO_PORT:
				debug_prefix = "msg epoint->port";
				port = find_port_id(message->id_to);
				if (port)
				{
					port->message_epoint(message->id_from, message->type, &message->param);
BUDETECT
				} else
				{
					PDEBUG(DEBUG_MSG, "Warning: message %s from endpoint %d to port %d. port doesn't exist anymore\n", messages_txt[message->type], message->id_from, message->id_to);
				}
				break;

				default:
				PERROR("Message flow %d unknown.\n", message->flow);
			}
			message_free(message);
			debug_count++;
			debug_prefix = "message";
		}
#ifdef DEBUG_DURATION
		GET_NOW();
		message_duration += (now_d - start_d);
		start_d = now_d;
#endif
BUDETECT

		/* handle socket */
		if (admin_handle())
			all_idle = 0;
#ifdef DEBUG_DURATION
		GET_NOW();
		admin_duration += (now_d - start_d);
		start_d = now_d;
#endif
BUDETECT

#if 0
		/* check for child to exit (eliminate zombies) */
		if (waitpid(-1, NULL, WNOHANG) > 0)
		{
			PDEBUG(DEBUG_EPOINT, "a child process (created by endpoint) has exitted.\n");
			all_idle = 0;
		}
#endif

		/* do idle checking */
		if (idlecheck != now)
		{
			PDEBUG(DEBUG_IDLETIME, "Idle time : %d%%\n", idletime/10000);
			idletime = 0;
			idlecheck = now;
		}
#ifdef DEBUG_DURATION
		GET_NOW();
		idle_duration += (now_d - start_d);
		start_d = now_d;
		if (durationupdate != now)
		{
			durationupdate = now;
			printf("Idle:%3d ISDN:%3d Port:%3d Epoint:%3d Call:%3d Message:%3d Admin:%3d\n",
				(int)(idle_duration*100),
				(int)(isdn_duration*100),
				(int)(port_duration*100),
				(int)(epoint_duration*100),
				(int)(call_duration*100),
				(int)(message_duration*100),
				(int)(admin_duration*100));
			idle_duration = isdn_duration = port_duration = epoint_duration = call_duration = message_duration = admin_duration = 0;
		}
#else
		GET_NOW();
#endif

		/* did we do nothing? so we wait to give time to other processes */
		if (all_idle)
		{
			usleep(4000); /* wait 32 samples */
			idletime += 4000;
		}
	}
	printf("PBX terminated\n");
	ret=0;

	/* free all */
free:


	/* set scheduler & priority
	 */
	if (options.schedule > 1)
	{
		memset(&schedp, 0, sizeof(schedp));
		schedp.sched_priority = 0;
		sched_setscheduler(0, SCHED_OTHER, &schedp);
	}
	/* reset signals */
	if (created_signal)
	{
		signal(SIGINT,SIG_DFL);
		signal(SIGHUP,SIG_DFL);
		signal(SIGTERM,SIG_DFL);
		signal(SIGPIPE,SIG_DFL);
	}

	/* destroy objects */
	debug_prefix = "free";

	while(port_first)
	{
		debug_count++;
		delete port_first;
	}
	while(epoint_first)
	{
		debug_count++;
		delete epoint_first;
	}
	epoint_first = NULL;
	debug_count++;
	call_free();

	/* free interfaces */
	if (interface_first)
		free_interfaces(interface_first);
	interface_first = NULL;

	/* close isdn ports */
	mISDNport_close_all();

	/* flush messages */
	debug_count++;
	i = 0;
	while ((message = message_get()))
	{
		i++;
		message_free(message);
	}
	if (i)
	{
		PDEBUG(DEBUG_MSG, "freed %d pending messages\n", i);
	}

	/* free tones */
	if (toneset_first)
		free_tones();

	/* free admin socket */
	admin_cleanup();

	/* close lock */
	if (created_lock)
		flock(lockfd, LOCK_UN);
	if (lockfd >= 0)
		close(lockfd);

	/* free rulesets */
	if (ruleset_first)
		ruleset_free(ruleset_first);
	ruleset_first = NULL;

	/* free mutex */
	if (created_mutexe)
		if (pthread_mutex_destroy(&mutexe))
			fprintf(stderr, "cannot destroy 'PERROR' mutex\n");
//	if (created_mutext)
//		if (pthread_mutex_destroy(&mutext))
//			fprintf(stderr, "cannot destroy 'trace' mutex\n");
	if (created_mutexd)
		if (pthread_mutex_destroy(&mutexd))
			fprintf(stderr, "cannot destroy 'PDEBUG' mutex\n");

	/* close debug */
	if (created_debug)
		debug_close();
	global_debug = 0;

	/* display memory leak */
#define MEMCHECK(a, b) \
	if (b) \
	{ \
		printf("\n******************************\n\007"); \
		printf("\nERROR: %d %s\n", b, a); \
		printf("\n******************************\n"); \
		ret = -1; \
	}
	MEMCHECK("",memuse)
	MEMCHECK("memory block(s) left (port.cpp)",pmemuse)
	MEMCHECK("memory block(s) left (epoint.cpp)",ememuse)
	MEMCHECK("memory block(s) left (call.cpp)",cmemuse)
	MEMCHECK("memory block(s) left (message.c)",mmemuse)
	MEMCHECK("memory block(s) left (route.c)",rmemuse)
	MEMCHECK("memory block(s) left (args)",amemuse)
	MEMCHECK("class(es) left",classuse)
	MEMCHECK("file descriptor(s) left",fduse)
	MEMCHECK("file handler(s) left",fhuse)

	/* take me out */
	if (ret)
		printf("PBX: Exit (code %d)\n", ret);
#ifdef VOIP
	return;
#else
	return(ret);
#endif
}


#ifdef BUDETECT_DEF
/* special debug function to detect buffer overflow
 */
int budetect_stop = 0;
void budetect(const char *file, int line, char *function)
{
	if (budetect_stop)
		return;
	/* modify this function to detect race-bugs */
#warning DID YOU MODIFY THIS FUNCTION TO DETECT THE BUFFER OVERFLOW BUG?
	class Port *port;
	class PmISDN *pmisdn;
	struct mISDNport *mISDNport = mISDNport_first;
	int i, ii;

	while(mISDNport)
	{
		i = 0;
		ii = mISDNport->b_num;
		while(i < ii)
		{
			if (mISDNport->b_port[i])
			{
				port = port_first;
				while(port)
				{
					if ((port->p_type&PORT_CLASS_MASK) == PORT_CLASS_ISDN)
					{
						pmisdn = (class PmISDN *)port;
						if (pmisdn->p_isdn_crypt_listen)
						{
							PERROR_RUNTIME("************************************************\n");
							PERROR_RUNTIME("** BUG detected in %s, line %d, function %s\n", file, line, function);
							PERROR_RUNTIME("** p_isdn_crypt_listen = %d\n", pmisdn->p_isdn_crypt_listen);
							PERROR_RUNTIME("************************************************\n");
							budetect_stop = 1;
						}
					}
					if (port == mISDNport->b_port[i])
						break;
					port = port->next;
					if (!port)
					{
						PERROR_RUNTIME("************************************************\n");
						PERROR_RUNTIME("** BUG detected in %s, line %d, function %s\n", file, line, function);
						PERROR_RUNTIME("** b_port not in list.\n");
						PERROR_RUNTIME("************************************************\n");
						budetect_stop = 1;
					}
				}
			}
			i++;
		}
		mISDNport = mISDNport->next;
	}

}
#endif

