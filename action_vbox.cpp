/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** dialing for answering machine is processed here                           **
**                                                                           **
\*****************************************************************************/ 

#include "main.h"


// note: the given display message (e_vbox_display) may include "%s" for the counter

/*
 * these are the state, the vbox is in. if the current tone has been played,
 * an action will be calles as defined in vbox_message_eof(), which is called 
 * from Epoint:handler().
 * also this state is used to determine the correct processing of the current key press
 */
enum {
	VBOX_STATE_MENU,		/* tell the menu */
VBOX_STATE_CALLINFO_BEGIN, /* this value defines the start of callinfo */
	VBOX_STATE_CALLINFO_INTRO,	/* tell that the "call is received at" */
	VBOX_STATE_CALLINFO_MONTH,	/* tell the month */
	VBOX_STATE_CALLINFO_DAY,	/* tell the day */
	VBOX_STATE_CALLINFO_HOUR,	/* tell the hour */
	VBOX_STATE_CALLINFO_OCLOCK,	/* tell the word "o'clock" */
	VBOX_STATE_CALLINFO_MIN,	/* tell the minute */
	VBOX_STATE_CALLINFO_MINUTES,	/* tell the word "minutes" */
	VBOX_STATE_CALLINFO_DIGIT,	/* tell the digits */
VBOX_STATE_CALLINFO_END, /* this value defines the end of callingo */
	VBOX_STATE_NOTHING,		/* tells that no calls are recorded */
	VBOX_STATE_PLAY,		/* play the current recording */
	VBOX_STATE_PAUSE,		/* tell that the recording is paused */
	VBOX_STATE_RECORD_ASK,	/* ask for recording */	
	VBOX_STATE_RECORD_PLAY,	/* play recording */	
	VBOX_STATE_RECORD_RECORD,	/* record recording */	
	VBOX_STATE_STORE_ASK,	/* ask for store */
	VBOX_STATE_DELETE_ASK,	/* ask for delete */
	VBOX_STATE_STORE_DONE,	/* tell that message is store */
	VBOX_STATE_DELETE_DONE,	/* tell that message is delete */
};

char *months_english[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
char *months_german[] = {"Jan","Feb","Maer","Apr","Mai","Jun","Jul","Aug","Sep","Okt","Nov","Dez"};

struct vbox_menu {
	char digit;
	char *english;
	char *german;
	} vbox_menu[] = {
	{'1', "<< previous", "<< zurueck"},
	{'2', "-> play", "-> anhoeren"},
	{'3', ">> next", ">> vor"},
	{'4', "<  rewind", "<  rueckspulen"},
	{'5', "[] stop", "[] stop"},
	{'6', ">  wind", ">  vorspulen"},
	{'7', "() record", "() Aufnahme"},
	{'8', "=  store", "=  speichern"},
	{'9', "X  delete", "X  loeschen"},
	{'0', "*  call", "*  anrufen"},
	{'\0', NULL, NULL}
	};

/*
 * initialize the vbox. this is called at process_dialing(), when the VBOX_PLAY
 * action has been selected by the caller
 */
void EndpointAppPBX::action_init_vbox_play(void)
{
	int			language = e_ext.vbox_language;
	struct route_param	*rparam;
	struct lcr_msg		*message;
	struct port_list	*portlist = ea_endpoint->ep_portlist;

	/* get extension */
	SCPY(e_vbox, e_ext.number);
	if ((rparam = routeparam(e_action, PARAM_EXTENSION)))
		SCPY(e_vbox, rparam->string_value);
	if (e_vbox[0] == '\0')
	{
		/* facility rejected */
		message_disconnect_port(portlist, CAUSE_FACILITYREJECTED, LOCATION_PRIVATE_LOCAL, "");
		new_state(EPOINT_STATE_OUT_DISCONNECT);
		set_tone(portlist,"cause_22");
		return;
	}

	/* connect, but still accept more digits */
	new_state(EPOINT_STATE_IN_OVERLAP);
	if (e_ext.number[0])
		e_dtmf = 1;
	message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_CONNECT);
	message_put(message);
	logmessage(message->type, &message->param, portlist->port_id, DIRECTION_OUT);

	/* initialize the vbox */
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) initializing answering vbox state\n", ea_endpoint->ep_serial);

	e_vbox_state = VBOX_STATE_MENU;
	SCPY(e_vbox_display, (char *)((language)?"druecke 2 f. wiedergabe":"press 2 to play"));
	e_vbox_display_refresh = 1;
	set_tone_vbox("menu");

	e_vbox_menu = -1;
	e_vbox_play = 0;
	vbox_index_read(e_vbox_play);
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) number of calls: %d\n", ea_endpoint->ep_serial, e_vbox_index_num);

	if (e_vbox_index_num == 0)
	{
		e_vbox_state = VBOX_STATE_NOTHING;
		SCPY(e_vbox_display, (char *)((language)?"keine Anrufe":"no calls"));
		e_vbox_display_refresh = 1;
		set_tone_vbox("nothing");
	}
}

/*
 * read index list, and fill the index variables with the given position
 * if the index is empty (or doesn't exist), the variables are not filled.
 * but alway the e_vbox_index_num is given.
 */
void EndpointAppPBX::vbox_index_read(int num)
{
	FILE *fp;
	char filename[256];
	char buffer[256];
	char name[sizeof(buffer)];
	char callerid[sizeof(buffer)];
	int year, mon, mday, hour, min;
	int i;

	e_vbox_index_num = 0;

	SPRINT(filename, "%s/%s/%s/vbox/index", INSTALL_DATA, options.extensions_dir, e_vbox);
	if (!(fp = fopen(filename, "r")))
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) no files in index\n", ea_endpoint->ep_serial);
		return;
	}
	fduse++;

	i = 0;
	while((fgets(buffer,sizeof(buffer),fp)))
	{
		buffer[sizeof(buffer)-1] = '\0';
		if (buffer[0]) buffer[strlen(buffer)-1] = '\0';

		name[0] = callerid[0] = '\0';
		mon = mday = hour = min = 0;
		sscanf(buffer, "%s %d %d %d %d %d %s", name, &year, &mon, &mday, &hour, &min, callerid);

		if (name[0]=='\0' || name[0]=='#')
			continue;

		/* the selected entry */
		if (i == num)
		{
			SCPY(e_vbox_index_file, name);
			e_vbox_index_year = year;
			e_vbox_index_mon = mon;
			e_vbox_index_mday = mday;
			e_vbox_index_hour = hour;
			e_vbox_index_min = min;
			SCPY(e_vbox_index_callerid, callerid);
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) read entry #%d: '%s', %02d:%02d %02d:%02d cid='%s'\n", ea_endpoint->ep_serial, i, name, mon+1, mday, hour, min, callerid);
		}

		i++;
	}

	e_vbox_index_num = i;

	fclose(fp);
	fduse--;
}


/*
 * removes given index from list
 * after removing, the list should be reread, since e_vbox_index_num
 * and the current variabled do not change
 */
void EndpointAppPBX::vbox_index_remove(int num)
{
	FILE *fpr, *fpw;
	char buffer[256];
	int i;
	char filename1[256], filename2[256];

	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) removing entrie #%d\n", ea_endpoint->ep_serial, num);

	SPRINT(filename1, "%s/%s/%s/vbox/index", INSTALL_DATA, options.extensions_dir, e_vbox);
	SPRINT(filename2, "%s/%s/%s/vbox/index-temp", INSTALL_DATA, options.extensions_dir, e_vbox);
	if (!(fpr = fopen(filename1, "r")))
	{
		return;
	}
	if (!(fpw = fopen(filename2, "w")))
	{
		fclose(fpr);
		return;
	}
	fduse += 2;

	i = 0;
	while((fgets(buffer,sizeof(buffer),fpr)))
	{
		buffer[sizeof(buffer)-1] = '\0';
		if (buffer[0]) buffer[strlen(buffer)-1] = '\0';

		if (buffer[0]=='\0' || buffer[0]=='#')
		{
			fprintf(fpw, "%s\n", buffer);
			continue;	
		}

		/* the selected entry will not be written */
		if (i != num)
		{
			fprintf(fpw, "%s\n", buffer);
		}

		i++;
	}

	fclose(fpr);
	fclose(fpw);
	fduse -= 2;

	rename(filename2, filename1);
}


/*
 * process dialing of vbox_play (actually the menu)
 * it is depended by the state, which action is performed
 */
void EndpointAppPBX::action_dialing_vbox_play(void)
{
	int language = e_ext.vbox_language;
	struct port_list *portlist;
	class Port *port;
	
	portlist = ea_endpoint->ep_portlist;

	if (e_extdialing[0] == '\0')
	{
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) called with no digit\n", ea_endpoint->ep_serial);
		return;
	}

	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) dialing digit: %c\n", ea_endpoint->ep_serial, e_extdialing[0]);

	e_vbox_display_refresh = 1;

	if (e_vbox_state == VBOX_STATE_RECORD_RECORD)
	{
		if (e_extdialing[0] == '1' || e_extdialing[0] == '0')
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) stopping recording of announcement.\n", ea_endpoint->ep_serial);

			port = find_port_id(portlist->port_id);
			if (port)
				port->close_record((e_extdialing[0]=='1')?6000:0, 2000); /* append beep */
			goto record_ask;
		}
		goto done;
	}

	if (e_vbox_state == VBOX_STATE_RECORD_PLAY)
	{
		if (e_extdialing[0] == '1')
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) stopping playback of announcement.\n", ea_endpoint->ep_serial);

			goto record_ask;
		}
		goto done;
	}

	if (e_vbox_state == VBOX_STATE_RECORD_ASK)
	{
		switch(e_extdialing[0])
		{
			case '3':
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) quit recoding menu.\n", ea_endpoint->ep_serial);
			ask_abort:
			/* abort */
			e_vbox_state = VBOX_STATE_MENU;
			SCPY(e_vbox_display, (char *)((language)?"druecke 2 f. wiedergabe":"press 2 to play"));
			set_tone_vbox("menu");
			break;

			case '2':
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) play recoding.\n", ea_endpoint->ep_serial);
			/* play announcement */
			e_vbox_counter = 0;
			e_vbox_counter_last = 0;
			e_vbox_counter_max = 0;
			e_vbox_speed = 1;
			e_vbox_state = VBOX_STATE_RECORD_PLAY;
			if (e_ext.vbox_language)
				SCPY(e_vbox_display, "Wied., 1=stop %s");
			else
				SCPY(e_vbox_display, "play, 1=stop %s");
			if (e_ext.vbox_display == VBOX_DISPLAY_BRIEF)
				SCPY(e_vbox_display, "1=stop %s");
			set_play_vbox("announcement", 0);
			break;

			case '1':
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) record announcement.\n", ea_endpoint->ep_serial);
			/* close recording if already recording */
			port = find_port_id(portlist->port_id);
			if (port)
			{
				port->close_record(0,0); 
				port->open_record(CODEC_MONO, 1, 4000, e_ext.number, 0, "", 0); /* record announcement, skip the first 4000 samples */
			}
			e_vbox_state = VBOX_STATE_RECORD_RECORD;
			if (e_ext.vbox_language)
				SCPY(e_vbox_display, "Aufnahme, 1=stop");
			else
				SCPY(e_vbox_display, "recording, 1=stop");
			set_tone_vbox(NULL);
			break;

			default:
			;
		}
		goto done;
	}

	if (e_vbox_state==VBOX_STATE_STORE_ASK || e_vbox_state==VBOX_STATE_DELETE_ASK)
	{
		char filename[256], filename2[256];

		switch(e_extdialing[0])
		{
			case '3':
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) quit store/delete menu.\n", ea_endpoint->ep_serial);
			goto ask_abort;

			case '1':
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) do store/delete.\n", ea_endpoint->ep_serial);
			SPRINT(filename, "%s/%s/%s/vbox/%s", INSTALL_DATA, options.extensions_dir, e_vbox, e_vbox_index_file);

			/* move file */
			if (e_vbox_state == VBOX_STATE_STORE_ASK)
			{
				SPRINT(filename, "%s/%s/%s/recordings", INSTALL_DATA, options.extensions_dir, e_vbox);
				if (mkdir(filename, 0755) < 0)
				{
					if (errno != EEXIST)
					{
						PERROR("EPOINT(%d) cannot create directory '%s'\n", ea_endpoint->ep_serial, filename);
						goto done;
					}
				}
				SPRINT(filename2, "%s/%s/%s/recordings/%s", INSTALL_DATA, options.extensions_dir, e_vbox, e_vbox_index_file);
				rename(filename, filename2);
				e_vbox_state = VBOX_STATE_STORE_DONE;
				if (e_ext.vbox_language)
					SCPY(e_vbox_display, "Nachricht gespeichert!");
				else
					SCPY(e_vbox_display, "Message stored!");
				set_tone_vbox("store_done");
			}

			/* remove file */
			if (e_vbox_state == VBOX_STATE_DELETE_ASK)
			{
				remove(filename);
				e_vbox_state = VBOX_STATE_DELETE_DONE;
				if (e_ext.vbox_language)
					SCPY(e_vbox_display, "Nachricht geloescht!");
				else
					SCPY(e_vbox_display, "Message deleted!");
				set_tone_vbox("delete_done");
			}

			/* remove from list */
			vbox_index_remove(e_vbox_play);
			vbox_index_read(e_vbox_play);
			/* stay at the last message+1, so we always get "no messages" */
			if (e_vbox_play>e_vbox_index_num && e_vbox_play)
			{
				e_vbox_play = e_vbox_index_num-1;
			}
			default:
			;
		}
		goto done;
	}

	/* dialing during menu */
	switch(e_extdialing[0])
	{
		/* process the vbox functions */
		case '1': /* previous */
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) previous call is selected.\n", ea_endpoint->ep_serial);
		if (e_vbox_index_num == 0) /* nothing to play */
		{
			no_calls:
			e_vbox_state = VBOX_STATE_MENU;
			SCPY(e_vbox_display, (char *)((language)?"keine Anrufe":"no calls"));
			set_tone_vbox("nothing");
			break;
		}
		e_vbox_play--;
		if (e_vbox_play < 0)
		{
			e_vbox_play = 0;

			e_vbox_state = VBOX_STATE_MENU;
			SCPY(e_vbox_display, (char *)((language)?"kein vorheriger Anruf":"no previous call"));
			set_tone_vbox("nothing");
			break;
		}
		/* announce call */
		announce_call:
		e_vbox_state = VBOX_STATE_CALLINFO_INTRO;
		SPRINT(e_vbox_display, "#%d", e_vbox_play+1);
		vbox_index_read(e_vbox_play);
		if (e_vbox_index_mon!=now_tm->tm_mon || e_vbox_index_year!=now_tm->tm_year)
		{
			UPRINT(strchr(e_vbox_display,'\0'), " %s", (language)?months_german[e_vbox_index_mon]:months_english[e_vbox_index_mon]);
		}
		if (e_vbox_index_mday!=now_tm->tm_mday || e_vbox_index_mon!=now_tm->tm_mon || e_vbox_index_year!=now_tm->tm_year)
		{
			UPRINT(strchr(e_vbox_display,'\0'), " %d", e_vbox_index_mday);
		}
		UPRINT(strchr(e_vbox_display,'\0'), " %02d:%02d", e_vbox_index_hour, e_vbox_index_min);
		if (e_ext.vbox_display == VBOX_DISPLAY_DETAILED)
			UPRINT(strchr(e_vbox_display,'\0'), " (%s)", e_vbox_index_callerid);
		set_tone_vbox("intro");
		break;

		case '2': /* play */
		if (e_vbox_play >= e_vbox_index_num)
			goto no_messages;
		if (e_vbox_index_num == 0) /* nothing to play */
		{
			goto no_calls;
		}
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) play call #%d.\n", ea_endpoint->ep_serial, e_vbox_play+1);
		if (e_vbox_state>VBOX_STATE_CALLINFO_BEGIN && e_vbox_state<VBOX_STATE_CALLINFO_END)
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) play call #%d. abborting announcement and starting with playback\n", ea_endpoint->ep_serial, e_vbox_play+1);
			/* the callinfo is played, so we start with the call */
			e_vbox_counter = 0;
			e_vbox_counter_last = 0;
			e_vbox_counter_max = 0;
			e_vbox_speed = 1;
			e_vbox_state = VBOX_STATE_PLAY;
			SPRINT(e_vbox_display, "#%d %%s", e_vbox_play+1);
			if (e_ext.vbox_display == VBOX_DISPLAY_DETAILED)
				UPRINT(strchr(e_vbox_display,'\0'), " (%s)", e_vbox_index_callerid);
			set_play_vbox(e_vbox_index_file, 0);
			break;
		} else
		if (e_vbox_state==VBOX_STATE_PLAY && e_vbox_speed!=1)
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) play call #%d. play speed is different from 1, so we play now with normal speed\n", ea_endpoint->ep_serial, e_vbox_play+1);
			/* we set play speed to normal */
			e_vbox_speed = 1;
			set_play_speed(e_vbox_speed);
		} else
		if (e_vbox_state == VBOX_STATE_PLAY)
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) play call #%d. play speed is equals 1, so we pause\n", ea_endpoint->ep_serial, e_vbox_play+1);
			/* we pause the current play */
			e_vbox_state = VBOX_STATE_PAUSE;
			SCPY(e_vbox_display, (char *)((language)?"druecke 2 f. wiedergabe":"press 2 to play"));
			set_tone_vbox("pause");
		} else
		if (e_vbox_state == VBOX_STATE_PAUSE)
		{
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) play call #%d. currently pause, so we continue play\n", ea_endpoint->ep_serial, e_vbox_play+1);
			/* we continue the current play */
			e_vbox_state = VBOX_STATE_PLAY;
			SPRINT(e_vbox_display, "#%d %%s", e_vbox_play+1);
			if (e_ext.vbox_display == VBOX_DISPLAY_DETAILED)
				UPRINT(strchr(e_vbox_display,'\0'), " (%s)", e_vbox_index_callerid);
			set_play_vbox(e_vbox_index_file, e_vbox_counter);
		} else
		{
			/* now we have something else going on, so we announce the call */
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) play call #%d. announcing call during any other state\n", ea_endpoint->ep_serial, e_vbox_play+1);
			goto announce_call;
		}
		break;

		case '3': /* next */
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) next call is selected.\n", ea_endpoint->ep_serial);
		if (e_vbox_index_num == 0) /* nothing to play */
		{
			goto no_calls;
		}
		e_vbox_play++;
		if (e_vbox_play >= e_vbox_index_num)
		{
			no_messages:
			e_vbox_play = e_vbox_index_num;

			e_vbox_state = VBOX_STATE_MENU;
			SCPY(e_vbox_display, (char *)((language)?"kein weiterer Anruf":"no next call"));
			set_tone_vbox("nothing");
			break;
		}
		/* announce call */
		goto announce_call;
		break;

		case '4': /* rewind */
		if (e_vbox_state==VBOX_STATE_PLAY)
		{
			if (e_vbox_speed >= -1)
				e_vbox_speed = -1;
			e_vbox_speed = e_vbox_speed * 2;
			set_play_speed(e_vbox_speed);
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) rewind speed has been changed to: %d\n", ea_endpoint->ep_serial, e_vbox_speed);
		} 
		break;

		case '5': /* stop */
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) stop is pressed, so we hear the menu\n", ea_endpoint->ep_serial);
		e_vbox_state = VBOX_STATE_MENU;
		SCPY(e_vbox_display, (char *)((language)?"druecke 2 f. wiedergabe":"press 2 to play"));
		set_tone_vbox("menu");
		break;

		case '6': /* wind */
		if (e_vbox_state==VBOX_STATE_PLAY)
		{
			if (e_vbox_speed <= 1)
				e_vbox_speed = 1;
			e_vbox_speed = e_vbox_speed * 2;
			set_play_speed(e_vbox_speed);
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) wind speed has been changed to: %d\n", ea_endpoint->ep_serial, e_vbox_speed);
		} 
		break;

		case '7': /* record announcement */
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) entering the record announcement menu\n", ea_endpoint->ep_serial);
		record_ask:
		e_vbox_state = VBOX_STATE_RECORD_ASK;
		SCPY(e_vbox_display, (char *)((language)?"1=Aufn. 2=Wied. 3=nein":"1=record 2=play 3=back"));
		set_tone_vbox("record_ask");
		break;

		case '8': /* store file */
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) entering the store menu\n", ea_endpoint->ep_serial);
		if (e_vbox_play >= e_vbox_index_num)
			goto no_messages;
		if (e_vbox_index_num == 0) /* nothing to play */
		{
			goto no_calls;
		}
		e_vbox_state = VBOX_STATE_STORE_ASK;
		SCPY(e_vbox_display, (char *)((language)?"speichern 1=ja 3=nein":"store 1=yes 3=back"));
		set_tone_vbox("store_ask");
		break;

		case '9': /* delete file */
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) entering the delete menu\n", ea_endpoint->ep_serial);
		if (e_vbox_play >= e_vbox_index_num)
			goto no_messages;
		if (e_vbox_index_num == 0) /* nothing to play */
		{
			goto no_calls;
		}
		e_vbox_state = VBOX_STATE_DELETE_ASK;
		SCPY(e_vbox_display, (char *)((language)?"loeschen 1=ja 3=nein":"delete 1=yes 3=back"));
		set_tone_vbox("delete_ask");
		break;


		/* process the menu */
		case '#':
		if (e_vbox_menu < 0)
			e_vbox_menu = 0;
		else
			e_vbox_menu++;
		if (vbox_menu[e_vbox_menu].english == NULL)
			e_vbox_menu = 0;
		/* show menu */
		show_menu:
		SPRINT(e_vbox_display, "%c: %s", vbox_menu[e_vbox_menu].digit, (language)?vbox_menu[e_vbox_menu].german:vbox_menu[e_vbox_menu].english);
		break;

		case '0':
		if (e_vbox_menu < 0) /* only if menu selection is pressed before*/
		{
			/* call if phonenumber is given */
			if (e_vbox_index_num)
			if (e_vbox_index_callerid[0]!='\0' && !!strcmp(e_vbox_index_callerid,"anonymous") && !!strcmp(e_vbox_index_callerid,"unknown"))
			{
				set_tone(portlist, "dialing");
				SPRINT(e_dialinginfo.id, "extern:%s", e_vbox_index_callerid);
				e_extdialing = e_dialinginfo.id;
				e_action = NULL;
				process_dialing();
				return;
			}
			break;
		}
		e_extdialing[0] = vbox_menu[e_vbox_menu].digit;
		e_extdialing[1] = '\0';
		e_vbox_menu = -1;
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) executing selected menu:%d\n", e_extdialing[0]);
		action_dialing_vbox_play(); /* redo this method using the digit */
		return;

		case '*':
		if (e_vbox_menu < 0)
			e_vbox_menu = 0;
		else
			e_vbox_menu--;
		if (e_vbox_menu < 0)
			while(vbox_menu[e_vbox_menu+1].english) /* jump to the end */
				e_vbox_menu++;
		/* show menu */
		goto show_menu;
		break;

		default:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) unsupported digit '%c'\n", ea_endpoint->ep_serial, e_extdialing);
	}

	done:
	/* reset menu after dialing a function */
	if (e_extdialing[0]!='*' && e_extdialing[0]!='#')
		e_vbox_menu = -1;


	e_extdialing[0] = '\0';

}


/*
 * this handler is called by Epoint::handler(), whenever the action is NUMB_ACTION_VBOX_PLAY
 */
void EndpointAppPBX::vbox_handler(void)
{
	/* refresh if counter changes */
	if (e_vbox_state==VBOX_STATE_PLAY || e_vbox_state==VBOX_STATE_RECORD_PLAY)
	if (e_vbox_counter != e_vbox_counter_last)
	{
		e_vbox_counter_last = e_vbox_counter;
		e_vbox_display_refresh = 1;
	}

	/* refresh display, if required (include counter) */
	if (e_vbox_display_refresh && e_ext.vbox_display!=VBOX_DISPLAY_OFF)
	{
		char counter[32];
		struct lcr_msg *message;

		SPRINT(counter, "%02d:%02d", e_vbox_counter/60, e_vbox_counter%60);
		if (e_vbox_counter_max)
			UPRINT(strchr(counter,'\0'), " of %02d:%02d", e_vbox_counter_max/60, e_vbox_counter_max%60);

		e_vbox_display_refresh = 0;
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_portlist->port_id, EPOINT_TO_PORT, MESSAGE_NOTIFY);
		SPRINT(message->param.notifyinfo.display, e_vbox_display, counter);
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) terminal %s pending display:%s\n", ea_endpoint->ep_serial, e_ext.number, message->param.notifyinfo.display);
		message_put(message);
		logmessage(message->type, &message->param, ea_endpoint->ep_portlist->port_id, DIRECTION_OUT);
	}
}


/*
 * the audio file has ended
 * this is called by Endpoint::message_port(), whenever an audio of has been received
 */
void EndpointAppPBX::vbox_message_eof(void)
{
	char buffer[32];
	int language = e_ext.vbox_language;

	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) terminal %s end of file during state: %d\n", ea_endpoint->ep_serial, e_ext.number, e_vbox_state);

	switch(e_vbox_state)
	{
		case VBOX_STATE_MENU:
		case VBOX_STATE_NOTHING:
		e_vbox_state = VBOX_STATE_MENU;
		SCPY(e_vbox_display, (char *)((language)?"druecke 2 f. wiedergabe":"press 2 to play"));
		e_vbox_display_refresh = 1;
		set_tone_vbox("menu");
		break;

		case VBOX_STATE_PLAY:
		if (e_vbox_speed > 0)
		{
			e_vbox_state = VBOX_STATE_MENU;
			SCPY(e_vbox_display, (char *)((language)?"druecke 3 f. Naechste":"press 3 for next"));
			e_vbox_display_refresh = 1;
			set_tone_vbox("menu");
		} else
		{
			/* if we have endoffile because we were playing backwards, we continue to play forward */
			e_vbox_speed = 1;
			e_vbox_counter = 1;
			set_play_vbox(e_vbox_index_file, e_vbox_counter);
		}
		break;

		case VBOX_STATE_PAUSE:
		SCPY(e_vbox_display, (char *)((language)?"druecke 2 f. weiterspielen":"press 2 to continue"));
		e_vbox_display_refresh = 1;
		break;

		case VBOX_STATE_CALLINFO_INTRO:
		if (e_vbox_index_mday==now_tm->tm_mday && e_vbox_index_mon==now_tm->tm_mon && e_vbox_index_year==now_tm->tm_year)
			goto skip_day_month;
		e_vbox_state = VBOX_STATE_CALLINFO_MONTH; //german day
		if (e_ext.vbox_language)
			/* german starts with day */
			SPRINT(buffer, "day_%02d", e_vbox_index_mday);
		else
			/* english starts with month */
			SPRINT(buffer, "month_%02d", e_vbox_index_mon+1);
		set_tone_vbox(buffer);
		break;

		case VBOX_STATE_CALLINFO_MONTH:
		e_vbox_state = VBOX_STATE_CALLINFO_DAY; //german month
		if (e_ext.vbox_language)
		{
			/* done with month, so we send the month*/
			SPRINT(buffer, "month_%02d", e_vbox_index_mon+1);
		} else
		{
			/* done with day, so we send the day */
			SPRINT(buffer, "day_%02d", e_vbox_index_mday);
		}
		set_tone_vbox(buffer);
		break;

		case VBOX_STATE_CALLINFO_DAY: //german month
		skip_day_month:
		e_vbox_state = VBOX_STATE_CALLINFO_HOUR;
		if (e_ext.vbox_language)
		{
			if (e_vbox_index_hour == 1)
				SCPY(buffer, "number_ein");
			else
				SPRINT(buffer, "number_%02d", e_vbox_index_hour); /* 1-23 hours */
		} else
		{
			SPRINT(buffer, "number_%02d", ((e_vbox_index_hour+11)%12)+1); /* 12 hours am/pm */
		}
		set_tone_vbox(buffer);
		break;

		case VBOX_STATE_CALLINFO_HOUR:
		e_vbox_state = VBOX_STATE_CALLINFO_OCLOCK;
		if (e_ext.vbox_language)
		{
			set_tone_vbox("oclock");
		} else
		{
			if (e_vbox_index_hour >= 12)
				set_tone_vbox("oclock_pm");
			else
				set_tone_vbox("oclock_am");
		}
		break;

		case VBOX_STATE_CALLINFO_OCLOCK:
		e_vbox_state = VBOX_STATE_CALLINFO_MIN;
		if (e_ext.vbox_language)
		{
// german says "zwölfuhr und eins"
//			if (e_vbox_index_min == 1)
//				SCPY(buffer, "number_eine");
//			else
				SPRINT(buffer, "number_%02d", e_vbox_index_min); /* 1-59 minutes */
		} else
		{
			SPRINT(buffer, "number_%02d", e_vbox_index_min);
		}
		set_tone_vbox(buffer);
		break;

		case VBOX_STATE_CALLINFO_MIN:
		if (e_ext.vbox_language)
			goto start_digits;
		e_vbox_state = VBOX_STATE_CALLINFO_MINUTES;
		if (e_vbox_index_mday == 1)
			set_tone_vbox("minute");
		else
			set_tone_vbox("minutes");
		break;

		case VBOX_STATE_CALLINFO_MINUTES:
		start_digits:
		e_vbox_state = VBOX_STATE_CALLINFO_DIGIT;
		if (e_vbox_index_callerid[0]=='\0' || !strcmp(e_vbox_index_callerid,"anonymous") || !strcmp(e_vbox_index_callerid,"unknown"))
		{
			set_tone_vbox("call_anonymous");
			e_vbox_index_callerid_index = strlen(e_vbox_index_callerid);
		} else
		{
			set_tone_vbox("call_from");
			e_vbox_index_callerid_index = 0;
		}
		break;

		case VBOX_STATE_CALLINFO_DIGIT:
		while (e_vbox_index_callerid[e_vbox_index_callerid_index] && (e_vbox_index_callerid[e_vbox_index_callerid_index]<'0' || e_vbox_index_callerid[e_vbox_index_callerid_index]>'9'))
			e_vbox_index_callerid_index++;
		if (e_vbox_index_callerid[e_vbox_index_callerid_index])
		{
			SPRINT(buffer, "number_%02d", e_vbox_index_callerid[e_vbox_index_callerid_index]-'0');
			set_tone_vbox(buffer);
			e_vbox_index_callerid_index ++;
		} else
		{
			/* the callinfo is played, so we start with the call */
			e_vbox_counter = 0;
			e_vbox_counter_last = 0;
			e_vbox_counter_max = 0;
			e_vbox_speed = 1;
			e_vbox_state = VBOX_STATE_PLAY;
			SPRINT(e_vbox_display, "#%d %%s", e_vbox_play);
			if (e_ext.vbox_display == VBOX_DISPLAY_DETAILED)
				UPRINT(strchr(e_vbox_display,'\0'), " (%s)", e_vbox_index_callerid);
			e_vbox_display_refresh = 1;
			set_play_vbox(e_vbox_index_file, 0);
		}
		break;

		case VBOX_STATE_RECORD_ASK:
		set_tone_vbox("record_ask");
		e_vbox_display_refresh = 1;
		break;

		case VBOX_STATE_STORE_ASK:
		set_tone_vbox("store_ask");
		e_vbox_display_refresh = 1;
		break;

		case VBOX_STATE_DELETE_ASK:
		set_tone_vbox("delete_ask");
		e_vbox_display_refresh = 1;
		break;

		case VBOX_STATE_RECORD_PLAY:
		e_vbox_state = VBOX_STATE_RECORD_ASK;
		SCPY(e_vbox_display, (char *)((language)?"1=Aufn. 2=Wied. 3=nein":"1=record 2=play 3=no"));
		e_vbox_display_refresh = 1;
		set_tone_vbox("record_ask");
		break;

		case VBOX_STATE_STORE_DONE:
		case VBOX_STATE_DELETE_DONE:
		if (e_vbox_index_num == 0) /* nothing to play */
		{
			e_vbox_state = VBOX_STATE_MENU;
			SCPY(e_vbox_display, (char *)((language)?"keine Anrufe":"no calls"));
			e_vbox_display_refresh = 1;
			set_tone_vbox("nothing");
		} else
		{
			e_vbox_state = VBOX_STATE_MENU;
			SCPY(e_vbox_display, (char *)((language)?"druecke 2 f. wiedergabe":"press 2 to play"));
			e_vbox_display_refresh = 1;
			set_tone_vbox("menu");
		}
		break;

		default:
		PERROR("vbox_message_eof(ep%d): terminal %s unknown state: %d\n", ea_endpoint->ep_serial, e_ext.number, e_vbox_state);
	}
}



/*
 * set the given vbox-tone with full path (without appending)
 * the tone is played and after eof, a message is received
 */
void EndpointAppPBX::set_tone_vbox(char *tone)
{
	struct lcr_msg *message;

	if (tone == NULL)
		tone = "";

	if (!ea_endpoint->ep_portlist)
	{
		PERROR("EPOINT(%d) no portlist\n", ea_endpoint->ep_serial);
	}
	message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_portlist->port_id, EPOINT_TO_PORT, MESSAGE_VBOX_TONE);
	SCPY(message->param.tone.dir, (char *)((e_ext.vbox_language)?"vbox_german":"vbox_english"));
	SCPY(message->param.tone.name, tone);
	message_put(message);

	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) terminal %s set tone '%s'\n", ea_endpoint->ep_serial, e_ext.number, tone);
}


/*
 * set the given recording file
 * the appendix is removed
 * the file is played and after eof, a message is received
 * the current counter value is also received by a message
 * set the offset in seconds of the current recording
 */
void EndpointAppPBX::set_play_vbox(char *file, int offset)
{
	char filename[256];
	struct lcr_msg *message;

	SPRINT(filename, "%s/%s/%s/vbox/%s", INSTALL_DATA, options.extensions_dir, e_vbox, file);
	
	/* remove .wav */
	if (!strcmp(filename+strlen(filename)-4, ".wav")) /* filename is always more than 4 digits long */
		filename[strlen(filename)-4] = '\0';
	else // to not check twice
	/* remove .isdn */
	if (!strcmp(filename+strlen(filename)-5, ".isdn")) /* filename is always more than 5 digits long */
		filename[strlen(filename)-5] = '\0';

	if (!ea_endpoint->ep_portlist)
	{
		PERROR("EPOINT(%d) no portlist\n", ea_endpoint->ep_serial);
	}
	message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_portlist->port_id, EPOINT_TO_PORT, MESSAGE_VBOX_PLAY);
	SCPY(message->param.play.file, filename);
	message->param.play.offset = offset;
	message_put(message);

	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) terminal %s set play '%s'\n", ea_endpoint->ep_serial, e_ext.number, filename);
}


/*
 * change speed of the recording file, the default is 1
 * negative values cause negative speed
 */
void EndpointAppPBX::set_play_speed(int speed)
{
	struct lcr_msg *message;

	if (!ea_endpoint->ep_portlist)
	{
		PERROR("EPOINT(%d) no portlist\n", ea_endpoint->ep_serial);
	}
	message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_portlist->port_id, EPOINT_TO_PORT, MESSAGE_VBOX_PLAY_SPEED);
	message->param.speed = speed;
	message_put(message);

	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) terminal %s set speed '%d'\n", ea_endpoint->ep_serial, e_ext.number, speed);
}



