/*****************************************************************************\
**                                                                           **
** LCR                                                                       **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** generate extension                                                        **
**                                                                           **
\*****************************************************************************/ 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include "main.h"

int memuse = 0;
int mmemuse = 0;
int cmemuse = 0;
int ememuse = 0;
int pmemuse = 0;
int classuse = 0;
int fduse = 0;
int fhuse = 0;
struct isdn_cause isdn_cause[128];
struct isdn_location isdn_location[16];

void _printdebug(const char *function, int line, unsigned int mask, const char *fmt, ...)
{
}

void _printerror(const char *function, int line, const char *fmt, ...)
{
	char buffer[4096];
	va_list args;

	va_start(args,fmt);
	VUNPRINT(buffer,sizeof(buffer)-1,fmt,args);
	buffer[sizeof(buffer)-1]=0;
	va_end(args);

	fprintf(stderr, "%s", buffer);
}

int main(int argc, char *argv[])
{
	struct extension ext;
	char pathname[256];
	FILE *fp;

	if (!read_options()) {
		PERROR("%s", options_error);
		return(-1);
	}

	if (argc != 4) {
		printf("Usage: %s <extension> <interfaces> <callerid>\n\n", argv[0]);
		printf("extension: any number for the extension (e.g 200)\n");
		printf("interfaces: internal interface(s) to reach extension, NOT port numbers\n");
		printf(" -> seperate multiple interfaces with commas. e.g Int1,Int2\n");
		printf("callerid: normal undefined called is (use what your telco assigned you)\n");
		return(0);
	}

	SPRINT(pathname, "%s/%s", EXTENSION_DATA, argv[1]);
	if (mkdir(pathname, 0755) < 0) {
		if (errno == EEXIST)
			PERROR("Extension's directory already exists. Nothing done!\n");
		else	PERROR("Cannot open extension's directory '%s'.\n", pathname);
		return(-1);
	}

	memset(&ext, 0, sizeof(ext));
	ext.rights = 4;
	ext.cfnr_delay = 20;
	ext.vbox_codec = CODEC_MONO;
	UCPY(ext.interfaces, argv[2]);
	UCPY(ext.callerid, argv[3]);
	ext.callerid_present = INFO_PRESENT_ALLOWED;
	ext.callerid_type = INFO_NTYPE_UNKNOWN;
	ext.change_forward = 1;
	ext.facility = 1;
	write_extension(&ext, argv[1]);

	SPRINT(pathname, "%s/%s/phonebook", EXTENSION_DATA, argv[1]);
	if (!(fp = fopen(pathname, "w"))) {
		PERROR("Failed to write phonebook example '%s'.\n", pathname);
		return(-1);
	} else {
		fprintf(fp, "# fromat: <shortcut> <phone number> [<Name>]\n");
		fprintf(fp, "# The shotcut may have any number of digits. \n");
		fprintf(fp, "# The phone number must include the dialing code for external, internal or\n");
		fprintf(fp, "# other type of dialing. \n");
		fprintf(fp, "# The name must not be in quotes. All 2 or 3 attributes must be seperated by\n");
		fprintf(fp, "# white space(s) and/or tab(s)\n");
		fprintf(fp, "# Empty lines and lines starting with '#' will be ignored.\n");
		fprintf(fp, "\n");
		fprintf(fp, "0   008003301000             German Telekom Service\n");
		fprintf(fp, "10  011880                   Directory Service Telegate\n");
		fprintf(fp, "11  011833                   Directory Service DTAG\n");
		fprintf(fp, "12  011811                   Directory Service Fred\n");
		fclose(fp);
	}

	SPRINT(pathname, "%s/%s/secrets", EXTENSION_DATA, argv[1]);
	if (!(fp = fopen(pathname, "w"))) {
		PERROR("Failed to write secrets example '%s'.\n", pathname);
		return(-1);
	} else {
		fprintf(fp, "# Format: <remote number> <key exchange> <cypher> [<key>]\n");
		fprintf(fp, "# The remote number must match the dialed number for outgoing calls.\n");
		fprintf(fp, "# The remote number must match the caller id for incoming calls.\n");
		fprintf(fp, "# The caller id must include the prefix digits as received.\n");
		fprintf(fp, "# The key exchange method must be given: e.g 'manual'\n");
		fprintf(fp, "# The cypher method must be given: e.g 'blowfish'\n");
		fprintf(fp, "# The key must be a string of characters (ASCII) or 0xXXXXXX...\n");
		fprintf(fp, "# All 2 or 3 attributes must be seperated by white space(s) and/or tab(s)\n");
		fprintf(fp, "# Empty lines and lines starting with '#' will be ignored.\n\n");
		fprintf(fp, "###############################################################################\n");
		fprintf(fp, "##       REFER TO THE DOCUMENTATION FOR DETAILS ON ENCRYPTION AND KEYS!      ##\n");
		fprintf(fp, "###############################################################################\n");
		fprintf(fp, "\n");
		fprintf(fp, "# This examples explains the format, NEVER USE IT, it would be dumb!\n");
		fprintf(fp, "021250993               manual  blowfish        0x012345678\n");
		fclose(fp);
	}
	printf("Extension %s created at %s/%s/.\n", argv[1], EXTENSION_DATA, argv[1]);

	return(0);
}
