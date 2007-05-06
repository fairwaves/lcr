/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** parse h323 gateway config file                                            **
**                                                                           **
\*****************************************************************************/ 

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "main.h"

/* parse h323_gateway.conf
 *
 * searches for the given ip and returns the extension or NULL if not found
 */
char *parse_h323gateway(char *ip, char *opt, int opt_size)
{
	FILE *fp=NULL;
	char filename[256];
	char *p;
	unsigned int line,i;
	char buffer[256];
	static char host_ip[32], extension[32], option[64];
	int found = 0;

	SPRINT(filename, "%s/h323_gateway.conf", INSTALL_DATA);

	if (!(fp = fopen(filename, "r")))
	{
		PERROR("Cannot open h323 gateway map: \"%s\"\n", filename);
		return(0);
	}

	line=0;
	while((fgets(buffer, sizeof(buffer), fp)))
	{
		line++;
		buffer[sizeof(buffer)-1] = '\0';
		if (buffer[0]) buffer[strlen(buffer)-1] = '\0';
		p = buffer;

		while(*p <= 32) /* skip spaces */
		{
			if (*p == 0)
				break;
			p++;
		}
		if (*p==0 || *p=='#') /* ignore comments and empty line */
			continue;

		host_ip[0]=0;
		extension[0]=0;
		option[0]=0;

		i=0; /* read host ip */
		while(*p > 32)
		{
			if (i+1 >= sizeof(host_ip))
			{
				PERROR_RUNTIME("Error in %s (line %d): ip too long.\n",filename,line);
				break;
			}
			host_ip[i+1] = '\0';
			host_ip[i++] = *p++;
		}

		while(*p <= 32) /* skip spaces */
		{
			if (*p == 0)
				break;
			p++;
		}

		if (*p!=0 && *p!='#') /* extension */
		{
			i=0; /* read extension */
			while(*p > 32)
			{
				if (i+1 >= sizeof(extension))
				{
					PERROR_RUNTIME("Error in %s (line %d): extension too long.\n",filename,line);
					break;
				}
				extension[i+1] = '\0';
				extension[i++] = *p++;
			}
			while(*p <= 32) /* skip spaces */
			{
				if (*p == 0)
					break;
				p++;
			}
		}

		if (*p!=0 && *p!='#') /* option */
		{
			i=0; /* read option */
			while(*p > 32)
			{
				if (i+1 >= sizeof(option))
				{
					PERROR_RUNTIME("Error in %s (line %d): option too long.\n",filename,line);
					break;
				}
				option[i+1] = '\0';
				option[i++] = *p++;
			}
			// ignoring more
		}

		if (!!strcasecmp(ip, host_ip))
			continue;

		if (extension[0] == '\0')
			continue;

		found = 1;
		break; /* found entry */
	}

	if (fp) fclose(fp);

	if (found)
	{
		UNCPY(opt, option, opt_size-1);
		opt[opt_size-1] = '\0';
		return(extension);
	}
	return(0);
}


