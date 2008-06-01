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

char mode[256];
int type[256];
int ptp[256];
int port[256];
char name[256];
int coredebug=0, carddebug=0, l1debug=0, l2debug=0, l3debug=0, dspdebug=0;
int lawopt=0;

static struct cards {
	char *card;
	char *module;
	int ntmode;
	int isac;
	int ports;
} cards[] = {
//	{ "AVM Fritz PCI (PNP)", "avmfritz", 0, 1, 1},
	{ "HFC PCI (Cologne Chip)", "hfcpci", 1, 0, 1},
	{ "HFC-4S 4 S/T Ports (Cologne Chip)", "hfcmulti", 1, 0, 4},
	{ "HFC-8S 8 S/T Ports (Cologne Chip)", "hfcmulti", 1, 0, 8},
	{ "HFC-E1 1 E1 Port (Cologne Chip)", "hfcmulti", 1, 0, 1},
	{ "HFC-S USB (Cologne Chip)", "hfcsusb", 1, 0, 1},
//	{ "HFC-S MINI (Cologne Chip)", "hfcsmini", 1, 0, 1},
//	{ "XHFC (Cologne Chip)", "xhfc", 1, 0, 1},
//	{ "Sedlbaur FAX", "sedlfax", 0, 1, 1},
//	{ "Winbond 6692 PCI", "w6692pci", 0, 0, 1},
	{ NULL, NULL, 0, 0},
};

int main(void)
{
	FILE *fp;
	int i = 0, j, jj, n, anyte = 0, remove_isac;
	char input[256];
	char protocol[1024], layermask[1024], types[256];

	printf("This program is outdated and requires update to mISDN V2 API\n");
	return (0);

	printf("\n\nThis program generates a script, which is used to start/stop/restart mISDN\n");
	printf("driver. All configuration of cards is done for using with the LCR.\n");

	while(i < (int)sizeof(mode)) /* number of cards */
	{
		moreports:
		do
		{
			printf("\nEnter mode of isdn port #%d. Should it run in NT-mode (for internal\nphones), or in TE-mode (for external lines)? If you do not like to add more\ncards, say 'done'.\n[nt | te | done]: ", i+1); fflush(stdout);
			scanf("%s", input);
		} while (input[0]!='t' && input[0]!='n' && input[0]!='d');
		mode[i] = input[0];
		if (mode[i] == 'd')
			break;
		ptp[i] = 0;
		do
		{
			printf("\nIs your port #%d connected to point-to-multipoint line/phone, which supports multiple\ntelephones (Mehrgeräteanschluss) OR is it a point-to-point link which is used\nfor LCR and supports extension dialing (Anlagenanschluss)?\n[ptp | ptm]: ", i+1); fflush(stdout);
			scanf("%s", input);
		} while (!!strcmp(input,"ptp") && !!strcmp(input,"ptm"));
		ptp[i] = (input[2]=='p')?1:0;
		anyte = 1;

		if (!i)
		{
			askcard:
			do
			{
				printf("\nSelect driver of ISDN port #%d.\n\n", i+1);
				jj = 0;
				while(cards[jj].card)
				{
					if (cards[jj].ntmode || mode[i]!='n')
						printf(" (%d) %s\n", jj+1, cards[jj].card);
					jj++;
				}
				printf("\n%sSelect card number[1-n]: ", (mode[i]=='n')?"Your card will run in NT-mode. The shown cards are capable of providing\nhardware layer for NT-mode.\n":""); fflush(stdout);
				scanf("%s", input);
			} while (atoi(input) <= 0);
			type[i] = atoi(input);
			port[i] = 1;
			j = 0;
			while(j < jj)
			{
				if (j+1==type[i] && (cards[j].ntmode || mode[i]!='n'))
					break;
				j++;
			}
			if (j == jj)
			{
				printf("\n\nWrong selection, please try again.\n");
				goto askcard;
			}
		} else
		if (cards[type[i-1]-1].ports == port[i-1])
			goto askcard;
		else {
			type[i] = type[i-1];
			port[i] = port[i-1]+1;
			printf("\nUsing port %d of card '%s'.", port[i], cards[type[i]-1].card);
		}

		printf("\n\n\nSummary: Port #%d of type %s will run in %s-mode and %s-mode.\n", i+1, cards[type[i]-1].card, (mode[i]=='n')?"NT":"TE", (ptp[i])?"point-to-point":"point-to-multipoint");

		i++;
	}

	if (!i)
	{
		printf("\nNo ports/cards defined!\n");
		return(-1);
	}
	if (cards[type[i-1]-1].ports > port[i-1])
	{
		printf("\nNot all ports for the last card are defined. Please do that even if they will be not\nused! Select 'NT-mode' for these unused ports.\n");
		goto moreports;
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
	printf("\nEnter l1 debugging flags of driver. For no debug, just enter 0.\n[0..n | 0xn]: "); fflush(stdout);
	scanf("%s", input);
	l1debug = strtoul(input, NULL, 0);
	printf("\nEnter l2 debugging flags of driver. For no debug, just enter 0.\n[0..n | 0xn]: "); fflush(stdout);
	scanf("%s", input);
	l2debug = strtoul(input, NULL, 0);
	printf("\nEnter l3 debugging flags of driver. For no debug, just enter 0.\n[0..n | 0xn]: "); fflush(stdout);
	scanf("%s", input);
	l3debug = strtoul(input, NULL, 0);
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

	printf("\n\nFinally tell me where to write the mISDN rc file.\Enter the name 'mISDN' for current directory.\nYou may want to say '/usr/local/lcr/mISDN' or '/etc/rc.d/mISDN'\n: "); fflush(stdout);
	scanf("%s", name);
	if (!(fp=fopen(name, "w")))
	{
		fprintf(stderr, "\nError: Failed to open '%s', try again.\n", name);
		exit(EXIT_FAILURE);
	}
	fprintf(fp, "# rc script for mISDN driver\n\n");
	fprintf(fp, "case \"$1\" in\n");
	fprintf(fp, "\tstart|--start)\n");
	fprintf(fp, "\t\t%s %smISDN_core%s debug=0x%x\n", input[0]?"insmod -f":"modprobe --ignore-install", input, input[0]?".ko":"", coredebug);
	if (anyte)
	{
		fprintf(fp, "\t\t%s %smISDN_l1%s debug=0x%x\n", input[0]?"insmod -f":"modprobe --ignore-install", input, input[0]?".ko":"", l1debug);
		fprintf(fp, "\t\t%s %smISDN_l2%s debug=0x%x\n", input[0]?"insmod -f":"modprobe --ignore-install", input, input[0]?".ko":"", l2debug);
		fprintf(fp, "\t\t%s %sl3udss1%s debug=0x%x\n", input[0]?"insmod -f":"modprobe --ignore-install", input, input[0]?".ko":"", l3debug);
	}
	fprintf(fp, "\t\t%s %smISDN_dsp%s debug=0x%x options=0x%x\n", input[0]?"insmod -f":"modprobe --ignore-install", input, input[0]?".ko":"", dspdebug, lawopt);
	j = 0;
	while(cards[j].card)
	{
		protocol[0] = layermask[0] = types[0] = '\0';
		i = 0;
		while(i < n)
		{
			if (j+1 == type[i])
			{
				if (!strcmp(cards[j].module, "hfcmulti") && port[i]==1)
					UPRINT(strchr(types,'\0'), "0x%x,", cards[j].ports+((lawopt)?0x100:0)+0x200);
				UPRINT(strchr(protocol,'\0'), "0x%x,", ((mode[i]=='n')?0x12:0x2) + (ptp[i]?0x20:0x0));
				UPRINT(strchr(layermask,'\0'), "0x%x,", (mode[i]=='n')?0x3:0x0f);
			}
			i++;
		}
		if (protocol[0])
		{
			protocol[strlen(protocol)-1] = '\0';
			layermask[strlen(layermask)-1] = '\0';
			if (types[0])
			{
				types[strlen(types)-1] = '\0';
				fprintf(fp, "\t\t%s %s%s%s type=%s protocol=%s layermask=%s debug=0x%x\n", input[0]?"insmod -f":"modprobe --ignore-install", input, cards[j].module, input[0]?".ko":"", types, protocol, layermask, carddebug);
			} else
				fprintf(fp, "\t\t%s %s%s%s protocol=%s layermask=%s debug=0x%x\n", input[0]?"insmod -f":"modprobe --ignore-install", input, cards[j].module, input[0]?".ko":"", protocol, layermask, carddebug);
		}
		j++;
	}
	fprintf(fp, "\t\tsleep 1\n");
	fprintf(fp, "\t\t;;\n\n");
	fprintf(fp, "\tstop|--stop)\n");
	remove_isac = 0;
	j = 0;
	while(cards[j].card)
	{
		protocol[0] = 0;
		i = 0;
		while(i < n)
		{
			if (j+1 == type[i])
			{
				protocol[0] = 1;
			}
			i++;
		}
		if (protocol[0])
		{
			fprintf(fp, "\t\trmmod %s\n", cards[j].module);
			if (cards[j].isac)
				remove_isac = 1;
		}
		j++;
	}
	if (remove_isac)
	{
		fprintf(fp, "\t\trmmod mISDN_isac\n");
	}
	fprintf(fp, "\t\trmmod mISDN_dsp\n");
	if (anyte)
	{
		fprintf(fp, "\t\trmmod l3udss1\n");
		fprintf(fp, "\t\trmmod mISDN_l2\n");
		fprintf(fp, "\t\trmmod mISDN_l1\n");
	}
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

	printf("\nFile '%s' is written to the current directory.\n", name);
}


