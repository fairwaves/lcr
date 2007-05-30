/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** reading options.conf and filling structure                                **
**                                                                           **
\*****************************************************************************/ 

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "main.h"

struct options options = {
	"/usr/local/pbx/log",		/* log file */
	{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
	0x0000,				/* debug mode */
	'a',				/* a-law */
	"0",				/* national prefix */
	"00",				/* international prefix */
	"tones_american",		/* directory of tones */
	"",				/* directories of tones to fetch */
	"extensions",			/* directory of extensions */
	0,				/* dtmf detection on */
	"",				/* dummy caller id */
	0,				/* use tones by dsp.o */
	0,				/* by default use priority 0 */
	"lcr@your.machine"		/* source mail adress */
};

/* read options
 *
 * read options from options.conf
 */
int read_options(void)
{
	FILE *fp=NULL;
	char filename[128];
	char *p;
	char option[32];
	char param[256];
	unsigned int line,i;
	char buffer[256];

	SPRINT(filename, "%s/options.conf", INSTALL_DATA);

	if (!(fp=fopen(filename,"r")))
	{
		PERROR("Cannot open %s\n",filename);
		return(-1);
	}

	line=0;
	while((fgets(buffer,sizeof(buffer),fp)))
	{
		line++;
		buffer[sizeof(buffer)-1]=0;
		if (buffer[0]) buffer[strlen(buffer)-1]=0;
		p=buffer;

		while(*p <= 32) /* skip spaces */
		{
			if (*p == 0)
				break;
			p++;
		}
		if (*p==0 || *p=='#') /* ignore comments and empty line */
			continue;

		option[0]=0;
		i=0; /* read option */
		while(*p > 32)
		{
			if (i+1 >= sizeof(option))
			{
				PERROR_RUNTIME("Error in %s (line %d): option too long.\n",filename,line);
				goto error;
			}
			option[i+1] = '\0';
			option[i++] = *p++;
		}

		while(*p <= 32) /* skip spaces */
		{
			if (*p == 0)
				break;
			p++;
		}

		param[0]=0;
		if (*p!=0 && *p!='#') /* param */
		{
			i=0; /* read param */
			while(*p > 31)
			{
				if (i+1 >= sizeof(param))
				{
					PERROR_RUNTIME("Error in %s (line %d): param too long.\n",filename,line);
					goto error;
				}
				param[i+1] = '\0';
				param[i++] = *p++;
			}
		}

		/* at this point we have option and param */

		/* check option */
		if (!strcmp(option,"nt_if") || !strcmp(option,"te_if"))
		{
			PERROR_RUNTIME("Error in %s (line %d): obsolete option %s. Use multiple 'port' options to define ports to use.\n",filename,line,option);
			goto error;
		} else
		if (!strcmp(option,"debug"))
		{
			if (param[0]==0)
			{
				PERROR_RUNTIME("Error in %s (line %d): parameter for option %s missing.\n",filename,line,option);
				goto error;
			}
			options.deb = strtol(param, NULL, 0);

			PDEBUG(DEBUG_CONFIG, "debugging: 0x%x\n", options.deb);
		} else
		if (!strcmp(option,"log"))
		{
			if (param[0]==0)
			{
				PERROR_RUNTIME("Error in %s (line %d): parameter for option %s missing.\n",filename,line, option);
				goto error;
			}
			SCPY(options.log, param);

			PDEBUG(DEBUG_CONFIG, "log file: %s\n", options.log);
		} else
		if (!strcmp(option,"port"))
		{
			i = strtol(param, NULL, 0);
			if (i < 1 || i > sizeof(options.ports))
			{
				PERROR_RUNTIME("Error in %s (line %d): port number %s out of range.\n", filename, line, option);
				goto error;
			}
			options.ports[i] |= FLAG_PORT_USE;

			PDEBUG(DEBUG_CONFIG, "adding interface: %d (param=%s)\n", i, param);
			if (strstr(param, "ptp"))
			{
				options.ports[i] |= FLAG_PORT_PTP;
				PDEBUG(DEBUG_CONFIG, " -> interface shall be ptp\n");
			}
		} else
#if 0
		if (!strcmp(option,"ptp"))
		{
			options.ptp = 1;

			PDEBUG(DEBUG_CONFIG, "ptp layer-2 watch and keep established.\n");
		} else
#endif
		if (!strcmp(option,"alaw"))
		{
			options.law = 'a';

			PDEBUG(DEBUG_CONFIG, "isdn audio type: alaw\n");
		} else
		if (!strcmp(option,"ulaw"))
		{
			options.law = 'u';

			PDEBUG(DEBUG_CONFIG, "isdn audio type: ulaw\n");
		} else
		if (!strcmp(option,"tones_dir"))
		{
			if (param[0]==0)
			{
				PERROR_RUNTIME("Error in %s (line %d): parameter for option %s missing.\n",filename,line,option);
				goto error;
			}
			if (param[strlen(param)-1] == '/')
				param[strlen(param)-1]=0;
			SCPY(options.tones_dir, param);

			PDEBUG(DEBUG_CONFIG, "directory of tones: %s\n",param);
		} else
		if (!strcmp(option,"fetch_tones"))
		{
			if (param[0]==0)
			{
				PERROR_RUNTIME("Error in %s (line %d): parameter for option %s missing.\n",filename,line,option);
				goto error;
			}
			if (param[strlen(param)-1] == '/')
				param[strlen(param)-1]=0;
			SCPY(options.fetch_tones, param);

			PDEBUG(DEBUG_CONFIG, "directories of tones to fetch: %s\n",param);
		} else
		if (!strcmp(option,"extensions_dir"))
		{
			if (param[0]==0)
			{
				PERROR_RUNTIME("Error in %s (line %d): parameter for option %s missing.\n",filename,line,option);
				goto error;
			}
			if (param[strlen(param)-1] == '/')
				param[strlen(param)-1]=0;
			SCPY(options.extensions_dir, param);

			PDEBUG(DEBUG_CONFIG, "directory of extensions: %s\n",param);
		} else
		if (!strcmp(option,"national"))
		{
			SCPY(options.national, param);

			PDEBUG(DEBUG_CONFIG, "national dial prefix: %s\n", param);
		} else
		if (!strcmp(option,"international"))
		{
			SCPY(options.international, param);

			PDEBUG(DEBUG_CONFIG, "inernational dial prefix: %s\n", param);
		} else
		if (!strcmp(option,"nodtmf"))
		{
			options.nodtmf = 1;

			PDEBUG(DEBUG_CONFIG, "disable dtmf detection\n");
		} else
		if (!strcmp(option,"dummyid"))
		{
			SCPY(options.dummyid, param);

			PDEBUG(DEBUG_CONFIG, "dummy caller id\n", param);
		} else
		if (!strcmp(option,"dsptones"))
		{
			if (!strcasecmp(param, "american"))
				options.dsptones = DSP_AMERICAN;
			else if (!strcasecmp(param, "german"))
				options.dsptones = DSP_GERMAN;
			else if (!strcasecmp(param, "oldgerman"))
				options.dsptones = DSP_OLDGERMAN;
			else if (!strcasecmp(param, "none"))
				options.dsptones = DSP_NONE;
			else {
				PERROR_RUNTIME("Error in %s (line %d): parameter for option %s missing.\n",filename,line,option);
				goto error;
			}

			PDEBUG(DEBUG_CONFIG, "dsp tones = %d\n", options.dsptones);
		} else
		if (!strcmp(option,"schedule"))
		{
			options.schedule = atoi(param);
			if (options.schedule < 0)
			{
				PERROR_RUNTIME("Error in %s (line %d): parameter for option %s must be at least '0'.\n", filename,line,option);
				goto error;
			}
			if (options.schedule > 99)
			{
				PERROR_RUNTIME("Error in %s (line %d): parameter for option %s must be '99' or less.\n", filename,line,option);
				goto error;
			}

			if (atoi(param))
				PDEBUG(DEBUG_CONFIG, "use real time scheduler priority: %d\n", atoi(param));
			else
				PDEBUG(DEBUG_CONFIG, "don't use real time scheduler\n");
		} else
		if (!strcmp(option,"email"))
		{
			if (param[0]==0)
			{
				PERROR_RUNTIME("Error in %s (line %d): parameter for option %s missing.\n", filename,line,option);
				goto error;
			}
			SCPY(options.email, param);

			PDEBUG(DEBUG_CONFIG, "source mail address of pbx: %s\n", param);
		} else
		{
			PERROR_RUNTIME("Error in %s (line %d): wrong option keyword %s.\n", filename,line,option);
			goto error;
		}
	}

#if 0
	if (!options.dsptones)
	{
		PERROR_RUNTIME("Error in %s (line %d): option 'dsptones' missing.\n", filename);
		goto error;
	}
#endif
	if (!options.tones_dir[0])
	{
		PERROR_RUNTIME("Error in %s (line %d): option 'tones_dir' with parameter missing.\n", filename);
		goto error;
	}
	if (fp) fclose(fp);
	return(1);
error:
	if (fp) fclose(fp);
	return(0);
}


