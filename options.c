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

#include "stdio.h"
#include "string.h"
#include "stdarg.h"
#include "unistd.h"
#include "stdlib.h"
#include "macro.h"
#include "extension.h"
#include "options.h"

struct options options = {
	"/usr/local/lcr/log",		/* log file */
	0x0000,				/* debug mode */
	'a',				/* a-law */
	"0",				/* national prefix */
	"00",				/* international prefix */
	"tones_american",		/* directory of tones */
	"",				/* directories of tones to fetch */
	"",				/* dummy caller id */
	0,				/* use tones by dsp.o */
	0,				/* by default use priority 0 */
	"lcr@your.machine",		/* source mail adress */
	"/var/tmp",			/* path of lock files */
	0700				/* rights of lcr admin socket */
};

char options_error[256];

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

	SPRINT(filename, "%s/options.conf", CONFIG_DATA);

	if (!(fp=fopen(filename,"r")))
	{
		SPRINT(options_error, "Cannot open %s\n",filename);
		return(0);
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
				SPRINT(options_error, "Error in %s (line %d): option too long.\n",filename,line);
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
					SPRINT(options_error, "Error in %s (line %d): param too long.\n",filename,line);
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
			SPRINT(options_error, "Error in %s (line %d): obsolete option %s. Use multiple 'port' options to define ports to use.\n",filename,line,option);
			goto error;
		} else
		if (!strcmp(option,"debug"))
		{
			if (param[0]==0)
			{
				SPRINT(options_error, "Error in %s (line %d): parameter for option %s missing.\n",filename,line,option);
				goto error;
			}
			options.deb = strtol(param, NULL, 0);

		} else
		if (!strcmp(option,"log"))
		{
			if (param[0]==0)
			{
				SPRINT(options_error, "Error in %s (line %d): parameter for option %s missing.\n",filename,line, option);
				goto error;
			}
			SCPY(options.log, param);

		} else
		if (!strcmp(option,"alaw"))
		{
			options.law = 'a';

		} else
		if (!strcmp(option,"ulaw"))
		{
			options.law = 'u';

		} else
		if (!strcmp(option,"tones_dir"))
		{
			if (param[0]==0)
			{
				SPRINT(options_error, "Error in %s (line %d): parameter for option %s missing.\n",filename,line,option);
				goto error;
			}
			if (param[strlen(param)-1] == '/')
				param[strlen(param)-1]=0;
			SCPY(options.tones_dir, param);

		} else
		if (!strcmp(option,"fetch_tones"))
		{
			if (param[0]==0)
			{
				SPRINT(options_error, "Error in %s (line %d): parameter for option %s missing.\n",filename,line,option);
				goto error;
			}
			if (param[strlen(param)-1] == '/')
				param[strlen(param)-1]=0;
			SCPY(options.fetch_tones, param);

		} else
		if (!strcmp(option,"extensions_dir"))
		{
			// obsolete
		} else
		if (!strcmp(option,"national"))
		{
			SCPY(options.national, param);

		} else
		if (!strcmp(option,"international"))
		{
			SCPY(options.international, param);

		} else
		if (!strcmp(option,"dummyid"))
		{
			SCPY(options.dummyid, param);

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
				SPRINT(options_error, "Error in %s (line %d): parameter for option %s missing.\n",filename,line,option);
				goto error;
			}

		} else
		if (!strcmp(option,"schedule"))
		{
			options.schedule = atoi(param);
			if (options.schedule < 0)
			{
				SPRINT(options_error, "Error in %s (line %d): parameter for option %s must be at least '0'.\n", filename,line,option);
				goto error;
			}
			if (options.schedule > 99)
			{
				SPRINT(options_error, "Error in %s (line %d): parameter for option %s must be '99' or less.\n", filename,line,option);
				goto error;
			}

		} else
		if (!strcmp(option,"email"))
		{
			if (param[0]==0)
			{
				SPRINT(options_error, "Error in %s (line %d): parameter for option %s missing.\n", filename,line,option);
				goto error;
			}
			SCPY(options.email, param);

		} else
		if (!strcmp(option,"lock"))
		{
			if (param[0]==0)
			{
				SPRINT(options_error, "Error in %s (line %d): parameter for option %s missing.\n",filename,line,option);
				goto error;
			}
			if (param[strlen(param)-1] == '/')
				param[strlen(param)-1]=0;
			SCPY(options.lock, param);

		} else
		if (!strcmp(option,"socketrights"))
		{
			options.socketrights = strtol(param, NULL, 0);
		} else
		{
			SPRINT(options_error, "Error in %s (line %d): wrong option keyword %s.\n", filename,line,option);
			goto error;
		}
	}

#if 0
	if (!options.dsptones)
	{
		SPRINT(options_error, "Error in %s (line %d): option 'dsptones' missing.\n", filename);
		goto error;
	}
#endif
	if (!options.tones_dir[0])
	{
		SPRINT(options_error, "Error in %s (line %d): option 'tones_dir' with parameter missing.\n", filename);
		goto error;
	}
	if (fp) fclose(fp);
	return(1);
error:
	if (fp) fclose(fp);
	return(0);
}


