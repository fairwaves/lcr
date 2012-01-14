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

int announce_timer(struct lcr_timer *timer, void *instance, int index);
int record_timeout(struct lcr_timer *timer, void *instance, int index);

/*
 * initialize vbox port
 */
VBoxPort::VBoxPort(int type, struct port_settings *settings) : Port(type, "vbox", settings)
{
	p_vbox_timeout = 0;
	p_vbox_announce_fh = -1;
	p_vbox_audio_start = 0;
	p_vbox_audio_transferred = 0;
	p_vbox_record_limit = 0;
	memset(&p_vbox_announce_timer, 0, sizeof(p_vbox_announce_timer));
	add_timer(&p_vbox_announce_timer, announce_timer, this, 0);
	memset(&p_vbox_record_timeout, 0, sizeof(p_vbox_record_timeout));
	add_timer(&p_vbox_record_timeout, record_timeout, this, 0);
}


/*
 * destructor
 */
VBoxPort::~VBoxPort()
{
	del_timer(&p_vbox_announce_timer);
	del_timer(&p_vbox_record_timeout);
	if (p_vbox_announce_fh >= 0) {
		close(p_vbox_announce_fh);
		p_vbox_announce_fh = -1;
		fhuse--;
	}
}


static void vbox_trace_header(class VBoxPort *vbox, const char *message, int direction)
{
	/* init trace with given values */
	start_trace(-1,
		    NULL,
		    vbox?numberrize_callerinfo(vbox->p_callerinfo.id, vbox->p_callerinfo.ntype, options.national, options.international):NULL,
		    vbox?vbox->p_dialinginfo.id:NULL,
		    direction,
		    CATEGORY_CH,
		    vbox?vbox->p_serial:0,
		    message);
}


int record_timeout(struct lcr_timer *timer, void *instance, int index)
{
	class VBoxPort *vboxport = (class VBoxPort *)instance;
	struct lcr_msg	*message;

	while(vboxport->p_epointlist) {
		/* send release */
		message = message_create(vboxport->p_serial, vboxport->p_epointlist->epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = 16;
		message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		message_put(message);
		vbox_trace_header(vboxport, "RELEASE from VBox (recoding limit reached)", DIRECTION_IN);
		add_trace("cause", "value", "%d", message->param.disconnectinfo.cause);
		add_trace("cause", "location", "%d", message->param.disconnectinfo.location);
		end_trace();
		/* remove epoint */
		vboxport->free_epointlist(vboxport->p_epointlist);
	}
	/* recording is close during destruction */
	delete vboxport;
	return 0;
}

int announce_timer(struct lcr_timer *timer, void *instance, int index)
{
	class VBoxPort *vboxport = (class VBoxPort *)instance;

	/* port my self destruct here */
	vboxport->send_announcement();

	return 0;
}

void VBoxPort::send_announcement(void)
{
	struct lcr_msg	*message;
	unsigned int	tosend;
	unsigned char	buffer[PORT_TRANSMIT + PORT_TRANSMIT]; /* use twice of the buffer, so we can send more in case of delayed main loop execution */
	class Endpoint	*epoint;
	int		temp;
	struct timeval current_time;
	long long	now;  /* Time in samples */

	/* don't restart timer, if announcement is played */
	if (p_vbox_announce_fh < 0)
		return;

	gettimeofday(&current_time, NULL);
	now = (current_time.tv_sec * MICRO_SECONDS + current_time.tv_usec)/125;

	/* set time the first time */
	if (!p_vbox_audio_start)
		p_vbox_audio_start = now - PORT_TRANSMIT;
	
	/* calculate the number of bytes */
	tosend = (unsigned int)(now - p_vbox_audio_start) - p_vbox_audio_transferred;
	if (tosend > sizeof(buffer))
		tosend = sizeof(buffer);

	/* schedule next event */
	temp = PORT_TRANSMIT + PORT_TRANSMIT - tosend;
	if (temp < 0)
		temp = 0;
	schedule_timer(&p_vbox_announce_timer, 0, temp*125);

	/* add the number of samples elapsed */
	p_vbox_audio_transferred += tosend;

	/* if announcement is currently played, send audio data */
	tosend = read_tone(p_vbox_announce_fh, buffer, p_vbox_announce_codec, tosend, p_vbox_announce_size, &p_vbox_announce_left, 1);
	if (tosend <= 0) {
		/* end of file */
		close(p_vbox_announce_fh);
		p_vbox_announce_fh = -1;
		fhuse--;

		if (p_vbox_record_limit)
			schedule_timer(&p_vbox_record_timeout, p_vbox_record_limit, 0);

		/* connect if not already */
		epoint = find_epoint_id(ACTIVE_EPOINT(p_epointlist));
		if (epoint) {
			/* if we sent our announcement during ringing, we must now connect */
			if (p_vbox_ext.vbox_free) {
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
		if (p_vbox_mode == VBOX_MODE_NORMAL) {
			/* recording start */
			open_record(p_vbox_ext.vbox_codec, 2, 0, p_vbox_ext.number, p_vbox_ext.anon_ignore, p_vbox_ext.vbox_email, p_vbox_ext.vbox_email_file);
			vbox_trace_header(this, "RECORDING (announcement is over)", DIRECTION_IN);
			end_trace();
		} else // else!!
		if (p_vbox_mode == VBOX_MODE_ANNOUNCEMENT) {
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
			return; /* must return because port is gone */
		}
	} else {
		if (p_record)
			record(buffer, tosend, 0); // from down
		/* send to remote, if bridged */
		bridge_tx(buffer, tosend);
	}
}

int VBoxPort::bridge_rx(unsigned char *data, int len)
{
	if (p_record)
		record(data, len, 1); // from up
	return 0;
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
	if (!epoint) {
		PDEBUG(DEBUG_EPOINT|DEBUG_VBOX, "PORT(%s) no endpoint object found where the message is from.\n", p_name);
		return(0);
	}

	switch(message_id) {
		case MESSAGE_DISCONNECT: /* call has been disconnected */
		new_state(PORT_STATE_OUT_DISCONNECT);
		vbox_trace_header(this, "DISCONNECT to VBox", DIRECTION_OUT);
		add_trace("cause", "value", "%d", param->disconnectinfo.cause);
		add_trace("cause", "location", "%d", param->disconnectinfo.location);
		end_trace();

		while(p_epointlist) {
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

		case MESSAGE_RELEASE: /* release vbox port */
		vbox_trace_header(this, "RELEASE to VBox", DIRECTION_OUT);
		add_trace("cause", "value", "%d", param->disconnectinfo.cause);
		add_trace("cause", "location", "%d", param->disconnectinfo.location);
		end_trace();

		/* we are done */
		/* recording is close during destruction */
		delete this;
		return(-1); /* must return because port is gone */

		case MESSAGE_SETUP: /* dial-out command received from epoint, answer with connect */
		/* get apppbx */
		memcpy(&p_vbox_ext, &((class EndpointAppPBX *)(epoint->ep_app))->e_ext, sizeof(p_vbox_ext));
		/* extract optional announcement file */
		if ((c = strchr(param->setup.dialinginfo.id, ','))) {
			if (c[1] == '/')
				SPRINT(filename, c+1);
			else
				SPRINT(filename, "%s/%s/vbox/%s", EXTENSION_DATA, p_vbox_ext.number);
			*c = '\0';
		} else {
			SPRINT(filename, "%s/%s/vbox/announcement", EXTENSION_DATA, p_vbox_ext.number);
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
		if (!p_vbox_ext.vbox_free) {
			/* send connect message */
			message = message_create(p_serial, epoint_id, PORT_TO_EPOINT, MESSAGE_CONNECT);
			memcpy(&message->param.connectinfo, &p_connectinfo, sizeof(struct connect_info));
			message_put(message);
			vbox_trace_header(this, "CONNECT from VBox (after setup)", DIRECTION_IN);
			end_trace();
			new_state(PORT_STATE_CONNECT);
		} else {
			/* send alerting message */
			message = message_create(p_serial, epoint_id, PORT_TO_EPOINT, MESSAGE_ALERTING);
			message_put(message);
			vbox_trace_header(this, "ALERTING from VBox (play announcement before connect)", DIRECTION_IN);
			end_trace();
			new_state(PORT_STATE_IN_ALERTING);
		}

		/* play the announcement */
		if ((p_vbox_announce_fh = open_tone(filename, &p_vbox_announce_codec, &p_vbox_announce_size, &p_vbox_announce_left)) >= 0) {
			fhuse++;
			schedule_timer(&p_vbox_announce_timer, 0, 300000);
		} 
		vbox_trace_header(this, "ANNOUNCEMENT", DIRECTION_OUT);
		add_trace("file", "name", "%s", filename);
		add_trace("file", "exists", "%s", (p_vbox_announce_fh>=0)?"yes":"no");
		end_trace();
		/* start recording if desired */
		p_vbox_mode = p_vbox_ext.vbox_mode;
		p_vbox_record_limit = p_vbox_ext.vbox_time;
		if (p_vbox_announce_fh<0 || p_vbox_mode==VBOX_MODE_PARALLEL) {
			/* recording start */
			open_record(p_vbox_ext.vbox_codec, 2, 0, p_vbox_ext.number, p_vbox_ext.anon_ignore, p_vbox_ext.vbox_email, p_vbox_ext.vbox_email_file);
			vbox_trace_header(this, "RECORDING", DIRECTION_IN);
			end_trace();
		}
		return(1);

		default:
		PDEBUG(DEBUG_VBOX, "PORT(%s) vbox port with (caller id %s) received an unsupported message: %d\n", p_name, p_callerinfo.id, message_id);
	}

	return(0);
}


