/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** answering machine port                                                    **
**                                                                           **
** this is a child of the port class, which emulates the recorder function   **
** of an answering machine                                                   **
** it will directly answer to the setup message with a connect               **
**                                                                           **
\*****************************************************************************/ 

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "main.h"

/* note: recording log is written at endpoint */

/*
 * initialize vbox port
 */
VBoxPort::VBoxPort(int type, struct port_settings *settings) : Port(type, "vbox", settings)
{
	p_vbox_timeout = 0;
	p_vbox_announce_fh = -1;
	p_vbox_audio_start = 0;
	p_vbox_audio_transferred = 0;
	p_vbox_record_start = 0;
	p_vbox_record_limit = 0;
}


/*
 * destructor
 */
VBoxPort::~VBoxPort()
{
	if (p_vbox_announce_fh >= 0)
	{
		close(p_vbox_announce_fh);
		p_vbox_announce_fh = -1;
		fhuse--;
	}
}


/*
 * handler of vbox
 */
int VBoxPort::handler(void)
{
	struct message	*message;
	unsigned long	tosend;
	unsigned char	buffer[ISDN_TRANSMIT];
	time_t		currenttime;
	class Endpoint	*epoint;
	int		ret;

	if ((ret = Port::handler()))
		return(ret);

	if (p_vbox_record_start && p_vbox_record_limit)
	{
		time(&currenttime);
		if (currenttime > (p_vbox_record_limit+p_vbox_record_start))
		{
			while(p_epointlist)
			{
				/* send release */
				message = message_create(p_serial, p_epointlist->epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
				message->param.disconnectinfo.cause = 16;
				message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
				message_put(message);
				/* remove epoint */
				free_epointlist(p_epointlist);
			}
			/* recording is close during destruction */
			delete this;
			return(-1); /* must return because port is gone */
		}
	}

	/* set time the first time */
	if (p_vbox_audio_start < 1)
	{
		p_vbox_audio_start = now_d;
		return(0);
	}
	
	/* calculate the number of bytes */
	tosend = (unsigned long)((now_d-p_vbox_audio_start)*8000) - p_vbox_audio_transferred;

	/* wait for more */
	if (tosend < sizeof(buffer))
		return(0);
	tosend = sizeof(buffer);

	/* add the number of samples elapsed */
	p_vbox_audio_transferred += tosend;

	/* if announcement is currently played, send audio data */
	if (p_vbox_announce_fh >=0)
	{
		tosend = read_tone(p_vbox_announce_fh, buffer, p_vbox_announce_codec, tosend, p_vbox_announce_size, &p_vbox_announce_left, 1);
		if (tosend <= 0)
		{
			/* end of file */
			close(p_vbox_announce_fh);
			p_vbox_announce_fh = -1;
			fhuse--;

			time(&currenttime);
			p_vbox_record_start = currenttime;

			/* connect if not already */
			epoint = find_epoint_id(ACTIVE_EPOINT(p_epointlist));
			if (epoint)
			{
				/* if we sent our announcement during ringing, we must now connect */
				if (p_vbox_ext.vbox_free)
				{
					/* send connect message */
					message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_CONNECT);
					memcpy(&message->param.connectinfo, &p_connectinfo, sizeof(struct connect_info));
					message_put(message);
					new_state(PORT_STATE_CONNECT);
				}
			}

			/* start recording, if not already */
			if (p_vbox_mode == VBOX_MODE_NORMAL)
			{
				/* recording start */
				open_record(p_vbox_ext.vbox_codec, 2, 0, p_vbox_ext.number, p_vbox_ext.anon_ignore, p_vbox_ext.vbox_email, p_vbox_ext.vbox_email_file);
			} else // else!!
			if (p_vbox_mode == VBOX_MODE_ANNOUNCEMENT)
			{
				/* send release */
				message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_RELEASE);
				message->param.disconnectinfo.cause = 16;
				message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
				message_put(message);
				/* recording is close during destruction */
				delete this;
				return(-1); /* must return because port is gone */
			}
		} else
		{
			if (p_record)
				record(buffer, tosend, 0); // from down
			message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_DATA);
			message->param.data.len = tosend;
			memcpy(message->param.data.data, buffer, tosend);
			message_put(message);
		}
	}

        return(1);
}


/*
 * endpoint sends messages to the vbox port
 */
int VBoxPort::message_epoint(unsigned long epoint_id, int message_id, union parameter *param)
{
	struct message *message;
	class Endpoint *epoint;
	char filename[256], *c;

	if (Port::message_epoint(epoint_id, message_id, param))
		return(1);

	epoint = find_epoint_id(epoint_id);
	if (!epoint)
	{
		PDEBUG(DEBUG_EPOINT|DEBUG_VBOX, "PORT(%s) no endpoint object found where the message is from.\n", p_name);
		return(0);
	}

	switch(message_id)
	{
		case MESSAGE_DATA:
		record(param->data.data, param->data.len, 1); // from up
		return(1);

		case MESSAGE_DISCONNECT: /* call has been disconnected */
		PDEBUG(DEBUG_VBOX, "PORT(%s) vbox port with (caller id %s) received disconnect cause=%d\n", p_name, p_callerinfo.id, param->disconnectinfo.cause);

		new_state(PORT_STATE_OUT_DISCONNECT);

		while(p_epointlist)
		{
			message = message_create(p_serial, p_epointlist->epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
			message->param.disconnectinfo.cause = CAUSE_NORMAL;
			message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
			message_put(message);
			/* remove epoint */
			free_epointlist(p_epointlist);
		}
		/* recording is close during destruction */
		delete this;
		return(-1); /* must return because port is gone */
		break;

		case MESSAGE_RELEASE: /* release vbox port */
		PDEBUG(DEBUG_VBOX, "PORT(%s) vbox port with (caller id %s) received release\n", p_name, p_callerinfo.id);

		/* we are done */
		/* recording is close during destruction */
		delete this;
		return(-1); /* must return because port is gone */
		break;

		case MESSAGE_SETUP: /* dial-out command received from epoint, answer with connect */
		/* get apppbx */
		memcpy(&p_vbox_ext, &((class EndpointAppPBX *)(epoint->ep_app))->e_ext, sizeof(p_vbox_ext));
		/* extract optional announcement file */
		if ((c = strchr(param->setup.dialinginfo.id, ',')))
		{
			if (c[1] == '/')
				SPRINT(filename, c+1);
			else
				SPRINT(filename, "%s/%s/%s/vbox/%s", INSTALL_DATA, options.extensions_dir, p_vbox_ext.number);
			*c = '\0';
		} else
		{
			SPRINT(filename, "%s/%s/%s/vbox/announcement", INSTALL_DATA, options.extensions_dir, p_vbox_ext.number);
		}
		PDEBUG(DEBUG_VBOX, "PORT(%s) vbox port received setup from '%s' to '%s'\n", p_name, param->setup.callerinfo.id, param->setup.dialinginfo.id);
		memcpy(&p_callerinfo, &param->setup.callerinfo, sizeof(p_callerinfo));
		memcpy(&p_redirinfo, &param->setup.redirinfo, sizeof(p_redirinfo));
		/* link relation */
		if (p_epointlist)
		{
			PERROR("PORT(%s) software error: epoint pointer is set in idle state, how bad!! exitting.\n", p_name);
			exit(-1);
		}
		if (!(epointlist_new(epoint_id)))
		{
			PERROR("no memory for epointlist\n");
			exit(-1);
		}

		/* copy setup infos to port */
		SCPY(p_vbox_extension, param->setup.dialinginfo.id);

		/* create connect info */
		SCPY(p_connectinfo.id, p_vbox_extension);
		p_connectinfo.itype = INFO_ITYPE_VBOX;
		p_connectinfo.present = INFO_PRESENT_ALLOWED;
		p_connectinfo.screen = INFO_SCREEN_NETWORK;

		/* connect unless we can send announcement while ringing */
		if (!p_vbox_ext.vbox_free)
		{
			/* send connect message */
			message = message_create(p_serial, epoint_id, PORT_TO_EPOINT, MESSAGE_CONNECT);
			memcpy(&message->param.connectinfo, &p_connectinfo, sizeof(struct connect_info));
			message_put(message);
			new_state(PORT_STATE_CONNECT);
		} else 
		{
			/* send alerting message */
			message = message_create(p_serial, epoint_id, PORT_TO_EPOINT, MESSAGE_ALERTING);
			message_put(message);
			new_state(PORT_STATE_IN_ALERTING);
		}

		/* play the announcement */
		if ((p_vbox_announce_fh = open_tone(filename, &p_vbox_announce_codec, &p_vbox_announce_size, &p_vbox_announce_left)) >= 0)
		{
			fhuse++;
		} 
		/* start recording if desired */
		p_vbox_mode = p_vbox_ext.vbox_mode;
		p_vbox_record_limit = p_vbox_ext.vbox_time;
		if (!p_vbox_announce_fh || p_vbox_mode==VBOX_MODE_PARALLEL)
		{
			PDEBUG(DEBUG_VBOX, "PORT(%s) parallel mode OR no announcement found at: '%s' so we start recording now.\n", p_name, filename);
			/* recording start */
			open_record(p_vbox_ext.vbox_codec, 2, 0, p_vbox_ext.number, p_vbox_ext.anon_ignore, p_vbox_ext.vbox_email, p_vbox_ext.vbox_email_file);
		}
		break;

		default:
		PDEBUG(DEBUG_VBOX, "PORT(%s) vbox port with (caller id %s) received an unsupported message: %d\n", p_name, p_callerinfo.id, message_id);
	}

	return(0);
}


