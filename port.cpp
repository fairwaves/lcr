/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** port                                                                      **
**                                                                           **
\*****************************************************************************/ 

/* HOW TO audio?

Audio flow has two ways:

* from channel to the upper layer
  -> sound from mISDN channel
  -> announcement from vbox channel

* from the upper layer to the channel
  -> sound from remote channel

Audio is required:

  -> if local or remote channel is not mISDN
  -> if call is recorded (vbox)


Functions:

* PmISDN::txfromup
  -> audio from upper layer is buffered for later transmission to channel
* PmISDN::handler
  -> buffered audio from upper layer or tones are transmitted via system clock
* mISDN_handler
  -> rx-data from port to record() and upper layer
  -> tx-data from port (dsp) to record()
* VboxPort::handler
  -> streaming announcement to upper layer
  -> recording announcement
* VboxPort::message_epoint
  -> recording audio message from upper layer
  

   
*/

#include "main.h"

#define SHORT_MIN -32768
#define SHORT_MAX 32767

class Port *port_first = NULL;

unsigned long port_serial = 1; /* must be 1, because 0== no port */


/* free epointlist relation
 */
void Port::free_epointlist(struct epoint_list *epointlist)
{
	struct epoint_list *temp, **tempp;

	temp = p_epointlist;
	tempp = &p_epointlist;
	while(temp)
	{
		if (temp == epointlist)
			break;

		tempp = &temp->next;
		temp = temp->next;
	}
	if (temp == 0)
	{
		PERROR("SOFTWARE ERROR: epointlist not in port's list.\n");
		return;
	}
	/* detach */
	*tempp=temp->next;

	/* free */
	PDEBUG(DEBUG_EPOINT, "PORT(%d) removed epoint from port\n", p_serial);
	FREE(temp, sizeof(struct epoint_list));
	ememuse--;
}


void Port::free_epointid(unsigned long epoint_id)
{
	struct epoint_list *temp, **tempp;

	temp = p_epointlist;
	tempp = &p_epointlist;
	while(temp)
	{
		if (temp->epoint_id == epoint_id)
			break;

		tempp = &temp->next;
		temp = temp->next;
	}
	if (temp == 0)
	{
		PERROR("epoint_id not in port's list.\n");
		return;
	}
	/* detach */
	*tempp=temp->next;

	/* free */
	PDEBUG(DEBUG_EPOINT, "PORT(%d) removed epoint from port\n", p_serial);
	FREE(temp, sizeof(struct epoint_list));
	ememuse--;
}


/* create new epointlist relation
 */
struct epoint_list *Port::epointlist_new(unsigned long epoint_id)
{
	struct epoint_list *epointlist, **epointlistpointer;

	/* epointlist structure */
	epointlist = (struct epoint_list *)MALLOC(sizeof(struct epoint_list));
	if (!epointlist)
		FATAL("No memory for epointlist\n");
	ememuse++;
	PDEBUG(DEBUG_EPOINT, "PORT(%d) allocating epoint_list.\n", p_serial);

	/* add epoint_list to chain */
	epointlist->next = NULL;
	epointlistpointer = &p_epointlist;
	while(*epointlistpointer)
		epointlistpointer = &((*epointlistpointer)->next);
	*epointlistpointer = epointlist;

	/* link to epoint */
	epointlist->epoint_id = epoint_id;
	epointlist->active = 1;

	return(epointlist);
}


/*
 * port constructor
 */
Port::Port(int type, char *portname, struct port_settings *settings)
{
	class Port *temp, **tempp;

	PDEBUG(DEBUG_PORT, "new port of type %d, name '%s'\n", type, portname);

	/* initialize object */
	if (settings)
		memcpy(&p_settings, settings, sizeof(struct port_settings));
	else
	{
		memset(&p_settings, 0, sizeof(p_settings));
		SCPY(p_settings.tones_dir, options.tones_dir);
	}
	SCPY(p_name, portname);
	SCPY(p_tone_dir, p_settings.tones_dir); // just to be sure
	p_type = type;
	p_serial = port_serial++;
	p_tone_fh = -1;
	p_tone_fetched = NULL;
	p_tone_name[0] = '\0';
	p_state = PORT_STATE_IDLE;
	p_epointlist = NULL;
	memset(&p_callerinfo, 0, sizeof(p_callerinfo));
	memset(&p_dialinginfo, 0, sizeof(p_dialinginfo));
	memset(&p_connectinfo, 0, sizeof(p_connectinfo));
	memset(&p_redirinfo, 0, sizeof(p_redirinfo));
	memset(&p_capainfo, 0, sizeof(p_capainfo));
	p_echotest = 0;

	/* call recording */
	p_record = NULL;
	p_record_type = 0;
	p_record_length = 0;
	p_record_skip = 0;
	p_record_filename[0] = '\0';
	p_record_buffer_readp = 0;
	p_record_buffer_writep = 0;
	p_record_buffer_dir = 0;

	/* append port to chain */
	next = NULL;
	temp = port_first;
	tempp = &port_first;
	while(temp)
	{
		tempp = &temp->next;
		temp = temp->next;
	}
	*tempp = this;

	classuse++;
}


/*
 * port destructor
 */
Port::~Port(void)
{
	class Port *temp, **tempp;
	struct lcr_msg *message;

	if (p_record)
		close_record(0, 0);

	classuse--;

	PDEBUG(DEBUG_PORT, "removing port of type %d, name '%s'\n", p_type, p_name);

	/* disconnect port from endpoint */
	while(p_epointlist)
	{
		/* send disconnect */
		message = message_create(p_serial, p_epointlist->epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = 16;
		message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		message_put(message);
		/* remove endpoint */
		free_epointlist(p_epointlist);
	}

	/* remove port from chain */
	temp=port_first;
	tempp=&port_first;
	while(temp)
	{
		if (temp == this)
			break;
		tempp = &temp->next;
		temp = temp->next;
	}
	if (temp == NULL)
		FATAL("PORT(%s) port not in port's list.\n", p_name);
	/* detach */
	*tempp=this->next;

	/* close open tones file */
	if (p_tone_fh >= 0)
	{
		close(p_tone_fh);
		p_tone_fh = -1;
		fhuse--;
	}
	p_tone_fetched = NULL;
}

PORT_STATE_NAMES

/* set new endpoint state
 */
void Port::new_state(int state)
{
	PDEBUG(DEBUG_PORT, "PORT(%s) new state %s --> %s\n", p_name, state_name[p_state], state_name[state]);
	p_state = state;
}


/*
 * find the port with port_id
 */ 
class Port *find_port_id(unsigned long port_id)
{
	class Port *port = port_first;

	while(port)
	{
//printf("comparing: '%s' with '%s'\n", name, port->name);
		if (port->p_serial == port_id)
			return(port);
		port = port->next;
	}

	return(NULL);
}


/*
 * set echotest
 */
void Port::set_echotest(int echotest)
{
	p_echotest = echotest;
}


/*
 * set the file in the tone directory with the given name
 */
void Port::set_tone(char *dir, char *name)
{
	int fh;
	char filename[128];

	if (name == NULL)
		name = "";

#if 0
	/* if tones is jingle, store next tone */
	if ((p_tone_fh >= 0 || p_tone_fetched)
	 && (!strcmp(p_tone_name, "left") || !strcmp(p_tone_name, "joined")))
	{
		SCPY(p_tone_dir, dir);
		SCPY(p_tone_name, name);
		return;
	}
#endif

	/* no counter, no eof, normal speed */
	p_tone_counter = 0;
	p_tone_eof = 0;
	p_tone_speed = 1;
	p_tone_codec = CODEC_LAW;

	if (p_tone_fh >= 0)
	{
		close(p_tone_fh);
		p_tone_fh = -1;
		fhuse--;
	}
	p_tone_fetched = NULL;

	if (name[0])
	{
		if (name[0] == '/')
		{
			SPRINT(p_tone_name, "%s", name);
			p_tone_dir[0] = '\0';
		} else
		{
			SCPY(p_tone_dir, dir);
			SCPY(p_tone_name, name);
		}
	} else
	{
	 	p_tone_name[0]= '\0';
	 	p_tone_dir[0]= '\0';
		return;
	}

	if (!!strncmp(name,"cause_",6))
		return;

	/* now we check if the cause exists, otherwhise we use error tone. */
	if ((p_tone_fetched=open_tone_fetched(p_tone_dir, p_tone_name, &p_tone_codec, 0, 0)))
	{
		p_tone_fetched = NULL;
		return;
	}
	SPRINT(filename, "%s_loop", p_tone_name);
	if ((p_tone_fetched=open_tone_fetched(p_tone_dir, filename, &p_tone_codec, 0, 0)))
	{
		p_tone_fetched = NULL;
		return;
	}
	SPRINT(filename, "%s/%s/%s", INSTALL_DATA, p_tone_dir, p_tone_name);
	if ((fh=open_tone(filename, &p_tone_codec, 0, 0)) >= 0)
	{
		close(fh);
		return;
	}
	SPRINT(filename, "%s/%s/%s_loop", INSTALL_DATA, p_tone_dir, p_tone_name);
	if ((fh=open_tone(filename, &p_tone_codec, 0, 0)) >= 0)
	{
		close(fh);
		return;
	}

	if (!strcmp(name,"cause_00") || !strcmp(name,"cause_10"))
	{
		PDEBUG(DEBUG_PORT, "PORT(%s) Given Cause 0x%s has no tone, using release tone\n", p_name, name+6);
		SPRINT(p_tone_name,"release");
	} else
	if (!strcmp(name,"cause_11"))
	{
		PDEBUG(DEBUG_PORT, "PORT(%s) Given Cause 0x%s has no tone, using busy tone\n", p_name, name+6);
		SPRINT(p_tone_name,"busy");
	} else
	{
		PDEBUG(DEBUG_PORT, "PORT(%s) Given Cause 0x%s has no tone, using error tone\n", p_name, name+6);
		SPRINT(p_tone_name,"error");
	}
}


/*
 * set the file in the tone directory for vbox playback
 * also set the play_eof-flag
 */
void Port::set_vbox_tone(char *dir, char *name)
{
	char filename[256];

	p_tone_speed = 1;
	p_tone_counter = 0;
	p_tone_codec = CODEC_LAW;
	p_tone_eof = 1;

	if (p_tone_fh >= 0)
	{
		close(p_tone_fh);
		p_tone_fh = -1;
		fhuse--;
	}
	p_tone_fetched = NULL;

	SPRINT(p_tone_dir,  dir);
	SPRINT(p_tone_name,  name);

	/* now we check if the cause exists, otherwhise we use error tone. */
	if (p_tone_dir[0])
	{
		if ((p_tone_fetched=open_tone_fetched(p_tone_dir, p_tone_name, &p_tone_codec, &p_tone_size, &p_tone_left)))
		{
			PDEBUG(DEBUG_PORT, "PORT(%s) opening fetched tone: %s\n", p_name, p_tone_name);
			return;
		}
		SPRINT(filename, "%s/%s/%s", INSTALL_DATA, p_tone_dir, p_tone_name);
		if ((p_tone_fh=open_tone(filename, &p_tone_codec, &p_tone_size, &p_tone_left)) >= 0)
		{
			fhuse++;
			PDEBUG(DEBUG_PORT, "PORT(%s) opening tone: %s\n", p_name, filename);
			return;
		}
	} else
	{
		SPRINT(filename, "%s", p_tone_name);
		if ((p_tone_fh=open_tone(filename, &p_tone_codec, &p_tone_size, &p_tone_left)) >= 0)
		{
			fhuse++;
			PDEBUG(DEBUG_PORT, "PORT(%s) opening tone: %s\n", p_name, filename);
			return;
		}
	}
}


/*
 * set the file in the given directory for vbox playback
 * also set the eof-flag
 * also set the counter-flag
 */
void Port::set_vbox_play(char *name, int offset)
{
	signed long size;
	struct lcr_msg *message;

	/* use ser_box_tone() */
	set_vbox_tone("", name);
	if (p_tone_fh < 0)
		return;

	/* enable counter */
	p_tone_counter = 1;

	/* seek */
	if (p_tone_name[0])
	{
		/* send message with counter value */
		if (p_tone_size>=0 && ACTIVE_EPOINT(p_epointlist))
		{
			message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_TONE_COUNTER);
			message->param.counter.current = offset;
			message->param.counter.max = size;
			message_put(message);
		}
	}
}


/*
 * set the playback speed (for recording playback with different speeds)
 */
void Port::set_vbox_speed(int speed)
{
	/* enable vbox play mode */
	p_tone_speed = speed;
}

/*
 * read from the given file as specified in port_set_tone and return sample data
 * if the tone ends, the result may be less samples than requested
 */
int Port::read_audio(unsigned char *buffer, int length)
{
	int l,len;
	int nodata=0; /* to detect 0-length files and avoid endless reopen */
	char filename[128];
	int tone_left_before; /* temp variable to determine the change in p_tone_left */

	/* nothing */
	if (length == 0)
		return(0);

	len = length;

	/* if there is no tone set, use silence */
	if (!p_tone_name[0])
		return(0);

	/* if the file pointer is not open, we open it */
	if (p_tone_fh<0 && p_tone_fetched==NULL)
	{
		if (p_tone_dir[0])
		{
			SPRINT(filename, "%s", p_tone_name);
			/* if file does not exist */
			if (!(p_tone_fetched=open_tone_fetched(p_tone_dir, filename, &p_tone_codec, &p_tone_size, &p_tone_left)))
			{
				SPRINT(filename, "%s/%s/%s", INSTALL_DATA, p_tone_dir, p_tone_name);
				/* if file does not exist */
				if ((p_tone_fh=open_tone(filename, &p_tone_codec, &p_tone_size, &p_tone_left)) < 0)
				{
					PDEBUG(DEBUG_PORT, "PORT(%s) no tone: %s\n", p_name, filename);
					goto try_loop;
				}
				fhuse++;
			}
		} else
		{
			SPRINT(filename, "%s", p_tone_name);
			/* if file does not exist */
			if ((p_tone_fh=open_tone(filename, &p_tone_codec, &p_tone_size, &p_tone_left)) < 0)
			{
				PDEBUG(DEBUG_PORT, "PORT(%s) no tone: %s\n", p_name, filename);
				goto try_loop;
			}
			fhuse++;
		}
		PDEBUG(DEBUG_PORT, "PORT(%s) opening %stone: %s\n", p_name, p_tone_fetched?"fetched ":"", filename);
	}

read_more:
	/* file descriptor is open read data */
	tone_left_before = p_tone_left;
	if (p_tone_fh >= 0)
	{
		l = read_tone(p_tone_fh, buffer, p_tone_codec, len, p_tone_size, &p_tone_left, p_tone_speed);
		if (l<0 || l>len) /* paranoia */
			l=0;
		buffer += l;
		len -= l;
	}
	if (p_tone_fetched)
	{
		l = read_tone_fetched(&p_tone_fetched, buffer, len, p_tone_size, &p_tone_left, p_tone_speed);
		if (l<0 || l>len) /* paranoia */
			l=0;
		buffer += l;
		len -= l;
	}

	/* if counter is enabled, we check if we have a change */
	if (p_tone_counter && p_tone_size>=0 && ACTIVE_EPOINT(p_epointlist))
	{
		/* if we jumed to the next second */
		if (((p_tone_size-p_tone_left)/8000) != (p_tone_size-tone_left_before)/8000)
		{
//printf("\nsize=%d left=%d\n\n",p_tone_size,p_tone_left);
			struct lcr_msg *message;
			message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_TONE_COUNTER);
			message->param.counter.current = (p_tone_size-p_tone_left)/8000;
			message->param.counter.max = -1;
			message_put(message);
		}
	}

	if (len==0)
		return(length-len);

	if (p_tone_fh >= 0)
	{
		close(p_tone_fh);
		p_tone_fh = -1;
		fhuse--;
	}
	p_tone_fetched = NULL;

	if (l)
		nodata=0;

	/* if the file has 0-length */
	if (nodata>1)
	{
		PDEBUG(DEBUG_PORT, "PORT(%s) 0-length loop: %s\n", p_name, filename);
		p_tone_name[0]=0;
		p_tone_dir[0]=0;
		return(length-len);
	}

	/* if eof is reached, or if the normal file cannot be opened, continue with the loop file if possible */
try_loop:
	if (p_tone_eof && ACTIVE_EPOINT(p_epointlist))
	{
		struct lcr_msg *message;
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_TONE_EOF);
		message_put(message);
	}

	if (p_tone_dir[0])
	{
		/* if file does not exist */
		SPRINT(filename, "%s_loop", p_tone_name);
		if (!(p_tone_fetched=open_tone_fetched(p_tone_dir, filename, &p_tone_codec, &p_tone_size, &p_tone_left)))
		{
			SPRINT(filename, "%s/%s/%s_loop", INSTALL_DATA, p_tone_dir, p_tone_name);
			/* if file does not exist */
			if ((p_tone_fh=open_tone(filename, &p_tone_codec, &p_tone_size, &p_tone_left)) < 0)
			{
				PDEBUG(DEBUG_PORT, "PORT(%s) no tone loop: %s\n",p_name, filename);
				p_tone_dir[0] = '\0';
				p_tone_name[0] = '\0';
				return(length-len);
			}
			fhuse++;
		}
	} else
	{
		SPRINT(filename, "%s_loop", p_tone_name);
		/* if file does not exist */
		if ((p_tone_fh=open_tone(filename, &p_tone_codec, &p_tone_size, &p_tone_left)) < 0)
		{
			PDEBUG(DEBUG_PORT, "PORT(%s) no tone loop: %s\n",p_name, filename);
			p_tone_dir[0] = '\0';
			p_tone_name[0] = '\0';
			return(length-len);
		}
		fhuse++;
	}
	nodata++;
	PDEBUG(DEBUG_PORT, "PORT(%s) opening %stone: %s\n", p_name, p_tone_fetched?"fetched ":"", filename);

	/* now we have opened the loop */
	goto read_more;
}


/* port handler:
 * process transmission clock */
int Port::handler(void)
{
	return(0);
}

/* endpoint sends messages to the port
 * this is called by the message_epoint inherited by child classes
 * therefor a return=1 means: stop, no more processing
 */
//extern struct lcr_msg *dddebug;
int Port::message_epoint(unsigned long epoint_id, int message_id, union parameter *param)
{
	/* check if we got audio data from one remote port */
	switch(message_id)
	{
		case MESSAGE_TONE: /* play tone */
		PDEBUG(DEBUG_PORT, "PORT(%s) isdn port with (caller id %s) setting tone '%s' dir '%s'\n", p_name, p_callerinfo.id, param->tone.name, param->tone.dir);
		set_tone(param->tone.dir,param->tone.name);
		return(1);

		case MESSAGE_VBOX_TONE: /* play tone of answering machine */
		PDEBUG(DEBUG_PORT, "PORT(%s) set answering machine tone '%s' '%s'\n", p_name, param->tone.dir, param->tone.name);
		set_vbox_tone(param->tone.dir, param->tone.name);
		return(1);

		case MESSAGE_VBOX_PLAY: /* play recording of answering machine */
		PDEBUG(DEBUG_PORT, "PORT(%s) set answering machine file to play '%s' (offset %d seconds)\n", p_name, param->play.file, param->play.offset);
		set_vbox_play(param->play.file, param->play.offset);
		return(1);

		case MESSAGE_VBOX_PLAY_SPEED: /* set speed of playback (recording of answering machine) */
		PDEBUG(DEBUG_PORT, "PORT(%s) set answering machine playback speed %d (times)\n", p_name, param->speed);
		set_vbox_speed(param->speed);
		return(1);

	}

	return(0);
}


/* wave header structure */
struct fmt {
	unsigned short	stereo; /* 1 = mono, 2 = stereo */
	unsigned short	channels; /* number of channels */
	unsigned long	sample_rate; /* sample rate */
	unsigned long	data_rate; /* data rate */
	unsigned short	bytes_sample; /* bytes per sample (all channels) */
	unsigned short	bits_sample; /* bits per sample (one channel) */
};


/*
 * open record file (actually a wave file with empty header which will be
 * written before close, because we do not know the size yet)
 * type=1 record annoucement,  type=0 record audio stream, type=2 record vbox
 */
int Port::open_record(int type, int vbox, int skip, char *extension, int anon_ignore, char *vbox_email, int vbox_email_file)
{
	/* RIFFxxxxWAVEfmt xxxx(fmt-size)dataxxxx... */
	char dummyheader[8+4+8+sizeof(fmt)+8];
	char filename[256];

	if (!extension)
	{
		PERROR("Port(%d) not an extension\n", p_serial);
		return(0);
	}
	SCPY(p_record_extension, extension);
	p_record_anon_ignore = anon_ignore;
	SCPY(p_record_vbox_email, vbox_email);
	p_record_vbox_email_file = vbox_email_file;
	
	if (p_record)
	{
		PERROR("Port(%d) already recording\n", p_serial);
		return(0);
	}

	if (vbox != 0)
		SPRINT(filename, "%s/%s/%s/vbox", INSTALL_DATA, options.extensions_dir, p_record_extension);
	else
		SPRINT(filename, "%s/%s/%s/recordings", INSTALL_DATA, options.extensions_dir, p_record_extension);
	if (mkdir(filename, 0755) < 0)
	{
		if (errno != EEXIST)
		{
			PERROR("Port(%d) cannot create directory '%s'\n", p_serial, filename);
			return(0);
		}
	}

	if (vbox == 1)
		UPRINT(strchr(filename,'\0'), "/announcement");
	else
		UPRINT(strchr(filename,'\0'), "/%04d-%02d-%02d_%02d%02d%02d", now_tm->tm_year+1900, now_tm->tm_mon+1, now_tm->tm_mday, now_tm->tm_hour, now_tm->tm_min, now_tm->tm_sec);
	if (vbox == 2)
	{
		p_record_vbox_year = now_tm->tm_year;
		p_record_vbox_mon = now_tm->tm_mon;
		p_record_vbox_mday = now_tm->tm_mday;
		p_record_vbox_hour = now_tm->tm_hour;
		p_record_vbox_min = now_tm->tm_min;
	}

	/* check, if file exists (especially when an extension calls the same extension) */
	if (vbox != 1)
	if ((p_record = fopen(filename, "r")))
	{
		fclose(p_record);
		SCAT(filename, "_2nd");
	}
			
	p_record = fopen(filename, "w");
	if (!p_record)
	{
		PERROR("Port(%d) cannot record because file cannot be opened '%s'\n", p_serial, filename);
		return(0);
	}
	fduse++;

	p_record_type = type;
	p_record_vbox = vbox;
	p_record_skip = skip;
	p_record_length = 0;
	switch(p_record_type)
	{
		case CODEC_MONO:
		case CODEC_STEREO:
		case CODEC_8BIT:
		fwrite(dummyheader, sizeof(dummyheader), 1, p_record);
		break;

		case CODEC_LAW:
		break;
	}
	UCPY(p_record_filename, filename);

	PDEBUG(DEBUG_PORT, "Port(%d) recording started with file name '%s'\n", p_serial, filename);
	return(1);
}


/*
 * close the recoding file, put header in front and rename
 */
void Port::close_record(int beep, int mute)
{
	static signed short beep_mono[256];
	unsigned long size, wsize;
	struct fmt fmt;
	char filename[512], indexname[512];
	FILE *fp;
	int i, ii;
	char number[256], callerid[256];
	char *p;
	struct caller_info callerinfo;
	char *valid_chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890_.-!$%&/()=+*;~";

	if (!p_record)
		return;
	PDEBUG(DEBUG_PORT, "data still in record buffer: %d (dir %d)\n", (p_record_buffer_writep - p_record_buffer_readp) & RECORD_BUFFER_MASK, p_record_buffer_dir);

	memcpy(&callerinfo, &p_callerinfo, sizeof(struct caller_info));
//	apply_callerid_restriction(p_record_anon_ignore, callerinfo.id, &callerinfo.ntype, &callerinfo.present, &callerinfo.screen, callerinfo.extension, callerinfo.name);

	SCPY(number, p_dialinginfo.id);
	SCPY(callerid, numberrize_callerinfo(callerinfo.id, callerinfo.ntype, options.national, options.international));
	if (callerid[0] == '\0')
	{
		if (callerinfo.present == INFO_PRESENT_RESTRICTED)
			UCPY(callerid,"anonymous");
		else
			UCPY(callerid,"unknown");
	}

	/* change verboten digits */
	p = callerid;
	while((p=strchr(p,'*')))
		*(p++) = 'x';
	p = callerid;
	while((p=strchr(p,'/')))
		*(p++) = 'x';
	p = number;
	while((p=strchr(p,'*')))
		*(p++) = 'x';
	p = number;
	while((p=strchr(p,'/')))
		*(p++) = 'x';
	i = 0;
	ii = strlen(callerid);
	while(i < ii)
	{
		if (!strchr(valid_chars, callerid[i]))
			callerid[i] = '_';
		i++;
	}
	i = 0;
	ii = strlen(number);
	while(i < ii)
	{
		if (!strchr(valid_chars, number[i]))
			number[i] = '_';
		i++;
	}

	/* mute */
	if (mute && p_record_type==CODEC_MONO)
	{
		i = p_record_length;
		if (i > mute)
			i = mute;	
		fseek(p_record, -(i<<1), SEEK_END);
		p_record_length -= (i<<1);
	}
	/* add beep to the end of recording */
	if (beep && p_record_type==CODEC_MONO)
	{
		i = 0;
		while(i < 256)
		{
			beep_mono[i] = (signed short)(sin((double)i / 5.688888888889 * 2.0 * 3.1415927) * 2000.0);
			i++;
		}
		i = 0;
		while(i < beep)
		{
			fwrite(beep_mono, sizeof(beep_mono), 1, p_record);
			i += sizeof(beep_mono);
			p_record_length += sizeof(beep_mono);
		}
	}

	/* complete header */
	switch(p_record_type)
	{
		case CODEC_MONO:
		case CODEC_STEREO:
		case CODEC_8BIT:
		/* cue */
		fprintf(p_record, "cue %c%c%c%c%c%c%c%c", 4, 0, 0, 0, 0,0,0,0);

		/* LIST */
		fprintf(p_record, "LIST%c%c%c%cadtl", 4, 0, 0, 0);

		/* go to header */
		fseek(p_record, 0, SEEK_SET);

		/* WAVEfmt xxxx(fmt-size)dataxxxx[data]cue xxxx0000LISTxxxxadtl*/
		size = p_record_length;
		wsize = 4+8+sizeof(fmt)+8+size+8+4+8+4;

		/* RIFF */
		fprintf(p_record, "RIFF%c%c%c%c", (unsigned char)(wsize&0xff), (unsigned char)((wsize>>8)&0xff), (unsigned char)((wsize>>16)&0xff), (unsigned char)(wsize>>24));

		/* WAVE */
		fprintf(p_record, "WAVE");

		/* fmt */
		fprintf(p_record, "fmt %c%c%c%c", sizeof(fmt), 0, 0, 0);
		switch(p_record_type)
		{
			case CODEC_MONO:
			fmt.stereo = 1;
			fmt.channels = 1;
			fmt.sample_rate = 8000; /* samples/sec */
			fmt.data_rate = 16000; /* full data rate */
			fmt.bytes_sample = 2; /* all channels */
			fmt.bits_sample = 16; /* one channel */
			break;

			case CODEC_STEREO:
			fmt.stereo = 1;
			fmt.channels = 2;
			fmt.sample_rate = 8000; /* samples/sec */
			fmt.data_rate = 32000; /* full data rate */
			fmt.bytes_sample = 4; /* all channels */
			fmt.bits_sample = 16; /* one channel */
			break;

			case CODEC_8BIT:
			fmt.stereo = 1;
			fmt.channels = 1;
			fmt.sample_rate = 8000; /* samples/sec */
			fmt.data_rate = 8000; /* full data rate */
			fmt.bytes_sample = 1; /* all channels */
			fmt.bits_sample = 8; /* one channel */
			break;
		}
		fwrite(&fmt, sizeof(fmt), 1, p_record);

		/* data */
		fprintf(p_record, "data%c%c%c%c", (unsigned char)(size&0xff), (unsigned char)((size>>8)&0xff), (unsigned char)((size>>16)&0xff), (unsigned char)(size>>24));

		/* rename file */
		if (p_record_vbox == 1)
			SPRINT(filename, "%s.wav", p_record_filename);
		else
			SPRINT(filename, "%s_%s-%s.wav", p_record_filename, callerid, number);
		break;

		case CODEC_LAW:
		/* rename file */
		if (p_record_vbox == 1)
			SPRINT(filename, "%s.isdn", p_record_filename);
		else
			SPRINT(filename, "%s_%s-%s.isdn", p_record_filename, callerid, number);
		break;
	}

	fclose(p_record);
	fduse--;
	p_record = NULL;

	if (rename(p_record_filename, filename) < 0)
	{
		PERROR("Port(%d) cannot rename from '%s' to '%s'\n", p_serial, p_record_filename, filename);
		return;
	}

	PDEBUG(DEBUG_PORT, "Port(%d) recording is written and renamed to '%s' and must have the following size:%lu raw:%lu samples:%lu\n", p_serial, filename, wsize+8, size, size>>1);

	if (p_record_vbox == 2)
	{
		SPRINT(indexname, "%s/%s/%s/vbox/index", INSTALL_DATA, options.extensions_dir, p_record_extension);
		if ((fp = fopen(indexname,"a")))
		{
			fduse++;

			/* remove path from file name */
			p = filename;
			while(strchr(p, '/'))
				p = strchr(p, '/')+1;
			fprintf(fp, "%s %d %d %d %d %d %s\n", p, p_record_vbox_year, p_record_vbox_mon, p_record_vbox_mday, p_record_vbox_hour, p_record_vbox_min, callerid);

			fclose(fp);
			fduse--;
		} else
		{
			PERROR("Port(%d) cannot open index file '%s' to append.\n", p_serial, indexname);
		}

		/* send email with sample*/
		if (p_record_vbox_email[0])
		{
			send_mail(p_record_vbox_email_file?filename:(char *)"", callerid, callerinfo.extension, callerinfo.name, p_record_vbox_email, p_record_vbox_year, p_record_vbox_mon, p_record_vbox_mday, p_record_vbox_hour, p_record_vbox_min, p_record_extension);
		}
	}
}


/*
 * recording function
 * Records all data from down and from up into one single stream.
 * Both streams may have gaps or jitter.
 * A Jitter buffer for both streams is used to compensate jitter.
 * 
 * If one stream (dir) received packets, they are stored to a
 * buffer to wait for the other stream (dir), so both streams can 
 * be combined. If the buffer is full, it's content is written
 * without mixing stream. (assuming only one stram (dir) exists.)
 * A flag is used to indicate what stream is currently in buffer.
 *
 * NOTE: First stereo sample (odd) is from down, second is from up.
 */
void Port::record(unsigned char *data, int length, int dir_fromup)
{
	unsigned char write_buffer[1024], *d;
	signed short *s;
	int free, i, ii;
	signed long sample;

	/* no recording */
	if (!p_record || !length)
		return;

	/* skip data from local caller (dtmf input) */
	if (p_record_skip && !dir_fromup)
	{
		/* more to skip than we have */
		if (p_record_skip > length)
		{
			p_record_skip -= length;
			return;
		}
		/* less to skip */
		data += p_record_skip;
		length -= p_record_skip;
		p_record_skip = 0;
	}

//printf("dir=%d len=%d\n", dir_fromup, length);

	free = ((p_record_buffer_readp - p_record_buffer_writep - 1) & RECORD_BUFFER_MASK);

//PDEBUG(DEBUG_PORT, "record(data,%d,%d): free=%d, p_record_buffer_dir=%d, p_record_buffer_readp=%d, p_record_buffer_writep=%d.\n", length, dir_fromup, free, p_record_buffer_dir, p_record_buffer_readp, p_record_buffer_writep);

	/* the buffer stores the same data stream */
	if (dir_fromup == p_record_buffer_dir)
	{
same_again:

//printf("same free=%d length=%d\n", free, length);
		/* first write what we can to the buffer */
		while(free && length)
		{
			p_record_buffer[p_record_buffer_writep] = audio_law_to_s32[*data++];
			p_record_buffer_writep = (p_record_buffer_writep + 1) & RECORD_BUFFER_MASK;
			free--;
			length--;
		}
		/* all written, so we return */
		if (!length)
			return;
		/* still data left, buffer is full, so we need to write a chunk to file */
		switch(p_record_type)
		{
			case CODEC_MONO:
			s = (signed short *)write_buffer;
			i = 0;
			while(i < 256)
			{
				*s++ = p_record_buffer[p_record_buffer_readp];
				p_record_buffer_readp = (p_record_buffer_readp + 1) & RECORD_BUFFER_MASK;
				i++;
			}
			fwrite(write_buffer, 512, 1, p_record);
			p_record_length += 512;
			break;

			case CODEC_STEREO:
			s = (signed short *)write_buffer;
			if (p_record_buffer_dir)
			{
				i = 0;
				while(i < 256)
				{
					*s++ = 0; /* nothing from down */
					*s++ = p_record_buffer[p_record_buffer_readp];
					p_record_buffer_readp = (p_record_buffer_readp + 1) & RECORD_BUFFER_MASK;
					i++;
				}
			} else
			{
				i = 0;
				while(i < 256)
				{
					*s++ = p_record_buffer[p_record_buffer_readp];
					*s++ = 0; /* nothing from up */
					p_record_buffer_readp = (p_record_buffer_readp + 1) & RECORD_BUFFER_MASK;
					i++;
				}
			}
			fwrite(write_buffer, 1024, 1, p_record);
			p_record_length += 1024;
			break;

			case CODEC_8BIT:
			d = write_buffer;
			i = 0;
			while(i < 256)
			{
				*d++ = ((unsigned short)(p_record_buffer[p_record_buffer_readp]+0x8000)) >> 8;
				p_record_buffer_readp = (p_record_buffer_readp + 1) & RECORD_BUFFER_MASK;
				i++;
			}
			fwrite(write_buffer, 512, 1, p_record);
			p_record_length += 512;
			break;

			case CODEC_LAW:
			d = write_buffer;
			i = 0;
			while(i < 256)
			{
				*d++ = audio_s16_to_law[p_record_buffer[p_record_buffer_readp] & 0xffff];
				p_record_buffer_readp = (p_record_buffer_readp + 1) & RECORD_BUFFER_MASK;
				i++;
			}
			fwrite(write_buffer, 256, 1, p_record);
			p_record_length += 256;
			break;
		}
		/* because we still have data, we write again */
		free += 256;
		goto same_again;
	}
	/* the buffer stores the other stream */
	
different_again:
	/* if buffer empty, change it */
	if (p_record_buffer_readp == p_record_buffer_writep)
	{
		p_record_buffer_dir = dir_fromup;
		goto same_again;
	}
	/* how much data can we mix ? */
	ii = (p_record_buffer_writep - p_record_buffer_readp) & RECORD_BUFFER_MASK;
	if (length < ii)
		ii = length;

	if (ii > 256)
		ii = 256;
//printf("same ii=%d length=%d\n", ii, length);
//PDEBUG(DEBUG_PORT, "record(data,%d,%d): free=%d, p_record_buffer_dir=%d, p_record_buffer_readp=%d, p_record_buffer_writep=%d: mixing %d bytes.\n", length, dir_fromup, free, p_record_buffer_dir, p_record_buffer_readp, p_record_buffer_writep, ii);

	/* write data mixed with the buffer */
	switch(p_record_type)
	{
		case CODEC_MONO:
		s = (signed short *)write_buffer;
		i = 0;
		while(i < ii)
		{
			sample = p_record_buffer[p_record_buffer_readp]
				+ audio_law_to_s32[*data++];
			p_record_buffer_readp = (p_record_buffer_readp + 1) & RECORD_BUFFER_MASK;
			if (sample < SHORT_MIN) sample = SHORT_MIN;
			if (sample > SHORT_MAX) sample = SHORT_MAX;
			*s++ = sample;
			i++;
		}
		fwrite(write_buffer, ii<<1, 1, p_record);
		p_record_length += (ii<<1);
		break;
		
		case CODEC_STEREO:
		s = (signed short *)write_buffer;
		if (p_record_buffer_dir)
		{
			i = 0;
			while(i < ii)
			{
				*s++ = audio_law_to_s32[*data++];
				*s++ = p_record_buffer[p_record_buffer_readp];
				p_record_buffer_readp = (p_record_buffer_readp + 1) & RECORD_BUFFER_MASK;
				i++;
			}
		} else
		{
			i = 0;
			while(i < ii)
			{
				*s++ = p_record_buffer[p_record_buffer_readp];
				*s++ = audio_law_to_s32[*data++];
				p_record_buffer_readp = (p_record_buffer_readp + 1) & RECORD_BUFFER_MASK;
				i++;
			}
		}
		fwrite(write_buffer, ii<<2, 1, p_record);
		p_record_length += (ii<<2);
		break;
		
		case CODEC_8BIT:
		d = write_buffer;
		i = 0;
		while(i < ii)
		{
			sample = p_record_buffer[p_record_buffer_readp]
				+ audio_law_to_s32[*data++];
			p_record_buffer_readp = (p_record_buffer_readp + 1) & RECORD_BUFFER_MASK;
			if (sample < SHORT_MIN) sample = SHORT_MIN;
			if (sample > SHORT_MAX) sample = SHORT_MAX;
			*d++ = (sample+0x8000) >> 8;
			i++;
		}
		fwrite(write_buffer, ii, 1, p_record);
		p_record_length += ii;
		break;
		
		case CODEC_LAW:
		d = write_buffer;
		i = 0;
		while(i < ii)
		{
			sample = p_record_buffer[p_record_buffer_readp]
				+ audio_law_to_s32[*data++];
			p_record_buffer_readp = (p_record_buffer_readp + 1) & RECORD_BUFFER_MASK;
			if (sample < SHORT_MIN) sample = SHORT_MIN;
			if (sample > SHORT_MAX) sample = SHORT_MAX;
			*d++ = audio_s16_to_law[sample & 0xffff];
			i++;
		}
		fwrite(write_buffer, ii, 1, p_record);
		p_record_length += ii;
		break;
	}
	length -= ii;
	/* still data */
	if (length)
		goto different_again;
	/* no data (maybe buffer) */
	return;

}


