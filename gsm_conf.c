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


/* read options
 *
 * read options from options.conf
 */
int gsm_conf(struct gsm_conf *gsm_conf, char *conf_error)
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

	SPRINT(filename, "%s/gsm.conf", CONFIG_DATA);

	if (!(fp=fopen(filename,"r"))) {
		UPRINT(conf_error, "Cannot open %s\n",filename);
		return(0);
	}

	line=0;
	while((GETLINE(buffer, fp))) {
		line++;
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
				UPRINT(conf_error, "Error in %s (line %d): option too long.\n",filename,line);
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
					UPRINT(conf_error, "Error in %s (line %d): param too long.\n",filename,line);
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
				UPRINT(conf_error, "Error in %s (line %d): parameter for option %s missing.\n",filename,line,option);
				goto error;
			}
			SCPY(gsm_conf->debug, params[0]);

		} else
		if (!strcmp(option,"interface-bsc")) {
			if (params[0][0]==0) {
				UPRINT(conf_error, "Error in %s (line %d): parameter for option %s missing.\n",filename,line, option);
				goto error;
			}
			SCPY(gsm_conf->interface_bsc, params[0]);

		} else
		if (!strcmp(option,"interface-lcr")) {
			if (params[0][0]==0) {
				UPRINT(conf_error, "Error in %s (line %d): parameter for option %s missing.\n",filename,line, option);
				goto error;
			}
			SCPY(gsm_conf->interface_lcr, params[0]);

		} else
		if (!strcmp(option,"config")) {
			if (params[0][0]==0) {
				UPRINT(conf_error, "Error in %s (line %d): parameter for option %s missing.\n",filename,line, option);
				goto error;
			}
			SCPY(gsm_conf->openbsc_cfg, params[0]);

		} else
		if (!strcmp(option,"hlr")) {
			if (params[0][0]==0) {
				UPRINT(conf_error, "Error in %s (line %d): parameter for option %s missing.\n",filename,line, option);
				goto error;
			}
			SCPY(gsm_conf->hlr, params[0]);

		} else
		if (!strcmp(option,"reject-cause")) {
			UPRINT(conf_error, "Option '%s' in gsm.conf has moved to openbsc.cfg", option);
			goto error;
		} else
		if (!strcmp(option,"allow-all")) {
			gsm_conf->allow_all = 1;
		} else
		if (!strcmp(option,"keep-l2")) {
			gsm_conf->keep_l2 = 1;

		} else
		if (!strcmp(option,"pcapfile")) {
			if (params[0][0]==0) {
				UPRINT(conf_error, "Error in %s (line %d): parameter for option %s missing.\n",filename,line, option);
				goto error;
			}
			SCPY(gsm_conf->pcapfile, params[0]);
		} else {
			UPRINT(conf_error, "Error in %s (line %d): wrong option keyword %s.\n", filename,line,option);
			goto error;
		}
	}

	if (fp) fclose(fp);
	return(1);
error:
	if (fp) fclose(fp);
	return(0);
}


