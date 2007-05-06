/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** PBX Watchdog with debug function                                          **
**                                                                           **
\*****************************************************************************/ 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include "save.h"

time_t now;
#define GET_NOW() \
	{ \
		now = time(&now); \
	}

int quit = 0;

void sighandler(int sigset)
{
	if (sigset == SIGHUP)
		return;
	if (sigset == SIGPIPE)
		return;
	printf("Signal received: %d\n", sigset);
	quit = 1;
}


int main(int argc, char *argv[])
{
	int debug = 0;
	int ret;
	char command[256], file[64] = "pbx";

	printf("** PBX-Watch ***\n\n");

	/* show options */
	if (argc <= 1)
	{
		usage:
		printf("\n");
		printf("Usage: pbxwatch (run | debug) [fork]\n");
		printf("run   = Starts PBX4Linux (pbx) and restart it on every crash.\n");
		printf("debug = Starts PBX4Linux using debugger an restarts it on every crash.\n");
		printf("        Log files and back trace are moved into a seperate directory.\n");
		printf("fork  = Option to make pbxwatch running as daemon.\n");
		printf("\n");
		return(0);
	}

	if (!(strcasecmp(argv[1],"run")))
		debug = 0;
	else
	if (!(strcasecmp(argv[1],"debug")))
		debug = 1;
	else
		goto usage;

	/* do fork */
	if (argc > 2)
	if (!(strcasecmp(argv[2],"fork")))
	{
		/* do daemon fork */
		pid_t pid;

		pid = fork();

		if (pid < 0)
		{
			fprintf(stderr, "Cannot fork!\n");
			exit(EXIT_FAILURE);
		}
		if (pid != 0)
		{
			printf("PBX-Watch: Starting daemon.\n");
			exit(0);
		}
		usleep(200000);
		printf("\n");
	}

	/* signal handlers */	
	signal(SIGINT,sighandler);
	signal(SIGHUP,sighandler);
	signal(SIGTERM,sighandler);
	signal(SIGPIPE,sighandler);

	while(42)
	{
		/* RUN */
		printf("*** RUNNING PBX4LINUX ***\n");
		if (debug)
		{
			/* write debugger batch list */
			SPRINT(command, "echo > /tmp/pbxwatch.batch -e \"handle SIGPIPE nostop\\\\nfile %s\\\\nrun start\\\\nbt\\\\n\"", file);
			system(command);
			SPRINT(command, "gdb --quiet --batch -x \"/tmp/pbxwatch.batch\" 2>&1 | tee %s/crashreport", INSTALL_DATA);
			printf("*** DEBUGGER STARTED ***\n");
			ret = system(command);
			printf("*** DEBUGGER FINISHED ***\n");
		} else
		{
			SCPY(command, file);
			ret = system(command);
			if (ret != 11)
			{
				printf("*** PBX4LINUX exitted with return code %d ***\n", ret);
				break;
			}
			printf("*** PBX4LINUX CRASHED ***\n");
		}

		/* LOG */
		printf("*** WRITING LOG  ***\n");
		GET_NOW();
		SPRINT(command, "mkdir \"%s/%d\" && mv \"%s/crashreport\" \"%s/%d\" && cp -a \"%s/debug*.log\" \"%s/%d\"\n", INSTALL_DATA, now, INSTALL_DATA, INSTALL_DATA, now, INSTALL_DATA, INSTALL_DATA, now);
		system(command);

		/* DELAY */
		printf("*** SLEEPING 10 SECONDS UNTIL RESTART ***\n");
		sleep(10);
		if (quit)
			break;
	}

	signal(SIGINT,SIG_DFL);
	signal(SIGHUP,SIG_DFL);
	signal(SIGTERM,SIG_DFL);
	signal(SIGPIPE,SIG_DFL);

	return(0);
}


