/*****************************************************************************\
**                                                                           **
** LCR                                                                       **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** mISDN dss1                                                                **
**                                                                           **
\*****************************************************************************/ 

#include "main.h"
#include "myisdn.h"
// socket mISDN
//#include <sys/socket.h>
extern "C" {
}
#include <mISDN/q931.h>
#ifdef OLD_MT_ASSIGN
extern unsigned int mt_assign_pid;
#endif

#include "ie.cpp"

static int delete_event(struct lcr_work *work, void *instance, int index);

/*
 * constructor
 */
Pdss1::Pdss1(int type, struct mISDNport *mISDNport, char *portname, struct port_settings *settings, int channel, int exclusive, int mode) : PmISDN(type, mISDNport, portname, settings, channel, exclusive, mode)
{
	p_callerinfo.itype = (mISDNport->ifport->interface->extension)?INFO_ITYPE_ISDN_EXTENSION:INFO_ITYPE_ISDN;
	p_m_d_ntmode = mISDNport->ntmode;
	p_m_d_tespecial = mISDNport->tespecial;
	p_m_d_l3id = 0;
	memset(&p_m_d_delete, 0, sizeof(p_m_d_delete));
	add_work(&p_m_d_delete, delete_event, this, 0);
	p_m_d_ces = -1;
	p_m_d_queue[0] = '\0';
	p_m_d_notify_pending = NULL;
	p_m_d_collect_cause = 0;
	p_m_d_collect_location = 0;

	PDEBUG(DEBUG_ISDN, "Created new mISDNPort(%s). Currently %d objects use, %s%s port #%d\n", portname, mISDNport->use, (mISDNport->ntmode)?"NT":"TE", (mISDNport->tespecial)?" (special)":"", p_m_portnum);
}


/*
 * destructor
 */
Pdss1::~Pdss1()
{
	del_work(&p_m_d_delete);

	/* remove queued message */
	if (p_m_d_notify_pending)
		message_free(p_m_d_notify_pending);
}


/*
 * create layer 3 message
 */
static struct l3_msg *create_l3msg(void)
{
	struct l3_msg *l3m;

	l3m = alloc_l3_msg();
	if (l3m)
		return(l3m);

	FATAL("Cannot allocate memory, system overloaded.\n");
	exit(0); // make gcc happy
}

/*
 * if we received a first reply to the setup message,
 * we will check if we have now channel information 
 * return: <0: error, call is released, -cause is given
 *	    0: ok, nothing to do
 */
int Pdss1::received_first_reply_to_setup(unsigned int cmd, int channel, int exclusive)
{
	int ret;
	l3_msg *l3m;

	/* correct exclusive to 0, if no explicit channel was given */
	if (exclusive<0 || channel<=0)
		exclusive = 0;
	
	/* select scenario */
	if (p_m_b_channel && p_m_b_exclusive) {
		/*** we gave an exclusive channel (or if we are done) ***/

		/* if not first reply, we are done */
		if (p_state != PORT_STATE_OUT_SETUP)
			return(0);

		chan_trace_header(p_m_mISDNport, this, "CHANNEL SELECTION (first reply to setup)", DIRECTION_NONE);
		add_trace("channel", "request", "%d (forced)", p_m_b_channel);
		add_trace("channel", "reply", (channel>=0)?"%d":"(none)", channel);

		/* if give channel not accepted or not equal */
		if (channel!=-1 && p_m_b_channel!=channel) {
			add_trace("conclusion", NULL, "forced channel not accepted");
			end_trace();
			ret = -44;
			goto channelerror;
		}

		add_trace("conclusion", NULL, "channel was accepted");
		add_trace("connect", "channel", "%d", p_m_b_channel);
		end_trace();

		/* activate our exclusive channel */
		bchannel_event(p_m_mISDNport, p_m_b_index, B_EVENT_USE);
	} else
	if (p_m_b_channel) {
		/*** we gave a non-exclusive channel ***/

		/* if not first reply, we are done */
		if (p_state != PORT_STATE_OUT_SETUP)
			return(0);

		chan_trace_header(p_m_mISDNport, this, "CHANNEL SELECTION (first reply to setup)", DIRECTION_NONE);
		add_trace("channel", "request", "%d (suggest)", p_m_b_channel);
		add_trace("channel", "reply", (channel>=0)?"%d":"(none)", channel);

		/* if channel was accepted as given */
		if (channel==-1 || p_m_b_channel==channel) {
			add_trace("conclusion", NULL, "channel was accepted as given");
			add_trace("connect", "channel", "%d", p_m_b_channel);
			end_trace();
			p_m_b_exclusive = 1; // we are done
			bchannel_event(p_m_mISDNport, p_m_b_index, B_EVENT_USE);
			return(0);
		}

		/* if channel value is faulty */
		if (channel <= 0) {
			add_trace("conclusion", NULL, "illegal reply");
			end_trace();
			ret = -111; // protocol error
			goto channelerror;
		}

		/* if channel was not accepted, try to get it */
		ret = seize_bchannel(channel, 1); // exclusively
		add_trace("channel", "available", ret<0?"no":"yes");
		if (ret < 0) {
			add_trace("conclusion", NULL, "replied channel not available");
			end_trace();
			goto channelerror;
		}
		add_trace("conclusion", NULL, "replied channel accepted");
		add_trace("connect", "channel", "%d", p_m_b_channel);
		end_trace();

		/* activate channel given by remote */
		bchannel_event(p_m_mISDNport, p_m_b_index, B_EVENT_USE);
	} else
	if (p_m_b_reserve) {
		/*** we sent 'any channel acceptable' ***/

		/* if not first reply, we are done */
		if (p_state != PORT_STATE_OUT_SETUP)
			return(0);

		chan_trace_header(p_m_mISDNport, this, "CHANNEL SELECTION (first reply to setup)", DIRECTION_NONE);
		add_trace("channel", "request", "any");
		add_trace("channel", "reply", (channel>=0)?"%d":"(none)", channel);
		/* if no channel was replied */
		if (channel <= 0) {
			add_trace("conclusion", NULL, "no channel, protocol error");
			end_trace();
			ret = -111; // protocol error
			goto channelerror;
		}

		/* we will see, if our received channel is available */
		ret = seize_bchannel(channel, 1); // exclusively
		add_trace("channel", "available", ret<0?"no":"yes");
		if (ret < 0) {
			add_trace("conclusion", NULL, "replied channel not available");
			end_trace();
			goto channelerror;
		}
		add_trace("conclusion", NULL, "replied channel accepted");
		add_trace("connect", "channel", "%d", p_m_b_channel);
		end_trace();

		/* activate channel given by remote */
		bchannel_event(p_m_mISDNport, p_m_b_index, B_EVENT_USE);
	} else {
		/*** we sent 'no channel available' ***/

		/* if not the first reply, but a connect, we are forced */
		if (cmd==MT_CONNECT && p_state!=PORT_STATE_OUT_SETUP) {
			chan_trace_header(p_m_mISDNport, this, "CHANNEL SELECTION (connect)", DIRECTION_NONE);
			add_trace("channel", "request", "no-channel");
			add_trace("channel", "reply", (channel>=0)?"%d%s":"(none)", channel, exclusive?" (forced)":"");
			if (channel > 0) {
				goto use_from_connect;
			}
			ret = seize_bchannel(CHANNEL_ANY, 0); // any channel
			add_trace("channel", "available", ret<0?"no":"yes");
			if (ret < 0) {
				add_trace("conclusion", NULL, "no channel available during call-waiting");
				end_trace();
				goto channelerror;
			}
			add_trace("conclusion", NULL, "using channel %d", p_m_b_channel);
			add_trace("connect", "channel", "%d", p_m_b_channel);
			end_trace();
			p_m_b_exclusive = 1; // we are done

			/* activate channel given by remote */
			bchannel_event(p_m_mISDNport, p_m_b_index, B_EVENT_USE);
			return(0);
		}
		
		/* if not first reply, we are done */
		if (p_state != PORT_STATE_OUT_SETUP)
			return(0);

		chan_trace_header(p_m_mISDNport, this, "CHANNEL SELECTION (first reply to setup)", DIRECTION_NONE);
		add_trace("channel", "request", "no-channel");
		add_trace("channel", "reply", (channel>=0)?"%d":"(none)", channel);
		/* if first reply has no channel, we are done */
		if (channel <= 0) {
			add_trace("conclusion", NULL, "no channel until connect");
			end_trace();
			return(0);
		}

		/* we will see, if our received channel is available */
		use_from_connect:
		ret = seize_bchannel(channel, exclusive);
		add_trace("channel", "available", ret<0?"no":"yes");
		if (ret < 0) {
			add_trace("conclusion", NULL, "replied channel not available");
			end_trace();
			goto channelerror;
		}
		add_trace("conclusion", NULL, "replied channel accepted");
		add_trace("connect", "channel", "%d", p_m_b_channel);
		end_trace();
		p_m_b_exclusive = 1; // we are done

		/* activate channel given by remote */
		bchannel_event(p_m_mISDNport, p_m_b_index, B_EVENT_USE);
	}
	return(0);

	channelerror:
	/*
	 * NOTE: we send MT_RELEASE_COMPLETE to "REJECT" the channel
	 * in response to the setup reply
	 */
	l3m = create_l3msg();
	l1l2l3_trace_header(p_m_mISDNport, this, L3_RELEASE_COMPLETE_REQ, DIRECTION_OUT);
	enc_ie_cause(l3m, (p_m_mISDNport->locally)?LOCATION_PRIVATE_LOCAL:LOCATION_PRIVATE_REMOTE, -ret);
	end_trace();
	p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_RELEASE_COMPLETE, p_m_d_l3id, l3m);
	new_state(PORT_STATE_RELEASE);
	trigger_work(&p_m_d_delete);
	return(-34); /* to epoint: no channel available */
}


/*
 * hunt bchannel for incoming setup or retrieve or resume
 */
int Pdss1::hunt_bchannel(int channel, int exclusive)
{
	struct select_channel *selchannel;
	struct interface_port *ifport = p_m_mISDNport->ifport;
	int i;

	chan_trace_header(p_m_mISDNport, this, "CHANNEL SELECTION (setup)", DIRECTION_NONE);
	if (exclusive<0)
		exclusive = 0;
	if (channel == CHANNEL_NO)
		add_trace("channel", "request", "no-channel");
	else
		add_trace("channel", "request", (channel>0)?"%d%s":"any", channel, exclusive?" (forced)":"");
	if (channel==CHANNEL_NO && p_type==PORT_TYPE_DSS1_TE_IN) {
		add_trace("conclusion", NULL, "incoming call-waiting not supported for TE-mode");
		end_trace();
		return(-6); // channel unacceptable
	}
	if (channel <= 0) /* not given, no channel, whatever.. */
		channel = CHANNEL_ANY; /* any channel */
	add_trace("channel", "reserved", "%d", p_m_mISDNport->b_reserved);
	if (p_m_mISDNport->b_reserved >= p_m_mISDNport->b_num) { // of out chan..
		add_trace("conclusion", NULL, "all channels are reserved");
		end_trace();
		return(-34); // no channel
	}
	if (channel == CHANNEL_ANY)
		goto get_from_list;
	if (channel > 0) {
		/* check for given channel in selection list */
		selchannel = ifport->in_channel;
		while(selchannel) {
			if (selchannel->channel == channel || selchannel->channel == CHANNEL_FREE)
				break;
			selchannel = selchannel->next;
		}
		if (!selchannel)
			channel = 0;

		/* exclusive channel requests must be in the list */
		if (exclusive) {
			/* no exclusive channel */
			if (!channel) {
				add_trace("conclusion", NULL, "exclusively requested channel not in list");
				end_trace();
				return(-6); // channel unacceptable
			}
			/* get index for channel */
			i = channel-1-(channel>=17);
			if (i < 0 || i >= p_m_mISDNport->b_num || channel == 16) {
				add_trace("conclusion", NULL, "exclusively requested channel outside interface range");
				end_trace();
				return(-6); // channel unacceptable
			}
			/* check if busy */
			if (p_m_mISDNport->b_port[i] == NULL)
				goto use_channel;
			add_trace("conclusion", NULL, "exclusively requested channel is busy");
			end_trace();
			return(-6); // channel unacceptable
		}

		/* requested channels in list will be used */
		if (channel) {
			/* get index for channel */
			i = channel-1-(channel>=17);
			if (i < 0 || i >= p_m_mISDNport->b_num || channel == 16) {
				add_trace("info", NULL, "requested channel %d outside interface range", channel);
			} else /* if inside range (else) check if available */
			if (p_m_mISDNport->b_port[i] == NULL)
				goto use_channel;
		}

		/* if channel is not available or not in list, it must be searched */
		get_from_list:
		/* check for first free channel in list */
		channel = 0;
		selchannel = ifport->in_channel;
		while(selchannel) {
			switch(selchannel->channel) {
				case CHANNEL_FREE: /* free channel */
				add_trace("hunting", "channel", "free");
				if (p_m_mISDNport->b_reserved >= p_m_mISDNport->b_num)
					break; /* all channel in use or reserverd */
				/* find channel */
				i = 0;
				while(i < p_m_mISDNport->b_num) {
					if (p_m_mISDNport->b_port[i] == NULL) {
						channel = i+1+(i>=15);
						break;
					}
					i++;
				}
				break;

				default:
				add_trace("hunting", "channel", "%d", selchannel->channel);
				if (selchannel->channel<1 || selchannel->channel==16)
					break; /* invalid channels */
				i = selchannel->channel-1-(selchannel->channel>=17);
				if (i >= p_m_mISDNport->b_num)
					break; /* channel not in port */
				if (p_m_mISDNport->b_port[i] == NULL) {
					channel = selchannel->channel;
					break;
				}
				break;
			}
			if (channel)
				break; /* found channel */
			selchannel = selchannel->next;
		}
		if (!channel) {
			add_trace("conclusion", NULL, "no channel available");
			end_trace();
			return(-6); // channel unacceptable
		}
	}
use_channel:
	add_trace("conclusion", NULL, "channel available");
	add_trace("connect", "channel", "%d", channel);
	end_trace();
	return(channel);
}

/*
 * handles all indications
 */
/* CC_SETUP INDICATION */
void Pdss1::setup_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m)
{
	int calling_type, calling_plan, calling_present, calling_screen;
	int calling_type2, calling_plan2, calling_present2, calling_screen2;
	int called_type, called_plan;
	int redir_type, redir_plan, redir_present, redir_screen, redir_reason;
	int hlc_coding, hlc_presentation, hlc_interpretation, hlc_hlc, hlc_exthlc;
	int bearer_coding, bearer_capability, bearer_mode, bearer_rate, bearer_multi, bearer_user;
	int exclusive, channel;
	int ret;
	unsigned char keypad[33] = "";
	unsigned char useruser[128];
	int useruser_len = 0, useruser_protocol;
	class Endpoint *epoint;
	struct lcr_msg *message;

	/* process given callref */
	l1l2l3_trace_header(p_m_mISDNport, this, L3_NEW_L3ID_IND, DIRECTION_IN);
	add_trace("callref", "new", "0x%x", pid);
	if (p_m_d_l3id) {
		/* release in case the ID is already in use */
		add_trace("error", NULL, "callref already in use");
		end_trace();
		l3m = create_l3msg();
		l1l2l3_trace_header(p_m_mISDNport, this, L3_RELEASE_COMPLETE_REQ, DIRECTION_OUT);
		enc_ie_cause(l3m, (p_m_mISDNport->locally)?LOCATION_PRIVATE_LOCAL:LOCATION_PRIVATE_REMOTE, 47);
		add_trace("reason", NULL, "callref already in use");
		end_trace();
		p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_RELEASE_COMPLETE, pid, l3m);
		new_state(PORT_STATE_RELEASE);
		trigger_work(&p_m_d_delete);
		return;
	}
	p_m_d_l3id = pid;
	p_m_d_ces = pid >> 16;
	end_trace();

	l1l2l3_trace_header(p_m_mISDNport, this, L3_SETUP_IND, DIRECTION_IN);
	dec_ie_calling_pn(l3m, &calling_type, &calling_plan, &calling_present, &calling_screen, (unsigned char *)p_callerinfo.id, sizeof(p_callerinfo.id), &calling_type2, &calling_plan2, &calling_present2, &calling_screen2, (unsigned char *)p_callerinfo.id2, sizeof(p_callerinfo.id2));
	dec_ie_called_pn(l3m, &called_type, &called_plan, (unsigned char *)p_dialinginfo.id, sizeof(p_dialinginfo.id));
	dec_ie_keypad(l3m, (unsigned char *)keypad, sizeof(keypad));
	/* te-mode: CNIP (calling name identification presentation) */
	dec_facility_centrex(l3m, (unsigned char *)p_callerinfo.name, sizeof(p_callerinfo.name));
	dec_ie_useruser(l3m, &useruser_protocol, useruser, &useruser_len);
	dec_ie_complete(l3m, &p_dialinginfo.sending_complete);
	dec_ie_redir_nr(l3m, &redir_type, &redir_plan, &redir_present, &redir_screen, &redir_reason, (unsigned char *)p_redirinfo.id, sizeof(p_redirinfo.id));
	dec_ie_channel_id(l3m, &exclusive, &channel);
	dec_ie_hlc(l3m, &hlc_coding, &hlc_interpretation, &hlc_presentation, &hlc_hlc, &hlc_exthlc);
	dec_ie_bearer(l3m, &bearer_coding, &bearer_capability, &bearer_mode, &bearer_rate, &bearer_multi, &bearer_user);
	dec_ie_display(l3m, (unsigned char *)p_dialinginfo.display, sizeof(p_dialinginfo.display));
	end_trace();

	/* if blocked, release call with MT_RELEASE_COMPLETE */
	if (p_m_mISDNport->ifport->block) {
		l3m = create_l3msg();
		l1l2l3_trace_header(p_m_mISDNport, this, L3_RELEASE_COMPLETE_REQ, DIRECTION_OUT);
		enc_ie_cause(l3m, (p_m_mISDNport->locally)?LOCATION_PRIVATE_LOCAL:LOCATION_PRIVATE_REMOTE, 27); /* temporary unavailable */
		add_trace("reason", NULL, "port blocked");
		end_trace();
		p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_RELEASE_COMPLETE, p_m_d_l3id, l3m);
		new_state(PORT_STATE_RELEASE);
		trigger_work(&p_m_d_delete);
		return;
	}

	/* caller info */
	switch (calling_present) {
		case 1:
		p_callerinfo.present = INFO_PRESENT_RESTRICTED;
		break;
		case 2:
		p_callerinfo.present = INFO_PRESENT_NOTAVAIL;
		break;
		default:
		p_callerinfo.present = INFO_PRESENT_ALLOWED;
		break;
	}
	switch (calling_screen) {
		case 0:
		p_callerinfo.screen = INFO_SCREEN_USER;
		break;
		case 1:
		p_callerinfo.screen = INFO_SCREEN_USER_VERIFIED_PASSED;
		break;
		case 2:
		p_callerinfo.screen = INFO_SCREEN_USER_VERIFIED_FAILED;
		break;
		default:
		p_callerinfo.screen = INFO_SCREEN_NETWORK;
		break;
	}
	switch (calling_type) {
		case -1:
		p_callerinfo.ntype = INFO_NTYPE_NOTPRESENT;
		break;
		case 0x0:
		p_callerinfo.ntype = INFO_NTYPE_UNKNOWN;
		break;
		case 0x1:
		p_callerinfo.ntype = INFO_NTYPE_INTERNATIONAL;
		break;
		case 0x2:
		p_callerinfo.ntype = INFO_NTYPE_NATIONAL;
		break;
		case 0x4:
		p_callerinfo.ntype = INFO_NTYPE_SUBSCRIBER;
		break;
		default:
		p_callerinfo.ntype = INFO_NTYPE_UNKNOWN;
		break;
	}
	p_callerinfo.isdn_port = p_m_portnum;
	SCPY(p_callerinfo.interface, p_m_mISDNport->ifport->interface->name);

	/* caller info2 */
	switch (calling_present2) {
		case 1:
		p_callerinfo.present2 = INFO_PRESENT_RESTRICTED;
		break;
		case 2:
		p_callerinfo.present2 = INFO_PRESENT_NOTAVAIL;
		break;
		default:
		p_callerinfo.present2 = INFO_PRESENT_ALLOWED;
		break;
	}
	switch (calling_screen2) {
		case 0:
		p_callerinfo.screen2 = INFO_SCREEN_USER;
		break;
		case 1:
		p_callerinfo.screen2 = INFO_SCREEN_USER_VERIFIED_PASSED;
		break;
		case 2:
		p_callerinfo.screen2 = INFO_SCREEN_USER_VERIFIED_FAILED;
		break;
		default:
		p_callerinfo.screen2 = INFO_SCREEN_NETWORK;
		break;
	}
	switch (calling_type2) {
		case -1:
		p_callerinfo.ntype2 = INFO_NTYPE_NOTPRESENT;
		break;
		case 0x0:
		p_callerinfo.ntype2 = INFO_NTYPE_UNKNOWN;
		break;
		case 0x1:
		p_callerinfo.ntype2 = INFO_NTYPE_INTERNATIONAL;
		break;
		case 0x2:
		p_callerinfo.ntype2 = INFO_NTYPE_NATIONAL;
		break;
		case 0x4:
		p_callerinfo.ntype2 = INFO_NTYPE_SUBSCRIBER;
		break;
		default:
		p_callerinfo.ntype2 = INFO_NTYPE_UNKNOWN;
		break;
	}

	/* dialing information */
	SCAT(p_dialinginfo.id, (char *)keypad);
	switch (called_type) {
		case 0x1:
		p_dialinginfo.ntype = INFO_NTYPE_INTERNATIONAL;
		break;
		case 0x2:
		p_dialinginfo.ntype = INFO_NTYPE_NATIONAL;
		break;
		case 0x4:
		p_dialinginfo.ntype = INFO_NTYPE_SUBSCRIBER;
		break;
		default:
		p_dialinginfo.ntype = INFO_NTYPE_UNKNOWN;
		break;
	}

	/* redir info */
	switch (redir_present) {
		case 1:
		p_redirinfo.present = INFO_PRESENT_RESTRICTED;
		break;
		case 2:
		p_redirinfo.present = INFO_PRESENT_NOTAVAIL;
		break;
		default:
		p_redirinfo.present = INFO_PRESENT_ALLOWED;
		break;
	}
	switch (redir_screen) {
		case 0:
		p_redirinfo.screen = INFO_SCREEN_USER;
		break;
		case 1:
		p_redirinfo.screen = INFO_SCREEN_USER_VERIFIED_PASSED;
		break;
		case 2:
		p_redirinfo.screen = INFO_SCREEN_USER_VERIFIED_FAILED;
		break;
		default:
		p_redirinfo.screen = INFO_SCREEN_NETWORK;
		break;
	}
	switch (redir_reason) {
		case 1:
		p_redirinfo.reason = INFO_REDIR_BUSY;
		break;
		case 2:
		p_redirinfo.reason = INFO_REDIR_NORESPONSE;
		break;
		case 15:
		p_redirinfo.reason = INFO_REDIR_UNCONDITIONAL;
		break;
		case 10:
		p_redirinfo.reason = INFO_REDIR_CALLDEFLECT;
		break;
		case 9:
		p_redirinfo.reason = INFO_REDIR_OUTOFORDER;
		break;
		default:
		p_redirinfo.reason = INFO_REDIR_UNKNOWN;
		break;
	}
	switch (redir_type) {
		case -1:
		p_redirinfo.ntype = INFO_NTYPE_NOTPRESENT;
		break;
		case 0x0:
		p_redirinfo.ntype = INFO_NTYPE_UNKNOWN;
		break;
		case 0x1:
		p_redirinfo.ntype = INFO_NTYPE_INTERNATIONAL;
		break;
		case 0x2:
		p_redirinfo.ntype = INFO_NTYPE_NATIONAL;
		break;
		case 0x4:
		p_redirinfo.ntype = INFO_NTYPE_SUBSCRIBER;
		break;
		default:
		p_redirinfo.ntype = INFO_NTYPE_UNKNOWN;
		break;
	}
	p_redirinfo.isdn_port = p_m_portnum;

	/* bearer capability */
	switch (bearer_capability) {
		case -1:
		p_capainfo.bearer_capa = INFO_BC_AUDIO;
		bearer_user = (options.law=='a')?3:2;
		break;
		default:
		p_capainfo.bearer_capa = bearer_capability;
		break;
	}
	switch (bearer_mode) {
		case 2:
		p_capainfo.bearer_mode = INFO_BMODE_PACKET;
		break;
		default:
		p_capainfo.bearer_mode = INFO_BMODE_CIRCUIT;
		break;
	}
	switch (bearer_user) {
		case -1:
		p_capainfo.bearer_info1 = INFO_INFO1_NONE;
		break;
		default:
		p_capainfo.bearer_info1 = bearer_user + 0x80;
		break;
	}

	/* hlc */
	switch (hlc_hlc) {
		case -1:
		p_capainfo.hlc = INFO_HLC_NONE;
		break;
		default:
		p_capainfo.hlc = hlc_hlc + 0x80;
		break;
	}
	switch (hlc_exthlc) {
		case -1:
		p_capainfo.exthlc = INFO_HLC_NONE;
		break;
		default:
		p_capainfo.exthlc = hlc_exthlc + 0x80;
		break;
	}

	/* set bchannel mode */
	if (p_capainfo.bearer_capa==INFO_BC_DATAUNRESTRICTED
	 || p_capainfo.bearer_capa==INFO_BC_DATARESTRICTED
	 || p_capainfo.bearer_capa==INFO_BC_VIDEO)
		p_capainfo.source_mode = B_MODE_HDLC;
	else
		p_capainfo.source_mode = B_MODE_TRANSPARENT;
	p_m_b_mode = p_capainfo.source_mode;

	/* hunt channel */
	ret = channel = hunt_bchannel(channel, exclusive);
	if (ret < 0)
		goto no_channel;

	/* open channel */
	ret = seize_bchannel(channel, 1);
	if (ret < 0) {
		no_channel:
		/*
		 * NOTE: we send MT_RELEASE_COMPLETE to "REJECT" the channel
		 * in response to the setup
		 */
		l3m = create_l3msg();
		l1l2l3_trace_header(p_m_mISDNport, this, L3_RELEASE_COMPLETE_REQ, DIRECTION_OUT);
		enc_ie_cause(l3m, (p_m_mISDNport->locally)?LOCATION_PRIVATE_LOCAL:LOCATION_PRIVATE_REMOTE, -ret);
		end_trace();
		p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_RELEASE_COMPLETE, p_m_d_l3id, l3m);
		new_state(PORT_STATE_RELEASE);
		trigger_work(&p_m_d_delete);
		return;
	}
	bchannel_event(p_m_mISDNport, p_m_b_index, B_EVENT_USE);

	/* create endpoint */
	if (p_epointlist)
		FATAL("Incoming call but already got an endpoint.\n");
	if (!(epoint = new Endpoint(p_serial, 0)))
		FATAL("No memory for Endpoint instance\n");
	if (!(epoint->ep_app = new DEFAULT_ENDPOINT_APP(epoint, 0))) //incoming
		FATAL("No memory for Endpoint Application instance\n");
	epointlist_new(epoint->ep_serial);

	/* send setup message to endpoit */
	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_SETUP);
	message->param.setup.isdn_port = p_m_portnum;
	message->param.setup.port_type = p_type;
//	message->param.setup.dtmf = !p_m_mISDNport->ifport->nodtmf;
	memcpy(&message->param.setup.dialinginfo, &p_dialinginfo, sizeof(struct dialing_info));
	memcpy(&message->param.setup.callerinfo, &p_callerinfo, sizeof(struct caller_info));
	memcpy(&message->param.setup.redirinfo, &p_redirinfo, sizeof(struct redir_info));
	memcpy(&message->param.setup.capainfo, &p_capainfo, sizeof(struct capa_info));
	memcpy(message->param.setup.useruser.data, &useruser, useruser_len);
	message->param.setup.useruser.len = useruser_len;
	message->param.setup.useruser.protocol = useruser_protocol;
	message_put(message);

	new_state(PORT_STATE_IN_SETUP);
}

/* CC_INFORMATION INDICATION */
void Pdss1::information_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m)
{
	int type, plan;
	unsigned char keypad[33] = "", display[128] = "";
	struct lcr_msg *message;

	l1l2l3_trace_header(p_m_mISDNport, this, L3_INFORMATION_IND, DIRECTION_IN);
	dec_ie_called_pn(l3m, &type, &plan, (unsigned char *)p_dialinginfo.id, sizeof(p_dialinginfo.id));
	dec_ie_keypad(l3m, (unsigned char *)keypad, sizeof(keypad));
	dec_ie_display(l3m, (unsigned char *)display, sizeof(display));
	dec_ie_complete(l3m, &p_dialinginfo.sending_complete);
	end_trace();

	SCAT(p_dialinginfo.id, (char *)keypad);
	switch (type) {
		case 0x1:
		p_dialinginfo.ntype = INFO_NTYPE_INTERNATIONAL;
		break;
		case 0x2:
		p_dialinginfo.ntype = INFO_NTYPE_NATIONAL;
		break;
		case 0x4:
		p_dialinginfo.ntype = INFO_NTYPE_SUBSCRIBER;
		break;
		default:
		p_dialinginfo.ntype = INFO_NTYPE_UNKNOWN;
		break;
	}
	SCPY(p_dialinginfo.display, (char *)display);
	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_INFORMATION);
	memcpy(&message->param.information, &p_dialinginfo, sizeof(struct dialing_info));
	message_put(message);
	/* reset overlap timeout */
	new_state(p_state);
}

/* CC_SETUP_ACCNOWLEDGE INDICATION */
void Pdss1::setup_acknowledge_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m)
{
	int exclusive, channel;
	int coding, location, progress;
	int ret;
	struct lcr_msg *message;
	int max = p_m_mISDNport->ifport->dialmax;
	char *number;
	l3_msg *nl3m;

	l1l2l3_trace_header(p_m_mISDNport, this, L3_SETUP_ACKNOWLEDGE_IND, DIRECTION_IN);
	dec_ie_channel_id(l3m, &exclusive, &channel);
	dec_ie_progress(l3m, &coding, &location, &progress);
	end_trace();

	if (progress >= 0) {
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_PROGRESS);
		message->param.progressinfo.progress = progress;
		message->param.progressinfo.location = location;
		message_put(message);
	}

	/* process channel */
	ret = received_first_reply_to_setup(cmd, channel, exclusive);
	if (ret < 0) {
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = -ret;
		message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		message_put(message);
		new_state(PORT_STATE_RELEASE);
		trigger_work(&p_m_d_delete);
		return;
	}

	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_OVERLAP);
	message_put(message);

	new_state(PORT_STATE_OUT_OVERLAP);

	number = p_m_d_queue;
	while (number[0]) { /* as long we have something to dial */
		if (max > (int)strlen(number) || max == 0)
			max = (int)strlen(number);
      
		nl3m = create_l3msg();
		l1l2l3_trace_header(p_m_mISDNport, this, L3_INFORMATION_REQ, DIRECTION_OUT);
		enc_ie_called_pn(nl3m, 0, 1, (unsigned char *)number, max);
		end_trace();
		p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_INFORMATION, p_m_d_l3id, nl3m);
		number += max;
	}
	p_m_d_queue[0] = '\0';
}

/* CC_PROCEEDING INDICATION */
void Pdss1::proceeding_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m)
{
	int exclusive, channel;
	int coding, location, progress;
	int ret;
	struct lcr_msg *message;
	int notify = -1, type, plan, present;
	char redir[32];

	l1l2l3_trace_header(p_m_mISDNport, this, L3_PROCEEDING_IND, DIRECTION_IN);
	dec_ie_channel_id(l3m, &exclusive, &channel);
	dec_ie_progress(l3m, &coding, &location, &progress);
	dec_ie_notify(l3m, &notify);
	dec_ie_redir_dn(l3m, &type, &plan, &present, (unsigned char *)redir, sizeof(redir));
	end_trace();

	if (progress >= 0) {
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_PROGRESS);
		message->param.progressinfo.progress = progress;
		message->param.progressinfo.location = location;
		message_put(message);
	}

	ret = received_first_reply_to_setup(cmd, channel, exclusive);
	if (ret < 0) {
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = -ret;
		message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		message_put(message);
		new_state(PORT_STATE_RELEASE);
		trigger_work(&p_m_d_delete);
		return;
	}
	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_PROCEEDING);
	message_put(message);

	new_state(PORT_STATE_OUT_PROCEEDING);
	
	if (notify >= 0)
		notify |= 0x80;
	else
		notify = 0;
	if (type >= 0 || notify) {
		if (!notify && type >= 0)
			notify = 251;
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_NOTIFY);
		message->param.notifyinfo.notify = notify;
		SCPY(message->param.notifyinfo.id, redir);
		/* redirection number */
		switch (present) {
			case 1:
			message->param.notifyinfo.present = INFO_PRESENT_RESTRICTED;
			break;
			case 2:
			message->param.notifyinfo.present = INFO_PRESENT_NOTAVAIL;
			break;
			default:
			message->param.notifyinfo.present = INFO_PRESENT_ALLOWED;
			break;
		}
		switch (type) {
			case -1:
			message->param.notifyinfo.ntype = INFO_NTYPE_NOTPRESENT;
			break;
			case 1:
			message->param.notifyinfo.ntype = INFO_NTYPE_INTERNATIONAL;
			break;
			case 2:
			message->param.notifyinfo.ntype = INFO_NTYPE_NATIONAL;
			break;
			case 4:
			message->param.notifyinfo.ntype = INFO_NTYPE_SUBSCRIBER;
			break;
			default:
			message->param.notifyinfo.ntype = INFO_NTYPE_UNKNOWN;
			break;
		}
		message->param.notifyinfo.isdn_port = p_m_portnum;
		message_put(message);
	}
}

/* CC_ALERTING INDICATION */
void Pdss1::alerting_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m)
{
	int exclusive, channel;
	int coding, location, progress;
	int ret;
	struct lcr_msg *message;
	int notify = -1, type, plan, present;
	char redir[32];

	l1l2l3_trace_header(p_m_mISDNport, this, L3_ALERTING_IND, DIRECTION_IN);
	dec_ie_channel_id(l3m, &exclusive, &channel);
	dec_ie_progress(l3m, &coding, &location, &progress);
	dec_ie_notify(l3m, &notify);
	dec_ie_redir_dn(l3m, &type, &plan, &present, (unsigned char *)redir, sizeof(redir));
	end_trace();

	if (progress >= 0) {
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_PROGRESS);
		message->param.progressinfo.progress = progress;
		message->param.progressinfo.location = location;
		message_put(message);
	}

	/* process channel */
	ret = received_first_reply_to_setup(cmd, channel, exclusive);
	if (ret < 0) {
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = -ret;
		message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		message_put(message);
		new_state(PORT_STATE_RELEASE);
		trigger_work(&p_m_d_delete);
		return;
	}
	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_ALERTING);
	message_put(message);

	new_state(PORT_STATE_OUT_ALERTING);

	if (notify >= 0)
		notify |= 0x80;
	else
		notify = 0;
	if (type >= 0 || notify) {
		if (!notify && type >= 0)
			notify = 251;
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_NOTIFY);
		message->param.notifyinfo.notify = notify;
		SCPY(message->param.notifyinfo.id, redir);
		switch (present) {
			case 1:
			message->param.notifyinfo.present = INFO_PRESENT_RESTRICTED;
			break;
			case 2:
			message->param.notifyinfo.present = INFO_PRESENT_NOTAVAIL;
			break;
			default:
			message->param.notifyinfo.present = INFO_PRESENT_ALLOWED;
			break;
		}
		switch (type) {
			case -1:
			message->param.notifyinfo.ntype = INFO_NTYPE_NOTPRESENT;
			break;
			case 1:
			message->param.notifyinfo.ntype = INFO_NTYPE_INTERNATIONAL;
			break;
			case 2:
			message->param.notifyinfo.ntype = INFO_NTYPE_NATIONAL;
			break;
			case 4:
			message->param.notifyinfo.ntype = INFO_NTYPE_SUBSCRIBER;
			break;
			default:
			message->param.notifyinfo.ntype = INFO_NTYPE_UNKNOWN;
			break;
		}
		message->param.notifyinfo.isdn_port = p_m_portnum;
		message_put(message);
	}
}

/* CC_CONNECT INDICATION */
void Pdss1::connect_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m)
{
	int exclusive, channel;
	int type, plan, present, screen;
	int ret;
	struct lcr_msg *message;
	int bchannel_before;

	l1l2l3_trace_header(p_m_mISDNport, this, L3_CONNECT_IND, DIRECTION_IN);
	dec_ie_channel_id(l3m, &exclusive, &channel);
	dec_ie_connected_pn(l3m, &type, &plan, &present, &screen, (unsigned char *)p_connectinfo.id, sizeof(p_connectinfo.id));
	dec_ie_display(l3m, (unsigned char *)p_connectinfo.display, sizeof(p_connectinfo.display));
	/* te-mode: CONP (connected name identification presentation) */
	dec_facility_centrex(l3m, (unsigned char *)p_connectinfo.name, sizeof(p_connectinfo.name));
	end_trace();

	/* select channel */
	bchannel_before = p_m_b_channel;
	ret = received_first_reply_to_setup(cmd, channel, exclusive);
	if (ret < 0) {
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = -ret;
		message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		message_put(message);
		new_state(PORT_STATE_RELEASE);
		trigger_work(&p_m_d_delete);
		return;
	}

	/* connect information */
	switch (present) {
		case 1:
		p_connectinfo.present = INFO_PRESENT_RESTRICTED;
		break;
		case 2:
		p_connectinfo.present = INFO_PRESENT_NOTAVAIL;
		break;
		default:
		p_connectinfo.present = INFO_PRESENT_ALLOWED;
		break;
	}
	switch (screen) {
		case 0:
		p_connectinfo.screen = INFO_SCREEN_USER;
		break;
		case 1:
		p_connectinfo.screen = INFO_SCREEN_USER_VERIFIED_PASSED;
		break;
		case 2:
		p_connectinfo.screen = INFO_SCREEN_USER_VERIFIED_FAILED;
		break;
		default:
		p_connectinfo.screen = INFO_SCREEN_NETWORK;
		break;
	}
	switch (type) {
		case -1:
		p_connectinfo.ntype = INFO_NTYPE_NOTPRESENT;
		break;
		case 0x1:
		p_connectinfo.ntype = INFO_NTYPE_INTERNATIONAL;
		break;
		case 0x2:
		p_connectinfo.ntype = INFO_NTYPE_NATIONAL;
		break;
		case 0x4:
		p_connectinfo.ntype = INFO_NTYPE_SUBSCRIBER;
		break;
		default:
		p_connectinfo.ntype = INFO_NTYPE_UNKNOWN;
		break;
	}
	p_connectinfo.isdn_port = p_m_portnum;
	SCPY(p_connectinfo.interface, p_m_mISDNport->ifport->interface->name);

	/* only in nt-mode we send connect ack. in te-mode it is done by stack itself or optional */
	if (p_m_d_ntmode) {
		/* send connect acknowledge */
		l3m = create_l3msg();
		l1l2l3_trace_header(p_m_mISDNport, this, L3_CONNECT_RES, DIRECTION_OUT);
		/* if we had no bchannel before, we send it now */
		if (!bchannel_before && p_m_b_channel)
			enc_ie_channel_id(l3m, 1, p_m_b_channel);
		end_trace();
		p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_CONNECT_ACKNOWLEDGE, p_m_d_l3id, l3m);
	}
	
	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_CONNECT);
	memcpy(&message->param.connectinfo, &p_connectinfo, sizeof(struct connect_info));
	message_put(message);

	new_state(PORT_STATE_CONNECT);
}

/* CC_DISCONNECT INDICATION */
void Pdss1::disconnect_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m)
{
	int location, cause;
	int coding, proglocation, progress;
	struct lcr_msg *message;
	unsigned char display[128] = "";

	l1l2l3_trace_header(p_m_mISDNport, this, L3_DISCONNECT_IND, DIRECTION_IN);
	dec_ie_progress(l3m, &coding, &proglocation, &progress);
	dec_ie_cause(l3m, &location, &cause);
	dec_ie_display(l3m, (unsigned char *)display, sizeof(display));
	end_trace();

	if (cause < 0) {
		cause = 16;
		location = LOCATION_PRIVATE_LOCAL;
	}

	if (progress >= 0) {
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_PROGRESS);
		message->param.progressinfo.progress = progress;
		message->param.progressinfo.location = proglocation;
		message_put(message);
	}

	/* release if remote sends us no tones */
	if (!p_m_mISDNport->earlyb) {
		l3_msg *l3m;

		l3m = create_l3msg();
		l1l2l3_trace_header(p_m_mISDNport, this, L3_RELEASE_REQ, DIRECTION_OUT);
		enc_ie_cause(l3m, location, cause); /* normal */
		add_trace("reason", NULL, "no remote patterns");
		end_trace();
		p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_RELEASE, p_m_d_l3id, l3m);

		/* sending release to endpoint */
		if (location == LOCATION_PRIVATE_LOCAL)
			location = LOCATION_PRIVATE_REMOTE;
		while(p_epointlist) {
			message = message_create(p_serial, p_epointlist->epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
			message->param.disconnectinfo.cause = cause;
			message->param.disconnectinfo.location = location;
			SCAT(message->param.disconnectinfo.display, (char *)display);
			message_put(message);
			/* remove epoint */
			free_epointlist(p_epointlist);
		}
		new_state(PORT_STATE_RELEASE);
		trigger_work(&p_m_d_delete);
		return;
	}

	/* sending disconnect to active endpoint and release to inactive endpoints */
	if (location == LOCATION_PRIVATE_LOCAL)
		location = LOCATION_PRIVATE_REMOTE;
	if (ACTIVE_EPOINT(p_epointlist)) {
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_DISCONNECT);
		message->param.disconnectinfo.location = location;
		message->param.disconnectinfo.cause = cause;
		SCAT(message->param.disconnectinfo.display, (char *)display);
		message_put(message);
	}
	while(INACTIVE_EPOINT(p_epointlist)) {
		message = message_create(p_serial, INACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.location = location;
		message->param.disconnectinfo.cause = cause;
		SCAT(message->param.disconnectinfo.display, (char *)display);
		message_put(message);
		/* remove epoint */
		free_epointid(INACTIVE_EPOINT(p_epointlist));
	}
	new_state(PORT_STATE_IN_DISCONNECT);
}

/* CC_DISCONNECT INDICATION */
void Pdss1::disconnect_ind_i(unsigned int cmd, unsigned int pid, struct l3_msg *l3m)
{
	int location, cause;

	/* cause */
	l1l2l3_trace_header(p_m_mISDNport, this, L3_DISCONNECT_IND, DIRECTION_IN);
	if (p_m_d_collect_cause > 0) {
		add_trace("old-cause", "location", "%d", p_m_d_collect_location);
		add_trace("old-cause", "value", "%d", p_m_d_collect_cause);
	}
	dec_ie_cause(l3m, &location, &cause);
	if (location == LOCATION_PRIVATE_LOCAL)
		location = LOCATION_PRIVATE_REMOTE;

	/* collect cause */
	collect_cause(&p_m_d_collect_cause, &p_m_d_collect_location, cause, location);
	add_trace("new-cause", "location", "%d", p_m_d_collect_location);
	add_trace("new-cause", "value", "%d", p_m_d_collect_cause);
	end_trace();

}

/* CC_RELEASE INDICATION */
void Pdss1::release_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m)
{
	int location, cause;
	struct lcr_msg *message;
	unsigned char display[128] = "";

	l1l2l3_trace_header(p_m_mISDNport, this, L3_RELEASE_IND, DIRECTION_IN);
	dec_ie_cause(l3m, &location, &cause);
	dec_ie_display(l3m, (unsigned char *)display, sizeof(display));
	end_trace();

	if (cause < 0) {
		cause = 16;
		location = LOCATION_PRIVATE_LOCAL;
	}

	/* sending release to endpoint */
	if (location == LOCATION_PRIVATE_LOCAL)
		location = LOCATION_PRIVATE_REMOTE;
	while(p_epointlist) {
		message = message_create(p_serial, p_epointlist->epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = cause;
		message->param.disconnectinfo.location = location;
		SCAT(message->param.disconnectinfo.display, (char *)display);
		message_put(message);
		/* remove epoint */
		free_epointlist(p_epointlist);
	}

	new_state(PORT_STATE_RELEASE);
	trigger_work(&p_m_d_delete);
}

/* CC_RESTART INDICATION */
void Pdss1::restart_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m)
{
	l1l2l3_trace_header(p_m_mISDNport, this, L3_RESTART_IND, DIRECTION_IN);
	end_trace();

	// L3 process is not toucht. (not even by network stack)
}

/* CC_RELEASE_COMPLETE INDICATION (a reject) */
void Pdss1::release_complete_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m)
{
	int location, cause;
	struct lcr_msg *message;
	
	l1l2l3_trace_header(p_m_mISDNport, this, L3_RELEASE_COMPLETE_IND, DIRECTION_IN);
	/* in case layer 2 is down during setup, we send cause 27 loc 5 */
	if (p_state == PORT_STATE_OUT_SETUP && p_m_mISDNport->l1link == 0) {
		cause = 27;
		location = 5;
	} else {
		dec_ie_cause(l3m, &location, &cause);
		if (p_m_mISDNport->l1link < 0)
			add_trace("layer 1", NULL, "unknown");
		else
			add_trace("layer 1", NULL, (p_m_mISDNport->l1link)?"up":"down");
	}
	end_trace();
	if (location == LOCATION_PRIVATE_LOCAL)
		location = LOCATION_PRIVATE_REMOTE;

	if (cause < 0) {
		cause = 16;
		location = LOCATION_PRIVATE_LOCAL;
	}

	/* sending release to endpoint */
	while(p_epointlist) {
		message = message_create(p_serial, p_epointlist->epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = cause;
		message->param.disconnectinfo.location = location;
		message_put(message);
		/* remove epoint */
		free_epointlist(p_epointlist);
	}

	new_state(PORT_STATE_RELEASE);
	trigger_work(&p_m_d_delete);
}

/* T312 timeout  */
void Pdss1::t312_timeout_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m)
{
	// not required, release is performed with MT_FREE
}

/* CC_NOTIFY INDICATION */
void Pdss1::notify_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m)
{
	struct lcr_msg *message;
	int notify, type, plan, present;
	unsigned char notifyid[sizeof(message->param.notifyinfo.id)];
	unsigned char display[128] = "";

	l1l2l3_trace_header(p_m_mISDNport, this, L3_NOTIFY_IND, DIRECTION_IN);
	dec_ie_notify(l3m, &notify);
	dec_ie_redir_dn(l3m, &type, &plan, &present, notifyid, sizeof(notifyid));
	dec_ie_display(l3m, (unsigned char *)display, sizeof(display));
	end_trace();

	if (!ACTIVE_EPOINT(p_epointlist))
		return;
	/* notification indicator */
	if (notify < 0)
		return;
	notify |= 0x80;
	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_NOTIFY);
	message->param.notifyinfo.notify = notify;
	SCPY(message->param.notifyinfo.id, (char *)notifyid);
	/* redirection number */
	switch (present) {
		case 1:
		message->param.notifyinfo.present = INFO_PRESENT_RESTRICTED;
		break;
		case 2:
		message->param.notifyinfo.present = INFO_PRESENT_NOTAVAIL;
		break;
		default:
		message->param.notifyinfo.present = INFO_PRESENT_ALLOWED;
		break;
	}
	switch (type) {
		case -1:
		message->param.notifyinfo.ntype = INFO_NTYPE_NOTPRESENT;
		break;
		case 1:
		message->param.notifyinfo.ntype = INFO_NTYPE_INTERNATIONAL;
		break;
		case 2:
		message->param.notifyinfo.ntype = INFO_NTYPE_NATIONAL;
		break;
		case 4:
		message->param.notifyinfo.ntype = INFO_NTYPE_SUBSCRIBER;
		break;
		default:
		message->param.notifyinfo.ntype = INFO_NTYPE_UNKNOWN;
		break;
	}
	SCAT(message->param.notifyinfo.display, (char *)display);
	message->param.notifyinfo.isdn_port = p_m_portnum;
	message_put(message);
}


/* CC_HOLD INDICATION */
	struct lcr_msg *message;
void Pdss1::hold_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m)
{
//	class Endpoint *epoint;

	l1l2l3_trace_header(p_m_mISDNport, this, L3_HOLD_IND, DIRECTION_IN);
	end_trace();

	if (!ACTIVE_EPOINT(p_epointlist) || p_m_hold) {
		l3m = create_l3msg();
		l1l2l3_trace_header(p_m_mISDNport, this, L3_HOLD_REJECT_REQ, DIRECTION_OUT);
		enc_ie_cause(l3m, (p_m_mISDNport->locally)?LOCATION_PRIVATE_LOCAL:LOCATION_PRIVATE_REMOTE, p_m_hold?101:31); /* normal unspecified / incompatible state */
		add_trace("reason", NULL, "no endpoint");
		end_trace();
		p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_HOLD_REJECT, p_m_d_l3id, l3m);

		return;
	}

	/* notify the hold of call */
	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_NOTIFY);
	message->param.notifyinfo.notify = INFO_NOTIFY_REMOTE_HOLD;
	message->param.notifyinfo.local = 1; /* call is held by supplementary service */
	message_put(message);

	/* deactivate bchannel */
	chan_trace_header(p_m_mISDNport, this, "CHANNEL RELEASE (hold)", DIRECTION_NONE);
	add_trace("disconnect", "channel", "%d", p_m_b_channel);
	end_trace();
	drop_bchannel();

	/* set hold state */
	p_m_hold = 1;
#if 0
	epoint = find_epoint_id(ACTIVE_EPOINT(p_epointlist));
	if (epoint && p_m_d_ntmode) {
		if (p_settings.tout_hold)
			schedule_timer(&p_m_timeout, p_settings.tout_hold, 0);
	}
#endif

	/* acknowledge hold */
	l3m = create_l3msg();
	l1l2l3_trace_header(p_m_mISDNport, this, L3_HOLD_ACKNOWLEDGE_REQ, DIRECTION_OUT);
	end_trace();
	p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_HOLD_ACKNOWLEDGE, p_m_d_l3id, l3m);
}


/* CC_RETRIEVE INDICATION */
void Pdss1::retrieve_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m)
{
	struct lcr_msg *message;
	int channel, exclusive, cause;
	int ret;

	l1l2l3_trace_header(p_m_mISDNport, this, L3_RETRIEVE_IND, DIRECTION_IN);
	dec_ie_channel_id(l3m, &exclusive, &channel);
	end_trace();

	if (!p_m_hold) {
		cause = 101; /* incompatible state */
		reject:

		l3m = create_l3msg();
		l1l2l3_trace_header(p_m_mISDNport, this, L3_RETRIEVE_REJECT_REQ, DIRECTION_OUT);
		enc_ie_cause(l3m, (p_m_mISDNport->locally)?LOCATION_PRIVATE_LOCAL:LOCATION_PRIVATE_REMOTE, cause);
		end_trace();
		p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_RETRIEVE_REJECT, p_m_d_l3id, l3m);

		return;
	}

	/* notify the retrieve of call */
	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_NOTIFY);
	message->param.notifyinfo.notify = INFO_NOTIFY_REMOTE_RETRIEVAL;
	message->param.notifyinfo.local = 1; /* call is retrieved by supplementary service */
	message_put(message);

	/* hunt channel */
	ret = channel = hunt_bchannel(channel, exclusive);
	if (ret < 0)
		goto no_channel;

	/* open channel */
	ret = seize_bchannel(channel, 1);
	if (ret < 0) {
		no_channel:
		cause = -ret;
		goto reject;
	}
	bchannel_event(p_m_mISDNport, p_m_b_index, B_EVENT_USE);

	/* set hold state */
	p_m_hold = 0;
	unsched_timer(&p_m_timeout);

	/* acknowledge retrieve */
	l3m = create_l3msg();
	l1l2l3_trace_header(p_m_mISDNport, this, L3_RETRIEVE_ACKNOWLEDGE_REQ, DIRECTION_OUT);
	enc_ie_channel_id(l3m, 1, p_m_b_channel);
	end_trace();
	p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_RETRIEVE_ACKNOWLEDGE, p_m_d_l3id, l3m);
}

/* CC_SUSPEND INDICATION */
void Pdss1::suspend_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m)
{
	struct lcr_msg *message;
	class Endpoint *epoint;
	unsigned char callid[8];
	int len;
	int ret = -31; /* normal, unspecified */

	l1l2l3_trace_header(p_m_mISDNport, this, L3_SUSPEND_IND, DIRECTION_IN);
	dec_ie_call_id(l3m, callid, &len);
	end_trace();

	if (!ACTIVE_EPOINT(p_epointlist)) {
		reject:
		l3m = create_l3msg();
		l1l2l3_trace_header(p_m_mISDNport, this, L3_SUSPEND_REJECT_REQ, DIRECTION_OUT);
		enc_ie_cause(l3m, (p_m_mISDNport->locally)?LOCATION_PRIVATE_LOCAL:LOCATION_PRIVATE_REMOTE, -ret);
		end_trace();
		p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_SUSPEND_REJECT, p_m_d_l3id, l3m);
		return;
	}

	/* call id */
	if (len<0) len = 0;

	/* check if call id is in use */
	epoint = epoint_first;
	while(epoint) {
		if (epoint->ep_park) {
			if (epoint->ep_park_len == len)
			if (!memcmp(epoint->ep_park_callid, callid, len)) {
				ret = -84; /* call id in use */
				goto reject;
			}
		}
		epoint = epoint->next;
	}

	/* notify the hold of call */
	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_NOTIFY);
	message->param.notifyinfo.notify = INFO_NOTIFY_USER_SUSPENDED;
	message->param.notifyinfo.local = 1; /* call is held by supplementary service */
	message_put(message);

	/* deactivate bchannel */
	chan_trace_header(p_m_mISDNport, this, "CHANNEL RELEASE (suspend)", DIRECTION_NONE);
	add_trace("disconnect", "channel", "%d", p_m_b_channel);
	end_trace();
	drop_bchannel();

	/* sending suspend to endpoint */
	while (p_epointlist) {
		message = message_create(p_serial, p_epointlist->epoint_id, PORT_TO_EPOINT, MESSAGE_SUSPEND);
		memcpy(message->param.parkinfo.callid, callid, sizeof(message->param.parkinfo.callid));
		message->param.parkinfo.len = len;
		message_put(message);
		/* remove epoint */
		free_epointlist(p_epointlist);
	}

	/* sending SUSPEND_ACKNOWLEDGE */
	l3m = create_l3msg();
	l1l2l3_trace_header(p_m_mISDNport, this, L3_SUSPEND_ACKNOWLEDGE_REQ, DIRECTION_OUT);
	end_trace();
	p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_SUSPEND_ACKNOWLEDGE, p_m_d_l3id, l3m);

	new_state(PORT_STATE_RELEASE);
	trigger_work(&p_m_d_delete);
}

/* CC_RESUME INDICATION */
void Pdss1::resume_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m)
{
	unsigned char callid[8];
	int len;
	int channel, exclusive;
	class Endpoint *epoint;
	struct lcr_msg *message;
	int ret;

	/* process given callref */
	l1l2l3_trace_header(p_m_mISDNport, this, L3_NEW_L3ID_IND, DIRECTION_IN);
	add_trace("callref", "new", "0x%x", pid);
	if (p_m_d_l3id) {
		/* release is case the ID is already in use */
		add_trace("error", NULL, "callref already in use");
		end_trace();
		l3m = create_l3msg();
		l1l2l3_trace_header(p_m_mISDNport, this, L3_RESUME_REJECT_REQ, DIRECTION_OUT);
		enc_ie_cause(l3m, (p_m_mISDNport->locally)?LOCATION_PRIVATE_LOCAL:LOCATION_PRIVATE_REMOTE, 47);
		add_trace("reason", NULL, "callref already in use");
		end_trace();
		p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_RESUME_REJECT, pid, l3m);
		new_state(PORT_STATE_RELEASE);
		trigger_work(&p_m_d_delete);
		return;
	}
	p_m_d_l3id = pid;
	p_m_d_ces = pid >> 16;
	end_trace();

	l1l2l3_trace_header(p_m_mISDNport, this, L3_RESUME_IND, DIRECTION_IN);
	dec_ie_call_id(l3m, callid, &len);
	end_trace();

	/* if blocked, release call */
	if (p_m_mISDNport->ifport->block) {
		ret = -27;
		goto reject;
	}

	/* call id */
	if (len<0) len = 0;

	/* channel_id (no channel is possible in message) */
	exclusive = 0;
	channel = -1; /* any channel */

	/* hunt channel */
	ret = channel = hunt_bchannel(channel, exclusive);
	if (ret < 0)
		goto no_channel;

// mode (if hdlc parked) to be done. never mind, this is almost never requested
	/* open channel */
	ret = seize_bchannel(channel, 1);
	if (ret < 0) {
		no_channel:
		reject:
		l3m = create_l3msg();
		l1l2l3_trace_header(p_m_mISDNport, this, L3_RESUME_REJECT_REQ, DIRECTION_OUT);
		enc_ie_cause(l3m, (p_m_mISDNport->locally)?LOCATION_PRIVATE_LOCAL:LOCATION_PRIVATE_REMOTE, -ret);
		if (ret == -27)
			add_trace("reason", NULL, "port blocked");
		end_trace();
		p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_RESUME_REJECT, p_m_d_l3id, l3m);
		new_state(PORT_STATE_RELEASE);
		trigger_work(&p_m_d_delete);
		return;
	}
	bchannel_event(p_m_mISDNport, p_m_b_index, B_EVENT_USE);

	/* create endpoint */
	if (p_epointlist)
		FATAL("Incoming resume but already got an endpoint.\n");
	ret = -85; /* no call suspended */
	epoint = epoint_first;
	while(epoint) {
		if (epoint->ep_park) {
			ret = -83; /* suspended call exists, but this not */
			if (epoint->ep_park_len == len)
			if (!memcmp(epoint->ep_park_callid, callid, len))
				break;
		}
		epoint = epoint->next;
	}
	if (!epoint)
		goto reject;

	epointlist_new(epoint->ep_serial);
	if (!(epoint->portlist_new(p_serial, p_type, p_m_mISDNport->earlyb)))
		FATAL("No memory for portlist\n");
	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_RESUME);
	message_put(message);

	/* notify the resume of call */
	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_NOTIFY);
	message->param.notifyinfo.notify = INFO_NOTIFY_USER_RESUMED;
	message->param.notifyinfo.local = 1; /* call is retrieved by supplementary service */
	message_put(message);

	/* sending RESUME_ACKNOWLEDGE */
	l3m = create_l3msg();
	l1l2l3_trace_header(p_m_mISDNport, this, L3_RESUME_ACKNOWLEDGE_REQ, DIRECTION_OUT);
	enc_ie_channel_id(l3m, 1, p_m_b_channel);
	end_trace();
	p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_RESUME_ACKNOWLEDGE, p_m_d_l3id, l3m);

	new_state(PORT_STATE_CONNECT);
}


/* CC_FACILITY INDICATION */
void Pdss1::facility_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m)
{
	unsigned char facil[256];
	int facil_len;
	struct lcr_msg *message;

	l1l2l3_trace_header(p_m_mISDNport, this, L3_FACILITY_IND, DIRECTION_IN);
	dec_ie_facility(l3m, facil, &facil_len);
	end_trace();

	/* facility */
	if (facil_len<=0)
		return;

	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_FACILITY);
	message->param.facilityinfo.len = facil_len;
	memcpy(message->param.facilityinfo.data, facil, facil_len);
	message_put(message);
}


/* CC_PROGRESS INDICATION */
void Pdss1::progress_ind(unsigned int cmd, unsigned int pid, struct l3_msg *l3m)
{
	int coding, location, progress;

	l1l2l3_trace_header(p_m_mISDNport, this, L3_PROGRESS_IND, DIRECTION_IN);
	dec_ie_progress(l3m, &coding, &location, &progress);
	end_trace();

	if (progress >= 0) {
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_PROGRESS);
		message->param.progressinfo.progress = progress;
		message->param.progressinfo.location = location;
		message_put(message);
	}
}


/*
 * handler for isdn connections
 * incoming information are parsed and sent via message to the endpoint
 */
void Pdss1::message_isdn(unsigned int cmd, unsigned int pid, struct l3_msg *l3m)
{
	int timer = 0;

	switch (cmd) {
		case MT_TIMEOUT:
		if (!l3m->cause) {
			PERROR("Pdss1(%s) timeout without cause.\n", p_name);
			break;
		}
		if (l3m->cause[0] != 5) {
			PERROR("Pdss1(%s) expecting timeout with timer diagnostic. (got len=%d)\n", p_name, l3m->cause[0]);
			break;
		}
		timer = (l3m->cause[3]-'0')*100;
		timer += (l3m->cause[4]-'0')*10;
		timer += (l3m->cause[5]-'0');
		l1l2l3_trace_header(p_m_mISDNport, this, L3_TIMEOUT_IND, DIRECTION_IN);
		add_trace("timer", NULL, "%d", timer);
		end_trace();
		if (timer == 312)
			t312_timeout_ind(cmd, pid, l3m);
		break;

		case MT_SETUP:
		if (p_state != PORT_STATE_IDLE)
			break;
		setup_ind(cmd, pid, l3m);
		break;

		case MT_INFORMATION:
		information_ind(cmd, pid, l3m);
		break;

		case MT_SETUP_ACKNOWLEDGE:
		if (p_state != PORT_STATE_OUT_SETUP) {
			PDEBUG(DEBUG_ISDN, "Pdss1(%s) received setup_acknowledge, but we are not in outgoing setup state, IGNORING.\n", p_name);
			break;
		}
		setup_acknowledge_ind(cmd, pid, l3m);
		break;

		case MT_CALL_PROCEEDING:
		if (p_state != PORT_STATE_OUT_SETUP
		 && p_state != PORT_STATE_OUT_OVERLAP) {
			PDEBUG(DEBUG_ISDN, "Pdss1(%s) received proceeding, but we are not in outgoing setup OR overlap state, IGNORING.\n", p_name);
			break;
		}
		proceeding_ind(cmd, pid, l3m);
		break;

		case MT_ALERTING:
		if (p_state != PORT_STATE_OUT_SETUP
		 && p_state != PORT_STATE_OUT_OVERLAP
		 && p_state != PORT_STATE_OUT_PROCEEDING) {
			PDEBUG(DEBUG_ISDN, "Pdss1(%s) received alerting, but we are not in outgoing setup OR overlap OR proceeding state, IGNORING.\n", p_name);
			break;
		}
		alerting_ind(cmd, pid, l3m);
		break;

		case MT_CONNECT:
		if (p_state != PORT_STATE_OUT_SETUP
		 && p_state != PORT_STATE_OUT_OVERLAP
		 && p_state != PORT_STATE_OUT_PROCEEDING
		 && p_state != PORT_STATE_OUT_ALERTING) {
			PDEBUG(DEBUG_ISDN, "Pdss1(%s) received alerting, but we are not in outgoing setup OR overlap OR proceeding OR ALERTING state, IGNORING.\n", p_name);
			break;
		}
		connect_ind(cmd, pid, l3m);
		if (p_m_d_notify_pending) {
			/* send pending notify message during connect */
			message_notify(ACTIVE_EPOINT(p_epointlist), p_m_d_notify_pending->type, &p_m_d_notify_pending->param);
			message_free(p_m_d_notify_pending);
			p_m_d_notify_pending = NULL;
		}
		break;

		case MT_CONNECT_ACKNOWLEDGE:
		if (p_state == PORT_STATE_CONNECT_WAITING)
			new_state(PORT_STATE_CONNECT);
		if (p_m_d_notify_pending) {
			/* send pending notify message during connect-ack */
			message_notify(ACTIVE_EPOINT(p_epointlist), p_m_d_notify_pending->type, &p_m_d_notify_pending->param);
			message_free(p_m_d_notify_pending);
			p_m_d_notify_pending = NULL;
		}
		break;

		case MT_DISCONNECT:
		disconnect_ind(cmd, pid, l3m);
		break;

		case MT_RELEASE:
		release_ind(cmd, pid, l3m);
		break;

		case MT_RELEASE_COMPLETE:
		release_complete_ind(cmd, pid, l3m);
		break;

		case MT_RESTART:
		restart_ind(cmd, pid, l3m);
		break;

		case MT_NOTIFY:
		notify_ind(cmd, pid, l3m);
		break;

		case MT_HOLD:
		hold_ind(cmd, pid, l3m);
		break;

		case MT_RETRIEVE:
		retrieve_ind(cmd, pid, l3m);
		break;

		case MT_SUSPEND:
		suspend_ind(cmd, pid, l3m);
		break;

		case MT_RESUME:
		resume_ind(cmd, pid, l3m);
		break;

		case MT_FACILITY:
		facility_ind(cmd, pid, l3m);
		break;

		case MT_PROGRESS:
		progress_ind(cmd, pid, l3m);
		break;

		case MT_FREE:
		l1l2l3_trace_header(p_m_mISDNport, this, L3_RELEASE_L3ID_IND, DIRECTION_IN);
		add_trace("callref", NULL, "0x%x", p_m_d_l3id);
		end_trace();
		p_m_d_l3id = 0;
		trigger_work(&p_m_d_delete);
		p_m_d_ces = -1;
		/* sending release to endpoint in case we still have an endpoint
		 * this is because we don't get any response if a release_complete is received (or a release in release state)
		 */
		while(p_epointlist) { // only if not already released
			struct lcr_msg *message;
			message = message_create(p_serial, p_epointlist->epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
			if (p_m_d_collect_cause) {
				message->param.disconnectinfo.cause = p_m_d_collect_cause;
				message->param.disconnectinfo.location = p_m_d_collect_location;
			} else {
				message->param.disconnectinfo.cause = CAUSE_NOUSER;
				message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
			}
			message_put(message);
			/* remove epoint */
			free_epointlist(p_epointlist);

			new_state(PORT_STATE_RELEASE);
		}
		break;

		default:
		l1l2l3_trace_header(p_m_mISDNport, this, L3_UNKNOWN_IND, DIRECTION_IN);
		add_trace("unhandled", "cmd", "0x%x", cmd);
		end_trace();
	}
}

void Pdss1::new_state(int state)
{
//	class Endpoint *epoint;

	/* set timeout */
	if (state == PORT_STATE_IN_OVERLAP) {
		if (p_m_mISDNport->ifport->tout_dialing)
			schedule_timer(&p_m_timeout, p_m_mISDNport->ifport->tout_dialing, 0);
	}
	if (state != p_state) {
		unsched_timer(&p_m_timeout);
		if (state == PORT_STATE_IN_SETUP
		 || state == PORT_STATE_OUT_SETUP
		 || state == PORT_STATE_IN_OVERLAP
		 || state == PORT_STATE_OUT_OVERLAP) {
		 	if (p_m_mISDNport->ifport->tout_setup)
				schedule_timer(&p_m_timeout, p_m_mISDNport->ifport->tout_setup, 0);
		}
		if (state == PORT_STATE_IN_PROCEEDING
		 || state == PORT_STATE_OUT_PROCEEDING) {
			if (p_m_mISDNport->ifport->tout_proceeding)
				schedule_timer(&p_m_timeout, p_m_mISDNport->ifport->tout_proceeding, 0);
		}
		if (state == PORT_STATE_IN_ALERTING
		 || state == PORT_STATE_OUT_ALERTING) {
			if (p_m_mISDNport->ifport->tout_alerting)
				schedule_timer(&p_m_timeout, p_m_mISDNport->ifport->tout_alerting, 0);
		}
#if 0
		if (state == PORT_STATE_CONNECT
		 || state == PORT_STATE_CONNECT_WAITING) {
			if (p_m_mISDNport->ifport->tout_connect)
				schedule_timer(&p_m_timeout, p_m_mISDNport->ifport->tout_connect, 0);
		}
#endif
		if (state == PORT_STATE_IN_DISCONNECT
		 || state == PORT_STATE_OUT_DISCONNECT) {
			if (p_m_mISDNport->ifport->tout_disconnect)
				schedule_timer(&p_m_timeout, p_m_mISDNport->ifport->tout_disconnect, 0);
		}
	}
	
	Port::new_state(state);
}


/* deletes only if l3id is release, otherwhise it will be triggered then */
static int delete_event(struct lcr_work *work, void *instance, int index)
{
	class Pdss1 *isdnport = (class Pdss1 *)instance;

	if (!isdnport->p_m_d_l3id)
		delete isdnport;

	return 0;
}


/*
 * handles all messages from endpoint
 */
/* MESSAGE_INFORMATION */
void Pdss1::message_information(unsigned int epoint_id, int message_id, union parameter *param)
{
	l3_msg *l3m;
	char *display = param->information.display;
	char *number = param->information.id;
	int max = p_m_mISDNport->ifport->dialmax;

	while (number[0]) { /* as long we have something to dial */
		if (max > (int)strlen(number) || max == 0)
			max = (int)strlen(number);
      
		l3m = create_l3msg();
		l1l2l3_trace_header(p_m_mISDNport, this, L3_INFORMATION_REQ, DIRECTION_OUT);
		enc_ie_called_pn(l3m, 0, 1, (unsigned char *)number, max);
 	 	if ((p_m_d_ntmode || p_m_d_tespecial) && display[0]) {
			enc_ie_display(l3m, (unsigned char *)display);
			display = (char *)"";
		}
		end_trace();
		p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_INFORMATION, p_m_d_l3id, l3m);
		number += max;
	}
	new_state(p_state);
}


/* MESSAGE_SETUP */
void Pdss1::message_setup(unsigned int epoint_id, int message_id, union parameter *param)
{
	l3_msg *l3m;
	int ret;
	int plan, type, screen, present, reason;
	int plan2, type2, screen2, present2;
	int capability, mode, rate, coding, user, presentation, interpretation, hlc, exthlc;
	int channel, exclusive;
	struct epoint_list *epointlist;
	int max = p_m_mISDNport->ifport->dialmax;

	/* release if port is blocked */
	if (p_m_mISDNport->ifport->block) {
		struct lcr_msg *message;

		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = 27; // temp. unavail.
		message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		message_put(message);
		new_state(PORT_STATE_RELEASE);
		trigger_work(&p_m_d_delete);
		return;
	}

	/* copy setup infos to port */
	memcpy(&p_callerinfo, &param->setup.callerinfo, sizeof(p_callerinfo));
	memcpy(&p_dialinginfo, &param->setup.dialinginfo, sizeof(p_dialinginfo));
	memcpy(&p_capainfo, &param->setup.capainfo, sizeof(p_capainfo));
	memcpy(&p_redirinfo, &param->setup.redirinfo, sizeof(p_redirinfo));
	/* screen outgoing caller id */
	do_screen(1, p_callerinfo.id, sizeof(p_callerinfo.id), &p_callerinfo.ntype, &p_callerinfo.present, p_m_mISDNport->ifport->interface);
	do_screen(1, p_callerinfo.id2, sizeof(p_callerinfo.id2), &p_callerinfo.ntype2, &p_callerinfo.present2, p_m_mISDNport->ifport->interface);

	/* only display at connect state: this case happens if endpoint is in connected mode */
	if (p_state==PORT_STATE_CONNECT) {
		if (p_type!=PORT_TYPE_DSS1_NT_OUT
		 && p_type!=PORT_TYPE_DSS1_NT_IN)
			return;
		if (p_callerinfo.display[0]) {
			/* sending information */
			l3m = create_l3msg();
			l1l2l3_trace_header(p_m_mISDNport, this, L3_INFORMATION_REQ, DIRECTION_OUT);
	 	 	if (p_m_d_ntmode || p_m_d_tespecial)
				enc_ie_display(l3m, (unsigned char *)p_callerinfo.display);
			end_trace();
			p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_INFORMATION, p_m_d_l3id, l3m);
			return;
		}
	}

	/* attach only if not already */
	epointlist = p_epointlist;
	while(epointlist) {
		if (epointlist->epoint_id == epoint_id)
			break;
		epointlist = epointlist->next;
	}
	if (!epointlist)
		epointlist_new(epoint_id);

	/* get channel */
	exclusive = 0;
	if (p_m_b_channel) {
		channel = p_m_b_channel;
		exclusive = p_m_b_exclusive;
	} else
		channel = CHANNEL_ANY;
	/* nt-port with no channel, not reserverd */
	if (!p_m_b_channel && !p_m_b_reserve && p_type==PORT_TYPE_DSS1_NT_OUT)
		channel = CHANNEL_NO;

	/* creating l3id */
	l1l2l3_trace_header(p_m_mISDNport, this, L3_NEW_L3ID_REQ, DIRECTION_OUT);
#ifdef OLD_MT_ASSIGN
	/* see MT_ASSIGN notes at do_layer3() */
	mt_assign_pid = 0;
	ret = p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_ASSIGN, 0, NULL);
	if (mt_assign_pid == 0 || ret < 0)
	p_m_d_l3id = mt_assign_pid;
	mt_assign_pid = ~0;
#else
	p_m_d_l3id = request_new_pid(p_m_mISDNport->ml3);
	if (p_m_d_l3id == MISDN_PID_NONE)
#endif
	{
		struct lcr_msg *message;

		add_trace("callref", NULL, "no free id");
		end_trace();
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.cause = 47;
		message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		message_put(message);
		new_state(PORT_STATE_RELEASE);
		trigger_work(&p_m_d_delete);
		return;
	}
#ifdef OLD_MT_ASSIGN
	p_m_d_l3id = mt_assign_pid;
	mt_assign_pid = ~0;
#endif
	add_trace("callref", "new", "0x%x", p_m_d_l3id);
	end_trace();

	/* preparing setup message */
	l3m = create_l3msg();
	l1l2l3_trace_header(p_m_mISDNport, this, L3_SETUP_REQ, DIRECTION_OUT);
	/* channel information */
	if (p_m_d_ntmode || channel != CHANNEL_ANY) /* only omit channel id in te-mode/any channel */
		enc_ie_channel_id(l3m, exclusive, channel);
	/* caller information */
	plan = 1;
	switch (p_callerinfo.ntype) {
		case INFO_NTYPE_UNKNOWN:
		type = 0x0;
		break;
		case INFO_NTYPE_INTERNATIONAL:
		type = 0x1;
		break;
		case INFO_NTYPE_NATIONAL:
		type = 0x2;
		break;
		case INFO_NTYPE_SUBSCRIBER:
		type = 0x4;
		break;
		default: /* INFO_NTYPE_NOTPRESENT */
		type = -1;
		break;
	}
	switch (p_callerinfo.screen) {
		case INFO_SCREEN_USER:
		screen = 0;
		break;
		case INFO_SCREEN_USER_VERIFIED_PASSED:
		screen = 1;
		break;
		case INFO_SCREEN_USER_VERIFIED_FAILED:
		screen = 2;
		break;
		default: /* INFO_SCREEN_NETWORK */
		screen = 3;
		break;
	}
	switch (p_callerinfo.present) {
		case INFO_PRESENT_ALLOWED:
		present = 0;
		break;
		case INFO_PRESENT_RESTRICTED:
		present = 1;
		break;
		default: /* INFO_PRESENT_NOTAVAIL */
		present = 2;
		break;
	}
	/* caller information 2 */
	plan2 = 1;
	switch (p_callerinfo.ntype2) {
		case INFO_NTYPE_UNKNOWN:
		type2 = 0x0;
		break;
		case INFO_NTYPE_INTERNATIONAL:
		type2 = 0x1;
		break;
		case INFO_NTYPE_NATIONAL:
		type2 = 0x2;
		break;
		case INFO_NTYPE_SUBSCRIBER:
		type2 = 0x4;
		break;
		default: /* INFO_NTYPE_NOTPRESENT */
		type2 = -1;
		break;
	}
	switch (p_callerinfo.screen2) {
		case INFO_SCREEN_USER:
		screen2 = 0;
		break;
		case INFO_SCREEN_USER_VERIFIED_PASSED:
		screen2 = 1;
		break;
		case INFO_SCREEN_USER_VERIFIED_FAILED:
		screen2 = 2;
		break;
		default: /* INFO_SCREEN_NETWORK */
		screen2 = 3;
		break;
	}
	switch (p_callerinfo.present2) {
		case INFO_PRESENT_ALLOWED:
		present2 = 0;
		break;
		case INFO_PRESENT_RESTRICTED:
		present2 = 1;
		break;
		default: /* INFO_PRESENT_NOTAVAIL */
		present2 = 2;
		break;
	}
	if (type >= 0)
		enc_ie_calling_pn(l3m, type, plan, present, screen, (unsigned char *)p_callerinfo.id, type2, plan2, present2, screen2, (unsigned char *)p_callerinfo.id2);
	/* dialing information */
	if (p_dialinginfo.id[0]) { /* only if we have something to dial */
		if (max > (int)strlen(p_dialinginfo.id) || max == 0)
			max = (int)strlen(p_dialinginfo.id);
		enc_ie_called_pn(l3m, 0, 1, (unsigned char *)p_dialinginfo.id, max);
		SCPY(p_m_d_queue, p_dialinginfo.id + max);
	}
	/* keypad */
	if (p_dialinginfo.keypad[0])
		enc_ie_keypad(l3m, (unsigned char *)p_dialinginfo.keypad);
	/* sending complete */
	if (p_dialinginfo.sending_complete)
		enc_ie_complete(l3m, 1);
	/* sending user-user */
	if (param->setup.useruser.len) {
		enc_ie_useruser(l3m, param->setup.useruser.protocol, param->setup.useruser.data, param->setup.useruser.len);
	}
	/* redirecting number */
	plan = 1;
	switch (p_redirinfo.ntype) {
		case INFO_NTYPE_UNKNOWN:
		type = 0x0;
		break;
		case INFO_NTYPE_INTERNATIONAL:
		type = 0x1;
		break;
		case INFO_NTYPE_NATIONAL:
		type = 0x2;
		break;
		case INFO_NTYPE_SUBSCRIBER:
		type = 0x4;
		break;
		default: /* INFO_NTYPE_NOTPRESENT */
		type = -1;
		break;
	}
	switch (p_redirinfo.screen) {
		case INFO_SCREEN_USER:
		screen = 0;
		break;
		case INFO_SCREEN_USER_VERIFIED_PASSED:
		screen = 1;
		break;
		case INFO_SCREEN_USER_VERIFIED_FAILED:
		screen = 2;
		break;
		default: /* INFO_SCREE_NETWORK */
		screen = 3;
		break;
	}
	switch (p_redirinfo.reason) {
		case INFO_REDIR_BUSY:
		reason = 1;
		break;
		case INFO_REDIR_NORESPONSE:
		reason = 2;
		break;
		case INFO_REDIR_UNCONDITIONAL:
		reason = 15;
		break;
		case INFO_REDIR_CALLDEFLECT:
		reason = 10;
		break;
		case INFO_REDIR_OUTOFORDER:
		reason = 9;
		break;
		default: /* INFO_REDIR_UNKNOWN */
		reason = 0;
		break;
	}
	switch (p_redirinfo.present) {
		case INFO_PRESENT_ALLOWED:
		present = 0;
		break;
		case INFO_PRESENT_RESTRICTED:
		present = 1;
		break;
		default: /* INFO_PRESENT_NOTAVAIL */
		present = 2;
		break;
	}
	/* sending redirecting number only in ntmode */
	if (type >= 0 && (p_m_d_ntmode || p_m_d_tespecial))
		enc_ie_redir_nr(l3m, type, plan, present, screen, reason, (unsigned char *)p_redirinfo.id);
	/* bearer capability */
//printf("hlc=%d\n",p_capainfo.hlc);
	coding = 0;
	capability = p_capainfo.bearer_capa;
	mode = p_capainfo.bearer_mode;
	rate = (mode==INFO_BMODE_CIRCUIT)?0x10:0x00;
	switch (p_capainfo.bearer_info1) {
		case INFO_INFO1_NONE:
		user = -1;
		break;
		default:
		user = p_capainfo.bearer_info1 & 0x7f;
		break;
	}
	enc_ie_bearer(l3m, coding, capability, mode, rate, -1, user);
	/* hlc */
	if (p_capainfo.hlc) {
		coding = 0;
		interpretation = 4;
		presentation = 1;
		hlc = p_capainfo.hlc & 0x7f;
		exthlc = -1;
		if (p_capainfo.exthlc)
			exthlc = p_capainfo.exthlc & 0x7f;
		enc_ie_hlc(l3m, coding, interpretation, presentation, hlc, exthlc);
	}

	/* display */
	if (p_callerinfo.display[0] && (p_m_d_ntmode || p_m_d_tespecial))
		enc_ie_display(l3m, (unsigned char *)p_callerinfo.display);
	/* nt-mode: CNIP (calling name identification presentation) */
//	if (p_callerinfo.name[0] && (p_m_d_ntmode || p_m_d_tespecial))
//		enc_facility_centrex(&setup->FACILITY, dmsg, (unsigned char *)p_callerinfo.name, 1);
	end_trace();

	/* send setup message now */
	p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_SETUP, p_m_d_l3id, l3m);

	new_state(PORT_STATE_OUT_SETUP);
}

/* MESSAGE_FACILITY */
void Pdss1::message_facility(unsigned int epoint_id, int message_id, union parameter *param)
{
	l3_msg *l3m;

	/* facility will not be sent to external lines */
	if (!p_m_d_ntmode && !p_m_d_tespecial)
		return;

	/* sending facility */
	l3m = create_l3msg();
	l1l2l3_trace_header(p_m_mISDNport, this, L3_FACILITY_REQ, DIRECTION_OUT);
	enc_ie_facility(l3m, (unsigned char *)param->facilityinfo.data, param->facilityinfo.len);
	end_trace();
	p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_FACILITY, p_m_d_l3id, l3m);
}

/* MESSAGE_NOTIFY */
void Pdss1::message_notify(unsigned int epoint_id, int message_id, union parameter *param)
{
	l3_msg *l3m;
	int notify;
	int plan = 0, type = -1, present = 0;

	if (p_m_mISDNport->ifport->nonotify) {
		l1l2l3_trace_header(p_m_mISDNport, this, L3_NOTIFY_REQ, DIRECTION_OUT);
		add_trace("info", NULL, "blocked by config");
		end_trace();
		return;
	}

//	printf("if = %d\n", param->notifyinfo.notify);
	if (param->notifyinfo.notify>INFO_NOTIFY_NONE)
		notify = param->notifyinfo.notify & 0x7f;
	else
		notify = -1;
	if (notify >= 0) {
		plan = 1;
		switch (param->notifyinfo.ntype) {
			case INFO_NTYPE_UNKNOWN:
			type = 0;
			break;
			case INFO_NTYPE_INTERNATIONAL:
			type = 1;
			break;
			case INFO_NTYPE_NATIONAL:
			type = 2;
			break;
			case INFO_NTYPE_SUBSCRIBER:
			type = 4;
			break;
			default: /* INFO_NTYPE_UNKNOWN */
			type = -1;
			break;
		}
		switch (param->notifyinfo.present) {
			case INFO_PRESENT_ALLOWED:
			present = 0;
			break;
			case INFO_PRESENT_RESTRICTED:
			present = 1;
			break;
			default: /* INFO_PRESENT_NOTAVAIL */
			present = 2;
			break;
		}
	}

	if (notify<0 && !param->notifyinfo.display[0]) {
		/* nothing to notify, nothing to display */
		return;
	}

	if (notify >= 0) {
		if (p_state!=PORT_STATE_CONNECT && p_state!=PORT_STATE_IN_PROCEEDING && p_state!=PORT_STATE_IN_ALERTING) {
			/* queue notification */
			if (p_m_d_notify_pending)
				message_free(p_m_d_notify_pending);
			p_m_d_notify_pending = message_create(ACTIVE_EPOINT(p_epointlist), p_serial, EPOINT_TO_PORT, message_id);
			memcpy(&p_m_d_notify_pending->param, param, sizeof(union parameter));
		} else {
			/* sending notification */
			l3m = create_l3msg();
			l1l2l3_trace_header(p_m_mISDNport, this, L3_NOTIFY_REQ, DIRECTION_OUT);
			enc_ie_notify(l3m, notify);
			/* sending redirection number only in ntmode */
			if (type >= 0 && (p_m_d_ntmode || p_m_d_tespecial))
				enc_ie_redir_dn(l3m, type, plan, present, (unsigned char *)param->notifyinfo.id);
			if (param->notifyinfo.display[0] && (p_m_d_ntmode || p_m_d_tespecial))
				enc_ie_display(l3m, (unsigned char *)param->notifyinfo.display);
			end_trace();
			p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_NOTIFY, p_m_d_l3id, l3m);
		}
	} else if (p_m_d_ntmode || p_m_d_tespecial) {
		/* sending information */
		l3m = create_l3msg();
		l1l2l3_trace_header(p_m_mISDNport, this, L3_INFORMATION_REQ, DIRECTION_OUT);
		enc_ie_display(l3m, (unsigned char *)param->notifyinfo.display);
		end_trace();
		p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_INFORMATION, p_m_d_l3id, l3m);
	}
}

/* MESSAGE_OVERLAP */
void Pdss1::message_overlap(unsigned int epoint_id, int message_id, union parameter *param)
{
	l3_msg *l3m;

	/* in case of sending complete, we proceed */
	if (p_dialinginfo.sending_complete) {
		PDEBUG(DEBUG_ISDN, "sending proceeding instead of setup_acknowledge, because address is complete.\n");
		message_proceeding(epoint_id, message_id, param);
		return;
	}

	/* sending setup_acknowledge */
	l3m = create_l3msg();
	l1l2l3_trace_header(p_m_mISDNport, this, L3_SETUP_ACKNOWLEDGE_REQ, DIRECTION_OUT);
	/* channel information */
	if (p_state == PORT_STATE_IN_SETUP)
		enc_ie_channel_id(l3m, 1, p_m_b_channel);
	/* progress information */
	if (p_capainfo.bearer_capa==INFO_BC_SPEECH
	 || p_capainfo.bearer_capa==INFO_BC_AUDIO
	 || p_capainfo.bearer_capa==INFO_BC_DATAUNRESTRICTED_TONES)
	if (p_m_mISDNport->tones)
		enc_ie_progress(l3m, 0, (p_m_d_ntmode)?1:5, 8);
	end_trace();
	p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_SETUP_ACKNOWLEDGE, p_m_d_l3id, l3m);

	new_state(PORT_STATE_IN_OVERLAP);
}

/* MESSAGE_PROCEEDING */
void Pdss1::message_proceeding(unsigned int epoint_id, int message_id, union parameter *param)
{
	l3_msg *l3m;

	/* sending proceeding */
	l3m = create_l3msg();
	l1l2l3_trace_header(p_m_mISDNport, this, L3_PROCEEDING_REQ, DIRECTION_OUT);
	/* channel information */
	if (p_state == PORT_STATE_IN_SETUP)
		enc_ie_channel_id(l3m, 1, p_m_b_channel);
	/* progress information */
	if (p_capainfo.bearer_capa==INFO_BC_SPEECH
	 || p_capainfo.bearer_capa==INFO_BC_AUDIO
	 || p_capainfo.bearer_capa==INFO_BC_DATAUNRESTRICTED_TONES)
	if (p_m_mISDNport->tones)
		enc_ie_progress(l3m, 0, (p_m_d_ntmode)?1:5, 8);
	end_trace();
	p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_CALL_PROCEEDING, p_m_d_l3id, l3m);

	new_state(PORT_STATE_IN_PROCEEDING);
}

/* MESSAGE_ALERTING */
void Pdss1::message_alerting(unsigned int epoint_id, int message_id, union parameter *param)
{
	l3_msg *l3m;

	/* NT-MODE in setup state we must send PROCEEDING first */
	if (p_m_d_ntmode && p_state==PORT_STATE_IN_SETUP) {
		/* sending proceeding */
		l3m = create_l3msg();
		l1l2l3_trace_header(p_m_mISDNport, this, L3_PROCEEDING_REQ, DIRECTION_OUT);
		/* channel information */
		enc_ie_channel_id(l3m, 1, p_m_b_channel);
		/* progress information */
		if (p_capainfo.bearer_capa==INFO_BC_SPEECH
		 || p_capainfo.bearer_capa==INFO_BC_AUDIO
		 || p_capainfo.bearer_capa==INFO_BC_DATAUNRESTRICTED_TONES)
		if (p_m_mISDNport->tones)
			enc_ie_progress(l3m, 0, (p_m_d_ntmode)?1:5, 8);
		end_trace();
		p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_CALL_PROCEEDING, p_m_d_l3id, l3m);
		new_state(PORT_STATE_IN_PROCEEDING);
	}

	/* sending alerting */
	l3m = create_l3msg();
	l1l2l3_trace_header(p_m_mISDNport, this, L3_ALERTING_REQ, DIRECTION_OUT);
	/* channel information */
	if (p_state == PORT_STATE_IN_SETUP)
		enc_ie_channel_id(l3m, 1, p_m_b_channel);
	/* progress information */
	if (p_capainfo.bearer_capa==INFO_BC_SPEECH
	 || p_capainfo.bearer_capa==INFO_BC_AUDIO
	 || p_capainfo.bearer_capa==INFO_BC_DATAUNRESTRICTED_TONES)
	if (p_m_mISDNport->tones)
		enc_ie_progress(l3m, 0, (p_m_d_ntmode)?1:5, 8);
	end_trace();
	p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_ALERTING, p_m_d_l3id, l3m);

	new_state(PORT_STATE_IN_ALERTING);
}

/* MESSAGE_CONNECT */
void Pdss1::message_connect(unsigned int epoint_id, int message_id, union parameter *param)
{
	l3_msg *l3m;
	int type, plan, present, screen;
	class Endpoint *epoint;
	time_t current_time;

	/* NT-MODE in setup state we must send PROCEEDING first */
	if (p_m_d_ntmode && p_state==PORT_STATE_IN_SETUP) {
		/* sending proceeding */
		l3m = create_l3msg();
		l1l2l3_trace_header(p_m_mISDNport, this, L3_PROCEEDING_REQ, DIRECTION_OUT);
		/* channel information */
		enc_ie_channel_id(l3m, 1, p_m_b_channel);
		end_trace();
		p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_CALL_PROCEEDING, p_m_d_l3id, l3m);
		new_state(PORT_STATE_IN_PROCEEDING);
	}

	/* copy connected information */
	memcpy(&p_connectinfo, &param->connectinfo, sizeof(p_connectinfo));
	/* screen outgoing caller id */
	do_screen(1, p_connectinfo.id, sizeof(p_connectinfo.id), &p_connectinfo.ntype, &p_connectinfo.present, p_m_mISDNport->ifport->interface);

	/* only display at connect state */
	if (p_state == PORT_STATE_CONNECT)
	if (p_connectinfo.display[0]) {
		/* sending information */
		l3m = create_l3msg();
		l1l2l3_trace_header(p_m_mISDNport, this, L3_INFORMATION_REQ, DIRECTION_OUT);
		if (p_m_d_ntmode || p_m_d_tespecial)
			enc_ie_display(l3m, (unsigned char *)p_connectinfo.display);
		end_trace();
		p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_INFORMATION, p_m_d_l3id, l3m);
		return;
	}

	if (p_state!=PORT_STATE_IN_SETUP && p_state!=PORT_STATE_IN_OVERLAP && p_state!=PORT_STATE_IN_PROCEEDING && p_state!=PORT_STATE_IN_ALERTING) {
		/* connect command only possible in setup, proceeding or alerting state */
		return;
	}

	/* preparing connect message */
	l3m = create_l3msg();
	l1l2l3_trace_header(p_m_mISDNport, this, L3_CONNECT_REQ, DIRECTION_OUT);
	/* connect information */
	plan = 1;
	switch (p_connectinfo.ntype) {
		case INFO_NTYPE_UNKNOWN:
		type = 0x0;
		break;
		case INFO_NTYPE_INTERNATIONAL:
		type = 0x1;
		break;
		case INFO_NTYPE_NATIONAL:
		type = 0x2;
		break;
		case INFO_NTYPE_SUBSCRIBER:
		type = 0x4;
		break;
		default: /* INFO_NTYPE_NOTPRESENT */
		type = -1;
		break;
	}
	switch (param->connectinfo.screen) {
		case INFO_SCREEN_USER:
		screen = 0;
		break;
		case INFO_SCREEN_USER_VERIFIED_PASSED:
		screen = 1;
		break;
		case INFO_SCREEN_USER_VERIFIED_FAILED:
		screen = 2;
		break;
		default: /* INFO_SCREE_NETWORK */
		screen = 3;
		break;
	}
	switch (p_connectinfo.present) {
		case INFO_PRESENT_ALLOWED:
		present = 0;
		break;
		case INFO_PRESENT_RESTRICTED:
		present = 1;
		break;
		default: /* INFO_PRESENT_NOTAVAIL */
		present = 2;
		break;
	}
	if (type >= 0)
		enc_ie_connected_pn(l3m, type, plan, present, screen, (unsigned char *)p_connectinfo.id);
	/* display */
	if (p_connectinfo.display[0] && (p_m_d_ntmode || p_m_d_tespecial))
		enc_ie_display(l3m, (unsigned char *)p_connectinfo.display);
	/* nt-mode: CONP (connected name identification presentation) */
//	if (p_connectinfo.name[0] && (p_m_d_ntmode || p_m_d_tespecial))
//		enc_facility_centrex(&connect->FACILITY, dmsg, (unsigned char *)p_connectinfo.name, 0);
	/* date & time */
	if (p_m_d_ntmode || p_m_d_tespecial) {
		epoint = find_epoint_id(epoint_id);
		time(&current_time);
		enc_ie_date(l3m, current_time, p_settings.no_seconds);
	}
	end_trace();
	/* finally send message */
	p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_CONNECT, p_m_d_l3id, l3m);

	if (p_m_d_ntmode)
		new_state(PORT_STATE_CONNECT);
	else
		new_state(PORT_STATE_CONNECT_WAITING);
	set_tone("", NULL);
}

/* MESSAGE_DISCONNECT */
void Pdss1::message_disconnect(unsigned int epoint_id, int message_id, union parameter *param)
{
	l3_msg *l3m;
	struct lcr_msg *message;
	char *p = NULL;

	/* we reject during incoming setup when we have no tones. also if we are in outgoing setup state */
//	if ((p_state==PORT_STATE_IN_SETUP && !p_m_mISDNport->tones)
if (/*	 ||*/ p_state==PORT_STATE_OUT_SETUP) {
		/* sending release to endpoint */
		while(p_epointlist) {
			message = message_create(p_serial, p_epointlist->epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
			memcpy(&message->param, param, sizeof(union parameter));
			message_put(message);
			/* remove epoint */
			free_epointlist(p_epointlist);
		}
		/* sending release */
		l3m = create_l3msg();
		l1l2l3_trace_header(p_m_mISDNport, this, L3_RELEASE_COMPLETE_REQ, DIRECTION_OUT);
		/* send cause */
		enc_ie_cause(l3m, (!p_m_mISDNport->locally && param->disconnectinfo.location==LOCATION_PRIVATE_LOCAL)?LOCATION_PRIVATE_REMOTE:param->disconnectinfo.location, param->disconnectinfo.cause);
		end_trace();
		p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_RELEASE_COMPLETE, p_m_d_l3id, l3m);
		new_state(PORT_STATE_RELEASE);
		trigger_work(&p_m_d_delete);
		return;
	}

	/* workarround: NT-MODE in setup state we must send PROCEEDING first to make it work */
	if (p_state==PORT_STATE_IN_SETUP) {
		/* sending proceeding */
		l3m = create_l3msg();
		l1l2l3_trace_header(p_m_mISDNport, this, L3_PROCEEDING_REQ, DIRECTION_OUT);
		/* channel information */
		enc_ie_channel_id(l3m, 1, p_m_b_channel);
		/* progress information */
		if (p_capainfo.bearer_capa==INFO_BC_SPEECH
		 || p_capainfo.bearer_capa==INFO_BC_AUDIO
		 || p_capainfo.bearer_capa==INFO_BC_DATAUNRESTRICTED_TONES)
		if (p_m_mISDNport->tones)
			enc_ie_progress(l3m, 0, (p_m_d_ntmode)?1:5, 8);
		end_trace();
		p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_CALL_PROCEEDING, p_m_d_l3id, l3m);
		new_state(PORT_STATE_IN_PROCEEDING);
	}

	/* sending disconnect */
	l3m = create_l3msg();
	l1l2l3_trace_header(p_m_mISDNport, this, L3_DISCONNECT_REQ, DIRECTION_OUT);
	/* progress information */
	if (p_capainfo.bearer_capa==INFO_BC_SPEECH
	 || p_capainfo.bearer_capa==INFO_BC_AUDIO
	 || p_capainfo.bearer_capa==INFO_BC_DATAUNRESTRICTED_TONES)
	if (p_m_mISDNport->tones)
		enc_ie_progress(l3m, 0, (p_m_d_ntmode)?1:5, 8);
	/* send cause */
	enc_ie_cause(l3m, (!p_m_mISDNport->locally && param->disconnectinfo.location==LOCATION_PRIVATE_LOCAL)?LOCATION_PRIVATE_REMOTE:param->disconnectinfo.location, param->disconnectinfo.cause);
	/* send display */
	if (param->disconnectinfo.display[0])
		p = param->disconnectinfo.display;
	if (p) if (*p && (p_m_d_ntmode || p_m_d_tespecial))
		enc_ie_display(l3m, (unsigned char *)p);
	end_trace();
	p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_DISCONNECT, p_m_d_l3id, l3m);
	new_state(PORT_STATE_OUT_DISCONNECT);
}

/* MESSAGE_RELEASE */
void Pdss1::message_release(unsigned int epoint_id, int message_id, union parameter *param)
{
	l3_msg *l3m;
	class Endpoint *epoint;
	char *p = NULL;

	/*
	 * if we are on incoming call setup, we may reject by sending a release_complete
	 * also on outgoing call setup, we send a release complete, BUT this is not conform. (i don't know any other way)
	 */
	if (p_state==PORT_STATE_IN_SETUP
	 || p_state==PORT_STATE_OUT_SETUP) {
//#warning remove me
//PDEBUG(DEBUG_LOG, "JOLLY sending release complete %d\n", p_serial);
		/* sending release complete */
		l3m = create_l3msg();
		l1l2l3_trace_header(p_m_mISDNport, this, L3_RELEASE_REQ, DIRECTION_OUT);
		/* send cause */
		enc_ie_cause(l3m, (p_m_mISDNport->locally && param->disconnectinfo.location==LOCATION_PRIVATE_LOCAL)?LOCATION_PRIVATE_LOCAL:param->disconnectinfo.location, param->disconnectinfo.cause);
		end_trace();
		p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_RELEASE_COMPLETE, p_m_d_l3id, l3m);
		new_state(PORT_STATE_RELEASE);
		/* remove epoint */
		free_epointid(epoint_id);
		// wait for callref to be released
		return;
	}
	/*
	 * we may only release during incoming disconnect state.
	 * this means that the endpoint doesnt require audio anymore
	 */
	if (p_state == PORT_STATE_IN_DISCONNECT
     	 || p_state == PORT_STATE_OUT_DISCONNECT
	 || param->disconnectinfo.force) {
		/* sending release */
		l3m = create_l3msg();
		l1l2l3_trace_header(p_m_mISDNport, this, L3_RELEASE_REQ, DIRECTION_OUT);
		/* send cause */
		enc_ie_cause(l3m, (p_m_mISDNport->locally && param->disconnectinfo.location==LOCATION_PRIVATE_LOCAL)?LOCATION_PRIVATE_LOCAL:param->disconnectinfo.location, param->disconnectinfo.cause);
		end_trace();
		p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_RELEASE, p_m_d_l3id, l3m);
		new_state(PORT_STATE_RELEASE);
		/* remove epoint */
		free_epointid(epoint_id);
		// wait for callref to be released
		return;

	}

#if 0
wirklich erst proceeding?:
	/* NT-MODE in setup state we must send PROCEEDING first */
	if (p_m_d_ntmode && p_state==PORT_STATE_IN_SETUP) {
		CALL_PROCEEDING_t *proceeding;

		/* sending proceeding */
		l3m = create_l3msg();
		l1l2l3_trace_header(p_m_mISDNport, this, L3_PROCEEDING_REQ, DIRECTION_OUT);
		/* channel information */
		enc_ie_channel_id(l3m, 1, p_m_b_channel);
		/* progress information */
		if (p_capainfo.bearer_capa==INFO_BC_SPEECH
		 || p_capainfo.bearer_capa==INFO_BC_AUDIO
		 || p_capainfo.bearer_capa==INFO_BC_DATAUNRESTRICTED_TONES)
		if (p_m_mISDNport->tones)
			enc_ie_progress(l3m, 0, (p_m_d_ntmode)?1:5, 8);
		end_trace();
		p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_CALL_PROCEEDING, p_m_d_l3id, l3m);
	}
#endif

	/* sending disconnect */
	l3m = create_l3msg();
	l1l2l3_trace_header(p_m_mISDNport, this, L3_DISCONNECT_REQ, DIRECTION_OUT);
	/* progress information */
	if (p_capainfo.bearer_capa==INFO_BC_SPEECH
	 || p_capainfo.bearer_capa==INFO_BC_AUDIO
	 || p_capainfo.bearer_capa==INFO_BC_DATAUNRESTRICTED_TONES)
	if (p_m_mISDNport->tones)
		enc_ie_progress(l3m, 0, (p_m_d_ntmode)?1:5, 8);
	/* send cause */
	enc_ie_cause(l3m, (p_m_mISDNport->locally && param->disconnectinfo.location==LOCATION_PRIVATE_LOCAL)?LOCATION_PRIVATE_LOCAL:param->disconnectinfo.location, param->disconnectinfo.cause);
	/* send display */
	epoint = find_epoint_id(epoint_id);
	if (param->disconnectinfo.display[0])
		p = param->disconnectinfo.display;
	if (p) if (*p && (p_m_d_ntmode || p_m_d_tespecial))
		enc_ie_display(l3m, (unsigned char *)p);
	end_trace();
	p_m_mISDNport->ml3->to_layer3(p_m_mISDNport->ml3, MT_DISCONNECT, p_m_d_l3id, l3m);
	new_state(PORT_STATE_OUT_DISCONNECT);
	/* remove epoint */
	free_epointid(epoint_id);
	// wait for release and callref to be released
//#warning remove me
//PDEBUG(DEBUG_LOG, "JOLLY sending disconnect %d\n", p_serial);
}


/*
 * endpoint sends messages to the port
 */
int Pdss1::message_epoint(unsigned int epoint_id, int message_id, union parameter *param)
{
	if (PmISDN::message_epoint(epoint_id, message_id, param))
		return(1);

	switch(message_id) {
		case MESSAGE_INFORMATION: /* overlap dialing */
		if (p_type==PORT_TYPE_DSS1_NT_OUT
		 && p_state!=PORT_STATE_OUT_OVERLAP
		 && p_state!=PORT_STATE_CONNECT
		 && p_state!=PORT_STATE_OUT_DISCONNECT
		 && p_state!=PORT_STATE_IN_DISCONNECT) {
			break;
		}
		if (p_type==PORT_TYPE_DSS1_TE_OUT
		 && p_state!=PORT_STATE_OUT_OVERLAP
		 && p_state!=PORT_STATE_OUT_PROCEEDING
		 && p_state!=PORT_STATE_OUT_ALERTING
		 && p_state!=PORT_STATE_CONNECT
		 && p_state!=PORT_STATE_OUT_DISCONNECT
		 && p_state!=PORT_STATE_IN_DISCONNECT) {
			break;
		}
		if ((p_type==PORT_TYPE_DSS1_NT_IN || p_type==PORT_TYPE_DSS1_TE_IN)
		 && p_state!=PORT_STATE_IN_OVERLAP
		 && p_state!=PORT_STATE_IN_PROCEEDING
		 && p_state!=PORT_STATE_IN_ALERTING
		 && p_state!=PORT_STATE_CONNECT
		 && p_state!=PORT_STATE_CONNECT_WAITING
		 && p_state!=PORT_STATE_OUT_DISCONNECT
		 && p_state!=PORT_STATE_IN_DISCONNECT) {
			break;
		}
		message_information(epoint_id, message_id, param);
		break;

		case MESSAGE_SETUP: /* dial-out command received from epoint */
		if (p_state!=PORT_STATE_IDLE
		 && p_state!=PORT_STATE_CONNECT) {
			PERROR("Pdss1(%s) ignoring setup because isdn port is not in idle state (or connected for sending display info).\n", p_name);
			break;
		}
		if (p_epointlist && p_state==PORT_STATE_IDLE)
			FATAL("Pdss1(%s): epoint pointer is set in idle state, how bad!!\n", p_name);
		message_setup(epoint_id, message_id, param);
		break;

		case MESSAGE_NOTIFY: /* display and notifications */
		message_notify(epoint_id, message_id, param);
		break;

		case MESSAGE_FACILITY: /* facility message */
		message_facility(epoint_id, message_id, param);
		break;

		case MESSAGE_OVERLAP: /* more information is needed */
		if (p_state!=PORT_STATE_IN_SETUP) {
			break;
		}
		message_overlap(epoint_id, message_id, param);
		break;

		case MESSAGE_PROCEEDING: /* call of endpoint is proceeding */
		if (p_state!=PORT_STATE_IN_SETUP
		 && p_state!=PORT_STATE_IN_OVERLAP) {
			break;
		}
		message_proceeding(epoint_id, message_id, param);
		if (p_m_d_notify_pending) {
			/* send pending notify message during connect */
			message_notify(ACTIVE_EPOINT(p_epointlist), p_m_d_notify_pending->type, &p_m_d_notify_pending->param);
			message_free(p_m_d_notify_pending);
			p_m_d_notify_pending = NULL;
		}
		break;

		case MESSAGE_ALERTING: /* call of endpoint is ringing */
		if (p_state!=PORT_STATE_IN_SETUP
		 && p_state!=PORT_STATE_IN_OVERLAP
		 && p_state!=PORT_STATE_IN_PROCEEDING) {
			break;
		}
		message_alerting(epoint_id, message_id, param);
		if (p_m_d_notify_pending) {
			/* send pending notify message during connect */
			message_notify(ACTIVE_EPOINT(p_epointlist), p_m_d_notify_pending->type, &p_m_d_notify_pending->param);
			message_free(p_m_d_notify_pending);
			p_m_d_notify_pending = NULL;
		}
		break;

		case MESSAGE_CONNECT: /* call of endpoint is connected */
		if (p_state!=PORT_STATE_IN_SETUP
		 && p_state!=PORT_STATE_IN_OVERLAP
		 && p_state!=PORT_STATE_IN_PROCEEDING
		 && p_state!=PORT_STATE_IN_ALERTING
		 && !(p_state==PORT_STATE_CONNECT && p_m_d_ntmode)) {
			break;
		}
		message_connect(epoint_id, message_id, param);
		if (p_m_d_notify_pending) {
			/* send pending notify message during connect */
			message_notify(ACTIVE_EPOINT(p_epointlist), p_m_d_notify_pending->type, &p_m_d_notify_pending->param);
			message_free(p_m_d_notify_pending);
			p_m_d_notify_pending = NULL;
		}
		break;

		case MESSAGE_DISCONNECT: /* call has been disconnected */
		if (p_state!=PORT_STATE_IN_SETUP
		 && p_state!=PORT_STATE_IN_OVERLAP
		 && p_state!=PORT_STATE_IN_PROCEEDING
		 && p_state!=PORT_STATE_IN_ALERTING
		 && p_state!=PORT_STATE_OUT_SETUP
		 && p_state!=PORT_STATE_OUT_OVERLAP
		 && p_state!=PORT_STATE_OUT_PROCEEDING
		 && p_state!=PORT_STATE_OUT_ALERTING
		 && p_state!=PORT_STATE_CONNECT
		 && p_state!=PORT_STATE_CONNECT_WAITING) {
			break;
		}
		message_disconnect(epoint_id, message_id, param);
		break;

		case MESSAGE_RELEASE: /* release isdn port */
		if (p_state==PORT_STATE_RELEASE) {
			break;
		}
		message_release(epoint_id, message_id, param);
		break;

		default:
		PERROR("Pdss1(%s) isdn port with (caller id %s) received a wrong message: %d\n", p_name, p_callerinfo.id, message_id);
	}

	return(1);
}



/*
 * data from isdn-stack (layer-3) to pbx (port class)
 */
int stack2manager(struct mISDNport *mISDNport, unsigned int cmd, unsigned int pid, struct l3_msg *l3m)
{
	class Port *port;
	class Pdss1 *pdss1;
	char name[32];

	PDEBUG(DEBUG_ISDN, "cmd(0x%x) pid(0x%x)\n", cmd, pid);

	if (pid == 0) {
		PDEBUG(DEBUG_ISDN, "ignoring dummy process from phone.\n");
		return(0);
	}

	/* find Port object of type ISDN */
	port = port_first;
	while(port) {
		/* are we ISDN ? */
		if ((port->p_type & PORT_CLASS_mISDN_MASK) == PORT_CLASS_DSS1) {
			pdss1 = (class Pdss1 *)port;
			/* check out correct stack and id */
			if (pdss1->p_m_mISDNport == mISDNport) {
				if (pdss1->p_m_d_l3id & MISDN_PID_CR_FLAG) {
					/* local callref, so match value only */
					if ((pdss1->p_m_d_l3id & MISDN_PID_CRVAL_MASK) == (pid & MISDN_PID_CRVAL_MASK))
						break; // found
				} else {
					/* remote callref, ref + channel id */
					if (pdss1->p_m_d_l3id == pid)
						break; // found
				}
			}
		}
		port = port->next;
	}

	/* aktueller prozess */
	if (port) {
		if (cmd == MT_ASSIGN) {
			/* stack gives us new layer 3 id (during connect) */
			l1l2l3_trace_header(mISDNport, pdss1, L3_NEW_L3ID_IND, DIRECTION_IN);
			add_trace("callref", "old", "0x%x", pdss1->p_m_d_l3id);
			/* nt-library now gives us a new id via CC_SETUP_CONFIRM */
			if ((pdss1->p_m_d_l3id&MISDN_PID_CRTYPE_MASK) != MISDN_PID_MASTER)
				PERROR("    strange setup-procid 0x%x\n", pdss1->p_m_d_l3id);
			pdss1->p_m_d_l3id = pid;
			if (port->p_state == PORT_STATE_CONNECT)
				pdss1->p_m_d_ces = pid >> 16;
			add_trace("callref", "new", "0x%x", pdss1->p_m_d_l3id);
			end_trace();
			return(0);
		}
		/* if process id is master process, but a child disconnects */
		if (mISDNport->ntmode
		 && (pid & MISDN_PID_CRTYPE_MASK) != MISDN_PID_MASTER
		 && (pdss1->p_m_d_l3id & MISDN_PID_CRTYPE_MASK) == MISDN_PID_MASTER) {
			if (cmd == MT_DISCONNECT
			 || cmd == MT_RELEASE) {
				/* send special indication for child disconnect */
				pdss1->disconnect_ind_i(cmd, pid, l3m);
				return(0);
			}
			if (cmd == MT_RELEASE_COMPLETE)
				return(0);
		}
		/* if we have child pid and got different child pid message, ignore */
		if (mISDNport->ntmode
		 && (pid & MISDN_PID_CRTYPE_MASK) != MISDN_PID_MASTER
		 && (pdss1->p_m_d_l3id & MISDN_PID_CRTYPE_MASK) != MISDN_PID_MASTER
		 && pid != pdss1->p_m_d_l3id)
			return(0);

		/* process message */
		pdss1->message_isdn(cmd, pid, l3m);
		return(0);
	}

	/* d-message */
	switch(cmd) {
		case MT_SETUP:
		/* creating port object, transparent until setup with hdlc */
		SPRINT(name, "%s-%d-in", mISDNport->ifport->interface->name, mISDNport->portnum);
		if (!(pdss1 = new Pdss1(PORT_TYPE_DSS1_NT_IN, mISDNport, name, NULL, 0, 0, B_MODE_TRANSPARENT)))

			FATAL("Cannot create Port instance.\n");
		pdss1->message_isdn(cmd, pid, l3m);
		break;

		case MT_RESUME:
		/* creating port object, transparent until setup with hdlc */
		SPRINT(name, "%s-%d-in", mISDNport->ifport->interface->name, mISDNport->portnum);
		if (!(pdss1 = new Pdss1(PORT_TYPE_DSS1_NT_IN, mISDNport, name, NULL, 0, 0, B_MODE_TRANSPARENT)))
			FATAL("Cannot create Port instance.\n");
		pdss1->message_isdn(cmd, pid, l3m);
		break;

		case MT_FREE:
		PDEBUG(DEBUG_ISDN, "unused call ref released (l3id=0x%x)\n", pid);
		break;

		case MT_RELEASE_COMPLETE:
		PERROR("must be ignored by stack, not sent to app\n");
		break;

		case MT_FACILITY:
		// facility als broadcast
		break;

		case MT_L2IDLE:
		// L2 became idle - we could sent a MT_L2RELEASE if we are the L2 master
		PDEBUG(DEBUG_ISDN, "Got L2 idle\n");
		break;

		default:
		PERROR("unhandled message: cmd(0x%x) pid(0x%x)\n", cmd, pid);
		port = port_first;
		while(port) {
			if (port->p_type == PORT_TYPE_DSS1_NT_IN || port->p_type == PORT_TYPE_DSS1_NT_OUT) {
				pdss1 = (class Pdss1 *)port;
				/* check out correct stack */
				if (pdss1->p_m_mISDNport == mISDNport)
				/* check out correct id */
				PERROR("unhandled message: pid=%x is not associated with port-dinfo=%x\n", pid, pdss1->p_m_d_l3id);
			}
			port = port->next;
		}
		return(-EINVAL);
	}
	return(0);
}





