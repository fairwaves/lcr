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

#include "main.h"


char *gsm_conf_error = (char *)"";

/* read options
 *
 * read options from options.conf
 */
int gsm_conf(struct gsm_conf *gsm_conf)
{
	FILE *fp=NULL;
	char filename[128];
	char *p;
	char option[32];
	char params[11][256];
	int pnum;
	unsigned int line,i;
	char buffer[256];

	/* set defaults */
	SCPY(gsm_conf->debug, "");
	SCPY(gsm_conf->interface_bsc, "mISDN_l1loop.1");
	SCPY(gsm_conf->interface_lcr, "mISDN_l1loop.2");
	SCPY(gsm_conf->hlr, "hlr.sqlite3");
	SCPY(gsm_conf->openbsc_cfg, "openbsc.cfg");
	gsm_conf->reject_cause = 0;
	gsm_conf->keep_l2 = 0;
	gsm_conf->noemergshut = 0;

	SPRINT(filename, "%s/gsm.conf", CONFIG_DATA);

	if (!(fp=fopen(filename,"r"))) {
		SPRINT(gsm_conf_error, "Cannot open %s\n",filename);
		return(0);
	}

	line=0;
	while((fgets(buffer,sizeof(buffer),fp))) {
		line++;
		buffer[sizeof(buffer)-1]=0;
		if (buffer[0]) buffer[strlen(buffer)-1]=0;
		p=buffer;

		while(*p <= 32) { /* skip spaces */
			if (*p == 0)
				break;
			p++;
		}
		if (*p==0 || *p=='#') /* ignore comments and empty line */
			continue;

		option[0]=0;
		i=0; /* read option */
		while(*p > 32) {
			if (i+1 >= sizeof(option)) {
				SPRINT(gsm_conf_error, "Error in %s (line %d): option too long.\n",filename,line);
				goto error;
			}
			option[i+1] = '\0';
			option[i++] = *p++;
		}

		while(*p <= 32) { /* skip spaces */
			if (*p == 0)
				break;
			p++;
		}

		params[0][0] = 0;
		pnum = 0;
		while(*p!=0 && *p!='#' && pnum < 10) { /* param */
			i=0; /* read param */
			while(*p > 32) {
				if (i+1 >= sizeof(params[pnum])) {
					SPRINT(gsm_conf_error, "Error in %s (line %d): param too long.\n",filename,line);
					goto error;
				}
				params[pnum][i+1] = '\0';
				params[pnum][i++] = *p++;
			}
			while(*p <= 32) { /* skip spaces */
				if (*p == 0)
					break;
				p++;
			}
			pnum++;
			params[pnum][0] = 0;
		}

		/* at this point we have option and param */

		/* check option */
		if (!strcmp(option,"debug")) {
			if (params[0][0]==0) {
				SPRINT(gsm_conf_error, "Error in %s (line %d): parameter for option %s missing.\n",filename,line,option);
				goto error;
			}
			SCPY(gsm_conf->debug, params[0]);

		} else
		if (!strcmp(option,"interface-bsc")) {
			if (params[0][0]==0) {
				SPRINT(gsm_conf_error, "Error in %s (line %d): parameter for option %s missing.\n",filename,line, option);
				goto error;
			}
			SCPY(gsm_conf->interface_bsc, params[0]);

		} else
		if (!strcmp(option,"interface-lcr")) {
			if (params[0][0]==0) {
				SPRINT(gsm_conf_error, "Error in %s (line %d): parameter for option %s missing.\n",filename,line, option);
				goto error;
			}
			SCPY(gsm_conf->interface_lcr, params[0]);

		} else
		if (!strcmp(option,"config")) {
			if (params[0][0]==0) {
				SPRINT(gsm_conf_error, "Error in %s (line %d): parameter for option %s missing.\n",filename,line, option);
				goto error;
			}
			SCPY(gsm_conf->openbsc_cfg, params[0]);

		} else
		if (!strcmp(option,"hlr")) {
			if (params[0][0]==0) {
				SPRINT(gsm_conf_error, "Error in %s (line %d): parameter for option %s missing.\n",filename,line, option);
				goto error;
			}
			SCPY(gsm_conf->hlr, params[0]);

		} else
		if (!strcmp(option,"reject-cause")) {
			if (params[0][0]==0) {
				SPRINT(gsm_conf_error, "Error in %s (line %d): parameter for option %s missing.\n",filename,line, option);
				goto error;
			}
			gsm_conf->reject_cause = atoi(params[0]);

		} else
		if (!strcmp(option,"allow-all")) {
			gsm_conf->allow_all = 1;
		} else
		if (!strcmp(option,"keep-l2")) {
			gsm_conf->keep_l2 = 1;

		} else
		if (!strcmp(option,"no-mergency-shutdown")) {
			gsm_conf->noemergshut = 1;
		} else
		if (!strcmp(option,"rtp-proxy")) {
			gsm_conf->rtp_proxy = 1;
		} else
		if (!strcmp(option,"pcapfile")) {
			if (params[0][0]==0) {
				SPRINT(gsm_conf_error, "Error in %s (line %d): parameter for option %s missing.\n",filename,line, option);
				goto error;
			}
			SCPY(gsm_conf->pcapfile, params[0]);
		} else {
			SPRINT(gsm_conf_error, "Error in %s (line %d): wrong option keyword %s.\n", filename,line,option);
			goto error;
		}
	}

	if (fp) fclose(fp);
	return(1);
error:
	if (fp) fclose(fp);
	return(0);
}


