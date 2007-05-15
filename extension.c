/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** reading and writing files for extensions                                  **
**                                                                           **
\*****************************************************************************/ 

#include "main.h"

/* extension */

char *ext_rights[] = {
	"none",
	"internal",
	"local",
	"national",
	"international",
	NULL
};

char *ext_yesno[] = {
	"no",
	"yes",
	NULL
};


/* read extension
 *
 * reads extension from given extension number and fills structure
 */
int read_extension(struct extension *ext, char *number)
{
	FILE *fp=NULL;
	char filename[256];
	char *p;
	char option[32];
	char param[256],param2[256];
	unsigned int line,i;
	char buffer[1024];
	int last_in_count = 0, last_out_count = 0;

	if (number[0] == '\0')
		return(0);

	SPRINT(filename, "%s/%s/%s/settings", INSTALL_DATA, options.extensions_dir, number);

	if (!(fp = fopen(filename, "r")))
	{
		PDEBUG(DEBUG_CONFIG, "the given extension doesn't exist: \"%s\"\n", filename);
		return(0);
	}

	/* default values */
	memset(ext, 0, sizeof(struct extension));
	ext->rights = 4; /* international */
	ext->tout_setup = 120;
	ext->tout_dialing = 120;
	ext->tout_proceeding = 120;
	ext->tout_alerting = 120;
	ext->tout_disconnect = 120;
//	ext->tout_hold = 900;
//	ext->tout_park = 900;
	ext->cfnr_delay = 20;
	ext->vbox_codec = CODEC_MONO;

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

		option[0]=0;
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

		while(*p <= 32) /* skip spaces */
		{
			if (*p == 0)
				break;
			p++;
		}

		param[0]=0;
		param2[0]=0;
		if (*p!=0 && *p!='#') /* param */
		{
			i=0; /* read param */
			while(*p > 32)
			{
				if (i+1 >= sizeof(param))
				{
					PERROR_RUNTIME("Error in %s (line %d): param too long.\n",filename,line);
					break;
				}
				param[i+1] = '\0';
				param[i++] = *p++;
			}

			while(*p <= 32) /* skip spaces */
			{
				if (*p == 0)
					break;
				p++;
			}

			if (*p!=0 && *p!='#') /* param2 */
			{
				i=0; /* read param2 */
				while(*p >= 32)
				{
					if (i+1 >= sizeof(param2))
					{
						PERROR_RUNTIME("Error in %s (line %d): param too long.\n",filename,line);
						break;
					}
					param2[i+1] = '\0';
					param2[i++] = *p++;
				}
			}
		}

		/* at this point we have option and param */

		/* check option */
		if (!strcmp(option,"name"))
		{
			SCPY(ext->name, param);
			if (param2[0])
			{
				SCAT(ext->name, " ");
				SCAT(ext->name, param2);
			}

			PDEBUG(DEBUG_CONFIG, "name of extension: %s\n",param);
		} else
		if (!strcmp(option,"prefix"))
		{
			SCPY(ext->prefix, param);

			PDEBUG(DEBUG_CONFIG, "dial prefix on pickup: %s\n",param);
		} else
		if (!strcmp(option,"next"))
		{
			SCPY(ext->next, param);

			PDEBUG(DEBUG_CONFIG, "dial next on pickup: %s\n",param);
		} else
		if (!strcmp(option,"alarm"))
		{
			SCPY(ext->alarm, param);

			PDEBUG(DEBUG_CONFIG, "alarm message (if prefix): %s\n",param);
		} else
		if (!strcmp(option,"cfu"))
		{
			SCPY(ext->cfu, param);

			PDEBUG(DEBUG_CONFIG, "call forward unconditional: %s\n",param);
		} else
		if (!strcmp(option,"cfb"))
		{
			SCPY(ext->cfb, param);

			PDEBUG(DEBUG_CONFIG, "call forward when busy: %s\n",param);
		} else
		if (!strcmp(option,"cfnr"))
		{
			SCPY(ext->cfnr, param);

			PDEBUG(DEBUG_CONFIG, "call forward on no response: %s\n",param);
		} else
		if (!strcmp(option,"cfnr_delay"))
		{
			ext->cfnr_delay = atoi(param);
			if (ext->cfnr_delay < 0)
				ext->cfnr_delay = 1;

			PDEBUG(DEBUG_CONFIG, "call forward no response delay: %d\n",ext->cfnr_delay);
		} else
		if (!strcmp(option,"cfp"))
		{
			SCPY(ext->cfp, param);

			PDEBUG(DEBUG_CONFIG, "call forward parallel: %s\n",param);
		} else
		if (!strcmp(option,"change_forward"))
		{
			i=0;
			while(ext_yesno[i])
			{
				if (!strcasecmp(param,ext_yesno[i]))
					break;
				i++;
			}
			if (ext_yesno[i])
			{
				ext->change_forward = i;
				PDEBUG(DEBUG_CONFIG, "allow the change of forwarding: %s\n", ext_yesno[i]);
			} else
			{
				PDEBUG(DEBUG_CONFIG, "unknown parameter for change_forward: %s\n", param);
			}
		} else
		if (!strcmp(option,"interfaces"))
		{
			SCPY(ext->interfaces, param);

			PDEBUG(DEBUG_CONFIG, "interfaces to ring calls to extension: %s %s\n",param,param2);
		} else
		if (!strcmp(option,"callerid"))
		{
			ext->callerid_present = INFO_PRESENT_ALLOWED;
			if (!strncasecmp(param2, "anonymous", 9))
				ext->callerid_present = INFO_PRESENT_RESTRICTED;
			if (!strncasecmp(param, "non", 3))
			{
				ext->callerid[0] = '\0';
				ext->callerid_present = INFO_PRESENT_NOTAVAIL;
				ext->callerid_type = INFO_NTYPE_UNKNOWN;
				PDEBUG(DEBUG_CONFIG, "caller id: ID NOT AVAILABLE\n");
			} else
			switch(param[0])
			{
				case 'i':
				case 'I':
				ext->callerid_type = INFO_NTYPE_INTERNATIONAL;
				SCPY(ext->callerid, param+1);
				PDEBUG(DEBUG_CONFIG, "caller id: %s INTERNATIONAL\n",param+1);
				break;
				case 'n':
				case 'N':
				ext->callerid_type = INFO_NTYPE_NATIONAL;
				SCPY(ext->callerid, param+1);
				PDEBUG(DEBUG_CONFIG, "caller id: %s NATIONAL\n",param+1);
				break;
				case 's':
				case 'S':
				ext->callerid_type = INFO_NTYPE_SUBSCRIBER;
				SCPY(ext->callerid, param+1);
				PDEBUG(DEBUG_CONFIG, "caller id: %s SUBSCRIBER\n",param+1);
				break;
				default:
				ext->callerid_type = INFO_NTYPE_UNKNOWN;
				SCPY(ext->callerid, param);
				PDEBUG(DEBUG_CONFIG, "caller id: %s UNKNOWN\n",param);
			}
			ext->callerid[sizeof(ext->callerid)-1] = 0;
		} else
		if (!strcmp(option,"id_next_call"))
		{
			ext->id_next_call_present = INFO_PRESENT_ALLOWED;
			if (!strncasecmp(param2, "anonymous", 9))
				ext->id_next_call_present = INFO_PRESENT_RESTRICTED;
			if (param[0] == '\0')
			{
				ext->id_next_call_present = -1;
				PDEBUG(DEBUG_CONFIG, "id next call: no id for next call\n");
			} else
			if (!strncasecmp(param, "none", 3))
			{
				ext->id_next_call[0] = '\0';
				ext->id_next_call_present = INFO_PRESENT_NOTAVAIL;
				ext->id_next_call_type = INFO_NTYPE_UNKNOWN;
				PDEBUG(DEBUG_CONFIG, "id next call: ID NOT AVAILABLE\n");
			} else
			switch(param[0])
			{
				case 'i':
				case 'I':
				ext->id_next_call_type = INFO_NTYPE_INTERNATIONAL;
				SCPY(ext->id_next_call, param+1);
				PDEBUG(DEBUG_CONFIG, "id next call: %s INTERNATIONAL\n",param+1);
				break;
				case 'n':
				case 'N':
				ext->id_next_call_type = INFO_NTYPE_NATIONAL;
				SCPY(ext->id_next_call, param+1);
				PDEBUG(DEBUG_CONFIG, "id next call: %s NATIONAL\n",param+1);
				break;
				case 's':
				case 'S':
				ext->id_next_call_type = INFO_NTYPE_SUBSCRIBER;
				SCPY(ext->id_next_call, param+1);
				PDEBUG(DEBUG_CONFIG, "id next call: %s SUBSCRIBER\n",param+1);
				break;
				default:
				ext->id_next_call_type = INFO_NTYPE_UNKNOWN;
				SCPY(ext->id_next_call, param);
				PDEBUG(DEBUG_CONFIG, "id next call: %s UNKNOWN\n",param);
			}



		} else
		if (!strcmp(option,"change_callerid"))
		{
			i=0;
			while(ext_yesno[i])
			{
				if (!strcasecmp(param,ext_yesno[i]))
					break;
				i++;
			}
			if (ext_yesno[i])
			{
				ext->change_callerid = i;
				PDEBUG(DEBUG_CONFIG, "allow the change of caller id: %s\n", ext_yesno[i]);
			} else
			{
				PDEBUG(DEBUG_CONFIG, "unknown parameter for change_callerid: %s\n", param);
			}
		} else
		if (!strcmp(option,"anon-ignore"))
		{
			i=0;
			while(ext_yesno[i])
			{
				if (!strcasecmp(param,ext_yesno[i]))
					break;
				i++;
			}
			if (ext_yesno[i])
			{
				ext->anon_ignore = i;
				PDEBUG(DEBUG_CONFIG, "ignore restriction of CLIP & COLP %s\n", ext_yesno[i]);
			} else
			{
				PDEBUG(DEBUG_CONFIG, "unknown parameter given anon-ignore: %s\n", param);
			}
		} else
		if (!strcmp(option,"clip"))
		{
			if (!strcasecmp(param, "hide"))
				ext->clip = CLIP_HIDE;
			else
				ext->clip = CLIP_ASIS;

			PDEBUG(DEBUG_CONFIG, "clip: %d\n",ext->clip);
		} else
		if (!strcmp(option,"colp"))
		{
			if (!strcasecmp(param, "hide"))
				ext->colp = COLP_HIDE;
			else if (!strcasecmp(param, "force"))
				ext->colp = COLP_FORCE;
			else
				ext->colp = COLP_ASIS;

			PDEBUG(DEBUG_CONFIG, "colp: %d\n",ext->colp);
		} else
		if (!strcmp(option,"clip_prefix"))
		{
			SCPY(ext->clip_prefix, param);

			PDEBUG(DEBUG_CONFIG, "clip prefix: %s\n",param);
		} else
		if (!strcmp(option,"keypad"))
		{
			i=0;
			while(ext_yesno[i])
			{
				if (!strcasecmp(param,ext_yesno[i]))
					break;
				i++;
			}
			if (ext_yesno[i])
			{
				ext->keypad = i;
				PDEBUG(DEBUG_CONFIG, "use keypad to do call control %s\n", ext_yesno[i]);
			} else
			{
				PDEBUG(DEBUG_CONFIG, "unknown parameter given keypad: %s\n", param);
			}
		} else
		if (!strcmp(option,"centrex"))
		{
			i=0;
			while(ext_yesno[i])
			{
				if (!strcasecmp(param,ext_yesno[i]))
					break;
				i++;
			}
			if (ext_yesno[i])
			{
				ext->centrex = i;
				PDEBUG(DEBUG_CONFIG, "use centrex to display name %s\n", ext_yesno[i]);
			} else
			{
				PDEBUG(DEBUG_CONFIG, "unknown parameter given centrex: %s\n", param);
			}
		} else
		if (!strcmp(option,"rights"))
		{
			i=0;
			while(ext_rights[i])
			{
				if (!strcasecmp(param,ext_rights[i]))
					break;
				i++;
			}
			if (ext_rights[i])
			{
				ext->rights = i;
				PDEBUG(DEBUG_CONFIG, "rights to dial: %s\n", ext_rights[i]);
			} else
			{
				PDEBUG(DEBUG_CONFIG, "given rights unknown: %s\n", param);
			}
		} else
		if (!strcmp(option,"delete_ext"))
		{
			i=0;
			while(ext_yesno[i])
			{
				if (!strcasecmp(param,ext_yesno[i]))
					break;
				i++;
			}
			if (ext_yesno[i])
			{
				ext->delete_ext = i;
				PDEBUG(DEBUG_CONFIG, "enables the delete key function for external calls: %s\n", ext_yesno[i]);
			} else
			{
				PDEBUG(DEBUG_CONFIG, "unknown parameter given delete: %s\n", param);
			}
		} else
		if (!strcmp(option,"noknocking"))
		{
			i=0;
			while(ext_yesno[i])
			{
				if (!strcasecmp(param,ext_yesno[i]))
					break;
				i++;
			}
			if (ext_yesno[i])
			{
				ext->noknocking = i;
				PDEBUG(DEBUG_CONFIG, "noknocking %s\n", ext_yesno[i]);
			} else
			{
				PDEBUG(DEBUG_CONFIG, "given noknocking param unknown: %s\n", param);
			}
		} else
		if (!strcmp(option,"rxvol"))
		{
			ext->rxvol = atoi(param);
			if (ext->rxvol<-8 || ext->rxvol>8)
				ext->rxvol = 0;

			PDEBUG(DEBUG_CONFIG, "receive volume: %d\n",ext->rxvol);
		} else
		if (!strcmp(option,"txvol"))
		{
			ext->txvol = atoi(param);
			if (ext->txvol<-8 || ext->txvol>8)
				ext->txvol = 0;

			PDEBUG(DEBUG_CONFIG, "transmit volume: %d\n",ext->txvol);
		} else
		if (!strcmp(option,"tout_setup"))
		{
			ext->tout_setup = atoi(param);
			if (ext->tout_setup < 0)
				ext->tout_setup = 0;

			PDEBUG(DEBUG_CONFIG, "timeout setup: %d\n",ext->tout_setup);
		} else
		if (!strcmp(option,"tout_dialing"))
		{
			ext->tout_dialing = atoi(param);
			if (ext->tout_dialing < 0)
				ext->tout_dialing = 0;

			PDEBUG(DEBUG_CONFIG, "timeout dialing: %d\n",ext->tout_dialing);
		} else
		if (!strcmp(option,"tout_proceeding"))
		{
			ext->tout_proceeding = atoi(param);
			if (ext->tout_proceeding < 0)
				ext->tout_proceeding = 0;

			PDEBUG(DEBUG_CONFIG, "timeout proceeding: %d\n",ext->tout_proceeding);
		} else
		if (!strcmp(option,"tout_alerting"))
		{
			ext->tout_alerting = atoi(param);
			if (ext->tout_alerting < 0)
				ext->tout_alerting = 0;

			PDEBUG(DEBUG_CONFIG, "timeout alerting: %d\n",ext->tout_alerting);
		} else
		if (!strcmp(option,"tout_disconnect"))
		{
			ext->tout_disconnect = atoi(param);
			if (ext->tout_disconnect < 0)
				ext->tout_disconnect = 0;

			PDEBUG(DEBUG_CONFIG, "timeout disconnect: %d\n",ext->tout_disconnect);
		} else
#if 0
		if (!strcmp(option,"tout_hold"))
		{
			ext->tout_hold = atoi(param);
			if (ext->tout_hold < 0)
				ext->tout_hold = 0;

			PDEBUG(DEBUG_CONFIG, "timeout hold: %d\n",ext->tout_hold);
		} else
		if (!strcmp(option,"tout_park"))
		{
			ext->tout_park = atoi(param);
			if (ext->tout_park < 0)
				ext->tout_park = 0;

			PDEBUG(DEBUG_CONFIG, "timeout park: %d\n",ext->tout_park);
		} else
#endif
		if (!strcmp(option,"own_setup"))
		{
			i=0;
			while(ext_yesno[i])
			{
				if (!strcasecmp(param,ext_yesno[i]))
					break;
				i++;
			}
			if (ext_yesno[i])
			{
				ext->own_setup = i;
				PDEBUG(DEBUG_CONFIG, "own_setup %s\n", ext_yesno[i]);
			} else
			{
				PDEBUG(DEBUG_CONFIG, "given own_setup param unknown: %s\n", param);
			}
		} else
		if (!strcmp(option,"own_proceeding"))
		{
			i=0;
			while(ext_yesno[i])
			{
				if (!strcasecmp(param,ext_yesno[i]))
					break;
				i++;
			}
			if (ext_yesno[i])
			{
				ext->own_proceeding = i;
				PDEBUG(DEBUG_CONFIG, "own_proceeding %s\n", ext_yesno[i]);
			} else
			{
				PDEBUG(DEBUG_CONFIG, "given own_proceeding param unknown: %s\n", param);
			}
		} else
		if (!strcmp(option,"own_alerting"))
		{
			i=0;
			while(ext_yesno[i])
			{
				if (!strcasecmp(param,ext_yesno[i]))
					break;
				i++;
			}
			if (ext_yesno[i])
			{
				ext->own_alerting = i;
				PDEBUG(DEBUG_CONFIG, "own_alerting %s\n", ext_yesno[i]);
			} else
			{
				PDEBUG(DEBUG_CONFIG, "given own_alerting param unknown: %s\n", param);
			}
		} else
		if (!strcmp(option,"own_cause"))
		{
			i=0;
			while(ext_yesno[i])
			{
				if (!strcasecmp(param,ext_yesno[i]))
					break;
				i++;
			}
			if (ext_yesno[i])
			{
				ext->own_cause = i;
				PDEBUG(DEBUG_CONFIG, "own_cause %s\n", ext_yesno[i]);
			} else
			{
				PDEBUG(DEBUG_CONFIG, "given own_cause param unknown: %s\n", param);
			}
		} else
		if (!strcmp(option,"facility"))
		{
			i=0;
			while(ext_yesno[i])
			{
				if (!strcasecmp(param,ext_yesno[i]))
					break;
				i++;
			}
			if (ext_yesno[i])
			{
				ext->facility = i;
				PDEBUG(DEBUG_CONFIG, "facility %s\n", ext_yesno[i]);
			} else
			{
				PDEBUG(DEBUG_CONFIG, "given facility param unknown: %s\n", param);
			}
		} else
		if (!strcmp(option,"display_cause"))
		{
			if (!strcasecmp(param, "german"))
				ext->display_cause = DISPLAY_CAUSE_GERMAN;
			else if (!strcasecmp(param, "english"))
				ext->display_cause = DISPLAY_CAUSE_ENGLISH;
			else if (!strcasecmp(param, "german-location"))
				ext->display_cause = DISPLAY_LOCATION_GERMAN;
			else if (!strcasecmp(param, "english-location"))
				ext->display_cause = DISPLAY_LOCATION_ENGLISH;
			else if (!strcasecmp(param, "number"))
				ext->display_cause = DISPLAY_CAUSE_NUMBER;
			else
				ext->display_cause = DISPLAY_CAUSE_NONE;

			PDEBUG(DEBUG_CONFIG, "display cause: %d\n",ext->display_cause);
		} else
#if 0
		if (!strcmp(option,"display_ext"))
		{
			if (!strcasecmp(param, "number"))
				ext->display_ext = DISPLAY_CID_NUMBER;
			else if (!strcasecmp(param, "abbrev"))
				ext->display_ext = DISPLAY_CID_ABBREVIATION;
			else if (!strcasecmp(param, "name"))
				ext->display_ext = DISPLAY_CID_NAME;
			else if (!strcasecmp(param, "number-name"))
				ext->display_ext = DISPLAY_CID_NUMBER_NAME;
			else if (!strcasecmp(param, "name-number"))
				ext->display_ext = DISPLAY_CID_NAME_NUMBER;
			else if (!strcasecmp(param, "abbrev-number"))
				ext->display_ext = DISPLAY_CID_ABBREV_NUMBER;
			else if (!strcasecmp(param, "abbrev-name"))
				ext->display_ext = DISPLAY_CID_ABBREV_NAME;
			else if (!strcasecmp(param, "abbrev-name-number"))
				ext->display_ext = DISPLAY_CID_ABBREV_NAME_NUMBER;
			else if (!strcasecmp(param, "abbrev-number-name"))
				ext->display_ext = DISPLAY_CID_ABBREV_NUMBER_NAME;
			else
				ext->display_ext = DISPLAY_CID_ASIS;

			PDEBUG(DEBUG_CONFIG, "display ext: %d\n",ext->display_ext);
		} else
#endif
		if (!strcmp(option,"display_ext"))
		{
			i=0;
			while(ext_yesno[i])
			{
				if (!strcasecmp(param,ext_yesno[i]))
					break;
				i++;
			}
			if (ext_yesno[i])
			{
				ext->display_ext = i;
				PDEBUG(DEBUG_CONFIG, "display ext %s\n", ext_yesno[i]);
			} else
			{
				PDEBUG(DEBUG_CONFIG, "given display_ext param unknown: %s\n", param);
			}
		} else
		if (!strcmp(option,"display_int"))
		{
			i=0;
			while(ext_yesno[i])
			{
				if (!strcasecmp(param,ext_yesno[i]))
					break;
				i++;
			}
			if (ext_yesno[i])
			{
				ext->display_int = i;
				PDEBUG(DEBUG_CONFIG, "display int %s\n", ext_yesno[i]);
			} else
			{
				PDEBUG(DEBUG_CONFIG, "given display_int param unknown: %s\n", param);
			}
		} else
		if (!strcmp(option,"display_fake"))
		{
			i=0;
			while(ext_yesno[i])
			{
				if (!strcasecmp(param,ext_yesno[i]))
					break;
				i++;
			}
			if (ext_yesno[i])
			{
				ext->display_fake = i;
				PDEBUG(DEBUG_CONFIG, "display fake caller ids %s\n", ext_yesno[i]);
			} else
			{
				PDEBUG(DEBUG_CONFIG, "given display_fake param unknown: %s\n", param);
			}
		} else
		if (!strcmp(option,"display_anon"))
		{
			i=0;
			while(ext_yesno[i])
			{
				if (!strcasecmp(param,ext_yesno[i]))
					break;
				i++;
			}
			if (ext_yesno[i])
			{
				ext->display_anon = i;
				PDEBUG(DEBUG_CONFIG, "display anonymouse ids %s\n", ext_yesno[i]);
			} else
			{
				PDEBUG(DEBUG_CONFIG, "given display_anon param unknown: %s\n", param);
			}
		} else
		if (!strcmp(option,"display_menu"))
		{
			i=0;
			while(ext_yesno[i])
			{
				if (!strcasecmp(param,ext_yesno[i]))
					break;
				i++;
			}
			if (ext_yesno[i])
			{
				ext->display_menu = i;
				PDEBUG(DEBUG_CONFIG, "display menu %s\n", ext_yesno[i]);
			} else
			{
				PDEBUG(DEBUG_CONFIG, "given display_menu param unknown: %s\n", param);
			}
		} else
		if (!strcmp(option,"display_dialing"))
		{
			i=0;
			while(ext_yesno[i])
			{
				if (!strcasecmp(param,ext_yesno[i]))
					break;
				i++;
			}
			if (ext_yesno[i])
			{
				ext->display_dialing = i;
				PDEBUG(DEBUG_CONFIG, "display dialing %s\n", ext_yesno[i]);
			} else
			{
				PDEBUG(DEBUG_CONFIG, "given display_dialing param unknown: %s\n", param);
			}
		} else
		if (!strcmp(option,"display_name"))
		{
			i=0;
			while(ext_yesno[i])
			{
				if (!strcasecmp(param,ext_yesno[i]))
					break;
				i++;
			}
			if (ext_yesno[i])
			{
				ext->display_name = i;
				PDEBUG(DEBUG_CONFIG, "display name %s\n", ext_yesno[i]);
			} else
			{
				PDEBUG(DEBUG_CONFIG, "given display_name param unknown: %s\n", param);
			}
		} else
		if (!strcmp(option,"tones_dir"))
		{
			if (param[strlen(param)-1] == '/')
				param[strlen(param)-1]=0;
			SCPY(ext->tones_dir, param);

			PDEBUG(DEBUG_CONFIG, "directory of tones: %s\n",param);
		} else
		if (!strcmp(option,"record"))
		{
			if (!strcasecmp(param, "mono"))
				ext->record = CODEC_MONO;
			else if (!strcasecmp(param, "stereo"))
				ext->record = CODEC_STEREO;
			else if (!strcasecmp(param, "8bit"))
				ext->record = CODEC_8BIT;
			else if (!strcasecmp(param, "law"))
				ext->record = CODEC_LAW;
			else
				ext->record = CODEC_OFF;
			PDEBUG(DEBUG_CONFIG, "given record param: %s\n", param);
		} else
		if (!strcmp(option,"password"))
		{
			SCPY(ext->password, param);

			PDEBUG(DEBUG_CONFIG, "password: %s\n",param);
		} else
		if (!strcmp(option,"vbox_mode"))
		{
			if (!strcasecmp(param, "parallel"))
				ext->vbox_mode = VBOX_MODE_PARALLEL;
			else if (!strcasecmp(param, "announcement"))
				ext->vbox_mode = VBOX_MODE_ANNOUNCEMENT;
			else
				ext->vbox_mode = VBOX_MODE_NORMAL;
			PDEBUG(DEBUG_CONFIG, "given vbox mode: %s\n", param);
		} else
		if (!strcmp(option,"vbox_codec"))
		{
			if (!strcasecmp(param, "stereo"))
				ext->vbox_codec = CODEC_STEREO;
			else if (!strcasecmp(param, "8bit"))
				ext->vbox_codec = CODEC_8BIT;
			else if (!strcasecmp(param, "law"))
				ext->vbox_codec = CODEC_LAW;
			else
				ext->vbox_codec = CODEC_MONO;
			PDEBUG(DEBUG_CONFIG, "given record param: %s\n", param);
		} else
		if (!strcmp(option,"vbox_time"))
		{
			ext->vbox_time = atoi(param);
			if (ext->vbox_time < 0)
				ext->vbox_time = 0;

			PDEBUG(DEBUG_CONFIG, "vbox time to record: %d\n",ext->vbox_time);
		} else
		if (!strcmp(option,"vbox_display"))
		{
			if (!strcasecmp(param, "detailed")
			 || !strcasecmp(param, "detailled"))
				ext->vbox_display = VBOX_DISPLAY_DETAILED;
			else if (!strcasecmp(param, "off"))
				ext->vbox_display = VBOX_DISPLAY_OFF;
			else
				ext->vbox_display = VBOX_DISPLAY_BRIEF;
			PDEBUG(DEBUG_CONFIG, "given vbox mode: %s\n", param);
		} else
		if (!strcmp(option,"vbox_language"))
		{
			if (!strcasecmp(param, "german"))
				ext->vbox_language = VBOX_LANGUAGE_GERMAN;
			else
				ext->vbox_language = VBOX_LANGUAGE_ENGLISH;
			PDEBUG(DEBUG_CONFIG, "given vbox mode: %s\n", param);
		} else
		if (!strcmp(option,"vbox_email"))
		{
			SCPY(ext->vbox_email, param);
			PDEBUG(DEBUG_CONFIG, "given vbox email: %s\n", param);
		} else
		if (!strcmp(option,"vbox_email_file"))
		{
			i=0;
			while(ext_yesno[i])
			{
				if (!strcasecmp(param,ext_yesno[i]))
					break;
				i++;
			}
			if (ext_yesno[i])
			{
				ext->vbox_email_file = i;
				PDEBUG(DEBUG_CONFIG, "attach audio file %s\n", ext_yesno[i]);
			} else
			{
				PDEBUG(DEBUG_CONFIG, "given vbox_email_file param unknown: %s\n", param);
			}
		} else
		if (!strcmp(option,"vbox_free"))
		{
			i=0;
			while(ext_yesno[i])
			{
				if (!strcasecmp(param,ext_yesno[i]))
					break;
				i++;
			}
			if (ext_yesno[i])
			{
				ext->vbox_free = i;
				PDEBUG(DEBUG_CONFIG, "vbox_free %s\n", ext_yesno[i]);
			} else
			{
				PDEBUG(DEBUG_CONFIG, "given vbox_free param unknown: %s\n", param);
			}
		} else
		if (!strcmp(option,"last_in"))
		{
			if (param[0] && last_in_count<MAX_REMEMBER)
			{
				SCPY(ext->last_in[last_in_count], param);
				last_in_count++;
			}
			PDEBUG(DEBUG_CONFIG, "last_in dialed number: %s\n",param);
		} else
		if (!strcmp(option,"last_out"))
		{
			if (param[0] && last_out_count<MAX_REMEMBER)
			{
				SCPY(ext->last_out[last_out_count], param);
				last_out_count++;
			}
			PDEBUG(DEBUG_CONFIG, "last_out dialed number: %s\n",param);
		} else
		if (!strcmp(option,"datacall"))
		{
			i=0;
			while(ext_yesno[i])
			{
				if (!strcasecmp(param,ext_yesno[i]))
					break;
				i++;
			}
			if (ext_yesno[i])
			{
				ext->datacall = i;
				PDEBUG(DEBUG_CONFIG, "datacall %s\n", ext_yesno[i]);
			} else
			{
				PDEBUG(DEBUG_CONFIG, "given datacall param unknown: %s\n", param);
			}
		} else
		if (!strcmp(option,"seconds"))
		{
			i=0;
			while(ext_yesno[i])
			{
				if (!strcasecmp(param,ext_yesno[i]))
					break;
				i++;
			}
			if (ext_yesno[i])
			{
				ext->no_seconds = 1-i;
				PDEBUG(DEBUG_CONFIG, "seconds %s\n", ext_yesno[i]);
			} else
			{
				PDEBUG(DEBUG_CONFIG, "unknown param for seconds: %s\n", param);
			}
		} else
		{
			PERROR_RUNTIME("Error in %s (line %d): wrong option keyword %s.\n",filename,line,option);
		}
	}

	if (fp) fclose(fp);
	return(1);
}


/* write extension
 *
 * writes extension for given extension number from structure
 */
int write_extension(struct extension *ext, char *number)
{
	FILE *fp=NULL;
	char filename[256];
	int i;

	if (number[0] == '\0')
		return(0);

	SPRINT(filename, "%s/%s/%s/settings", INSTALL_DATA, options.extensions_dir, number);

	if (!(fp = fopen(filename, "w")))
	{
		PERROR("Cannot open settings: \"%s\"\n", filename);
		return(0);
	}

	fprintf(fp,"# Settings of extension %s\n\n", number);

	fprintf(fp,"# Name of extension:\n");
	fprintf(fp,"name            %s\n\n",ext->name);

	fprintf(fp,"# Predialed prefix after pick-up of the phone\n");
	fprintf(fp,"prefix          %s\n\n",ext->prefix);

	fprintf(fp,"# Next prefix to dial pick-up of the phone\n");
	fprintf(fp,"# This will be cleared on hangup.\n");
	fprintf(fp,"next            %s\n\n",ext->next);

//	fprintf(fp,"# Set up alarm message after prefix is dialed and connection is established\n");
//	fprintf(fp,"alarm           %s\n\n",ext->alarm);

	fprintf(fp,"# Ports to ring on calls to extension (starting from 1)\n");
	fprintf(fp,"# Seperate ports by using komma. (example: 1,3 would ring incoming calls on\n# port 1 and 3)\n");
	fprintf(fp,"interfaces      %s\n\n",ext->interfaces);

	fprintf(fp,"# Call Forward Unconditional (CFU)\n");
	fprintf(fp,"# No port will be called, CFB, CFNR and CFP is ignored.\n");
	fprintf(fp,"# Use keyword \"vbox\" to forward call directly to answering machine.\n");
	fprintf(fp,"cfu             %s\n\n",ext->cfu);

	fprintf(fp,"# Call Forward when Busy (CFB)\n");
	fprintf(fp,"# If the extension is in use at least once, this forward is done.\n");
	fprintf(fp,"# In case of busy line, CFNR and CFP is ignored.\n");
	fprintf(fp,"# Use keyword \"vbox\" to forward call to answering machine when busy.\n");
	fprintf(fp,"cfb             %s\n\n",ext->cfb);

	fprintf(fp,"# Call Forward on No Response (CFNR)\n");
	fprintf(fp,"# If noone answers, the call is forwarded, ports and CFP will be released.\n");
	fprintf(fp,"# The default delay is 20 seconds.\n");
	fprintf(fp,"# Use keyword \"vbox\" to forward call to answering machine on no response.\n");
	fprintf(fp,"cfnr            %s\n",ext->cfnr);
	fprintf(fp,"cfnr_delay      %d\n\n",ext->cfnr_delay);

	fprintf(fp,"# Call Forward Parallel (CFP)\n");
	fprintf(fp,"# Call will ring on the forwarded number, simulaniousely with the ports.\n");
	fprintf(fp,"cfp             %s\n\n",ext->cfp);

	fprintf(fp,"# Allow user to change call forwarding.\n");
	fprintf(fp,"change_forward  %s\n\n", ext_yesno[ext->change_forward]);

	fprintf(fp,"# Caller id\n# This must be one of the following:\n");
	fprintf(fp,"# <number> (as dialed from your local area)\n");
	fprintf(fp,"# <number> anonymous (will only be shown to emergency phones)\n");
	fprintf(fp,"# none (no number available at all)\n");
	fprintf(fp,"# by default the number is of type UNKNOWN (for MULTIPOINT lines)\n");
	fprintf(fp,"# if your caller id is not screened on outgoing calls use one of the following:\n");
	fprintf(fp,"# use prefix 'i' for TYPE INTERNATIONAL (i<county code><areacode+number>)\n");
	fprintf(fp,"# use prefix 'n' for TYPE NATIONAL (n<areacode+number>)\n");
	fprintf(fp,"# use prefix 's' for TYPE SUBSCRIBER (s<local number>)\n");
	if (ext->callerid_present == INFO_PRESENT_NOTAVAIL)
		fprintf(fp,"callerid        none\n\n");
	else
	{
		switch(ext->callerid_type)
		{
			case INFO_NTYPE_INTERNATIONAL:
			fprintf(fp,"callerid        i%s%s\n\n",ext->callerid, (ext->callerid_present==INFO_PRESENT_RESTRICTED)?" anonymous":"");
			break;
			case INFO_NTYPE_NATIONAL:
			fprintf(fp,"callerid        n%s%s\n\n",ext->callerid, (ext->callerid_present==INFO_PRESENT_RESTRICTED)?" anonymous":"");
			break;
			case INFO_NTYPE_SUBSCRIBER:
			fprintf(fp,"callerid        s%s%s\n\n",ext->callerid, (ext->callerid_present==INFO_PRESENT_RESTRICTED)?" anonymous":"");
			break;
			default:
			fprintf(fp,"callerid        %s%s\n\n",ext->callerid, (ext->callerid_present==INFO_PRESENT_RESTRICTED)?" anonymous":"");
		}
	}

	fprintf(fp,"# Caller id for next call (see caller id)\n");
	if (ext->id_next_call_present < 0)
		fprintf(fp,"id_next_call    \n\n");
	else if (ext->id_next_call_present == INFO_PRESENT_NOTAVAIL)
		fprintf(fp,"id_next_call    none\n\n");
	else
	{
		switch(ext->id_next_call_type)
		{
			case INFO_NTYPE_INTERNATIONAL:
			fprintf(fp,"id_next_call    i%s%s\n\n",ext->id_next_call, (ext->id_next_call_present==INFO_PRESENT_RESTRICTED)?" anonymous":"");
			break;
			case INFO_NTYPE_NATIONAL:
			fprintf(fp,"id_next_call    n%s%s\n\n",ext->id_next_call, (ext->id_next_call_present==INFO_PRESENT_RESTRICTED)?" anonymous":"");
			break;
			case INFO_NTYPE_SUBSCRIBER:
			fprintf(fp,"id_next_call    s%s%s\n\n",ext->id_next_call, (ext->id_next_call_present==INFO_PRESENT_RESTRICTED)?" anonymous":"");
			break;
			default:
			fprintf(fp,"id_next_call    %s%s\n\n",ext->id_next_call, (ext->id_next_call_present==INFO_PRESENT_RESTRICTED)?" anonymous":"");
		}
	}

	fprintf(fp,"# Allow user to change caller ID.\n");
	fprintf(fp,"change_callerid %s\n\n", ext_yesno[ext->change_callerid]);

	fprintf(fp,"# Caller Line Identification Presentation (CLIP)\n");
	fprintf(fp,"# clip (asis|hide)\n");
	fprintf(fp,"# asis: On forwarded calls the CLIP is used as presented by the calling party.\n");
	fprintf(fp,"# hide: Always use extension's caller id, even on forwared calls.\n");
	switch(ext->clip)
	{
		case CLIP_HIDE:
		fprintf(fp,"clip            hide\n\n");
		break;
		default:
		fprintf(fp,"clip            asis\n\n");
	}

	fprintf(fp,"# Connected Line Identification Presentation (COLP)\n");
	fprintf(fp,"# colp (asis|hide|force)\n");
	fprintf(fp,"# asis: Provides colp as defined by the extension's caller id.\n");
	fprintf(fp,"#       On forwarded calls the COLP is used as presented by the called party.\n");
	fprintf(fp,"# hide: Always use extension's caller id, even on forwared calls.\n");
	fprintf(fp,"# force: If COLP is not presented by forwarded calls the dialed number is used.\n");
	switch(ext->colp)
	{
		case COLP_HIDE:
		fprintf(fp,"colp            hide\n\n");
		break;
		case COLP_FORCE:
		fprintf(fp,"colp            force\n\n");
		break;
		default:
		fprintf(fp,"colp            asis\n\n");
	}

	fprintf(fp,"# CLIP Prefix\n");
	fprintf(fp,"# Adds a prefix to incomming caller IDs, so telephones will be able to respond\n");
	fprintf(fp,"# to unanswered calls from their list. The prefix must be the digit(s) to get\n");
	fprintf(fp,"# an external line. The caller ID will then be extendet so that they can be\n");
	fprintf(fp,"# dialed from internal telephones. Many telephones have this feature, but some\n");
	fprintf(fp,"# don't.\n");
	fprintf(fp,"clip_prefix     %s\n\n",ext->clip_prefix);

	fprintf(fp,"# Keypad control\n");
	fprintf(fp,"# If supported by telephone, pressing a key on the keypad will not result in\n");
	fprintf(fp,"# DTMF tone, but the digit is transmitted via D-channel diaing info.\n");
	fprintf(fp,"keypad          %s\n\n",(ext->keypad)?"yes":"no");

	fprintf(fp,"# Called Name Identification Presentation (CNIP/CONP)\n");
	fprintf(fp,"# If supported by telephone, special information element on the d-channel are\n");
	fprintf(fp,"# used to show name of caller. It is supported by newer Siemens telephones\n# (Centrex).\n");
	fprintf(fp,"centrex         %s  #this is currently not working!!!\n\n",(ext->centrex)?"yes":"no");

	fprintf(fp,"# Ignore restriction of COLP and CLIP\n");
	fprintf(fp,"# In this case even restricted numbers are presented to this extension.\n");
	fprintf(fp,"# This also works for incoming external anonymous calls IF:\n");
	fprintf(fp,"# You have the CLIRIGN feature like POLICE or equivalent.\n");
	fprintf(fp,"anon-ignore     %s\n\n",(ext->anon_ignore)?"yes":"no");

	fprintf(fp,"# Dialing rights (none|internal|local|national|international)\n");
	fprintf(fp,"rights          %s\n\n",ext_rights[ext->rights]);

	fprintf(fp,"# Delete function for external calls. '*' will delete the last digit, '#' will\n");
	fprintf(fp,"# delete the complete number. Also enable 'display_dialing' to see on the\n");
	fprintf(fp,"# display what actually happens.\n");
	fprintf(fp,"delete_ext      %s\n\n",ext_yesno[ext->delete_ext]);

	fprintf(fp,"# If noknocking is enabled, the caller will get a busy message when the\n");
	fprintf(fp,"# extension is doing at least one call.\n");
	fprintf(fp,"noknocking      %s\n\n",ext_yesno[ext->noknocking]);

	fprintf(fp,"# Transmit volume (-8 .. 8)\n");
	fprintf(fp,"# 0 = normal\n");
	fprintf(fp,"# 1 = double, 2 = quadrupel, 8 = 256 times (amplitude)\n");
	fprintf(fp,"# -1 = half, -2 = quarter, 8 = 1/256th (amplitude)\n");
	fprintf(fp,"# Audio data is limited to the maximum value when exceeds limit.\n");
	fprintf(fp,"txvol           %d\n\n",ext->txvol);

	fprintf(fp,"# Receive volume (-8 .. 8)\n");
	fprintf(fp,"# (see txvol)\n");
	fprintf(fp,"rxvol           %d\n\n",ext->rxvol);

	fprintf(fp,"# Timeout values\n# The keywords specify the following timeouts:\n");
	fprintf(fp,"# tout_setup: after pickup before dialing anything. (default 60 seconds)\n");
	fprintf(fp,"# tout_dialing: after dialing last digit of uncomplete number (default 15)\n");
	fprintf(fp,"# tout_proceeding: after start proceeding (default 120)\n");
	fprintf(fp,"# tout_alerting: after start ringing (default 120)\n");
	fprintf(fp,"# tout_disconnect: after disconnect (default 120)\n");
//	fprintf(fp,"# tout_hold: maximum time to hold a call (default 900)\n");
//	fprintf(fp,"# tout_park: maximum time to park a call (default 900)\n");
	fprintf(fp,"# All timeouts may be disabled by using keyword 'off' instead of seconds.\n");
	fprintf(fp,"# All timeouts refer to internal ports only. External timeouts are controlled\n");
	fprintf(fp,"# by external line.\n");
	if (ext->tout_setup)
		fprintf(fp,"tout_setup      %d\n",ext->tout_setup);
	else
		fprintf(fp,"tout_setup      off\n");
	if (ext->tout_dialing)
		fprintf(fp,"tout_dialing    %d\n",ext->tout_dialing);
	else
		fprintf(fp,"tout_dialing    off\n");
	if (ext->tout_proceeding)
		fprintf(fp,"tout_proceeding %d\n",ext->tout_proceeding);
	else
		fprintf(fp,"tout_proceeding off\n");
	if (ext->tout_alerting)
		fprintf(fp,"tout_alerting   %d\n",ext->tout_alerting);
	else
		fprintf(fp,"tout_alerting   off\n");
	if (ext->tout_disconnect)
		fprintf(fp,"tout_disconnect %d\n\n",ext->tout_disconnect);
	else
		fprintf(fp,"tout_disconnect off\n\n");
//	if (ext->tout_hold)
//		fprintf(fp,"tout_hold       %d\n",ext->tout_hold);
//	else
//		fprintf(fp,"tout_hold       off\n");
//	if (ext->tout_park)
//		fprintf(fp,"tout_park       %d\n\n",ext->tout_park);
//	else
//		fprintf(fp,"tout_park       off\n\n");

	fprintf(fp,"# Force to use tones and announcements generated by the pbx.\n");
	fprintf(fp,"# For internal calls always own tones are used. You may specify own tones for\n");
	fprintf(fp,"# different call states:\n");
	fprintf(fp,"# own_setup (dialtone and during dialing)\n");
	fprintf(fp,"# own_proceeding (call in poceeding state)\n");
	fprintf(fp,"# own_alerting (call is ringing)\n");
	fprintf(fp,"# own_cause (when the call gets disconnected or failed to be completed)\n");
	fprintf(fp,"own_setup       %s\n",ext_yesno[ext->own_setup]);
	fprintf(fp,"own_proceeding  %s\n",ext_yesno[ext->own_proceeding]);
	fprintf(fp,"own_alerting    %s\n",ext_yesno[ext->own_alerting]);
	fprintf(fp,"own_cause       %s\n\n",ext_yesno[ext->own_cause]);

	fprintf(fp,"# Allow facility information to be transfered to the telephone.\n");
	fprintf(fp,"# This is equired to receive advice of charge.\n");
	fprintf(fp,"facility        %s\n\n",ext_yesno[ext->facility]);

	fprintf(fp,"# Display clear causes using display messages (Q.850)\n# This must be one of the following:\n");
	fprintf(fp,"# none (no displaying of clear causes)\n");
	fprintf(fp,"# english (display cause text in english)\n");
	fprintf(fp,"# german (display cause text in german)\n");
	fprintf(fp,"# number (display cause number only)\n");
	fprintf(fp,"# english-location (display cause text in english and location)\n");
	fprintf(fp,"# german-location (display cause text in german and location)\n");
	switch(ext->display_cause)
	{
		case DISPLAY_CAUSE_ENGLISH:
		fprintf(fp,"display_cause   english\n\n");
		break;
		case DISPLAY_CAUSE_GERMAN:
		fprintf(fp,"display_cause   german\n\n");
		break;
		case DISPLAY_LOCATION_ENGLISH:
		fprintf(fp,"display_cause   english-location\n\n");
		break;
		case DISPLAY_LOCATION_GERMAN:
		fprintf(fp,"display_cause   german-location\n\n");
		break;
		case DISPLAY_CAUSE_NUMBER:
		fprintf(fp,"display_cause   number\n\n");
		break;
		default:
		fprintf(fp,"display_cause   none\n\n");
	}

	fprintf(fp,"# Display external caller ids using display override (yes or no)\n");
	fprintf(fp,"# example: \"15551212\"\n");
	fprintf(fp,"display_ext     %s\n\n",(ext->display_ext)?"yes":"no");

	fprintf(fp,"# Display internal caller ids using display override (yes or no)\n");
	fprintf(fp,"# example: \"200 (int)\"\n");
	fprintf(fp,"display_int     %s\n\n",(ext->display_int)?"yes":"no");

	fprintf(fp,"# Display if calls are anonymous using display override (yes or no)\n");
	fprintf(fp,"# This makes only sense if the anon-ignore feature is enabled.\n");
	fprintf(fp,"# example: \"15551212 anon\"\n");
	fprintf(fp,"display_anon    %s\n\n",(ext->display_anon)?"yes":"no");

	fprintf(fp,"# Display fake caller ids using display override (yes or no)\n");
	fprintf(fp,"# If the caller uses \"clip no screening\", you will see if the number is\n");
	fprintf(fp,"# real or fake\n");
	fprintf(fp,"# example: \"15551212 fake\"\n");
	fprintf(fp,"display_fake    %s\n\n",(ext->display_fake)?"yes":"no");

	fprintf(fp,"# Display caller's name if available. (yes or no)\n");
	fprintf(fp,"# example: \"15551212 Axel\"\n");
	fprintf(fp,"display_name    %s\n\n",(ext->display_name)?"yes":"no");

	fprintf(fp,"# Display menu when '*' and '#' is pressed. The menu shows all prefixes for\n");
	fprintf(fp,"# internal dialing by pressing '*' for previous prefix and '#' for next prefix.\n");
	fprintf(fp,"# Also the dialed prefix is show on display. (yes or no)\n");
	fprintf(fp,"display_menu    %s\n\n",(ext->display_menu)?"yes":"no");

	fprintf(fp,"# Display digits as they are interpreted by pbx. (yes or no)\n");
	fprintf(fp,"display_dialing %s\n\n",(ext->display_dialing)?"yes":"no");

	fprintf(fp,"# Tones directory for announcements and patterns\n");
	fprintf(fp,"# Enter nothing for default tones as selected by options.conf.\n");
	fprintf(fp,"tones_dir       %s\n\n",ext->tones_dir);

	fprintf(fp,"# Record calls to extension's directory. The file is written as wave.\n");
	fprintf(fp,"# This must be one of the following:\n");
	fprintf(fp,"# off (no recording)\n");
	fprintf(fp,"# mono (records wave 16 bit mono, 128kbits/s)\n");
	fprintf(fp,"# stereo (records wave 32 bit stereo, 256kbits/s)\n");
	fprintf(fp,"# 8bit (records wave 8 bit mono, 64kbits/s)\n");
	fprintf(fp,"# law (records xLaw encoded, as specified in options.conf, 64kbps/s)\n");
	switch(ext->record)
	{
		case CODEC_MONO:
		fprintf(fp,"record          mono\n\n");
		break;
		case CODEC_STEREO:
		fprintf(fp,"record          stereo\n\n");
		break;
		case CODEC_8BIT:
		fprintf(fp,"record          8bit\n\n");
		break;
		case CODEC_LAW:
		fprintf(fp,"record          law\n\n");
		break;
		default:
		fprintf(fp,"record          off\n\n");
	}

	fprintf(fp,"# Password for callback and login\n");
	fprintf(fp,"# Enter nothing if callback and login should not be possible.\n");
	fprintf(fp,"password        %s\n\n",ext->password);

	fprintf(fp,"# The Answering Machine. Enter the mode of answering machine.\n");
	fprintf(fp,"# This must be one of the following:\n");
	fprintf(fp,"# normal (plays announcement and records after that)\n");
	fprintf(fp,"# parallel (plays announcement and records also DURING announcement.)\n");
	fprintf(fp,"# announcement (just plays announcement and hangs up)\n");
	switch(ext->vbox_mode)
	{
		case VBOX_MODE_PARALLEL:
		fprintf(fp,"vbox_mode       parallel\n\n");
		break;
		case VBOX_MODE_ANNOUNCEMENT:
		fprintf(fp,"vbox_mode       announcement\n\n");
		break;
		default:
		fprintf(fp,"vbox_mode       normal\n\n");
	}

	fprintf(fp,"# The Answering Machine. Enter the type of codec for recording.\n");
	fprintf(fp,"# This must be one of the following:\n");
	fprintf(fp,"# law (alaw/ulas codec, as specified in options.conf)\n");
	fprintf(fp,"# mono (16 bit mono wave file)\n");
	fprintf(fp,"# stereo (16 bit stereo wave file)\n");
	fprintf(fp,"# 8bit (8 bit mono wave file)\n");
	switch(ext->vbox_codec)
	{
		case CODEC_LAW:
		fprintf(fp,"vbox_codec      law\n\n");
		break;
		case CODEC_STEREO:
		fprintf(fp,"vbox_codec      stereo\n\n");
		break;
		case CODEC_8BIT:
		fprintf(fp,"vbox_codec      8bit\n\n");
		break;
		default:
		fprintf(fp,"vbox_codec      mono\n\n");
	}

	fprintf(fp,"# The Answering Machine. Enter maximum time to record after announcement.\n");
	fprintf(fp,"# Leave empty, enter \"infinite\" or give time in seconds.\n");
	fprintf(fp,"# Enter nothing if callback and login should not be possible.\n");
	if (ext->vbox_time)
		fprintf(fp,"vbox_time       %d\n\n",ext->vbox_time);
	else
		fprintf(fp,"vbox_time       infinite\n\n");

	fprintf(fp,"# The Answering Machine. Enter mode for display current state.\n");
	fprintf(fp,"# This must be one of the following:\n");
	fprintf(fp,"# brief (displays brief information, for small displays)\n");
	fprintf(fp,"# detailed (displays detailed information, for larger displays)\n");
	fprintf(fp,"# off (don't display anything)\n");
	switch(ext->vbox_display)
	{
		case VBOX_DISPLAY_OFF:
		fprintf(fp,"vbox_display    off\n\n");
		break;
		case VBOX_DISPLAY_DETAILED:
		fprintf(fp,"vbox_display    detailed\n\n");
		break;
		default:
		fprintf(fp,"vbox_display    brief\n\n");
	}

	fprintf(fp,"# The Answering Machine. Enter type of language: \"english\" or \"german\"\n");
	fprintf(fp,"# Display information of the menu, will be provided as specified.\n");
	fprintf(fp,"# The menu's voice is located in \"vbox_english\" and \"vbox_german\".\n");
	if (ext->vbox_language)
		fprintf(fp,"vbox_language   german\n\n");
	else
		fprintf(fp,"vbox_language   english\n\n");

	fprintf(fp,"# The Answering Machine. Enter email to send incoming messages to:\n");
	fprintf(fp,"# All incoming message will be send to the given address.\n");
	fprintf(fp,"# The audio file is attached if \"vbox_email_file\" is 'yes'\n");
	fprintf(fp,"vbox_email      %s\n", ext->vbox_email);
	fprintf(fp,"vbox_email_file %s\n\n",ext_yesno[ext->vbox_email_file]);

	fprintf(fp,"# If audio path is connected prior answering of a call, say 'yes'\n");
	fprintf(fp,"# will cause the call to be billed after playing the announcement. (yes or no)\n");
	fprintf(fp,"vbox_free       %s\n\n",(ext->vbox_free)?"yes":"no");

	fprintf(fp,"# Accept incoming data calls as it would be an audio call.\n");
	fprintf(fp,"datacall        %s\n\n",ext_yesno[ext->datacall]);

	fprintf(fp,"# Include seconds (time) in the connect message. (Should be always enabled.)\n");
	fprintf(fp,"seconds         %s\n\n",ext_yesno[1-ext->no_seconds]);

	fprintf(fp,"# Last outgoing and incoming numbers (including prefix)\n");
	i = 0;
	while(i < MAX_REMEMBER)
	{
		if (ext->last_out[i][0])
			fprintf(fp,"last_out        %s\n",ext->last_out[i]);
		i++;
	}
	i = 0;
	while(i < MAX_REMEMBER)
	{
		if (ext->last_in[i][0])
			fprintf(fp,"last_in         %s\n",ext->last_in[i]);
		i++;
	}
	fprintf(fp,"\n");


	if (fp) fclose(fp);
	return(1);
}


/* write log for extension
 *
 */
int write_log(char *number, char *callerid, char *calledid, time_t start, time_t stop, int aoce, int cause, int location)
{
	char *mon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	FILE *fp=NULL;
	char filename[256];
	struct tm *tm;

	if (callerid[0] == '\0')
		callerid = "<unknown>";

	SPRINT(filename, "%s/%s/%s/log", INSTALL_DATA, options.extensions_dir, number);

	if (!(fp = fopen(filename, "a")))
	{
		PERROR("Cannot open log: \"%s\"\n", filename);
		return(0);
	}

	tm = localtime(&start);
	fprintf(fp,"%s %2d %04d %02d:%02d:%02d %s", mon[tm->tm_mon], tm->tm_mday, tm->tm_year+1900, tm->tm_hour, tm->tm_min, tm->tm_sec, number);
	if (stop)
		fprintf(fp," %2ld:%02ld:%02ld", (stop-start)/3600, (((unsigned long)(stop-start))/60)%60, ((unsigned long)(stop-start))%60);
	else
		fprintf(fp," --:--:--");
	fprintf(fp," %s -> %s", callerid, calledid);
	if (cause >= 1 && cause <=127 && location>=0 && location<=15)
		fprintf(fp," (cause=%d '%s' location=%d '%s')", cause, isdn_cause[cause].german, location, isdn_location[location].german);
	fprintf(fp,"\n");

	if (fp) fclose(fp);
	return(1);
}


/* parse phonebook
 *
 * reads phone book of extextension and compares the given elements which
 * are: abreviation, phone number, name (name is not compared)
 * on success a 1 is returned and the pointers of elements are set to the
 * result.
 */
int parse_phonebook(char *number, char **abbrev_pointer, char **phone_pointer, char **name_pointer)
{
	FILE *fp=NULL;
	char filename[256];
	char *p;
	static char abbrev[32], phone[256], name[256];
	unsigned int line,i;
	char buffer[1024];
	int found = 0, found_if_more_digits = 0;

	SPRINT(filename, "%s/%s/%s/phonebook", INSTALL_DATA, options.extensions_dir, number);

	if (!(fp = fopen(filename, "r")))
	{
		PERROR("Cannot open phonebook: \"%s\"\n", filename);
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

		abbrev[0]=0;
		phone[0]=0;
		name[0]=0;

		i=0; /* read abbrev */
		while(*p > 32)
		{
			if (i+1 >= sizeof(abbrev))
			{
				PERROR_RUNTIME("Error in %s (line %d): abbrev too long.\n",filename,line);
				break;
			}
			abbrev[i+1] = '\0';
			abbrev[i++] = *p++;
		}

		while(*p <= 32) /* skip spaces */
		{
			if (*p == 0)
				break;
			p++;
		}

		if (*p!=0 && *p!='#') /* phone */
		{
			i=0; /* read phone */
			while(*p > 32)
			{
				if (i+1 >= sizeof(phone))
				{
					PERROR_RUNTIME("Error in %s (line %d): phone too long.\n",filename,line);
					break;
				}
				phone[i+1] = '\0';
				phone[i++] = *p++;
			}
			while(*p <= 32) /* skip spaces */
			{
				if (*p == 0)
					break;
				p++;
			}
		}

		if (*p!=0 && *p!='#') /* name */
		{
			i=0; /* read name */
			while(*p > 0)
			{
				if (i+1 >= sizeof(name))
				{
					PERROR_RUNTIME("Error in %s (line %d): name too long.\n",filename,line);
					break;
				}
				name[i+1] = '\0';
				name[i++] = *p++;
			}
		}

		if (*abbrev_pointer)
		{
			if (!strncmp(*abbrev_pointer, abbrev, strlen(*abbrev_pointer)))
			{
				/* may match if abbreviation is longer */
				found_if_more_digits = 1;
			}
			if (!!strcasecmp(*abbrev_pointer, abbrev))
				continue;
		}
		if (*phone_pointer)
			if (!!strcasecmp(*phone_pointer, phone))
				continue;
		if (*name_pointer)
			if (!!strcasecmp(*name_pointer, name))
				continue;

		found = 1;
		break; /* found entry */
	}

	if (fp) fclose(fp);

	if (found)
	{
		*abbrev_pointer = abbrev;
		*phone_pointer = phone;
		*name_pointer = name;
	}

	if (found == 0)
	{
		if (found_if_more_digits)
			found = -1;
	}
	return(found);
}

/* parsing secrets file
 *
 * 'number' specifies the externsion number, not the caller id
 * 'remote_id' specifies the dialed number, or the caller id for incoming calls
 * the result is the auth, crypt and key string, and 1 is returned.
 * on failure or not matching number, the 0 is returned
 */
int parse_secrets(char *number, char *remote_id, char **auth_pointer, char **crypt_pointer, char **key_pointer)
{
	FILE *fp=NULL;
	char filename[256];
	char *p;
	char remote[128];
	static char auth[64], crypt[64], key[4096];
	unsigned int line,i;
	char buffer[4096];
	int found = 0;

	SPRINT(filename, "%s/%s/%s/secrets", INSTALL_DATA, options.extensions_dir, number);

	if (!(fp = fopen(filename, "r")))
	{
		PERROR("Cannot open secrets: \"%s\"\n", filename);
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

		remote[0]=0;
		auth[0]=0;
		crypt[0]=0;
		key[0]=0;

		i=0; /* read auth */
		while(*p > 32)
		{
			if (i+1 >= sizeof(remote))
			{
				PERROR_RUNTIME("Error in %s (line %d): remote too long.\n",filename,line);
				break;
			}
			remote[i+1] = '\0';
			remote[i++] = *p++;
		}

		while(*p <= 32) /* skip spaces */
		{
			if (*p == 0)
				break;
			p++;
		}

		if (*p!=0 && *p!='#') /* auth */
		{
			i=0; /* read auth */
			while(*p > 32)
			{
				if (i+1 >= sizeof(auth))
				{
					PERROR_RUNTIME("Error in %s (line %d): auth too long.\n",filename,line);
					break;
				}
				auth[i+1] = '\0';
				auth[i++] = *p++;
			}
			while(*p <= 32) /* skip spaces */
			{
				if (*p == 0)
					break;
				p++;
			}
		}

		if (*p!=0 && *p!='#') /* crypt */
		{
			i=0; /* read crypt */
			while(*p > 32)
			{
				if (i+1 >= sizeof(crypt))
				{
					PERROR_RUNTIME("Error in %s (line %d): crypt too long.\n",filename,line);
					break;
				}
				crypt[i+1] = '\0';
				crypt[i++] = *p++;
			}
			while(*p <= 32) /* skip spaces */
			{
				if (*p == 0)
					break;
				p++;
			}
		}

		if (*p!=0 && *p!='#') /* key */
		{
			i=0; /* read key */
			while(*p > 0)
			{
				if (i+1 >= sizeof(key))
				{
					PERROR_RUNTIME("Error in %s (line %d): key too long.\n",filename,line);
					break;
				}
				key[i+1] = '\0';
				key[i++] = *p++;
			}
		}
//printf("COMPARING: '%s' with '%s' %s %s %s\n", remote_id, remote, auth, crypt, key);

		if (!!strcasecmp(remote, remote_id))
			continue;

		found = 1;
		break; /* found entry */
	}

	if (fp) fclose(fp);

	if (found)
	{
		*auth_pointer = auth;
		*crypt_pointer = crypt;
		*key_pointer = key;
	}

	return(found);
}

/* parse directory
 *
 * the caller id is given and the name is returned. if the name is not found,
 * NULL is returned.
 * on success a 1 is returned and the pointers of elements are set to the
 * result.
 */
char *parse_directory(char *number, int type)
{
	FILE *fp=NULL;
	char filename[256];
	char *p;
	static char phone[32], name[64];
	unsigned int line,i;
	char buffer[256];
	int found = 0;

	SPRINT(filename, "%s/directory.list", INSTALL_DATA);

	if (!(fp = fopen(filename, "r")))
	{
		PERROR("Cannot open directory: \"%s\"\n", filename);
		return(NULL);
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

		phone[0]=0;
		name[0]=0;

		i=0; /* read number */
		while(*p > 32)
		{
			if (i+1 >= sizeof(phone))
			{
				PERROR_RUNTIME("Error in %s (line %d): number too long.\n",filename,line);
				break;
			}
			phone[i+1] = '\0';
			phone[i++] = *p++;
		}

		while(*p <= 32) /* skip spaces */
		{
			if (*p == 0)
				break;
			p++;
		}

		i=0; /* read name */
		while(*p >= 32)
		{
			if (i+1 >= sizeof(name))
			{
				PERROR_RUNTIME("Error in %s (line %d): name too long.\n",filename,line);
				break;
			}
			name[i+1] = '\0';
			name[i++] = *p++;
		}

		if (phone[0] == 'i')
		{
			if (type != INFO_NTYPE_INTERNATIONAL)
				continue;
			if (!strcmp(number, phone+1))
			{
				found = 1;
				break;
			}
			continue;
		}
		if (phone[0] == 'n')
		{
			if (type != INFO_NTYPE_NATIONAL)
				continue;
			if (!strcmp(number, phone+1))
			{
				found = 1;
				break;
			}
			continue;
		}
		if (phone[0] == 's')
		{
			if (type==INFO_NTYPE_NATIONAL || type==INFO_NTYPE_INTERNATIONAL)
				continue;
			if (!strcmp(number, phone+1))
			{
				found = 1;
				break;
			}
			continue;
		}
		if (!strncmp(phone, options.international, strlen(options.international)))
		{
			if (type != INFO_NTYPE_INTERNATIONAL)
				continue;
			if (!strcmp(number, phone+strlen(options.international)))
			{
				found = 1;
				break;
			}
			continue;
		}
		if (!options.national[0]) /* no national prefix */
		{
			if (type == INFO_NTYPE_INTERNATIONAL)
				continue;
			if (!strcmp(number, phone))
			{
				found = 1;
				break;
			}
			continue;
		}
		if (!strncmp(phone, options.national, strlen(options.national)))
		{
			if (type != INFO_NTYPE_NATIONAL)
				continue;
			if (!strcmp(number, phone+strlen(options.national)))
			{
				found = 1;
				break;
			}
			continue;
		}
		if (type==INFO_NTYPE_NATIONAL || type==INFO_NTYPE_INTERNATIONAL)
			continue;
		if (!strcmp(number, phone))
		{
			found = 1;
			break;
		}
	}

	if (fp) fclose(fp);

	if (found)
		return(name);
	else
		return(NULL);
}

/* parse callbackauth
 *
 * searches for the given caller id and returns 1 == true or 0 == false
 */
int parse_callbackauth(char *number, struct caller_info *callerinfo)
{
	FILE *fp = NULL;
	char filename[256];
	char *p;
	unsigned int line,i;
	char buffer[256];
	static char caller_type[32], caller_id[64];
	int found = 0;

	SPRINT(filename, "%s/%s/%s/callbackauth", INSTALL_DATA, options.extensions_dir, number);

	if (!(fp = fopen(filename, "r")))
	{
		PDEBUG(DEBUG_EPOINT, "Cannot open callbackauth: \"%s\"\n", filename);
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

		caller_type[0]=0;
		caller_id[0]=0;

		i=0; /* read caller_type */
		while(*p > 32)
		{
			if (i+1 >= sizeof(caller_type))
			{
				PERROR_RUNTIME("Error in %s (line %d): caller_type too long.\n",filename,line);
				break;
			}
			caller_type[i+1] = '\0';
			caller_type[i++] = *p++;
		}

		while(*p <= 32) /* skip spaces */
		{
			if (*p == 0)
				break;
			p++;
		}

		if (*p!=0 && *p!='#') /* caller_id */
		{
			i=0; /* read caller_id */
			while(*p > 32)
			{
				if (i+1 >= sizeof(caller_id))
				{
					PERROR_RUNTIME("Error in %s (line %d): caller_id too long.\n",filename,line);
					break;
				}
				caller_id[i+1] = '\0';
				caller_id[i++] = *p++;
			}
			// ignoring more
		}

		if (caller_type[0]=='\0' && caller_id[0]=='\0')
			continue;

		if (atoi(caller_type) != callerinfo->ntype)
			continue;

		if (!!strcmp(caller_id, callerinfo->id))
			continue;

		found = 1;
		break; /* found entry */
	}

	if (fp) fclose(fp);

	if (found)
		return(1);
	return(0);
}


/* append line to callbackauth
 *
 */
void append_callbackauth(char *number, struct caller_info *callerinfo)
{
	FILE *fp = NULL;
	char filename[256];

	SPRINT(filename, "%s/%s/%s/callbackauth", INSTALL_DATA, options.extensions_dir, number);

	if (callerinfo->id[0]=='\0')
	{
		PERROR("caller has no id.\n");
		return;
	}
	if (!(fp = fopen(filename, "a")))
	{
		PERROR("Cannot open callbackauth: \"%s\"\n", filename);
		return;
	}

	fprintf(fp, "%6d  %s\n", callerinfo->ntype, callerinfo->id);

	fclose(fp);

}


