/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
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


static void vbox_trace_header(class VBoxPort *vbox, const char *message, int direction)
{
	/* init trace with given values */
	start_trace(0,
		    NULL,
		    vbox?numberrize_callerinfo(vbox->p_callerinfo.id, vbox->p_callerinfo.ntype, options.national, options.international):NULL,
		    vbox?vbox->p_dialinginfo.id:NULL,
		    direction,
		    CATEGORY_CH,
		    vbox?vbox->p_serial:0,
		    message);
}


/*
 * handler of vbox
 */
int VBoxPort::handler(void)
{
	struct lcr_msg	*message;
	unsigned int	tosend;
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
				vbox_trace_header(this, "RELEASE from VBox (recoding limit reached)", DIRECTION_IN);
				add_trace("cause", "value", "%d", message->param.disconnectinfo.cause);
				add_trace("cause", "location", "%d", message->param.disconnectinfo.location);
				end_trace();
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
	tosend = (unsigned int)((now_d-p_vbox_audio_start)*8000) - p_vbox_audio_transferred;

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
					vbox_trace_header(this, "CONNECT from VBox (announcement is over)", DIRECTION_IN);
					end_trace();
					new_state(PORT_STATE_CONNECT);
				}
			}

			/* start recording, if not already */
			if (p_vbox_mode == VBOX_MODE_NORMAL)
			{
				/* recording start */
				open_record(p_vbox_ext.vbox_codec, 2, 0, p_vbox_ext.number, p_vbox_ext.anon_ignore, p_vbox_ext.vbox_email, p_vbox_ext.vbox_email_file);
				vbox_trace_header(this, "RECORDING (announcement is over)", DIRECTION_IN);
				end_trace();
			} else // else!!
			if (p_vbox_mode == VBOX_MODE_ANNOUNCEMENT)
			{
				/* send release */
				message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_RELEASE);
				message->param.disconnectinfo.cause = 16;
				message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
				message_put(message);
				vbox_trace_header(this, "RELEASE from VBox (after annoucement)", DIRECTION_IN);
				add_trace("cause", "value", "%d", message->param.disconnectinfo.cause);
				add_trace("cause", "location", "%d", message->param.disconnectinfo.location);
				end_trace();
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
int VBoxPort::message_epoint(unsigned int epoint_id, int message_id, union parameter *param)
{
	struct lcr_msg *message;
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
		new_state(PORT_STATE_OUT_DISCONNECT);
		vbox_trace_header(this, "DISCONNECT to VBox", DIRECTION_OUT);
		add_trace("cause", "value", "%d", param->disconnectinfo.cause);
		add_trace("cause", "location", "%d", param->disconnectinfo.location);
		end_trace();

		while(p_epointlist)
		{
			message = message_create(p_serial, p_epointlist->epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
			message->param.disconnectinfo.cause = CAUSE_NORMAL;
			message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
			message_put(message);
			vbox_trace_header(this, "RELEASE from VBox (after disconnect)", DIRECTION_IN);
			add_trace("cause", "value", "%d", message->param.disconnectinfo.cause);
			add_trace("cause", "location", "%d", message->param.disconnectinfo.location);
			end_trace();
			/* remove epoint */
			free_epointlist(p_epointlist);
		}
		/* recording is close during destruction */
		delete this;
		return(-1); /* must return because port is gone */
		break;

		case MESSAGE_RELEASE: /* release vbox port */
		vbox_trace_header(this, "RELEASE to VBox", DIRECTION_OUT);
		add_trace("cause", "value", "%d", param->disconnectinfo.cause);
		add_trace("cause", "location", "%d", param->disconnectinfo.location);
		end_trace();

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
		vbox_trace_header(this, "SETUP to VBox", DIRECTION_OUT);
		add_trace("from", "id", "%s", param->setup.callerinfo.id);
		add_trace("to", "box", "%s", param->setup.dialinginfo.id);
		end_trace();
		memcpy(&p_dialinginfo, &param->setup.dialinginfo, sizeof(p_dialinginfo));
		memcpy(&p_callerinfo, &param->setup.callerinfo, sizeof(p_callerinfo));
		memcpy(&p_redirinfo, &param->setup.redirinfo, sizeof(p_redirinfo));
		/* link relation */
		if (p_epointlist)
			FATAL("PORT(%s) Epoint pointer is set in idle state, how bad!!\n", p_name);
		epointlist_new(epoint_id);

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
			vbox_trace_header(this, "CONNECT from VBox (after setup)", DIRECTION_IN);
			end_trace();
			new_state(PORT_STATE_CONNECT);
		} else 
		{
			/* send alerting message */
			message = message_create(p_serial, epoint_id, PORT_TO_EPOINT, MESSAGE_ALERTING);
			message_put(message);
			vbox_trace_header(this, "ALERTING from VBox (play announcement before connect)", DIRECTION_IN);
			end_trace();
			new_state(PORT_STATE_IN_ALERTING);
		}

		/* play the announcement */
		if ((p_vbox_announce_fh = open_tone(filename, &p_vbox_announce_codec, &p_vbox_announce_size, &p_vbox_announce_left)) >= 0)
		{
			fhuse++;
		} 
		vbox_trace_header(this, "ANNOUNCEMENT", DIRECTION_OUT);
		add_trace("file", "name", "%s", filename);
		add_trace("file", "exists", "%s", (p_vbox_announce_fh>=0)?"yes":"no");
		end_trace();
		/* start recording if desired */
		p_vbox_mode = p_vbox_ext.vbox_mode;
		p_vbox_record_limit = p_vbox_ext.vbox_time;
		if (p_vbox_announce_fh<0 || p_vbox_mode==VBOX_MODE_PARALLEL)
		{
			/* recording start */
			open_record(p_vbox_ext.vbox_codec, 2, 0, p_vbox_ext.number, p_vbox_ext.anon_ignore, p_vbox_ext.vbox_email, p_vbox_ext.vbox_email_file);
			vbox_trace_header(this, "RECORDING", DIRECTION_IN);
			end_trace();
		}
		break;

		default:
		PDEBUG(DEBUG_VBOX, "PORT(%s) vbox port with (caller id %s) received an unsupported message: %d\n", p_name, p_callerinfo.id, message_id);
	}

	return(0);
}


