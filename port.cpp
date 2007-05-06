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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "main.h"

#define BETTERDELAY

//#define MIXER_DEBUG /* debug mixer buffer overflow and underrun */

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
	memset(temp, 0, sizeof(struct epoint_list));
	free(temp);
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
		PERROR("epoint_id not in port's list, exitting.\n");
		return;
	}
	/* detach */
	*tempp=temp->next;

	/* free */
	PDEBUG(DEBUG_EPOINT, "PORT(%d) removed epoint from port\n", p_serial);
	memset(temp, 0, sizeof(struct epoint_list));
	free(temp);
	ememuse--;
}


/* create new epointlist relation
 */
struct epoint_list *Port::epointlist_new(unsigned long epoint_id)
{
	struct epoint_list *epointlist, **epointlistpointer;

	/* epointlist structure */
	epointlist = (struct epoint_list *)calloc(1, sizeof(struct epoint_list));
	if (!epointlist)
	{
		PERROR("no mem for allocating epoint_list\n");
		return(0);
	}
	ememuse++;
	PDEBUG(DEBUG_EPOINT, "PORT(%d) allocating epoint_list.\n", p_serial);
	memset(epointlist, 0, sizeof(struct epoint_list));

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
	p_debug_nothingtosend = 0;
	p_tone_fh = -1;
	p_tone_fetched = NULL;
	p_tone_name[0] = '\0';
//	p_knock_fh = -1;
//	p_knock_fetched = NULL;
	p_state = PORT_STATE_IDLE;
	p_epointlist = NULL;
	memset(&p_callerinfo, 0, sizeof(p_callerinfo));
	memset(&p_dialinginfo, 0, sizeof(p_dialinginfo));
	memset(&p_connectinfo, 0, sizeof(p_connectinfo));
	memset(&p_redirinfo, 0, sizeof(p_redirinfo));
	memset(&p_capainfo, 0, sizeof(p_capainfo));
	memset(p_mixer_buffer, 0, sizeof(p_mixer_buffer));
	memset(p_record_buffer, 0, sizeof(p_record_buffer));
	memset(p_stereo_buffer, 0, sizeof(p_stereo_buffer));
	p_mixer_rel = NULL;
	p_mixer_readp = 0;
	p_echotest = 0;
	next = NULL;
	p_record = NULL;
	p_record_type = 0;
	p_record_length = 0;
	p_record_filename[0] = '\0';

	/* append port to chain */
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
	struct mixer_relation *relation, *rtemp;
	class Port *temp, **tempp;
	struct message *message;

	if (p_record)
		close_record(0);

	classuse--;

	PDEBUG(DEBUG_PORT, "removing port of type %d, name '%s'\n", p_type, p_name);

	/* free mixer relation chain */
	relation = p_mixer_rel;
	while(relation)
	{
		rtemp = relation;
		relation = relation->next;
		memset(rtemp, 0, sizeof(struct mixer_relation));
		free(rtemp);
		pmemuse--;
	}
	p_mixer_rel = NULL; /* beeing paranoid */

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
	{
		PERROR("PORT(%s) port not in port's list.\n", p_name);
		exit(-1);
	}
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
 * find the port using h323 token
 */ 
class Port *find_port_with_token(char *name)
{
	class Port *port = port_first;

	while(port)
	{
//printf("comparing: '%s' with '%s'\n", name, p_name);
		if ((port->p_type==PORT_TYPE_H323_IN || port->p_type==PORT_TYPE_H323_OUT) && !strcmp(port->p_name, name))
			return(port);
		port = port->next;
	}

	return(NULL);
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

#if 0
/*
 * open the knock sound
 */
void Port::set_knock(char *tones_dir_epoint)
{
	char filename[128], *tones_dir;

	/* check if we have the epoint override the tones_dir of options.conf */
	tones_dir = options.tones_dir;
	if (tones_dir_epoint[0])
	{
		tones_dir = tones_dir_epoint;
	}

	if (p_knock_fh >= 0)
	{
		close(p_knock_fh);
		p_knock_fh = -1;
		fhuse--;
	}
	p_knock_fetched = NULL;

	if ((p_knock_fetched=open_tone_fetched(tones_dir, "knock", &p_knock_codec, &p_knock_size, &p_knock_left)))
	{
		PDEBUG(DEBUG_PORT, "PORT(%s) opening fetched tone: %s\n", p_name, "knock");
		return;
	}

	SPRINT(filename, "%s/%s/knock", INSTALL_DATA, tones_dir);
	if ((p_knock_fh=open_tone(filename, &p_knock_codec, &p_knock_size, &p_knock_left)) >= 0)
	{
		PDEBUG(DEBUG_PORT, "PORT(%s) opening tone: %s\n", p_name, filename);
		fhuse++;
	} else
	{
		p_knock_fh = -1;
	}
}
#endif


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
	/* knocking ? */
	if (!strcmp(name, "knock"))
	{
		set_knock(tones_dir_epoint);
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
	struct message *message;

	/* use ser_box_tone() */
	set_vbox_tone("", name);
	if (p_tone_fh < 0)
		return;

	/* enable counter */
	p_tone_counter = 1;

	/* seek */
	if (p_tone_name[0])
	{
//printf("\n\n\n tone_codec = %d\n\n\n\n",p_tone_codec);
#if 0
		switch(p_tone_codec)
		{
			case CODEC_LAW:
			case CODEC_8BIT:
			lseek(p_tone_fh, offset*8000L, SEEK_SET);
			size = p_tone_size / 8000L;
			if (offset*8000L <= p_tone_left)
				p_tone_left -= offset*8000L;
			break;

			case CODEC_MONO:
			lseek(p_tone_fh, offset*16000L, SEEK_SET);
			size = p_tone_size / 16000L;
			if (offset*16000L <= p_tone_left)
				p_tone_left -= offset*16000L;
//printf("\n\n\n size = %d\n\n\n\n",size);
			break;

			case CODEC_STEREO:
			lseek(p_tone_fh, offset*32000L, SEEK_SET);
			size = p_tone_size / 32000L;
			if (offset*32000L <= p_tone_left)
				p_tone_left -= offset*32000L;
			break;
	
			default:
			PERROR("no codec specified! exitting...\n");
			exit(-1);
		}
#else
		lseek(p_tone_fh, offset*8000L, SEEK_SET);
		size = p_tone_size / 8000L;
		if (offset*8000L <= p_tone_left)
			p_tone_left -= offset*8000L;
#endif


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

/* write data to port's mixer buffer
 * it will be read by the port using read_audio
 * synchronisation is also done when writing is too fast and buffer is full
 * the mixer buffer will only mix what cannot be mixed by kernel mixer
 *
 * also write data to the record buffer. the record buffer will mix everything.
 */
void Port::mixer(union parameter *param)
{
	struct mixer_relation *relation, **relationpointer;
	int len;
	unsigned char *data;
	signed short *data_16;
	int writep;
	signed long *buffer, *record;
	int must_mix = 1; /* set, if we need to mix (not done by kernel) */

	/* we do not mix if we have audio from ourself but we need to record
	 * unless we have a local echo enabled
	 */
	if (param->data.port_id==p_serial && !p_echotest)
		must_mix = 0;

	if ((param->data.port_type&PORT_CLASS_MASK)==PORT_CLASS_mISDN
	 && (p_type&PORT_CLASS_MASK)==PORT_CLASS_mISDN)
	{
		must_mix = 0;
	}
	if (!p_record && !must_mix) /* if we do not record AND no need to mix */
	{
		return;
	}

	/* get the relation to the write pointer. if no relation exists
	 * for the given source port, we create one.
	 */
	relation = p_mixer_rel;
	relationpointer = &(p_mixer_rel);
	if (!relation) /* there is no relation at all */
	{
		/* clear buffer to 0-volume and reset writep */
		memset(p_mixer_buffer, 0, sizeof(p_mixer_buffer));
		memset(p_record_buffer, 0, sizeof(p_record_buffer));
		memset(p_stereo_buffer, 0, sizeof(p_stereo_buffer));
	} else /* else because we do not need to run a 0-loop */
	while(relation)
	{
		if (relation->port_id == param->data.port_id)
			break;
		relationpointer = &(relation->next);
		relation = relation->next;
	}
	if (!relation)
	{
		relation = *relationpointer = (struct mixer_relation *)calloc(1, sizeof(struct mixer_relation));
		if (!relation)
		{
			PERROR("fatal error: cannot alloc memory for port mixer relation\n");	
			return; /* no mem */
		}
		pmemuse++;
		memset(relation, 0, sizeof(struct mixer_relation));
		relation->port_id = param->data.port_id;
		/* put write buffer in front of read buffer */
#ifndef BETTERDELAY
		relation->mixer_writep = (p_mixer_readp+(PORT_BUFFER/2))%PORT_BUFFER;
#else
		relation->mixer_writep = p_mixer_readp;
#endif
#ifdef MIXER_DEBUG
		PDEBUG(DEBUG_PORT, "PORT(%s) Adding new mixer relation from port #%d (readp=%d, writep=%d, PORT_BUFFER=%d).\n", p_name, param->data.port_id, p_mixer_readp, relation->mixer_writep, PORT_BUFFER);
#endif
	}

	/* adding remote's audio data to our mixer_buffer / record_buffer */
	len = param->data.len;
	if (param->data.compressed == 0) /* in case of 16 bit data */
		len >>= 1;
	if (len>PORT_BUFFER)
		PERROR("fatal error: audio data from remote port #%d is larger than mixer buffer %d>%d\n", param->data.port_id, len, PORT_BUFFER);
	writep = relation->mixer_writep;
	buffer = p_mixer_buffer;
	record = p_record_buffer;
	/* if stereo should be recorded */
	if (param->data.port_id == p_serial)
	if (p_record_type == CODEC_STEREO)
		record = p_stereo_buffer;
	/* NOTE: if read and write pointer are equal, the buffer is full */
	if ((p_mixer_readp-writep+PORT_BUFFER)%PORT_BUFFER < len) /* we would overrun the read pointer */
	{
#ifdef MIXER_DEBUG
		PERROR("PORT(%s) buffer overrun, source port #%d is sending too fast. (dropping %d samples)\n", p_name, param->data.port_id, len);
#endif
		// we do not resync since dropping causes slowing writepointer down anyway...
		//relation->mixer_writep = (p_mixer_readp+(PORT_BUFFER/2))%PORT_BUFFER;
		return;
	}

	if (!p_record && must_mix)
	{
		/* WE MUST MIX BUT DO NOT RECORD */
		if (param->data.compressed)
		{
			/* compressed data */
			data = param->data.data;
			if (len+writep >= PORT_BUFFER) /* data hits the buffer end */
			{
				len = len+writep-PORT_BUFFER; /* rest to write in front of buffer */
				while(writep < PORT_BUFFER) /* write till buffer end */
					buffer[writep++] += audio_law_to_s32[*(data++)];
				writep = 0;
			}
			while(len--) /* write rest */
				buffer[writep++] += audio_law_to_s32[*(data++)];
		} else
		{
			/* uncompressed data */
			data_16 = (signed short *)param->data.data;
			if (len+writep >= PORT_BUFFER) /* data hits the buffer end */
			{
				len = len+writep-PORT_BUFFER; /* rest to write in front of buffer */
				while(writep < PORT_BUFFER) /* write till buffer end */
					buffer[writep++] += *(data_16++);
				writep = 0;
			}
			while(len--) /* write rest */
				buffer[writep++] += *(data_16++);
		}
	} else /* else */
	if (p_record && !must_mix)
	{
		/* WE MUST RECORD BUT DO NOT MIX */
		if (param->data.compressed)
		{
			/* compressed data */
			data = param->data.data;
			if (len+writep >= PORT_BUFFER) /* data hits the buffer end */
			{
				len = len+writep-PORT_BUFFER; /* rest to write in front of buffer */
				while(writep < PORT_BUFFER) /* write till buffer end */
					record[writep++] += audio_law_to_s32[*(data++)];
				writep = 0;
			}
			while(len--) /* write rest */
				record[writep++] += audio_law_to_s32[*(data++)];
		} else
		{
			/* uncompressed data */
			data_16 = (signed short *)param->data.data;
			if (len+writep >= PORT_BUFFER) /* data hits the buffer end */
			{
				len = len+writep-PORT_BUFFER; /* rest to write in front of buffer */
				while(writep < PORT_BUFFER) /* write till buffer end */
					record[writep++] += *(data_16++);
				writep = 0;
			}
			while(len--) /* write rest */
				record[writep++] += *(data_16++);
		}
	} else
	{
		/* WE MUST MIX AND MUST RECORD */
		if (param->data.compressed)
		{
			/* compressed data */
			data = param->data.data;
			if (len+writep >= PORT_BUFFER) /* data hits the buffer end */
			{
				len = len+writep-PORT_BUFFER; /* rest to write in front of buffer */
				while(writep < PORT_BUFFER) /* write till buffer end */
				{
					buffer[writep] += audio_law_to_s32[*data];
					record[writep++] += audio_law_to_s32[*(data++)];
				}
				writep = 0;
			}
			while(len--) /* write rest */
			{
				buffer[writep] += audio_law_to_s32[*data];
				record[writep++] += audio_law_to_s32[*(data++)];
			}
		} else
		{
			/* uncompressed data */
			data_16 = (signed short *)param->data.data;
			if (len+writep >= PORT_BUFFER) /* data hits the buffer end */
			{
				len = len+writep-PORT_BUFFER; /* rest to write in front of buffer */
				while(writep < PORT_BUFFER) /* write till buffer end */
				{
					buffer[writep] += *data_16;
					record[writep++] += *(data_16++);
				}
				writep = 0;
			}
			while(len--) /* write rest */
			{
				buffer[writep] += *data_16;
				record[writep++] += *(data_16++);
			}
		}
	}
	relation->mixer_writep = writep; /* new write pointer */
//	PDEBUG(DEBUG_PORT "written data len=%d port=%d (readp=%d, writep=%d, PORT_BUFFER=%d).\n", param->data.len, param->data.port_id, p_mixer_readp, relation->mixer_writep, PORT_BUFFER);
}


/*
 * read from the given file as specified in port_set_tone and return sample data
 * this data is mixed with the user space mixer (if anything mixed) and
 * filled into the buffer as many as given by length.
 * if compressed is true, the result in buffer is alaw/ulaw-compressed
 * otherwhise it is 16 bit audio. in this case the buffer size must be twice the lenght.
 * the length is the number of samples, not bytes in buffer!
 */
int Port::read_audio(unsigned char *buffer, int length, int compressed)
{
	int l,len;
	unsigned short temp_buffer[PORT_BUFFER]; /* buffer for up to 32 bit of data */
	int codec_in; /* current codec to use */
	unsigned char *buf_in8; /* buffer pointer for alaw/ulaw/8bit data */
	signed short *buf_in16; /* buffer pointer for alaw/ulaw/8bit data */
	signed short *buf_out16; /* buffer pointer for outgoing audio data */
	signed long *mix_buffer, *mix_buffer2, sample; /* pointer to mixer buffer */
	struct mixer_relation *relation, **relationpointer;
	int readp;
	int nodata=0; /* to detect 0-length files */
	char filename[128];
	signed short record_buffer[PORT_BUFFER<<1], *rec_buffer; /* buffer of recorded part which will be written (*2 for stereo) */
	unsigned char *rec8_buffer; /* used to send 8-bit audio (wave-8bit, law) */
	int tone_left_before; /* temp variable to determine the change in p_tone_left */

	/* nothing */
	if (length == 0)
		return(0);

	/* just in case, we get too much to do */
	if (length > PORT_BUFFER-1)
		length = PORT_BUFFER-1;

	len = length;
	buf_in8 = (unsigned char *)temp_buffer;
	buf_in16 = (signed short *)temp_buffer;

	codec_in = p_tone_codec;

	/* if there is no tone set, use silence */
	if (p_tone_name[0] == 0)
	{
		codec_in = CODEC_LAW;
rest_is_silence:
		switch(codec_in)
		{
			case CODEC_LAW:
			memset(buf_in8, (options.law=='a')?0x2a:0xff, len); /* silence */
			break;

			case CODEC_MONO:
			case CODEC_8BIT:
			case CODEC_STEREO:
			memset(buf_in16, 0, len<<1); /* silence */
			break;

			default:
			PERROR("Software error: no codec during silence\n");
			exit(-1);
		}
		goto mix_to_buffer;
	}

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
		codec_in = p_tone_codec;
//printf("\n\ncodec=%d\n\n\n", p_tone_codec);
		PDEBUG(DEBUG_PORT, "PORT(%s) opening %stone: %s\n", p_name, p_tone_fetched?"fetched ":"", filename);
	}

read_more:
	/* file descriptor is open read data */
	tone_left_before = p_tone_left;
	if (p_tone_fh >= 0)
	{
		switch(codec_in)
		{
			case CODEC_LAW:
			l = read_tone(p_tone_fh, buf_in8, codec_in, len, p_tone_size, &p_tone_left, p_tone_speed);
			if (l<0 || l>len) /* paranoia */
				l=0;
			buf_in8 += l;
			len -= l;
			break;

			case CODEC_8BIT:
			l = read_tone(p_tone_fh, buf_in16, codec_in, len, p_tone_size, &p_tone_left, p_tone_speed);
			if (l<0 || l>len) /* paranoia */
				l=0;
			buf_in16 += l;
			len -= l;
			break;

			case CODEC_MONO:
			l = read_tone(p_tone_fh, buf_in16, codec_in, len, p_tone_size, &p_tone_left, p_tone_speed);
			if (l<0 || l>len) /* paranoia */
				l=0;
			buf_in16 += l;
			len -= l;
			break;

			case CODEC_STEREO:
			l = read_tone(p_tone_fh, buf_in16, codec_in, len, p_tone_size, &p_tone_left, p_tone_speed);
			if (l<0 || l>len) /* paranoia */
				l=0;
			buf_in16 += l;
			len -= l;
			break;

			default:
			PERROR("Software error: current tone reading has no codec\n");
			exit(-1);
		}
	}
	if (p_tone_fetched)
	{
		switch(codec_in)
		{
			case CODEC_LAW:
			l = read_tone_fetched(&p_tone_fetched, buf_in8, codec_in, len, p_tone_size, &p_tone_left, p_tone_speed);
			if (l<0 || l>len) /* paranoia */
				l=0;
			buf_in8 += l;
			len -= l;
			break;

			case CODEC_MONO:
			l = read_tone_fetched(&p_tone_fetched, buf_in16, codec_in, len, p_tone_size, &p_tone_left, p_tone_speed);
			if (l<0 || l>len) /* paranoia */
				l=0;
			buf_in16 += l;
			len -= l;
			break;

			default:
			PERROR("Software error: current tone reading has no codec\n");
			exit(-1);
		}
	}

	/* if counter is enabled, we check if we have a change */
	if (p_tone_counter && p_tone_size>=0 && ACTIVE_EPOINT(p_epointlist))
	{
		/* if we jumed to the next second */
		if (((p_tone_size-p_tone_left)/8000) != (p_tone_size-tone_left_before)/8000)
		{
//printf("\nsize=%d left=%d\n\n",p_tone_size,p_tone_left);
			struct message *message;
			message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_TONE_COUNTER);
			message->param.counter.current = (p_tone_size-p_tone_left)/8000;
			message->param.counter.max = -1;
			message_put(message);
		}
	}

	if (len==0)
		goto mix_to_buffer;

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
		goto rest_is_silence;
	}

	/* if eof is reached, or if the normal file cannot be opened, continue with the loop file if possible */
try_loop:
	if (p_tone_eof && ACTIVE_EPOINT(p_epointlist))
	{
		struct message *message;
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
		//		codec_in = CODEC_LAW;
				goto rest_is_silence;
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
	//		codec_in = CODEC_LAW;
			goto rest_is_silence;
		}
		fhuse++;
	}
	codec_in = p_tone_codec;
	nodata++;
	PDEBUG(DEBUG_PORT, "PORT(%s) opening %stone: %s\n", p_name, p_tone_fetched?"fetched ":"", filename);

	/* now we have opened the loop */
	goto read_more;

	/* **********
	 * now we mix
	 * we take the buffer content and mix the mixer_buffer to what we got
	 */ 
mix_to_buffer:
	/* release all writepointer which will underrun, since writing of
	 * remote port data is too slow
	 */
	relation = p_mixer_rel;
	readp = p_mixer_readp;
	relationpointer = &(p_mixer_rel);
	while(relation) /* if no relation, this is skipped */
	{
		/* NOTE: if writep and readp are equal, the distance ist max
		 * == PORT_BUFFER
		 */
		if (((relation->mixer_writep-readp-1+PORT_BUFFER)%PORT_BUFFER)+1 <= length) /* underrun */
		{
			/* remove port relation in order to resync.
			 * this is also caused by ports which do not transmit
			 * anymore.
			 */
#ifdef MIXER_DEBUG
			PERROR("PORT(%s) Buffer underrun, source port is sending too slow or stopped sending, removing relation. (readp=%d, writep=%d, PORT_BUFFER=%d)\n", p_name, readp, relation->mixer_writep, PORT_BUFFER);
#endif
			
			*relationpointer = relation->next;
			memset(relation, 0, sizeof(struct mixer_relation));
			free(relation);
			pmemuse--;
			relation = *relationpointer;
			continue;
		}
		relationpointer = &(relation->next);
		relation = relation->next;
	}

	/* if we do recording, we write the record data and the buffer data to the record fp and increment record_count */
	if (p_record)
	switch (p_record_type)
	{
		case CODEC_MONO:
		/* convert from mixer to uncompressed 16 bit mono audio */
		switch(codec_in)
		{
			case CODEC_MONO:
			case CODEC_STEREO:
			case CODEC_8BIT:
			len = length;
			mix_buffer = p_record_buffer;
			rec_buffer = record_buffer;
			buf_in16 = (signed short *)temp_buffer;
			if (len+readp >= PORT_BUFFER) /* data hits the buffer end */
			{
				len = len+readp-PORT_BUFFER; /* rest to read from buffer start */
				while(readp < PORT_BUFFER)
				{	/* mix till buffer end */
					sample = mix_buffer[readp]+*(buf_in16++);
					mix_buffer[readp++] = 0;
					if (sample < -32767)
						sample = -32767;
					if (sample > 32767)
						sample = 32767;
					*(rec_buffer++) = sample;
				}
				readp = 0;
			}
			while(len--) /* write rest */
			{	/* mix till buffer end */
				sample = mix_buffer[readp]+*(buf_in16++);
				mix_buffer[readp++] = 0;
				if (sample < -32767)
					sample = -32767;
				if (sample > 32767)
					sample = 32767;
				*(rec_buffer++) = sample;
			}

			/* restore (changed) read pointer for further use */
			readp = p_mixer_readp;

			/* now write the rec_buffer to the file */
			if (p_record_skip)
			{
				p_record_skip -= length;
				if (p_record_skip < 0)
					p_record_skip = 0;
			} else
			{
				fwrite(record_buffer, (length<<1), 1, p_record);
				p_record_length = (length<<1) + p_record_length;
			}
			break;

			case CODEC_LAW:
			len = length;
			mix_buffer = p_record_buffer;
			rec_buffer = record_buffer;
			buf_in8 = (unsigned char *)temp_buffer;
			if (len+readp >= PORT_BUFFER) /* data hits the buffer end */
			{
				len = len+readp-PORT_BUFFER; /* rest to read from buffer start */
				while(readp < PORT_BUFFER)
				{	/* mix till buffer end */
					sample = mix_buffer[readp]+audio_law_to_s32[*(buf_in8++)];
					mix_buffer[readp++] = 0;
					if (sample < -32767)
						sample = -32767;
					if (sample > 32767)
						sample = 32767;
					*(rec_buffer++) = sample;
				}
				readp = 0;
			}
			while(len--) /* write rest */
			{	/* mix till buffer end */
				sample = mix_buffer[readp]+audio_law_to_s32[*(buf_in8++)];
				mix_buffer[readp++] = 0;
				if (sample < -32767)
					sample = -32767;
				if (sample > 32767)
					sample = 32767;
				*(rec_buffer++) = sample;
			}

			/* restore (changed) read pointer for further use */
			readp = p_mixer_readp;

			/* now write the rec_buffer to the file */
			if (p_record_skip)
			{
				p_record_skip -= length;
				if (p_record_skip < 0)
					p_record_skip = 0;
			} else
			{
				fwrite(record_buffer, (length<<1), 1, p_record);
				p_record_length = (length<<1) + p_record_length;
			}
			break;

		}
		break;

		case CODEC_STEREO:
		/* convert from mixer to uncompressed 16 bit stereo audio */
		switch(codec_in)
		{
			case CODEC_MONO:
			case CODEC_STEREO:
			case CODEC_8BIT:
			len = length;
			mix_buffer = p_record_buffer;
			mix_buffer2 = p_stereo_buffer;
			rec_buffer = record_buffer;
			buf_in16 = (signed short *)temp_buffer;
			if (len+readp >= PORT_BUFFER) /* data hits the buffer end */
			{
				len = len+readp-PORT_BUFFER; /* rest to read from buffer start */
				while(readp < PORT_BUFFER)
				{	/* mix till buffer end */
					sample = mix_buffer[readp]+*(buf_in16++);
					mix_buffer[readp] = 0;
					if (sample < -32767)
						sample = -32767;
					if (sample > 32767)
						sample = 32767;
					*(rec_buffer++) = sample;

					*(rec_buffer++) = mix_buffer2[readp];
					mix_buffer2[readp++] = 0;
				}
				readp = 0;
			}
			while(len--) /* write rest */
			{	/* mix till buffer end */
				sample = mix_buffer[readp]+*(buf_in16++);
				mix_buffer[readp] = 0;
				if (sample < -32767)
					sample = -32767;
				if (sample > 32767)
					sample = 32767;
				*(rec_buffer++) = sample;
				*(rec_buffer++) = mix_buffer2[readp];
				mix_buffer2[readp++] = 0;
			}

			/* restore (changed) read pointer for further use */
			readp = p_mixer_readp;

			/* now write the rec_buffer to the file */
			if (p_record_skip)
			{
				p_record_skip -= length;
				if (p_record_skip < 0)
					p_record_skip = 0;
			} else
			{
				fwrite(record_buffer, (length<<2), 1, p_record);
				p_record_length = (length<<2) + p_record_length;
			}
			break;

			case CODEC_LAW:
			len = length;
			mix_buffer = p_record_buffer;
			mix_buffer2 = p_stereo_buffer;
			rec_buffer = record_buffer;
			buf_in8 = (unsigned char *)temp_buffer;
			if (len+readp >= PORT_BUFFER) /* data hits the buffer end */
			{
				len = len+readp-PORT_BUFFER; /* rest to read from buffer start */
				while(readp < PORT_BUFFER)
				{	/* mix till buffer end */
					sample = mix_buffer[readp]+audio_law_to_s32[*(buf_in8++)];
					mix_buffer[readp] = 0;
					if (sample < -32767)
						sample = -32767;
					if (sample > 32767)
						sample = 32767;
					*(rec_buffer++) = sample;

					*(rec_buffer++) = mix_buffer2[readp];
					mix_buffer2[readp++] = 0;
				}
				readp = 0;
			}
			while(len--) /* write rest */
			{	/* mix till buffer end */
				sample = mix_buffer[readp]+audio_law_to_s32[*(buf_in8++)];
				mix_buffer[readp] = 0;
				if (sample < -32767)
					sample = -32767;
				if (sample > 32767)
					sample = 32767;
				*(rec_buffer++) = sample;
				*(rec_buffer++) = mix_buffer2[readp];
				mix_buffer2[readp++] = 0;
			}

			/* restore (changed) read pointer for further use */
			readp = p_mixer_readp;

			/* now write the rec_buffer to the file */
			if (p_record_skip)
			{
				p_record_skip -= length;
				if (p_record_skip < 0)
					p_record_skip = 0;
			} else
			{
				fwrite(record_buffer, (length<<2), 1, p_record);
				p_record_length = (length<<2) + p_record_length;
			}
			break;
		}
		break;

		case CODEC_8BIT:
		/* convert from mixer to uncompressed 8 bit mono audio */
		switch(codec_in)
		{
			case CODEC_MONO:
			case CODEC_STEREO:
			case CODEC_8BIT:
			len = length;
			mix_buffer = p_record_buffer;
			rec8_buffer = (unsigned char *)record_buffer;
			buf_in16 = (signed short *)temp_buffer;
			if (len+readp >= PORT_BUFFER) /* data hits the buffer end */
			{
				len = len+readp-PORT_BUFFER; /* rest to read from buffer start */
				while(readp < PORT_BUFFER)
				{	/* mix till buffer end */
					sample = mix_buffer[readp]+*(buf_in16++);
					mix_buffer[readp++] = 0;
					if (sample < -32767)
						sample = -32767;
					if (sample > 32767)
						sample = 32767;
					*(rec8_buffer++) = (sample>>8)+0x80;
				}
				readp = 0;
			}
			while(len--) /* write rest */
			{	/* mix till buffer end */
				sample = mix_buffer[readp]+*(buf_in16++);
				mix_buffer[readp++] = 0;
				if (sample < -32767)
					sample = -32767;
				if (sample > 32767)
					sample = 32767;
				*(rec8_buffer++) = (sample>>8)+0x80;
			}

			/* restore (changed) read pointer for further use */
			readp = p_mixer_readp;

			/* now write the rec_buffer to the file */
			if (p_record_skip)
			{
				p_record_skip -= length;
				if (p_record_skip < 0)
					p_record_skip = 0;
			} else
			{
				fwrite(record_buffer, (length), 1, p_record);
				p_record_length = (length) + p_record_length;
			}
			break;

			case CODEC_LAW:
			len = length;
			mix_buffer = p_record_buffer;
			rec8_buffer = (unsigned char *)record_buffer;
			buf_in8 = (unsigned char *)temp_buffer;
			if (len+readp >= PORT_BUFFER) /* data hits the buffer end */
			{
				len = len+readp-PORT_BUFFER; /* rest to read from buffer start */
				while(readp < PORT_BUFFER)
				{	/* mix till buffer end */
					sample = mix_buffer[readp]+audio_law_to_s32[*(buf_in8++)];
					mix_buffer[readp++] = 0;
					if (sample < -32767)
						sample = -32767;
					if (sample > 32767)
						sample = 32767;
					*(rec8_buffer++) = (sample>>8)+0x80;
				}
				readp = 0;
			}
			while(len--) /* write rest */
			{	/* mix till buffer end */
				sample = mix_buffer[readp]+audio_law_to_s32[*(buf_in8++)];
				mix_buffer[readp++] = 0;
				if (sample < -32767)
					sample = -32767;
				if (sample > 32767)
					sample = 32767;
				*(rec8_buffer++) = (sample>>8)+0x80;
			}

			/* restore (changed) read pointer for further use */
			readp = p_mixer_readp;

			/* now write the rec_buffer to the file */
			if (p_record_skip)
			{
				p_record_skip -= length;
				if (p_record_skip < 0)
					p_record_skip = 0;
			} else
			{
				fwrite(record_buffer, (length), 1, p_record);
				p_record_length = (length) + p_record_length;
			}
			break;

		}
		break;

		case CODEC_LAW:
		case CODEC_OFF: /* if no codec is specified, the recorded data will be stored as LAW */
		/* convert from mixer to law */
		switch(codec_in)
		{
			case CODEC_MONO:
			case CODEC_STEREO:
			case CODEC_8BIT:
			len = length;
			mix_buffer = p_record_buffer;
			rec8_buffer = (unsigned char *)record_buffer;
			buf_in16 = (signed short *)temp_buffer;
			if (len+readp >= PORT_BUFFER) /* data hits the buffer end */
			{
				len = len+readp-PORT_BUFFER; /* rest to read from buffer start */
				while(readp < PORT_BUFFER)
				{	/* mix till buffer end */
					sample = mix_buffer[readp]+*(buf_in16++);
					mix_buffer[readp++] = 0;
					if (sample < -32767)
						sample = -32767;
					if (sample > 32767)
						sample = 32767;
					*(rec8_buffer++) = audio_s16_to_law[sample & 0xffff];
				}
				readp = 0;
			}
			while(len--) /* write rest */
			{	/* mix till buffer end */
				sample = mix_buffer[readp]+*(buf_in16++);
				mix_buffer[readp++] = 0;
				if (sample < -32767)
					sample = -32767;
				if (sample > 32767)
					sample = 32767;
				*(rec8_buffer++) = audio_s16_to_law[sample & 0xffff];
			}

			/* restore (changed) read pointer for further use */
			readp = p_mixer_readp;

			/* now write the rec_buffer to the file */
			if (p_record_skip)
			{
				p_record_skip -= length;
				if (p_record_skip < 0)
					p_record_skip = 0;
			} else
			{
				fwrite(record_buffer, (length), 1, p_record);
				p_record_length = (length) + p_record_length;
			}
			break;

			case CODEC_LAW:
			len = length;
			mix_buffer = p_record_buffer;
			rec8_buffer = (unsigned char *)record_buffer;
			buf_in8 = (unsigned char *)temp_buffer;
			if (len+readp >= PORT_BUFFER) /* data hits the buffer end */
			{
				len = len+readp-PORT_BUFFER; /* rest to read from buffer start */
				while(readp < PORT_BUFFER)
				{	/* mix till buffer end */
					sample = mix_buffer[readp]+audio_law_to_s32[*(buf_in8++)];
					mix_buffer[readp++] = 0;
					if (sample < -32767)
						sample = -32767;
					if (sample > 32767)
						sample = 32767;
					*(rec8_buffer++) = audio_s16_to_law[sample & 0xffff];
				}
				readp = 0;
			}
			while(len--) /* write rest */
			{	/* mix till buffer end */
				sample = mix_buffer[readp]+audio_law_to_s32[*(buf_in8++)];
				mix_buffer[readp++] = 0;
				if (sample < -32767)
					sample = -32767;
				if (sample > 32767)
					sample = 32767;
				*(rec8_buffer++) = audio_s16_to_law[sample & 0xffff];
			}

			/* restore (changed) read pointer for further use */
			readp = p_mixer_readp;

			/* now write the rec_buffer to the file */
			if (p_record_skip)
			{
				p_record_skip -= length;
				if (p_record_skip < 0)
					p_record_skip = 0;
			} else
			{
				fwrite(record_buffer, (length), 1, p_record);
				p_record_length = (length) + p_record_length;
			}
			break;
		}
		break;
	}

	/* if we have no transmitting relation, we do not need read mixer */
	if (!p_mixer_rel)
	{	/* nothing mixed to(no rel), so we are just done */
		if (compressed)
		{
			/* compress to law */
			len = length;
			buf_in8 = (unsigned char *)temp_buffer;
			buf_in16 = (signed short *)temp_buffer;
			buf_out16 = (signed short *)buffer;
			switch(codec_in)
			{
				case CODEC_MONO:
				case CODEC_STEREO:
				case CODEC_8BIT:
				while(len--)
					*(buffer++) = audio_s16_to_law[*(buf_in16++) & 0xffff];
				break;

				case CODEC_LAW:
				memcpy(buffer, temp_buffer, length);
				break;

				default:
				PERROR("Software error: current tone for unmixed & uncompressed, has no codec\n");
				exit(-1);
			}
		} else
		{
			/* uncompress law files */
			len = length;
			buf_in8 = (unsigned char *)temp_buffer;
			buf_in16 = (signed short *)temp_buffer;
			buf_out16 = (signed short *)buffer;
			switch(codec_in)
			{
				case CODEC_MONO:
				case CODEC_STEREO:
				case CODEC_8BIT:
				while(len--)
					(*(buf_out16++)) = *(buf_in16++);
				break;

				case CODEC_LAW:
				while(len--)
					(*(buf_out16++)) = audio_law_to_s32[*(buf_in8++)];
				break;

				default:
				PERROR("Software error: current tone for unmixed & uncompressed, has no codec\n");
				exit(-1);
			}
		}
		return(length);
	}

//	PDEBUG(DEBUG_PORT, "PORT(%s) mixing %d bytes. (readp=%d, PORT_BUFFER=%d)\n", p_name, length, readp, PORT_BUFFER);
	/* now we got our stuff and we'll mix it baby */

	len = length;
	mix_buffer = p_mixer_buffer;
	if (compressed)
	{
		/* convert from mixer to compressed law data */
		switch(codec_in)
		{
			case CODEC_MONO:
			case CODEC_STEREO:
			case CODEC_8BIT:
			buf_in16 = (signed short *)temp_buffer;
			if (len+readp >= PORT_BUFFER) /* data hits the buffer end */
			{
				len = len+readp-PORT_BUFFER; /* rest to read from buffer start */
				while(readp < PORT_BUFFER)
				{	/* mix till buffer end */
					sample = mix_buffer[readp]+*(buf_in16++);
					mix_buffer[readp++] = 0;
					if (sample < -32767)
						sample = -32767;
					if (sample > 32767)
						sample = 32767;
					*(buffer++) = audio_s16_to_law[sample & 0xffff];
				}
				readp = 0;
			}
			while(len--) /* write rest */
			{	/* mix till buffer end */
				sample = mix_buffer[readp]+*(buf_in16++);
				mix_buffer[readp++] = 0;
				if (sample < -32767)
					sample = -32767;
				if (sample > 32767)
					sample = 32767;
				*(buffer++) = audio_s16_to_law[sample & 0xffff];
			}
			break;

			case CODEC_LAW:
			buf_in8 = (unsigned char *)temp_buffer;
			if (len+readp >= PORT_BUFFER) /* data hits the buffer end */
			{
				len = len+readp-PORT_BUFFER; /* rest to read from buffer start */
				while(readp < PORT_BUFFER)
				{	/* mix till buffer end */
					sample = mix_buffer[readp]+audio_law_to_s32[*(buf_in8++)];
					mix_buffer[readp++] = 0;
					if (sample < -32767)
						sample = -32767;
					if (sample > 32767)
						sample = 32767;
					*(buffer++) = audio_s16_to_law[sample & 0xffff];
				}
				readp = 0;
			}
			while(len--) /* write rest */
			{	/* mix till buffer end */
				sample = mix_buffer[readp]+audio_law_to_s32[*(buf_in8++)];
				mix_buffer[readp++] = 0;
				if (sample < -32767)
					sample = -32767;
				if (sample > 32767)
					sample = 32767;
				*(buffer++) = audio_s16_to_law[sample & 0xffff];
			}
			break;

			default:
			PERROR("Software error: current tone for compressed data has no codec\n");
			exit(-1);
		}
	} else
	{
		/* convert from mixer to uncompressed 16 bit audio */
		switch(codec_in)
		{
			case CODEC_MONO:
			case CODEC_STEREO:
			case CODEC_8BIT:
			buf_in16 = (signed short *)temp_buffer;
			buf_out16 = (signed short *)buffer;
			if (len+readp >= PORT_BUFFER) /* data hits the buffer end */
			{
				len = len+readp-PORT_BUFFER; /* rest to read from buffer start */
				while(readp < PORT_BUFFER)
				{	/* mix till buffer end */
					sample = mix_buffer[readp]+*(buf_in16++);
					mix_buffer[readp++] = 0;
					if (sample < -32767)
						sample = -32767;
					if (sample > 32767)
						sample = 32767;
					*(buf_out16++) = sample;
				}
				readp = 0;
			}
			while(len--) /* write rest */
			{	/* mix till buffer end */
				sample = mix_buffer[readp]+*(buf_in16++);
				mix_buffer[readp++] = 0;
				if (sample < -32767)
					sample = -32767;
				if (sample > 32767)
					sample = 32767;
				*(buf_out16++) = sample;
			}
			break;

			case CODEC_LAW:
			buf_in8 = (unsigned char *)temp_buffer;
			buf_out16 = (signed short *)buffer;
			if (len+readp >= PORT_BUFFER) /* data hits the buffer end */
			{
				len = len+readp-PORT_BUFFER; /* rest to read from buffer start */
				while(readp < PORT_BUFFER)
				{	/* mix till buffer end */
					sample = mix_buffer[readp]+audio_law_to_s32[*(buf_in8++)];
					mix_buffer[readp++] = 0;
					if (sample < -32767)
						sample = -32767;
					if (sample > 32767)
						sample = 32767;
					*(buf_out16++) = sample;
				}
				readp = 0;
			}
			while(len--) /* write rest */
			{	/* mix till buffer end */
				sample = mix_buffer[readp]+audio_law_to_s32[*(buf_in8++)];
				mix_buffer[readp++] = 0;
				if (sample < -32767)
					sample = -32767;
				if (sample > 32767)
					sample = 32767;
				*(buf_out16++) = sample;
			}
			break;

			default:
			PERROR("Software error: current tone for uncompressed data has no codec\n");
			exit(-1);
		}
	}

	p_mixer_readp = readp; /* new read pointer */

	return(length);
}


/* dummy handler */
int Port::handler(void)
{
	return(0);
}

/* endpoint sends messages to the port
 * this is called by the message_epoint inherited by child classes
 * therefor a return=1 means: stop, no more processing
 */
//extern struct message *dddebug;
int Port::message_epoint(unsigned long epoint_id, int message_id, union parameter *param)
{
	/* check if we got audio data from one remote port */
	switch(message_id)
	{
		case MESSAGE_TONE: /* play tone */
		PDEBUG(DEBUG_PORT, "PORT(%s) isdn port with (caller id %s) setting tone '%s' dir '%s'\n", p_name, p_callerinfo.id, param->tone.name, param->tone.dir);
		set_tone(param->tone.dir,param->tone.name);
		return(1);

		case MESSAGE_DATA:
//printf("port=%s, epoint=%d\n",p_cardname, epoint->e_serial);
		mixer(param);
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


/*
 * special function generate individual isdn debug logs
 */
void Port::printisdn(char *fmt, ...)
{
	char buffer[4096];
	char name[128];
	va_list args;
	FILE *fp;

	va_start(args,fmt);
	VUNPRINT(buffer,sizeof(buffer)-1,fmt,args);
	buffer[sizeof(buffer)-1]=0;
	va_end(args);

	PDEBUG_RUNTIME(NULL, 0, DEBUG_PORT, "PORT(%s serial=%ld): %s\n", p_name, p_serial, buffer);
	if (options.deb & DEBUG_LOG)
	{
		SPRINT(name, "%s/debug_%s.log", INSTALL_DATA, p_name);
		if (!(fp = fopen(name, "a")))
			return;
	
		fprintf(fp, "%04d.%02d.%02d %02d:%02d:%02d %s(%ld): %s", now_tm->tm_year+1900, now_tm->tm_mon+1, now_tm->tm_mday, now_tm->tm_hour, now_tm->tm_min, now_tm->tm_sec, p_name, p_serial, buffer);
		fclose(fp);
	}
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
int Port::open_record(int type, int vbox, int skip, char *terminal, int anon_ignore, char *vbox_email, int vbox_email_file)
{
	/* RIFFxxxxWAVEfmt xxxx(fmt-size)dataxxxx... */
	char dummyheader[8+4+8+sizeof(fmt)+8];
	char filename[256];

	if (!terminal)
	{
		PERROR("Port(%d) not a terminal\n", p_serial);
		return(0);
	}
	SCPY(p_record_extension, terminal);
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
void Port::close_record(int beep)
{
	static signed long beep_mono[] = {-10000, 10000, -10000, 10000, -10000, 10000, -10000, 10000, -10000, 10000, -10000, 10000, -10000, 10000, -10000, 10000};
	static unsigned char beep_8bit[] = {48, 208, 48, 208, 48, 208, 48, 208, 48, 208, 48, 208, 48, 208, 48, 208, 48, 208, 48, 208, 48, 208, 48, 208, 48, 208, 48, 208, 48, 208, 48, 208, 48, 208};
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

	memcpy(&callerinfo, &p_callerinfo, sizeof(struct caller_info));
	apply_callerid_restriction(p_record_anon_ignore, -1, callerinfo.id, &callerinfo.ntype, &callerinfo.present, &callerinfo.screen, callerinfo.voip, callerinfo.intern, callerinfo.name);

	SCPY(number, p_dialinginfo.number);
	SCPY(callerid, numberrize_callerinfo(callerinfo.id, callerinfo.ntype));
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

	/* add beep to the end of recording */
	if (beep)
	switch(p_record_type)
	{
		case CODEC_MONO:
		i = 0;
		while(i < beep)
		{
			fwrite(beep_mono, sizeof(beep_mono), 1, p_record);
			i += sizeof(beep_mono);
			p_record_length += sizeof(beep_mono);
		}
		break;
		case CODEC_8BIT:
		i = 0;
		while(i < beep)
		{
			fwrite(beep_8bit, sizeof(beep_8bit), 1, p_record);
			i += sizeof(beep_8bit);
			p_record_length += sizeof(beep_8bit);
		}
		break;
#if 0
		case CODEC_LAW:
		i = 0;
		while(i < beep)
		{
			fwrite(beep_law, sizeof(beep_law), 1, p_record);
			i += sizeof(beep_law);
			p_record_length += sizeof(beep_law);
		}
		break;
#endif
		default:
		PERROR("codec %d not supported for beep adding\n", p_record_type);
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

		default:
		if (p_record_vbox == 1)
			SPRINT(filename, "%s.unknown", p_record_filename);
		else
			SPRINT(filename, "%s_%s-%s.unknown", p_record_filename, callerid, number);
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
			send_mail(p_record_vbox_email_file?filename:(char *)"", callerid, callerinfo.intern, callerinfo.name, p_record_vbox_email, p_record_vbox_year, p_record_vbox_mon, p_record_vbox_mday, p_record_vbox_hour, p_record_vbox_min, p_record_extension);
		}
	}
}



