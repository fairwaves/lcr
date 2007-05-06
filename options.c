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
	"",				/* h323 endpoint name */
	0,				/* h323 ringconnect */
	0,4, 0,2, 0, 0, 0, 0,4, 0,4, 0,64, /* h323 codecs to use */
	0,"",1720,			/* allow incoming h323 calls */
	0,"",				/* register with h323 gatekeeper */
	5060, 5,			/* SIP port, maxqueue */
	0,				/* dtmf detection on */
	"",				/* dummy caller id */
	0,				/* inband patterns on external calls */
	0,				/* use tones by dsp.o */
	0,				/* by default use priority 0 */
	"pbx@jolly.de"			/* source mail adress */
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
#ifdef H323
	int codecpri = 0;
#endif

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
		if (!strcmp(option,"h323_name"))
		{
#ifdef H323
			SCPY(options.h323_name, param);

			PDEBUG(DEBUG_CONFIG, "H323 endpoint name: '%s'\n", param);
#endif
		} else
		if (!strcmp(option,"h323_ringconnect"))
		{
#ifdef H323
			options.h323_ringconnect = 1;

			PDEBUG(DEBUG_CONFIG, "H323 ringconnect: enabled\n");
#endif
		} else
		if (!strcmp(option,"h323_gsm"))
		{
#ifdef H323
			codecpri ++;
			options.h323_gsm_pri = codecpri;
			options.h323_gsm_opt = atoi(param);
			if (atoi(param)<1 && atoi(param)>7)
			{
				PERROR_RUNTIME("Error in %s (line %d): parameter for option %s must be in range 1..7.\n",filename,line,option);
				goto error;
			}

			PDEBUG(DEBUG_CONFIG, "H323 codec to use: GSM, MicrosoftGSM priority %d\n", codecpri);
#endif
		} else
		if (!strcmp(option,"h323_g726"))
		{
#ifdef H323
			codecpri ++;
			options.h323_g726_pri = codecpri;
			options.h323_g726_opt = atoi(param);
			if (atoi(param)<2 && atoi(param)>5)
			{
				PERROR_RUNTIME("Error in %s (line %d): parameter for option %s must be in range 2..5.\n",filename,line,option);
				goto error;
			}

			PDEBUG(DEBUG_CONFIG, "H323 codec to use: G726 priority %d\n", codecpri);
#endif
		} else
		if (!strcmp(option,"h323_g7231"))
		{
#ifdef H323
			codecpri ++;
			options.h323_g7231_pri = codecpri;

			PDEBUG(DEBUG_CONFIG, "H323 codec to use: G7231 priority %d\n", codecpri);
#endif
		} else
		if (!strcmp(option,"h323_g729a"))
		{
#ifdef H323
			codecpri ++;
			options.h323_g729a_pri = codecpri;

			PDEBUG(DEBUG_CONFIG, "H323 codec to use: G729A priority %d\n", codecpri);
#endif
		} else
		if (!strcmp(option,"h323_lpc10"))
		{
#ifdef H323
			codecpri ++;
			options.h323_lpc10_pri = codecpri;

			PDEBUG(DEBUG_CONFIG, "H323 codec to use: LPC-10 priority %d\n", codecpri);
#endif
		} else
		if (!strcmp(option,"h323_speex"))
		{
#ifdef H323
			codecpri ++;
			options.h323_speex_pri = codecpri;
			options.h323_speex_opt = atoi(param);
			if (atoi(param)<2 && atoi(param)>6)
			{
				PERROR_RUNTIME("Error in %s (line %d): parameter for option %s must be in range 2..6.\n",filename,line,option);
				goto error;
			}

			PDEBUG(DEBUG_CONFIG, "H323 codec to use: Speex priority %d\n", codecpri);
#endif
		} else
		if (!strcmp(option,"h323_xspeex"))
		{
#ifdef H323
			codecpri ++;
			options.h323_xspeex_pri = codecpri;
			options.h323_xspeex_opt = atoi(param);
			if (atoi(param)<2 && atoi(param)>6)
			{
				PERROR_RUNTIME("Error in %s (line %d): parameter for option %s must be in range 2..6.\n",filename,line,option);
				goto error;
			}

			PDEBUG(DEBUG_CONFIG, "H323 codec to use: XiphSpeex priority %d\n", codecpri);
#endif
		} else
		if (!strcmp(option,"h323_law"))
		{
#ifdef H323
			codecpri ++;
			options.h323_law_pri = codecpri;
			options.h323_law_opt = atoi(param);
			if (atoi(param)<10 && atoi(param)>240)
			{
				PERROR_RUNTIME("Error in %s (line %d): parameter for option %s must be in range 10..240.\n",filename,line,option);
				goto error;
			}

			PDEBUG(DEBUG_CONFIG, "H323 codec to use: Alaw, muLaw priority %d\n", codecpri);
#endif
		} else
		if (!strcmp(option,"h323_icall"))
		{
#ifdef H323
			options.h323_icall = 1;
			SCPY(options.h323_icall_prefix, param);

			PDEBUG(DEBUG_CONFIG, "process incoming H323 call with prefix '%s'\n", param);
#endif
		} else
		if (!strcmp(option,"h323_port"))
		{
#ifdef H323
			options.h323_port = atoi(param);

			PDEBUG(DEBUG_CONFIG, "use port for incoming H323 calls: %d\n", atoi(param));
#endif
		} else
		if (!strcmp(option,"sip_port"))
		{
#ifdef SIP
			options.sip_port = atoi(param);

			PDEBUG(DEBUG_CONFIG, "use port for incoming SIP calls: %d\n", atoi(param));
#endif
		} else
		if (!strcmp(option,"sip_maxqueue"))
		{
#ifdef SIP
			options.sip_maxqueue = atoi(param);

			PDEBUG(DEBUG_CONFIG, "number of simultanious incoming sockets for SIP calls: %d\n", atoi(param));
#endif
		} else
		if (!strcmp(option,"h323_gatekeeper"))
		{
#ifdef H323
			options.h323_gatekeeper = 1;
			if (param[0])
			{
				SCPY(options.h323_gatekeeper_host, param);
			}
			PDEBUG(DEBUG_CONFIG, "register with H323 gatekeeper (%s)\n", (param[0])?param:"automatically");
#endif
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
		if (!strcmp(option,"inbandpattern"))
		{
			if (!strcasecmp(param, "yes"))
				options.inbandpattern = 1;

			PDEBUG(DEBUG_CONFIG, "inband pattern = %s\n", (options.inbandpattern)?"yes":"no");
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


