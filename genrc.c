/*****************************************************************************\
**                                                                           **
** LCR                                                                       **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** generate start/stop script                                                **
**                                                                           **
\*****************************************************************************/ 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "macro.h"

int type[256];
int coredebug=0, carddebug=0, dspdebug=0;
int lawopt=0;

struct cards {
	const char *name;
	const char *module;
} cards[] = {
//	{ "AVM Fritz PCI (PNP)", "avmfritz"},
	{ "HFC PCI (Cologne Chip)", "hfcpci"},
	{ "HFC-4S / HFC-8S / HFC-E1 (Cologne Chip)", "hfcmulti"},
	{ "HFC-S USB (Cologne Chip)", "hfcsusb"},
//	{ "HFC-S MINI (Cologne Chip)", "hfcsmini"},
//	{ "XHFC (Cologne Chip)", "xhfc"},
//	{ "Sedlbaur FAX", "sedlfax"},
//	{ "Winbond 6692 PCI", "w6692pci"},
	{NULL, NULL}
};

int main(void)
{
	FILE *fp;
	int i = 0, j, jj, n;
	char input[256], file[256];

	printf("\n\nThis program generates a script, which is used to start/stop/restart mISDN\n");
	printf("driver. Please select card only once. Mode and options are given by LCR.\n");

	while(1) {
		printf("\nSelect %sdriver for cards:\n\n", i?"another ":"");
		jj = 0;
		while(cards[jj].name) {
			printf(" (%d) %s\n", jj+1, cards[jj].name);
			jj++;
		}
		do {
			printf("\nSelect driver number[1-n] (or enter 'done'): "); fflush(stdout);
			scanf("%s", input);
		} while (atoi(input) <= 0 && !!strcmp(input, "done"));
		type[i] = atoi(input);
		i++;
		if (!strcmp(input, "done"))
			break;
	}

	if (!i) {
		printf("\nNo cards defined!\n");
		return(-1);
	}

	printf("\nEnter LAW audio mode. For a-LAW (default), just enter 0. For u-LAW enter 1.\n[0..n | 0xn]: "); fflush(stdout);
	scanf("%s", input);
	lawopt = strtoul(input, NULL, 0);
	printf("\nEnter debugging flags of mISDN core. For no debug, just enter 0.\n[0..n | 0xn]: "); fflush(stdout);
	scanf("%s", input);
	coredebug = strtoul(input, NULL, 0);
	printf("\nEnter debugging flags of cards. For no debug, just enter 0.\n[0..n | 0xn]: "); fflush(stdout);
	scanf("%s", input);
	carddebug = strtoul(input, NULL, 0);
	printf("\nEnter dsp debugging flags of driver. For no debug, just enter 0.\n[0..n | 0xn]: "); fflush(stdout);
	scanf("%s", input);
	dspdebug = strtoul(input, NULL, 0);

	n = i;

	printf("\nWhere do you like to load the modules from, enter 0 for default, 1 for\n'/usr/local/lcr/modules/' or the full path.\n[0 | 1 | <path>]: "); fflush(stdout);
	scanf("%s", input);
	if (!strcmp(input, "0"))
		SCPY(input, "");
	if (!strcmp(input, "1"))
		SCPY(input, "/usr/local/lcr/modules");
	if (input[0]) if (input[strlen(input)-1] != '/')
		SCAT(input, "/");

	printf("\n\nFinally tell me where to write the mISDN rc file.\nEnter the name 'mISDN' for current directory.\nYou may want to say '/usr/local/lcr/mISDN' or '/etc/rc.d/mISDN'\n: "); fflush(stdout);
	scanf("%s", file);
	if (!(fp=fopen(file, "w"))) {
		fprintf(stderr, "\nError: Failed to open '%s', try again.\n", file);
		exit(EXIT_FAILURE);
	}
	fprintf(fp, "# rc script for mISDN driver\n\n");
	fprintf(fp, "case \"$1\" in\n");
	fprintf(fp, "\tstart|--start)\n");
	fprintf(fp, "\t\t%s %smISDN_core%s debug=0x%x\n", input[0]?"insmod -f":"modprobe --ignore-install", input, input[0]?".ko":"", coredebug);
	fprintf(fp, "\t\t%s %smISDN_dsp%s debug=0x%x options=0x%x\n", input[0]?"insmod -f":"modprobe --ignore-install", input, input[0]?".ko":"", dspdebug, lawopt);
	j = 0;
	while(cards[j].name) {
		jj = 0;
		while (jj < n) {
			if (type[jj] == j+1)
				fprintf(fp, "\t\t%s %s%s%s debug=0x%x\n", input[0]?"insmod -f":"modprobe --ignore-install", input, cards[j].module, input[0]?".ko":"", carddebug);
			jj++;
		}
		j++;
	}
	fprintf(fp, "\t\tsleep 1\n");
	fprintf(fp, "\t\t;;\n\n");
	fprintf(fp, "\tstop|--stop)\n");
	while(j) {
		j--;
		jj = 0;
		while (jj < n) {
			if (type[jj] == j+1)
				fprintf(fp, "\t\trmmod %s\n", cards[j].module);
			jj++;
		}
	}
	fprintf(fp, "\t\trmmod mISDN_dsp\n");
	fprintf(fp, "\t\trmmod mISDN_core\n");
	fprintf(fp, "\t\t;;\n\n");
	fprintf(fp, "\trestart|--restart)\n");
	fprintf(fp, "\t\tsh $0 stop\n");
	fprintf(fp, "\t\tsleep 2 # some phones will release tei when layer 1 is down\n");
	fprintf(fp, "\t\tsh $0 start\n");
	fprintf(fp, "\t\t;;\n\n");
	fprintf(fp, "\thelp|--help)\n");
	fprintf(fp, "\t\techo \"Usage: $0 {start|stop|restart|help}\"\n");
	fprintf(fp, "\t\texit 0\n");
	fprintf(fp, "\t\t;;\n\n");
	fprintf(fp, "\t*)\n");
	fprintf(fp, "\t\techo \"Usage: $0 {start|stop|restart|help}\"\n");
	fprintf(fp, "\t\texit 2\n");
	fprintf(fp, "\t\t;;\n\n");
	fprintf(fp, "esac\n");
	fclose(fp);

	printf("\nFile '%s' is written to the current directory.\n", file);
}


