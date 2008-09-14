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

#include "main.h"

MESSAGES

double now_d, last_d;
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

FILE *debug_fp = NULL;
int quit=0;

#if 0
struct lcr_fdset lcr_fdset[FD_SETSIZE];
#endif

pthread_mutex_t mutexd; // debug output mutex
pthread_mutex_t mutext; // trace output mutex
pthread_mutex_t mutexe; // error output mutex
//pthread_mutex_t mutex_lcr; // lcr process mutex

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

const char *debug_prefix = NULL;
int debug_count = 0;
int last_debug = 0;
int debug_newline = 1;
int nooutput = 0;

void debug_usleep(int msec, const char *file, int line, int hour, int min, int sec)
{
	usleep(msec);
}

void debug(const char *function, int line, const char *prefix, char *buffer)
{
	/* if we have a new debug count, we add a mark */
	if (last_debug != debug_count)
	{
		last_debug = debug_count;
		if (!nooutput)
			printf("\033[34m--------------------- %04d.%02d.%02d %02d:%02d:%02d %06d\033[36m\n", now_tm->tm_year+1900, now_tm->tm_mon+1, now_tm->tm_mday, now_tm->tm_hour, now_tm->tm_min, now_tm->tm_sec, debug_count%1000000);
		if (debug_fp)
			fprintf(debug_fp, "--------------------- %04d.%02d.%02d %02d:%02d:%02d %06d\n", now_tm->tm_year+1900, now_tm->tm_mon+1, now_tm->tm_mday, now_tm->tm_hour, now_tm->tm_min, now_tm->tm_sec, debug_count%1000000);
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

	if (debug_fp)
	{
		if (debug_newline)
		{
			if (function)
				fprintf(debug_fp, "%s%s(in %s() line %d): %s", prefix?prefix:"", prefix?" ":"", function, line, buffer);
			else
				fprintf(debug_fp, "%s%s: %s", prefix?prefix:"", prefix?" ":"", buffer);
		}
	}

	debug_newline = 0;
	if (buffer[0])
		if (buffer[strlen(buffer)-1] == '\n')
			debug_newline = 1;
}


void _printdebug(const char *function, int line, unsigned int mask, const char *fmt, ...)
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
		quit = sigset;
		/* set scheduler & priority */
		if (options.schedule > 1)
		{
			memset(&schedp, 0, sizeof(schedp));
			schedp.sched_priority = 0;
			sched_setscheduler(0, SCHED_OTHER, &schedp);
		}
		fprintf(stderr, "LCR: Signal received: %d\n", sigset);
		PDEBUG(DEBUG_LOG, "Signal received: %d\n", sigset);
	}
}


/*
 * the main
 */
int main(int argc, char *argv[])
{
	int			ret = -1;
	int			lockfd = -1; /* file lock */
	struct lcr_msg		*message;
	class Port		*port;
	class Endpoint		*epoint;
	class Join		*join;
	int			i;
	int			all_idle;
	char			prefix_string[64];
	struct sched_param	schedp;
	const char		*debug_prefix = "alloc";
	int			created_mutexd = 0,/* created_mutext = 0,*/ created_mutexe = 0,
        			created_lock = 0, created_signal = 0, created_debug = 0,
				created_misdn = 0;
	int			idletime = 0, idlecheck = 0;
	char			tracetext[256], lock[128];

#if 0
	/* init fdset */
	memset(lcr_fdset, 0, sizeof(lcr_fdset));
#endif

	/* lock LCR process */
//	pthread_mutex_lock(&mutex_lcr);

	/* current time */
	GET_NOW();

	/* show version */
	printf("\n** %s  Version %s\n\n", NAME, VERSION_STRING);

	/* show options */
	if (argc <= 1)
	{
		usage:
		printf("\n");
		printf("Usage: lcr (query | start | fork | rules | route)\n");
		printf("query     = Show available isdn ports.\n");
		printf("start     = Run lcr normally, abort with CTRL+C.\n");
		printf("fork      = Do daemon fork and run as background process.\n");
		printf("interface = Get help of available interface syntax.\n");
		printf("rules     = Get help of available routing rule syntax.\n");
		printf("rules [action] = Get individual help for given action.\n");
//		printf("route = Show current routing as it is parsed.\n");
		printf("\n");
		ret = 999;
		goto free;
	}

	/* init crc */
	crc_init();

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
	if (!(strcasecmp(argv[1],"interface")))
	{
		doc_interface();
		ret = 0;
		goto free;
	}

	/* show rules */
	if (!(strcasecmp(argv[1],"rules")))
	{
		if (argc <= 2)
			doc_rules(NULL);
		else
			doc_rules(argv[2]);
		ret = 0;
		goto free;
	}

	/* query available isdn ports */
	if (!(strcasecmp(argv[1],"query")))
	{
		fprintf(stderr, "-> Using 'misdn_info'\n");
		system("misdn_info");
		ret = 0;
		goto free;
	}

	/* read options */
	if (read_options() == 0)
	{
		PERROR("%s", options_error);
		goto free;
	}

	/* init mISDN */
	if (mISDN_initialize() < 0)
		goto free;
	created_misdn = 1;
	created_debug = 1;

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
	if (!(strcasecmp(argv[1],"route")))
	{
		ruleset_debug(ruleset_first);
		ret = 0;
		goto free;
	}
#endif

	/* do fork in special cases */
	if (!(strcasecmp(argv[1],"fork")))
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
			printf("LCR: Starting daemon.\n");
			exit(0);
		}
		nooutput = 1;
	} else
	/* if not start */
	if (!!strcasecmp(argv[1],"start"))
	{
		goto usage;
	}

	/* create lock and lock! */
	SPRINT(lock, "%s/lcr.lock", options.lock);
	if ((lockfd = open(lock, O_CREAT | O_WRONLY, S_IWUSR)) < 0)
	{
		fprintf(stderr, "Cannot create lock file: %s\n", lock);
		fprintf(stderr, "Check options.conf to change to path with permissions for you.\n");
		goto free;
	}
	if (flock(lockfd, LOCK_EX|LOCK_NB) < 0)
	{
		if (errno == EWOULDBLOCK)
			fprintf(stderr, "LCR: Another LCR process is running. Please kill the other one.\n");
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
			fprintf(stderr, "Warning: Not enough memory to lock paging, exitting...\n");
			break;
			case EPERM:
			fprintf(stderr, "Warning: No permission to lock paging, exitting...\n");
			break;
			case EFAULT:
			fprintf(stderr, "Warning: 'Bad address' while locking paging, exitting...\n");
			break;
			default:
			fprintf(stderr, "Warning: Unknown error %d while locking paging, exitting...\n", errno);
		}
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
	SPRINT(tracetext, "%s %s started, waiting for calls...", NAME, VERSION_STRING);
	start_trace(-1, NULL, NULL, NULL, 0, 0, 0, tracetext);
	printf("%s\n", tracetext);
	end_trace();
	GET_NOW();
	quit = 0;
	while(!quit)
	{

		last_d = now_d;
		GET_NOW();
		if (now_d-last_d > 1.0)
		{
			PERROR("LCR was stalling %d.%d seconds\n", ((int)((now_d-last_d)*10.0))/10, (int)((now_d-last_d)*10.0));
		}
		/* all loops must be counted from the beginning since nodes might get freed during handler */
		all_idle = 1;

//#warning debugging usleep crash
//		debug_usleep(1, __FILE__, __LINE__, now_tm->tm_hour, now_tm->tm_min, now_tm->tm_sec);

		/* handle mISDN messages from kernel */
		debug_prefix = "ISDN";
		if (mISDN_handler())
			all_idle = 0;
//#warning debugging usleep crash
//		debug_usleep(1, __FILE__, __LINE__, now_tm->tm_hour, now_tm->tm_min, now_tm->tm_sec);

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

		/* loop through all joins and call their handler */
		join_again:
		join = join_first;
		while(join)
		{
			debug_prefix = "join";
			debug_count++;
			ret = join->handler();
			if (ret)
				all_idle = 0;
			if (ret < 0) /* join has been destroyed */
				goto join_again;
			join = join->next;
		}

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

				case EPOINT_TO_JOIN:
				debug_prefix = "msg epoint->join";
				join = find_join_id(message->id_to);
				if (join)
				{
					join->message_epoint(message->id_from, message->type, &message->param);
				} else
				{
					PDEBUG(DEBUG_MSG, "Warning: message %s from endpoint %d to join %d. join doesn't exist anymore\n", messages_txt[message->type], message->id_from, message->id_to);
				}
				break;

				case JOIN_TO_EPOINT:
				debug_prefix = "msg join->epoint";
				epoint = find_epoint_id(message->id_to);
				if (epoint)
				{
					if (epoint->ep_app)
					{
						epoint->ep_app->ea_message_join(message->id_from, message->type, &message->param);
					} else
					{
						PDEBUG(DEBUG_MSG, "Warning: message %s from join %d to endpoint %d. endpoint doesn't have an application.\n", messages_txt[message->type], message->id_from, message->id_to);
					}
				} else
				{
					PDEBUG(DEBUG_MSG, "Warning: message %s from join %d to endpoint %d. endpoint doesn't exist anymore.\n", messages_txt[message->type], message->id_from, message->id_to);
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
BUDETECT

		/* handle socket */
		if (admin_handle())
			all_idle = 0;
BUDETECT

#if 0
		/* check for child to exit (eliminate zombies) */
		if (waitpid(-1, NULL, WNOHANG) > 0)
		{
			PDEBUG(DEBUG_EPOINT, "a child process (created by endpoint) has exitted.\n");
			all_idle = 0;
		}
#endif
//#warning debugging usleep crash
//		debug_usleep(1, __FILE__, __LINE__, now_tm->tm_hour, now_tm->tm_min, now_tm->tm_sec);

		/* do idle checking */
		if (idlecheck != now)
		{
			PDEBUG(DEBUG_IDLETIME, "Idle time : %d%%\n", idletime/10000);
			idletime = 0;
			idlecheck = now;
		}

		/* did we do nothing? so we wait to give time to other processes */
		if (all_idle)
		{
//			pthread_mutex_unlock(&mutex_lcr); // unlock LCR
			debug_usleep(4000, __FILE__, __LINE__, now_tm->tm_hour, now_tm->tm_min, now_tm->tm_sec);
//			pthread_mutex_lock(&mutex_lcr); // lock LCR
			idletime += 4000;
		}
	}
	SPRINT(tracetext, "%s terminated", NAME);
	printf("%s\n", tracetext);
	start_trace(-1, NULL, NULL, NULL, 0, 0, 0, tracetext);
	if (quit)
		add_trace((char *)"signal", NULL, "%d", quit);
	end_trace();
	ret=0;

	/* free all */
free:


	/* set scheduler & priority
	 */
	if (options.schedule > 1)
	{
		memset(&schedp, 0, sizeof(schedp));
		schedp.sched_priority = options.schedule;
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
	join_free();

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
	{
		chmod(lock, 0700);
		unlink(lock);
		close(lockfd);
	}

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

	/* deinitialize mISDN */
	if (created_misdn)
		mISDN_deinitialize();

	/* display memory leak */
#define MEMCHECK(a, b) \
	if (b) \
	{ \
		SPRINT(tracetext, a, NAME); \
		start_trace(-1, NULL, NULL, NULL, 0, 0, 0, tracetext); \
		if (ret) add_trace("blocks", NULL, "%d", b); \
		end_trace(); \
		printf("\n******************************\n\007"); \
		printf("\nERROR: %d %s\n", b, a); \
		printf("\n******************************\n"); \
		ret = -1; \
	}
	MEMCHECK("",memuse)
	MEMCHECK("memory block(s) left (port.cpp ...)",pmemuse)
	MEMCHECK("memory block(s) left (epoint*.cpp ...)",ememuse)
	MEMCHECK("memory block(s) left (join*.cpp)",cmemuse)
	MEMCHECK("memory block(s) left (message.c)",mmemuse)
	MEMCHECK("memory block(s) left (route.c)",rmemuse)
	MEMCHECK("memory block(s) left (args)",amemuse)
	MEMCHECK("class(es) left",classuse)
	MEMCHECK("file descriptor(s) left",fduse)
	MEMCHECK("file handler(s) left",fhuse)

	/* unlock LCR process */
//	pthread_mutex_unlock(&mutex_lcr);

	/* take me out */
	return(ret);
}


#ifdef BUDETECT_DEF
/* special debug function to detect buffer overflow
 */
int budetect_stop = 0;
void budetect(const char *file, int line, const char *function)
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

