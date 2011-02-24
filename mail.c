/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** use mailer to send mail about message                                     **
**                                                                           **
\*****************************************************************************/ 

#include "main.h"

static const char *months[] = {
	"January", "February", "March", "April", "Mai", "June", "July",
	"August", "September", "October", "November", "December"
};


/*
 * create mail with or without sample
 * the process creates forks to keep pbx running
 */
struct mail_args {
	char	email[128];
	char	filename[256];
	int	year;
	int	mon;
	int	mday;
	int	hour;
	int	min;
	char	callerid[64];
	char	callerintern[32];
	char	callername[64];
	char	terminal[32];
};

static void *mail_child(void *arg)
{
	struct mail_args *args = (struct mail_args *)arg;
	char *email = args->email;
	char *filename = args->filename;
	int year = args->year;
	int mon = args->mon;
	int mday = args->mday;
	int hour = args->hour;
	int min = args->min;
	char *callerid = args->callerid;
	char *callerintern = args->callerintern;
	char *callername = args->callername;
	char *terminal = args->terminal;

	char command[128];
	char buffer[256];
	char rbuf[54];
	FILE *ph;
	int fh;
	unsigned char e1, e2, e3;
	int i, n, cnt;
	unsigned char base64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	struct sched_param schedp;
	int ret;

	PDEBUG(DEBUG_EPOINT, "child process started for sending a mail\n");

	/* lower priority to keep pbx running fluently */
	if (options.schedule > 0) {
		memset(&schedp, 0, sizeof(schedp));
		schedp.sched_priority = 0;
		ret = sched_setscheduler(0, SCHED_OTHER, &schedp);
		if (ret < 0) {
			PERROR("Scheduling to normal priority failed (errno = %d).\nExitting child process...\n", errno);
			goto done;
		}
	}

	/* open process */
	SPRINT(command, "%s -f%s %s", SENDMAIL, options.email, email);
	if ((ph = popen(command, "w")) < 0) {
		PERROR("Cannot send mail using command '%s'\n", command);
		goto done;
	}

	/* send header */
	fprintf(ph, "MIME-Version: 1.0\n");
	fprintf(ph, "Content-Type: multipart/mixed;\n\tboundary=\"next_part\"\n");
 	fprintf(ph, "From: %s <%s>\n", NAME, options.email);
	fprintf(ph, "To: %s\n", email);
	fprintf(ph, "Subject: Message from '%s' recorded.\n\n", callerid);

	/* send message */
	fprintf(ph, "This is a MIME-encapsulated message\n--next_part\n");
	fprintf(ph, "Content-Type: text/plain; charset=us-ascii\nContent-Transfer-Encoding: 7bit\n\n");
 	fprintf(ph, "\nThe voice box of %s has recorded a message:\n\n * extension: %s\n * from: %s", NAME, terminal, callerid);
	if (callerintern[0])
		fprintf(ph, " (intern %s)", callerintern);
	if (callername[0])
		fprintf(ph, " %s", callername);
 	fprintf(ph, "\n * date: %s %d %d %d:%02d\n\n", months[mon], mday, year+1900, hour, min);

	/* attach audio file */
	if (filename[0]) {
   	    if ((fh = open(filename, O_RDONLY))) {
		while(strchr(filename, '/'))
			filename = strchr(filename, '/')+1;
		fprintf(ph, "--next_part\n");
		if (strlen(filename) >= 4)
		if (!strcasecmp(filename+strlen(filename)-4, ".wav"))
			fprintf(ph, "Content-Type: audio/x-wav;\n\tname=\"%s\"\n", filename);
		fprintf(ph, "Content-Transfer-Encoding: base64\nContent-Disposition: inline;\n\tfilename=\"%s\"\n\n", filename);

		/* stream from disk and encode */
		while(42) {
			/* read exactly one line */
			cnt = read(fh, rbuf, 54);
			if (cnt <= 0)
				break;
			/* encode */
			n = cnt;
			while (n%3) {
				rbuf[n] = 0;
				n++;
			}
			n = n/3;
			i = 0;
			while(i<n) {
				e1 = rbuf[i+i+i];
				e2 = rbuf[i+i+i+1];
				e3 = rbuf[i+i+i+2];
				buffer[(i<<2)+3] = base64[e3 & 0x3f];
				buffer[(i<<2)+2] = base64[((e3>>6)+(e2<<2)) & 0x3f];
				buffer[(i<<2)+1] = base64[((e2>>4)+(e1<<4)) & 0x3f];
				buffer[i<<2] = base64[e1 >> 2];
				i++;
			}
			if ((cnt%3) > 0)
				buffer[(i<<2)-1] = '=';
			if ((cnt%3) == 1)
				buffer[(i<<2)-2] = '=';
			buffer[(i<<2)] = '\n';
			buffer[(i<<2)+1] = '\0';
			/* write */
			fprintf(ph, "%s", buffer);
		}

		fprintf(ph, "\n\n");
		close(fh);
    	    } else {
		SPRINT(buffer, "-Error- Failed to read audio file: '%s'.\n\n", filename);
		fprintf(ph, "%s", buffer);
		PERROR("%s", buffer);
	    }
        }

	/* finish mail */
	fprintf(ph, ".\n");

	/* wait for mail to be sent and close process */
	pclose(ph);

	done:
	PDEBUG(DEBUG_EPOINT, "child process done for sending a mail\n");

	/* exit process */
	FREE(args, sizeof(struct mail_args));
	amemuse--;
	return(NULL);
}

void send_mail(char *filename, char *callerid, char *callerintern, char *callername, char *vbox_email, int vbox_year, int vbox_mon, int vbox_mday, int vbox_hour, int vbox_min, char *terminal)
{
	struct mail_args *arg;
	pthread_t tid;

	arg = (struct mail_args *)MALLOC(sizeof(struct mail_args));
	amemuse++;

	SCPY(arg->email, vbox_email);
	SCPY(arg->filename, filename);
	arg->year = vbox_year;
	arg->mon = vbox_mon;
	arg->mday = vbox_mday;
	arg->hour = vbox_hour;
	arg->min = vbox_min;
	SCPY(arg->callerid, callerid);
	SCPY(arg->callerintern, callerintern);
	SCPY(arg->callername, callername);
	SCPY(arg->terminal, terminal);

	if ((pthread_create(&tid, NULL, mail_child, arg)<0)) {
		PERROR("failed to create mail-thread.\n");
		return;
	}

	PDEBUG(DEBUG_EPOINT, "EPOINT '%s' send mail: child process created for sending a mail\n", terminal);
}

