/*****************************************************************************\
**                                                                           **
** LCR                                                                       **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** mISDN ss5                                                                 **
**                                                                           **
\*****************************************************************************/ 

/*
 * STATES:
 *
 * there are three types of states
 *
 * - the port state (p_state): used for current call state
 * - the ss5 state (p_m_s_state): used for current tone
 * - the ss5 signal state (p_m_s_signal): used for current signal state of current tone
 *
 * the port state differs from isdn state:
 * 
 * - PORT_STATE_IDLE: used until number is complete. outgoing overlap dialing is received in this state.
 * - PORT_STATE_OUT_SETUP: the seizing procedure is started.
 * - PORT_STATE_OUT_OVERLAP: the transmitter is sending the digits.
 * - PORT_STATE_OUT_PROCEEDING: the digits are sent, we wait until someone answers.
 * - PORT_STATE_CONNECT: a call is answered on either side.
 * - PORT_STATE_IN_SETUP: the seizing is received, we wait for the first digit.
 * - PORT_STATE_IN_OVERLAP: the digits are received.
 * - PORT_STATE_IN_PROCEEDING: the number is complete, a SETUP is indicated.
 * - PORT_STATE_IN_DISCONNECT: a clear-back was received, an DISCONNECT is indicated.
 * - PORT_STATE_RELEASE: the clear forward procedure is started.
 *
 */

#include "main.h"

//#define DEBUG_DETECT

/* ss5 signal states */
enum {
	SS5_STATE_IDLE,			/* no signal */
	SS5_STATE_SEIZING,		/* seizing */
	SS5_STATE_PROCEED_TO_SEND,	/* proceed-to-send */
	SS5_STATE_BUSY_FLASH,		/* busy-flash / clear back */
	SS5_STATE_ACK_BUSY_FLASH,	/* acknowledge of busy/answer/clear-back */
	SS5_STATE_ANSWER,		/* answer */
	SS5_STATE_ACK_ANSWER,		/* acknowledge of busy/answer/clear-back */
	SS5_STATE_FORWARD_TRANSFER,	/* forward transfer */
	SS5_STATE_CLEAR_BACK,		/* clear-back */
	SS5_STATE_ACK_CLEAR_BACK,	/* acknowledge of busy/answer/clear-back */
	SS5_STATE_CLEAR_FORWARD,	/* clear-forward */
	SS5_STATE_RELEASE_GUARD,	/* release-guard */
	SS5_STATE_DIAL_OUT,		/* dialing state (transmitter) */
	SS5_STATE_DIAL_IN,		/* dialing state (receiver) */
	SS5_STATE_DIAL_IN_PULSE,	/* dialing state (receiver with pulses) */
	SS5_STATE_DELAY,		/* after signal wait until next signal can be sent */
	SS5_STATE_DOUBLE_SEIZE,		/* in case of a double seize, we make the remote size recognize it */
};
const char *ss5_state_name[] = {
	"STATE_IDLE",
	"STATE_SEIZING",
	"STATE_PROCEED_TO_SEND",
	"STATE_BUSY_FLASH",
	"STATE_ACK_BUSY_FLASH",
	"STATE_ANSWER",
	"STATE_ACK_ANSWER",
	"STATE_FORWARD_TRANSFER",
	"STATE_CLEAR_BACK",
	"STATE_ACK_CLEAR_BACK",
	"STATE_CLEAR_FORWARD",
	"STATE_RELEASE_GUARD",
	"STATE_DIAL_OUT",
	"STATE_DIAL_IN",
	"STATE_DIAL_IN_PULSE",
	"STATE_DELAY",
	"STATE_DOUBLE_SEIZE",
};

enum {
	SS5_SIGNAL_NULL,
	/* sending signal states */
	SS5_SIGNAL_SEND_ON,		/* sending signal, waiting for acknowledge */
	SS5_SIGNAL_SEND_ON_RECOG,	/* sending signal, receiving ack, waiting for recogition timer */
	SS5_SIGNAL_SEND_OFF,		/* silence, receiving ack, waiting for stop */
	/* receiving signal states */
	SS5_SIGNAL_RECEIVE_RECOG,	/* receiving signal, waiting for recognition timer */
	SS5_SIGNAL_RECEIVE,		/* receiving signal / send ack, waiting for stop */
	SS5_SIGNAL_DELAY,		/* delay after release guard to prevent ping-pong */
	/* sending / receiving digit states */
	SS5_SIGNAL_DIGIT_PAUSE,		/* pausing before sending (next) digit */
	SS5_SIGNAL_DIGIT_ON,		/* sending digit */
	SS5_SIGNAL_PULSE_OFF,		/* make */
	SS5_SIGNAL_PULSE_ON,		/* break */
};
const char *ss5_signal_name[] = {
	"NULL",
	"SIGNAL_SEND_ON",
	"SIGNAL_SEND_ON_RECOG",
	"SIGNAL_SEND_OFF",
	"SIGNAL_RECEIVE_RECOG",
	"SIGNAL_RECEIVE",
	"SIGNAL_DELAY",
	"SIGNAL_DIGIT_PAUSE",
	"SIGNAL_DIGIT_ON",
	"SIGNAL_PULSE_OFF",
	"SIGNAL_PULSE_ON",
};

/* ss5 signal timers (in samples) */
#define	SS5_TIMER_AFTER_SIGNAL	(100*8)	/* wait after signal is terminated */
#define SS5_TIMER_KP		(100*8)	/* duration of KP1 or KP2 digit */
#define SS5_TIMER_DIGIT		(55*8)	/* duration of all other digits */
#define SS5_TIMER_PAUSE		(55*8)	/* pause between digits */
#define SS5_TIMER_FORWARD	(850*8)	/* forward transfer length */
#define SS5_TIMER_RECOG_SEIZE	(40*8)	/* recognition time of seizing / proceed-to-send signal */
#define SS5_TIMER_RECOG_OTHER	(125*8)	/* recognition time of all other f1/f2 signals */
#define SS5_TIMER_SIGNAL_LOSS	(15*8)	/* minimum time of signal loss for a continous signal */
#define SS5_TIMER_DOUBLE_SEIZE	(850*8)	/* double seize length */
#define SS5_TIMER_RELEASE_GUARD	(850*8)	/* be sure to release after clear-forward */
#define SS5_TIMER_RELEASE_MAX	(2000*8)/* maximum time for release guard to prevent 'double-releasing' */
#define	SS5_TIMER_RELEASE_DELAY	(4000*8)/* wait after release guard to prevent ping-pong */
#define BELL_TIMER_BREAK	(50*8)	/* loop open, tone */
#define BELL_TIMER_MAKE		(50*8)	/* loop closed, no tone */
#define BELL_TIMER_PAUSE	(800*8)	/* interdigit delay */
#define BELL_TIMER_RECOG_HANGUP	(200*8) /* time to recognize hangup */
#define BELL_TIMER_RECOG_END	(300*8) /* recognize end of digit */

/* ss5 timers */
#define SS5_TIMER_OVERLAP	5.0	/* timeout for overlap digits received on outgoing exchange */


/*
 * ss5 trace header
 */
enum { /* even values are indications, odd values are requests */
	SS5_SEIZING_IND,
	SS5_SEIZING_REQ,
	SS5_PROCEED_TO_SEND_IND,
	SS5_PROCEED_TO_SEND_REQ,
	SS5_BUSY_FLASH_IND,
	SS5_BUSY_FLASH_REQ,
	SS5_ANSWER_IND,
	SS5_ANSWER_REQ,
	SS5_CLEAR_BACK_IND,
	SS5_CLEAR_BACK_REQ,
	SS5_CLEAR_FORWARD_IND,
	SS5_CLEAR_FORWARD_REQ,
	SS5_RELEASE_GUARD_IND,
	SS5_RELEASE_GUARD_REQ,
	SS5_ACKNOWLEDGE_IND,
	SS5_ACKNOWLEDGE_REQ,
	SS5_DOUBLE_SEIZURE_IND,
	SS5_DOUBLE_SEIZURE_REQ,
	SS5_DIALING_IND,
	SS5_DIALING_REQ,
	SS5_FORWARD_TRANSFER_IND,
	SS5_FORWARD_TRANSFER_REQ,
};
static struct isdn_message {
	const char *name;
	unsigned int value;
} ss5_message[] = {
	{"SEIZING RECEIVED", SS5_SEIZING_IND},
	{"SEIZING SENDING", SS5_SEIZING_REQ},
	{"PROCEED-TO-SEND RECEIVED", SS5_PROCEED_TO_SEND_IND},
	{"PROCEED-TO-SEND SENDING", SS5_PROCEED_TO_SEND_REQ},
	{"BUSY-FLASH RECEIVED", SS5_BUSY_FLASH_IND},
	{"BUSY-FLASH SENDING", SS5_BUSY_FLASH_REQ},
	{"ANSWER RECEIVED", SS5_ANSWER_IND},
	{"ANSWER SENDING", SS5_ANSWER_REQ},
	{"CLEAR-BACK RECEIVED", SS5_CLEAR_BACK_IND},
	{"CLEAR-BACK SENDING", SS5_CLEAR_BACK_REQ},
	{"CLEAR-FORWARD RECEIVED", SS5_CLEAR_FORWARD_IND},
	{"CLEAR-FORWARD SENDING", SS5_CLEAR_FORWARD_REQ},
	{"RELEASE-GUARD RECEIVED", SS5_RELEASE_GUARD_IND},
	{"RELEASE-GUARD SENDING", SS5_RELEASE_GUARD_REQ},
	{"ACKNOWLEDGE RECEIVED", SS5_ACKNOWLEDGE_IND},
	{"ACKNOWLEDGE SENDING", SS5_ACKNOWLEDGE_REQ},
	{"DOUBLE-SEIZURE RECEIVED", SS5_DOUBLE_SEIZURE_IND},
	{"DOUBLE-SEIZURE SENDING", SS5_DOUBLE_SEIZURE_REQ},
	{"DIALING RECEIVED", SS5_DIALING_IND},
	{"DIALING SENDING", SS5_DIALING_REQ},
	{"FORWARD-TRANSFER RECEIVED", SS5_FORWARD_TRANSFER_IND},
	{"FORWARD-TRANSFER SENDING", SS5_FORWARD_TRANSFER_REQ},
	{NULL, 0},
};
static void ss5_trace_header(struct mISDNport *mISDNport, class PmISDN *port, unsigned int msg, int channel)
{
	int i;
	char msgtext[64];

	SCPY(msgtext, "<<UNKNOWN MESSAGE>>");
	/* select message and primitive text */
	i = 0;
	while(ss5_message[i].name) {
//		if (msg == L3_NOTIFY_REQ) printf("val = %x %s\n", isdn_message[i].value, isdn_message[i].name);
		if (ss5_message[i].value == msg) {
			SCPY(msgtext, ss5_message[i].name);
			break;
		}
		i++;
	}

	/* init trace with given values */
	start_trace(mISDNport?mISDNport->portnum:-1,
		    mISDNport?(mISDNport->ifport?mISDNport->ifport->interface:NULL):NULL,
		    port?numberrize_callerinfo(port->p_callerinfo.id, port->p_callerinfo.ntype, options.national, options.international):NULL,
		    port?port->p_dialinginfo.id:NULL,
		    (msg&1)?DIRECTION_OUT:DIRECTION_IN,
		    CATEGORY_CH,
		    port?port->p_serial:0,
		    msgtext);
	add_trace("channel", NULL, "%d", channel);
	switch (port->p_type) {
		case PORT_TYPE_SS5_OUT:
		add_trace("state", NULL, "outgoing");
		break;
		case PORT_TYPE_SS5_IN:
		add_trace("state", NULL, "incomming");
		break;
		default:
		add_trace("state", NULL, "idle");
		break;
	}
}


/*
 * changes release tone into silence
 * this makes the line sound more authentic
 */
void Pss5::set_tone(const char *dir, const char *name)
{
	if (name && !strcmp(name, "cause_10"))
		name = NULL;

	PmISDN::set_tone(dir, name);
}

/*
 * creation of static channels
 */
void ss5_create_channel(struct mISDNport *mISDNport, int i)
{
	class Pss5		*ss5port;
	char			portname[32];
	struct port_settings	port_settings;

	SPRINT(portname, "%s-%d", mISDNport->name, i+1);

	memset(&port_settings, 0, sizeof(port_settings));
	SCPY(port_settings.tones_dir, options.tones_dir);

	ss5port = new Pss5(PORT_TYPE_SS5_IDLE, mISDNport, portname, &port_settings, i + (i>=15) + 1, 1, B_MODE_TRANSPARENT);
	if (!ss5port)
		FATAL("No memory for Pss5 class.\n");
	if (!ss5port->p_m_b_channel)
		FATAL("No bchannel on given index.\n");

	/* connect channel */
	bchannel_event(mISDNport, ss5port->p_m_b_index, B_EVENT_USE);

}


/*
 * hunt for a free line
 * this function returns a port object in idle state.
 */
class Pss5 *ss5_hunt_line(struct mISDNport *mISDNport)
{
	int i;
	class Port	*port;
	class Pss5	*ss5port = NULL;
	struct select_channel *selchannel; 

	PDEBUG(DEBUG_SS5, "Entered name=%s\n", mISDNport->name);
	selchannel = mISDNport->ifport->out_channel;
	while(selchannel) {
		switch(selchannel->channel) {
			case CHANNEL_FREE: /* free channel */
			case CHANNEL_ANY: /* any channel */
			for (i = 0; i < mISDNport->b_num; i++) {
				port = mISDNport->b_port[i];
				PDEBUG(DEBUG_SS5, "Checking port %p on index\n", port, i);
				if (!port)
					continue;
				if (port->p_type == PORT_TYPE_SS5_IN || port->p_type == PORT_TYPE_SS5_OUT)
					PDEBUG(DEBUG_SS5, "Checking port %s: channel %d not available, because port not idle type.\n",
						mISDNport->name, i);
				if (port->p_type != PORT_TYPE_SS5_IDLE)
					continue;
				ss5port = (class Pss5 *)port;
				/* is really idle ? */
				if (ss5port->p_state == PORT_STATE_IDLE
				 && ss5port->p_m_s_state == SS5_STATE_IDLE)
					return ss5port;
				PDEBUG(DEBUG_SS5, "Checking port %s: channel %d not available, because p_state=%d, ss5_state=%d.\n",
					mISDNport->name, i, ss5port->p_state,ss5port->p_m_s_state);
			}
			PDEBUG(DEBUG_SS5, "no free interface\n");
			return NULL;

			case CHANNEL_NO:
			break;

			default:
			if (selchannel->channel<1 || selchannel->channel==16)
				break;
			i = selchannel->channel-1-(selchannel->channel>=17);
			if (i >= mISDNport->b_num)
				break;
			port = mISDNport->b_port[i];
			if (!port)
				break;
			if (port->p_type == PORT_TYPE_SS5_IN || port->p_type == PORT_TYPE_SS5_OUT)
				PDEBUG(DEBUG_SS5, "Checking port %s: channel %d not available, because port not idle type.\n",
					mISDNport->name, i);
			if (port->p_type != PORT_TYPE_SS5_IDLE)
				break;
			ss5port = (class Pss5 *)port;
			/* is really idle ? */
			if (ss5port->p_state == PORT_STATE_IDLE
			 && ss5port->p_m_s_state == SS5_STATE_IDLE)
				return ss5port;
			PDEBUG(DEBUG_SS5, "Checking port %s: channel %d not available, because p_state=%d, ss5_state=%d.\n",
				mISDNport->name, i, ss5port->p_state,ss5port->p_m_s_state);
		}
		selchannel = selchannel->next;
	}
	PDEBUG(DEBUG_SS5, "no free interface in channel list\n");
	return NULL;
}


/*
 * constructor
 */
Pss5::Pss5(int type, struct mISDNport *mISDNport, char *portname, struct port_settings *settings, int channel, int exclusive, int mode) : PmISDN(type, mISDNport, portname, settings, channel, exclusive, mode)
{
	p_callerinfo.itype = (mISDNport->ifport->interface->extension)?INFO_ITYPE_ISDN_EXTENSION:INFO_ITYPE_ISDN;
	p_m_s_state = SS5_STATE_IDLE;
	p_m_s_signal = SS5_SIGNAL_NULL;
	p_m_s_dial[0] = '\0';
	p_m_s_digit_i = 0;
	p_m_s_pulsecount = 0;
	p_m_s_last_digit = ' ';
	p_m_s_last_digit_used = ' ';
	p_m_s_signal_loss = 0;
	p_m_s_decoder_count = 0;
	//p_m_s_decoder_buffer;
	p_m_s_sample_nr = 0;
	p_m_s_recog = 0;
	p_m_s_timer = 0.0;
	p_m_s_timer_fn = NULL;
	p_m_s_answer = 0;
	p_m_s_busy_flash = 0;
	p_m_s_clear_back = 0;
	memset(p_m_s_delay_digits, ' ', sizeof(p_m_s_delay_digits));
	memset(p_m_s_delay_mute, ' ', sizeof(p_m_s_delay_mute));

	/* turn on signalling receiver */
	inband_receive_on();

	PDEBUG(DEBUG_SS5, "Created new mISDNPort(%s). Currently %d objects use.\n", portname, mISDNport->use);
}


/*
 * destructor
 */
Pss5::~Pss5()
{
}


/*
 * change ss5 states
 */
void Pss5::_new_ss5_state(int state, const char *func, int line)
{
	PDEBUG(DEBUG_SS5, "%s(%s:%d): changing SS5 state from %s to %s\n", p_name, func, line, ss5_state_name[p_m_s_state], ss5_state_name[state]);
	p_m_s_state = state;
	p_m_s_signal = SS5_SIGNAL_NULL;
}
void Pss5::_new_ss5_signal(int signal, const char *func, int line)
{
	if (p_m_s_signal)
		PDEBUG(DEBUG_SS5, "%s(%s:%d): changing SS5 signal state from %s to %s\n", p_name, func, line, ss5_signal_name[p_m_s_signal], ss5_signal_name[signal]);
	else
		PDEBUG(DEBUG_SS5, "%s(%s:%d): changing SS5 signal state to %s\n", p_name, func, line, ss5_signal_name[signal]);
	p_m_s_signal = signal;
}


/*
 * signalling receiver
 *
 * this function will be called for every audio received.
 */
void Pss5::inband_receive(unsigned char *buffer, int len)
{
	int count = 0, tocopy, space;
	char digit;

	again:
	/* how much to copy ? */
	tocopy = len - count;
	space = SS5_DECODER_NPOINTS - p_m_s_decoder_count;
	if (space < 0)
		FATAL("p_m_s_decoder_count overflows\n");
	if (space < tocopy)
		tocopy = space;
	/* copy an count */
	memcpy(p_m_s_decoder_buffer+p_m_s_decoder_count, buffer+count, tocopy);
	p_m_s_decoder_count += tocopy;
	count += tocopy;
	/* decoder buffer not completely filled ? */
	if (tocopy < space)
		return;

	/* decode one frame */
	digit = ss5_decode(p_m_s_decoder_buffer, SS5_DECODER_NPOINTS);
	p_m_s_decoder_count = 0;

#ifdef DEBUG_DETECT
	if (p_m_s_last_digit != digit && digit != ' ')
		PDEBUG(DEBUG_SS5, "%s: detecting signal '%c' start (state=%s signal=%s)\n", p_name, digit, ss5_state_name[p_m_s_state], ss5_signal_name[p_m_s_signal]);
#endif

	/* ignore short loss of signal, or change within one decode window */
	if (p_m_s_signal_loss) {
		if (digit == ' ') {
			/* still lost */
			if (p_m_s_signal_loss >= SS5_TIMER_SIGNAL_LOSS) {
#ifdef DEBUG_DETECT
				PDEBUG(DEBUG_SS5, "%s: signal '%c' lost too long\n", p_name, p_m_s_last_digit);
#endif
				/* long enough, we stop loss-timer */
				p_m_s_signal_loss = 0;
			} else {
				/* not long enough, so we use last signal */
				p_m_s_signal_loss += SS5_DECODER_NPOINTS;
				digit = p_m_s_last_digit;
			}
		} else {
			/* signal is back, we stop timer and store */
#ifdef DEBUG_DETECT
			PDEBUG(DEBUG_SS5, "%s: signal '%c' lost, but continues with '%c'\n", p_name, p_m_s_last_digit, digit);
#endif
			p_m_s_signal_loss = 0;
			p_m_s_last_digit = digit;
		}
	} else {
		if (p_m_s_last_digit != ' ' && digit == ' ') {
#ifdef DEBUG_DETECT
			PDEBUG(DEBUG_SS5, "%s: signal '%c' lost\n", p_name, p_m_s_last_digit);
#endif
			/* restore last digit until signal is really lost */
			p_m_s_last_digit = digit;
			/* starting to loose signal */
			p_m_s_signal_loss = SS5_DECODER_NPOINTS;
		} else if (digit != p_m_s_last_digit) {
			/* digit changes, but we keep old digit until it is detected twice */
#ifdef DEBUG_DETECT
			PDEBUG(DEBUG_SS5, "%s: signal '%c' changes to '%c'\n", p_name, p_m_s_last_digit, digit);
#endif
			p_m_s_last_digit = digit;
			digit = p_m_s_last_digit_used;
		} else {
			/* storing last signal, in case it is lost */
			p_m_s_last_digit = digit;
		}
	}
	p_m_s_last_digit_used = digit;

	/* update mute */
	if ((p_m_mISDNport->ss5 & SS5_FEATURE_SUPPRESS)) {
		int mdigit;
		memcpy(p_m_s_delay_mute, p_m_s_delay_mute+1, sizeof(p_m_s_delay_mute)-1);
		p_m_s_delay_mute[sizeof(p_m_s_delay_mute)-1] = digit;
		mdigit = p_m_s_delay_mute[0];
		if (p_m_mute) {
			/* mute is on */
			if (mdigit != 'A' && mdigit != 'B' && mdigit != 'C')
				mute_off();
		} else {
			/* mute is off */
			if (mdigit == 'A' || mdigit == 'B' || mdigit == 'C')
				mute_on();
		}
	}

	/* delay decoded tones */
	if ((p_m_mISDNport->ss5 & SS5_FEATURE_DELAY)) {
		/* shift buffer */
		memcpy(p_m_s_delay_digits, p_m_s_delay_digits+1, sizeof(p_m_s_delay_digits)-1);
		/* first in */
		p_m_s_delay_digits[sizeof(p_m_s_delay_digits)-1] = digit;
		/* first out */
		digit = p_m_s_delay_digits[0];
	}

	/* clear forward is always recognized */
	if (digit == 'C' && p_m_s_state != SS5_STATE_CLEAR_FORWARD && p_m_s_state != SS5_STATE_RELEASE_GUARD) {
		switch (p_type) {
			case PORT_TYPE_SS5_OUT:
			PDEBUG(DEBUG_SS5, "%s: received release-guard, waiting for recognition\n", p_name);
			break;
			case PORT_TYPE_SS5_IN:
			PDEBUG(DEBUG_SS5, "%s: received clear-forward, waiting for recognition\n", p_name);
			break;
			default:
			PDEBUG(DEBUG_SS5, "%s: received clear-forward in idle state, waiting for recognition\n", p_name);
			break;
		}
		new_ss5_state(SS5_STATE_RELEASE_GUARD);
		new_ss5_signal(SS5_SIGNAL_RECEIVE_RECOG);
		p_m_s_recog = 0;
	} else
	switch(p_m_s_state) {
		case SS5_STATE_IDLE:
		/* seizing only recognized in port idle state */
		if (p_state == PORT_STATE_IDLE) {
			if (digit != 'A')
				break;
			seize:
			PDEBUG(DEBUG_SS5, "%s: received seize, waiting for recognition\n", p_name);
			p_type = PORT_TYPE_SS5_IN;
			new_ss5_state(SS5_STATE_PROCEED_TO_SEND);
			new_ss5_signal(SS5_SIGNAL_RECEIVE_RECOG);
			p_m_s_recog = 0;
			break;
		}
		/* other signals */
		if (digit == 'A') {
			if (p_type != PORT_TYPE_SS5_OUT)
				break;
			PDEBUG(DEBUG_SS5, "%s: received answer, waiting for recognition\n", p_name);
			new_ss5_state(SS5_STATE_ACK_ANSWER);
			new_ss5_signal(SS5_SIGNAL_RECEIVE_RECOG);
			p_m_s_recog = 0;
			break;
		}
		if (digit == 'B') {
			if (p_type == PORT_TYPE_SS5_IN) {
				if ((p_m_mISDNport->ss5 & SS5_FEATURE_BELL)) {
					new_ss5_state(SS5_STATE_DIAL_IN_PULSE); /* go pulsing state */
					new_ss5_signal(SS5_SIGNAL_PULSE_OFF); /* we are starting with pulse off */
					p_m_s_pulsecount = 0; /* init pulse counter */
					p_m_s_dial[0] = '\0'; /* init dial string */
					pulse_ind(1); /* also inits recogition timer... */
					break;
				}
				PDEBUG(DEBUG_SS5, "%s: received forward-transfer, waiting for recognition\n", p_name);
				/* forward transfer on incomming lines */
				new_ss5_state(SS5_STATE_FORWARD_TRANSFER);
				new_ss5_signal(SS5_SIGNAL_RECEIVE_RECOG);
				p_m_s_recog = 0;
				break;
			}
			if (p_state == PORT_STATE_CONNECT) {
				PDEBUG(DEBUG_SS5, "%s: received clear-back, waiting for recognition\n", p_name);
				new_ss5_state(SS5_STATE_ACK_CLEAR_BACK);
			} else {
				PDEBUG(DEBUG_SS5, "%s: received busy-flash, waiting for recognition\n", p_name);
				new_ss5_state(SS5_STATE_ACK_BUSY_FLASH);
			}
			new_ss5_signal(SS5_SIGNAL_RECEIVE_RECOG);
			p_m_s_recog = 0;
			break;
		}
		/* dialing only allowed in incomming setup state */
		if (p_state == PORT_STATE_IN_SETUP) {
			if (!strchr("1234567890*#abc", digit))
				break;
			PDEBUG(DEBUG_SS5, "%s: received dialing start with '%c'\n", p_name, digit);
			new_ss5_state(SS5_STATE_DIAL_IN);
			new_ss5_signal(SS5_SIGNAL_DIGIT_ON);
			p_m_s_dial[0] = '\0';
			digit_ind(digit);
			break;
		}
		break;
		/* sending seizing */
		case SS5_STATE_SEIZING:
		switch (p_m_s_signal) {
			case SS5_SIGNAL_SEND_ON:
			if (digit == 'A') { /* double seize */
				PDEBUG(DEBUG_SS5, "%s: received double seizure\n", p_name, digit);
				double_seizure_ind();
				break;
			}
			if (digit == 'B') {
				PDEBUG(DEBUG_SS5, "%s: received answer to outgoing seize, waiting for recognition\n", p_name);
				/* set recognition timer */
				new_ss5_signal(SS5_SIGNAL_SEND_ON_RECOG);
				p_m_s_recog = 0;
			}
			break;
			case SS5_SIGNAL_SEND_ON_RECOG:
			if (digit != 'B') { /* seize */
				PDEBUG(DEBUG_SS5, "%s: answer to outgoing seize is gone before recognition\n", p_name);
				new_ss5_signal(SS5_SIGNAL_SEND_ON);
//				p_m_s_sample_nr = 0;
//				inband_send_on();
				break;
			}
			p_m_s_recog += SS5_DECODER_NPOINTS;
			if (p_m_s_recog < SS5_TIMER_RECOG_SEIZE)
				break;
			PDEBUG(DEBUG_SS5, "%s: answer to outgoing seize recognized, turning off, waiting for recognition\n", p_name);
			new_ss5_signal(SS5_SIGNAL_SEND_OFF);
			break;
			case SS5_SIGNAL_SEND_OFF:
			if (digit == 'B')
				break;
			PDEBUG(DEBUG_SS5, "%s: outgoing seizure is complete, proceeding...\n", p_name);
			new_ss5_state(SS5_STATE_IDLE);
			proceed_to_send_ind();
			break;
		}
		break;
		/* answer to seize */
		case SS5_STATE_PROCEED_TO_SEND:
		if (p_m_s_signal == SS5_SIGNAL_RECEIVE_RECOG) {
			if (digit != 'A') {
				PDEBUG(DEBUG_SS5, "%s: incomming seize is gone before recognition\n", p_name);
				new_ss5_state(SS5_STATE_IDLE);
				p_type = PORT_TYPE_SS5_IDLE;
				break;
			}
			p_m_s_recog += SS5_DECODER_NPOINTS;
			if (p_m_s_recog < SS5_TIMER_RECOG_SEIZE)
				break;
			PDEBUG(DEBUG_SS5, "%s: incomming seize is recognized, responding...\n", p_name);
			new_ss5_signal(SS5_SIGNAL_RECEIVE);
			p_m_s_sample_nr = 0;
			inband_send_on();
			break;
		}
		if (digit != 'A') {
			PDEBUG(DEBUG_SS5, "%s: incomming seize is gone after responding\n", p_name);
			new_ss5_state(SS5_STATE_IDLE);
			seizing_ind();
		}
		break;
		/* sending busy flash / answer / clear-back */
		case SS5_STATE_BUSY_FLASH:
		case SS5_STATE_ANSWER:
		case SS5_STATE_CLEAR_BACK:
		switch (p_m_s_signal) {
			case SS5_SIGNAL_SEND_ON:
			if (digit == 'A') {
				PDEBUG(DEBUG_SS5, "%s: received acknowledge, waiting for recognition\n", p_name);
				/* set recognition timer */
				new_ss5_signal(SS5_SIGNAL_SEND_ON_RECOG);
				p_m_s_recog = 0;
			}
			break;
			case SS5_SIGNAL_SEND_ON_RECOG:
			if (digit != 'A') {
				PDEBUG(DEBUG_SS5, "%s: acknowledge is gone before recognition\n", p_name);
				new_ss5_signal(SS5_SIGNAL_SEND_ON);
//				p_m_s_sample_nr = 0;
//				inband_send_on();
				break;
			}
			p_m_s_recog += SS5_DECODER_NPOINTS;
			if (p_m_s_recog < SS5_TIMER_RECOG_OTHER)
				break;
			PDEBUG(DEBUG_SS5, "%s: acknowledge recognized, turning off, waiting for recognition\n", p_name);
			new_ss5_signal(SS5_SIGNAL_SEND_OFF);
			break;
			case SS5_SIGNAL_SEND_OFF:
			if (digit == 'A')
				break;
			PDEBUG(DEBUG_SS5, "%s: outgoing signal is complete\n", p_name);
			new_ss5_state(SS5_STATE_IDLE);
			break;
		}
		break;
		/* answer to busy-flash / clear back */
		case SS5_STATE_ACK_BUSY_FLASH:
		case SS5_STATE_ACK_CLEAR_BACK:
		if (p_m_s_signal == SS5_SIGNAL_RECEIVE_RECOG) {
			if (digit != 'B') {
				PDEBUG(DEBUG_SS5, "%s: incomming clear-back/busy-flash is gone before recognition\n", p_name);
				new_ss5_state(SS5_STATE_IDLE);
				break;
			}
			p_m_s_recog += SS5_DECODER_NPOINTS;
			if (p_m_s_recog < SS5_TIMER_RECOG_OTHER)
				break;
			PDEBUG(DEBUG_SS5, "%s: incomming clear-back/busy-flash is recognized, responding...\n", p_name);
			new_ss5_signal(SS5_SIGNAL_RECEIVE);
			p_m_s_sample_nr = 0;
			inband_send_on();
			break;
		}
		if (digit != 'B') {
			PDEBUG(DEBUG_SS5, "%s: incomming clear-back/busy-flash is gone after responding\n", p_name);
			new_ss5_state(SS5_STATE_IDLE);
			if (p_m_s_state == SS5_STATE_ACK_BUSY_FLASH)
				busy_flash_ind();
			else
				clear_back_ind();
		}
		break;
		/* answer to answer */
		case SS5_STATE_ACK_ANSWER:
		if (p_m_s_signal == SS5_SIGNAL_RECEIVE_RECOG) {
			if (digit != 'A') {
				PDEBUG(DEBUG_SS5, "%s: incomming answer is gone before recognition\n", p_name);
				new_ss5_state(SS5_STATE_IDLE);
				break;
			}
			p_m_s_recog += SS5_DECODER_NPOINTS;
			if (p_m_s_recog < SS5_TIMER_RECOG_OTHER)
				break;
			PDEBUG(DEBUG_SS5, "%s: incomming answer is recognized, responding...\n", p_name);
			new_ss5_signal(SS5_SIGNAL_RECEIVE);
			p_m_s_sample_nr = 0;
			inband_send_on();
			break;
		}
		if (digit != 'A') {
			PDEBUG(DEBUG_SS5, "%s: incomming answer is gone after responding\n", p_name);
			new_ss5_state(SS5_STATE_IDLE);
			answer_ind();
		}
		break;
		/* sending clear-forward */
		case SS5_STATE_CLEAR_FORWARD:
		switch (p_m_s_signal) {
			case SS5_SIGNAL_SEND_ON:
			if (digit == 'C') {
				PDEBUG(DEBUG_SS5, "%s: received answer to clear-forward, waiting for recognition\n", p_name);
				/* set recognition timer */
				new_ss5_signal(SS5_SIGNAL_SEND_ON_RECOG);
				p_m_s_recog = 0;
			}
			break;
			case SS5_SIGNAL_SEND_ON_RECOG:
			if (digit != 'C') {
				PDEBUG(DEBUG_SS5, "%s: answer to clear-forward is gone before recognition\n", p_name);
				new_ss5_signal(SS5_SIGNAL_SEND_ON);
//				p_m_s_sample_nr = 0;
//				inband_send_on();
				break;
			}
			p_m_s_recog += SS5_DECODER_NPOINTS;
			if (p_m_s_recog < SS5_TIMER_RECOG_OTHER)
				break;
			PDEBUG(DEBUG_SS5, "%s: answer to clear-forward recognized, turning off, waiting for recognition\n", p_name);
			new_ss5_signal(SS5_SIGNAL_SEND_OFF);
			break;
			case SS5_SIGNAL_SEND_OFF:
			if (digit == 'A') {
				PDEBUG(DEBUG_SS5, "%s: received seize right after clear-forward answer, continue with seize\n", p_name);
				new_state(PORT_STATE_IDLE);
				goto seize;
			}
			if (digit == 'C')
				break;
			PDEBUG(DEBUG_SS5, "%s: answer to clear-forward is complete\n", p_name);
			release_guard_ind();
			new_ss5_signal(SS5_SIGNAL_DELAY);
			p_m_s_recog = 0; /* use recog to delay */
			PDEBUG(DEBUG_SS5, "%s: answer to clear-forward on outgoing interface starting delay to prevent ping-pong\n", p_name);
			break;
			case SS5_SIGNAL_DELAY:
			if (digit == 'A') {
				PDEBUG(DEBUG_SS5, "%s: received seize right after clear-forward answer, continue with seize\n", p_name);
				new_state(PORT_STATE_IDLE);
				goto seize;
			}
			p_m_s_recog += SS5_DECODER_NPOINTS;
			if (p_m_s_recog < SS5_TIMER_RELEASE_DELAY)
				break;
			PDEBUG(DEBUG_SS5, "%s: delay time over, going idle\n", p_name);
			new_ss5_state(SS5_STATE_IDLE);
			new_state(PORT_STATE_IDLE);
			p_type = PORT_TYPE_SS5_IDLE;
			break;
		}
		break;
		/* answer to release-guard*/
		case SS5_STATE_RELEASE_GUARD:
		switch (p_m_s_signal) {
			case SS5_SIGNAL_RECEIVE_RECOG:
			if (digit != 'C') {
				if (p_type == PORT_TYPE_SS5_OUT)
					PDEBUG(DEBUG_SS5, "%s: incomming release-guard is gone before recognition\n", p_name);
				else
					PDEBUG(DEBUG_SS5, "%s: incomming clear forward is gone before recognition\n", p_name);
				new_ss5_state(SS5_STATE_IDLE);
				break;
			}
			p_m_s_recog += SS5_DECODER_NPOINTS;
			if (p_m_s_recog < SS5_TIMER_RECOG_OTHER)
				break;
			if (p_type == PORT_TYPE_SS5_OUT)
				PDEBUG(DEBUG_SS5, "%s: incomming release-guard is recognized, responding...\n", p_name);
			else
				PDEBUG(DEBUG_SS5, "%s: incomming clear-forward is recognized, responding...\n", p_name);
			new_state(PORT_STATE_RELEASE);
			new_ss5_signal(SS5_SIGNAL_RECEIVE);
			p_m_s_sample_nr = 0;
			inband_send_on();
			break;
			case SS5_SIGNAL_RECEIVE:
			if (digit == 'C'
			 || p_m_s_sample_nr < 256) /* small hack to keep answer for at least some time */
			 break;
#if 0
			if (digit == 'A') {
				PDEBUG(DEBUG_SS5, "%s: received seize right after clear-forward is received\n", p_name);
				new_state(PORT_STATE_IDLE);
				goto seize;
			}
#endif
			/* if clear forward stops right after recognition on the incomming side,
			 * the release guard signal stops and may be too short to be recognized at the outgoing side.
			 * to prevent this, a timer can be started to force a release guard that is long
			 * enough to be recognized on the outgoing side.
			 * this will prevent braking via blueboxing (other tricks may still be possible).
			 */
			if ((p_m_mISDNport->ss5 & SS5_FEATURE_RELEASEGUARDTIMER)
			 && p_m_s_sample_nr < SS5_TIMER_RELEASE_GUARD)
				break;
			if (p_type == PORT_TYPE_SS5_OUT)
				PDEBUG(DEBUG_SS5, "%s: incomming release-guard is gone after responding\n", p_name);
			else
				PDEBUG(DEBUG_SS5, "%s: incomming clear-forward is gone after responding\n", p_name);
			if (p_type == PORT_TYPE_SS5_OUT) {
				release_guard_ind();
				new_ss5_signal(SS5_SIGNAL_DELAY);
				p_m_s_recog = 0; /* use recog to delay */
				PDEBUG(DEBUG_SS5, "%s: incomming release-guard on outgoing interface starting delay to prevent ping-pong\n", p_name);
			} else {
				clear_forward_ind();
				new_ss5_state(SS5_STATE_IDLE);
			}
			break;
			case SS5_SIGNAL_DELAY:
			if (digit == 'A') {
				PDEBUG(DEBUG_SS5, "%s: received seize right after release guard is gone, continue with seize\n", p_name);
				new_state(PORT_STATE_IDLE);
				goto seize;
			}
			p_m_s_recog += SS5_DECODER_NPOINTS;
			if (p_m_s_recog < SS5_TIMER_RELEASE_DELAY)
				break;
			PDEBUG(DEBUG_SS5, "%s: delay time over, going idle\n", p_name);
			new_ss5_state(SS5_STATE_IDLE);
			new_state(PORT_STATE_IDLE);
			p_type = PORT_TYPE_SS5_IDLE;
			break;
		}
		break;
		/* wait time to recognize forward transfer */
		case SS5_STATE_FORWARD_TRANSFER:
		if (p_m_s_signal == SS5_SIGNAL_RECEIVE_RECOG) {
			if (digit != 'B') {
				PDEBUG(DEBUG_SS5, "%s: incomming forward-transfer is gone before recognition\n", p_name);
				new_ss5_state(SS5_STATE_IDLE);
				break;
			}
			p_m_s_recog += SS5_DECODER_NPOINTS;
			if (p_m_s_recog < SS5_TIMER_RECOG_OTHER)
				break;
			PDEBUG(DEBUG_SS5, "%s: incomming forward-transfer is recognized, responding, if BELL feature was selected...\n", p_name);
			new_ss5_signal(SS5_SIGNAL_RECEIVE);
#if 0
			p_m_s_sample_nr = 0;
			inband_send_on();
#endif
			break;
		}
		if (digit != 'B') {
			PDEBUG(DEBUG_SS5, "%s: incomming forward-transfer is gone after recognition\n", p_name);
			new_ss5_state(SS5_STATE_IDLE);
			forward_transfer_ind();
			break;
		}
		break;
		/* dialing is received */
		case SS5_STATE_DIAL_IN:
		if (strchr("1234567890*#abc", digit)) {
			if (p_m_s_signal != SS5_SIGNAL_DIGIT_PAUSE)
				break;
			PDEBUG(DEBUG_SS5, "%s: incomming digit '%c' is recognized\n", p_name, digit);
			new_ss5_signal(SS5_SIGNAL_DIGIT_ON);
			digit_ind(digit);
		} else {
			if (p_m_s_signal != SS5_SIGNAL_DIGIT_ON)
				break;
			PDEBUG(DEBUG_SS5, "%s: incomming digit is gone after recognition\n", p_name);
			new_ss5_signal(SS5_SIGNAL_DIGIT_PAUSE);
		}
		break;
		case SS5_STATE_DIAL_IN_PULSE:
		if (digit == 'B')
			pulse_ind(1);
		else
			pulse_ind(0);
		break;
	}

	/* something more to decode ? */
	if (count != len)
		goto again;
}


/*
 * signalling sender
 *
 * this function generates tones and assembles dial string with digits and pause
 * the result is sent to mISDN. it uses the ss5_encode() function.
 * except for dialing and forward-transfer, tones are continuous and will not change state.
 */
int Pss5::inband_send(unsigned char *buffer, int len)
{
	int count = 0; /* sample counter */
	int duration;
	char digit;
	int tocode, tosend;

	switch(p_m_s_state) {
		/* turn off transmitter in idle state */
		case SS5_STATE_IDLE:
		inband_send_off();
		break;

		case SS5_STATE_SEIZING:
		if (p_m_s_signal != SS5_SIGNAL_SEND_ON
		 && p_m_s_signal != SS5_SIGNAL_SEND_ON_RECOG)
		 	break;
		duration = -1; /* continuous */
		digit = 'A';
		send:
		/* how much samples do we have left */
		if (duration < 0)
			tocode = len;
		else
			tocode = duration - p_m_s_sample_nr;
		if (tocode > 0) {
			if (tocode > len)
				tocode = len;
			ss5_encode(buffer, tocode, digit, p_m_s_sample_nr);
			/* increase counters */
			p_m_s_sample_nr += tocode;
			count += tocode;
		}
		/* more to come ? */
		if (duration > 0 && p_m_s_sample_nr >= duration) {
			PDEBUG(DEBUG_SS5, "%s: sending tone '%c' complete, starting delay\n", p_name, digit);
			if (p_m_s_state == SS5_STATE_DOUBLE_SEIZE) {
				do_release(CAUSE_NOCHANNEL, LOCATION_BEYOND);
				break;
			}
			new_ss5_state(SS5_STATE_DELAY);
			p_m_s_sample_nr = 0;
		}
#if 0
		/* stop sending if too long */
		if (duration < 0 && p_m_s_sample_nr >= SS5_TIMER_MAX_SIGNAL) {
			PDEBUG(DEBUG_SS5, "%s: sending tone '%c' too long, stopping\n", p_name, digit);
			inband_send_off();
			break;
		}
#endif
		break;
		/* incomming seizing */
		case SS5_STATE_PROCEED_TO_SEND:
		if (p_m_s_signal != SS5_SIGNAL_RECEIVE)
		 	break;
		duration = -1; /* continuous */
		digit = 'B';
		goto send;

		case SS5_STATE_BUSY_FLASH:
		case SS5_STATE_CLEAR_BACK:
		if (p_m_s_signal != SS5_SIGNAL_SEND_ON
		 && p_m_s_signal != SS5_SIGNAL_SEND_ON_RECOG)
		 	break;
		duration = -1; /* continuous */
		digit = 'B';
		goto send;

		case SS5_STATE_ANSWER:
		if (p_m_s_signal != SS5_SIGNAL_SEND_ON
		 && p_m_s_signal != SS5_SIGNAL_SEND_ON_RECOG)
		 	break;
		duration = -1; /* continuous */
		digit = 'A';
		goto send;

		case SS5_STATE_ACK_BUSY_FLASH:
		case SS5_STATE_ACK_ANSWER:
		case SS5_STATE_ACK_CLEAR_BACK:
		if (p_m_s_signal != SS5_SIGNAL_RECEIVE)
		 	break;
		duration = -1; /* continuous */
		digit = 'A';
		goto send;

#if 0
		case SS5_STATE_FORWARD_TRANSFER:
		if (p_m_s_signal != SS5_SIGNAL_RECEIVE)
		 	break;
		/* only on bell systems continue and acknowledge tone */
		if (!(p_m_mISDNport->ss5 & SS5_FEATURE_BELL))
			break;
		duration = SS5_TIMER_FORWARD;
		digit = 'B';
		goto send;
#endif

		case SS5_STATE_CLEAR_FORWARD:
		if (p_m_s_signal != SS5_SIGNAL_SEND_ON
		 && p_m_s_signal != SS5_SIGNAL_SEND_ON_RECOG)
		 	break;
		duration = -1; /* continuous */
		digit = 'C';
		goto send;

		case SS5_STATE_RELEASE_GUARD:
		if (p_m_s_signal != SS5_SIGNAL_RECEIVE
		 && p_m_s_signal != SS5_SIGNAL_DELAY)
		 	break;
		/* prevent from sending release guard too long */
		if (p_m_s_sample_nr >= SS5_TIMER_RELEASE_MAX)
			break;
		duration = -1; /* continuous */
		digit = 'C';
		goto send;

		case SS5_STATE_DIAL_OUT:
		if ((p_m_mISDNport->ss5 & SS5_FEATURE_PULSEDIALING))
			count = inband_dial_pulse(buffer, len, count);
		else
			count = inband_dial_mf(buffer, len, count);
		break;
		break;

		case SS5_STATE_DELAY:
		tosend = len - count;
		memset(buffer+count, audio_s16_to_law[0], tosend);
		p_m_s_sample_nr += tosend;
		count += tosend;
		if (p_m_s_sample_nr >= SS5_TIMER_AFTER_SIGNAL) {
			PDEBUG(DEBUG_SS5, "%s: delay done, ready for next signal\n", p_name);
			new_ss5_state(SS5_STATE_IDLE);
			inband_send_off();
		}
		break;

		case SS5_STATE_DOUBLE_SEIZE:
		duration = SS5_TIMER_DOUBLE_SEIZE;
		digit = 'A';
		goto send;

		/* nothing to send */
		default:
		PERROR("inband signalling is turned on, but no signal is processed here.");
		new_ss5_state(SS5_STATE_IDLE);
		inband_send_off();
		return 0;
	}

	/* return (partly) filled buffer */
	return count;
}


int Pss5::inband_dial_mf(unsigned char *buffer, int len, int count)
{
	int duration;
	int tocode, tosend;
	char digit;

	/* dialing
	 *
	 * p_m_s_dial: digits to be dialed
	 * p_m_s_digit_i: current digit counter
	 * p_m_s_signal: current signal state
	 * p_m_s_sample_nr: current sample number
	 */
	again:
	/* get digit and duration */
	digit = p_m_s_dial[p_m_s_digit_i];
	if (!digit) { /* if end of string reached */
		new_ss5_state(SS5_STATE_DELAY);
		p_m_s_sample_nr = 0;
		return count;
	}
	if (p_m_s_signal == SS5_SIGNAL_DIGIT_ON) {
		if (!p_m_s_digit_i) // first digit
			duration = SS5_TIMER_KP;
		else
			duration = SS5_TIMER_DIGIT;
	} else {
		duration = SS5_TIMER_PAUSE;
	}
	/* end of digit/pause ? */
	if (p_m_s_sample_nr >= duration) {
		p_m_s_sample_nr = 0;
		if (p_m_s_signal == SS5_SIGNAL_DIGIT_PAUSE)
			new_ss5_signal(SS5_SIGNAL_DIGIT_ON);
		else {
			new_ss5_signal(SS5_SIGNAL_DIGIT_PAUSE);
			p_m_s_digit_i++;
			goto again;
		}
	}
	/* how much samples do we have left */
	tosend = len - count;
	tocode = duration - p_m_s_sample_nr;
	if (tocode < 0)
		FATAL("sample_nr overrun duration");
	if (tosend < tocode)
		tocode = tosend;
	/* digit or pause */
	if (p_m_s_signal == SS5_SIGNAL_DIGIT_PAUSE) {
		memset(buffer+count, audio_s16_to_law[0], tocode);
//		printf("coding pause %d bytes\n", tocode);
	} else {
		ss5_encode(buffer+count, tocode, digit, p_m_s_sample_nr);
//		printf("coding digit '%c' %d bytes\n", digit, tocode);
	}
	/* increase counters */
	p_m_s_sample_nr += tocode;
	count += tocode;
	/* can we take more ? */
	if (len != count)
		goto again;
	return count;
}


int Pss5::inband_dial_pulse(unsigned char *buffer, int len, int count)
{
	int tocode, tosend;
	int duration;
	char digit;

	/* dialing
	 *
	 * p_m_s_dial: digits to be dialed
	 * p_m_s_digit_i: current digit counter
	 * p_m_s_signal: current signal state
	 * p_m_s_sample_nr: current sample number
	 */
	again:
	/* get digit */
	digit = p_m_s_dial[p_m_s_digit_i];
	if (!digit) { /* if end of string reached */
		new_ss5_state(SS5_STATE_DELAY);
		p_m_s_sample_nr = 0;
		return count;
	}
	/* convert digit to pulse */
	switch (digit) {
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
		digit -= '0';
		break;
		case '0':
		digit = 10;
		break;
		case '*':
		digit = 11;
		break;
		case '#':
		digit = 12;
		break;
		default:
		p_m_s_digit_i++;
		goto again;
	}
	/* get duration */
	if (p_m_s_signal == SS5_SIGNAL_DIGIT_ON) {
		if (p_m_s_pulsecount & 1)
			duration = BELL_TIMER_MAKE; /* loop closed */
		else
			duration = BELL_TIMER_BREAK; /* loop open, tone */
	} else {
		duration = BELL_TIMER_PAUSE;
	}
	/* end of digit/pause ? */
	if (p_m_s_sample_nr >= duration) {
		p_m_s_sample_nr = 0;
		if (p_m_s_signal == SS5_SIGNAL_DIGIT_PAUSE) {
			new_ss5_signal(SS5_SIGNAL_DIGIT_ON);
			PDEBUG(DEBUG_SS5, "%s: starting pusling digit '%c'\n", p_name, digit);
		} else {
			p_m_s_pulsecount++; /* toggle pulse */
			if (!(p_m_s_pulsecount & 1)) {
				/* pulse now on again, but if end is reached... */
				if (p_m_s_pulsecount == (digit<<1)) {
					new_ss5_signal(SS5_SIGNAL_DIGIT_PAUSE);
					p_m_s_pulsecount = 0;
					p_m_s_digit_i++;
					goto again;
				}
			}
		}
	}
	/* how much samples do we have left */
	tosend = len - count;
	tocode = duration - p_m_s_sample_nr;
	if (tocode < 0)
		FATAL("sample_nr overrun duration");
	if (tosend < tocode)
		tocode = tosend;
	/* digit or pause */
	if (p_m_s_signal == SS5_SIGNAL_DIGIT_PAUSE
	 || (p_m_s_pulsecount&1)) /* ...or currently on and no pulse */
		memset(buffer+count, audio_s16_to_law[0], tocode);
	else
		ss5_encode(buffer+count, tocode, 'B', p_m_s_sample_nr);
	/* increase counters */
	p_m_s_sample_nr += tocode;
	count += tocode;
	/* can we take more ? */
	if (len != count)
		goto again;
	return count;
}


/*
 * start signal
 */ 
void Pss5::start_signal(int state)
{
	PDEBUG(DEBUG_SS5, "%s: starting singal '%s'\n", p_name, ss5_state_name[state]);
	/* start signal */
	new_ss5_state(state);
	if (state == SS5_STATE_DIAL_OUT) {
		p_m_s_digit_i = 0;
		p_m_s_pulsecount = 0;
		new_ss5_signal(SS5_SIGNAL_DIGIT_ON);
	} else
		new_ss5_signal(SS5_SIGNAL_SEND_ON);

	/* double seize must continue the current seize tone, so don't reset sample_nr */
	if (state != SS5_STATE_DOUBLE_SEIZE) {
		/* (re)set sound phase to 0 */
		p_m_s_sample_nr = 0;
	}

	/* turn on inband transmitter */
	inband_send_on();
}


/*
 * handles all indications
 */
void Pss5::seizing_ind(void)
{
	ss5_trace_header(p_m_mISDNport, this, SS5_SEIZING_IND, p_m_b_channel);
	end_trace();

	new_state(PORT_STATE_IN_SETUP);
	set_tone("", "noise");
}

void Pss5::digit_ind(char digit)
{
	int i;
	char string[128] = "", dial[128] = "";
	int dash, first_digit, last_was_digit;

	/* add digit */
	SCCAT(p_m_s_dial, digit);

	if (p_state == PORT_STATE_IN_SETUP)
		new_state(PORT_STATE_IN_OVERLAP);

	/* not last digit ? */
	if (digit != 'c')
		return;

	/* parse string */
	dash = 0; /* dash must be used next time */
	first_digit = 1;
	last_was_digit = 0;
	p_dialinginfo.ntype = INFO_NTYPE_UNKNOWN;
	for (i = 0; p_m_s_dial[i]; i++) {
		if (dash || (last_was_digit && (p_m_s_dial[i]<'0' || p_m_s_dial[i]>'9')))
			SCCAT(string, '-');
		dash = 0;
		last_was_digit = 0;
		switch(p_m_s_dial[i]) {
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
			case '0':
			if (first_digit)
				dash = 1;
			first_digit = 0;
			last_was_digit = 1;
			SCCAT(string, p_m_s_dial[i]);
			SCCAT(dial, p_m_s_dial[i]);
			break;
			case '*':
			SCAT(string, "C11");
			SCCAT(dial, p_m_s_dial[i]);
			dash = 1;
			break;
			case '#':
			SCAT(string, "C12");
			SCCAT(dial, p_m_s_dial[i]);
			dash = 1;
			break;
			case 'a':
			SCAT(string, "KP1");
			SCCAT(dial, p_m_s_dial[i]);
			dash = 1;
			break;
			case 'b':
			SCAT(string, "KP2");
			SCCAT(dial, p_m_s_dial[i]);
			dash = 1;
			break;
			case 'c':
			SCAT(string, "ST");
			dash = 1;
			break;
			default:
			break;
		}
	}
	ss5_trace_header(p_m_mISDNport, this, SS5_DIALING_IND, p_m_b_channel);
	add_trace("string", NULL, "%s", string);
	add_trace("number", NULL, "%s", dial);
	end_trace();
	new_ss5_state(SS5_STATE_IDLE);

	do_setup(dial, 1);
	new_state(PORT_STATE_IN_PROCEEDING);
}

void Pss5::pulse_ind(int on)
{
	struct lcr_msg *message;
	char dial[3] = "a.";

	if (p_m_s_signal == SS5_SIGNAL_PULSE_OFF) {
		if (on) {
			/* pulse turns on */
			p_m_s_recog = 0;
			new_ss5_signal(SS5_SIGNAL_PULSE_ON);
			/* pulse turns of, count it */
			p_m_s_pulsecount++;
			PDEBUG(DEBUG_SS5, "%s: pulse turns on, counting\n", p_name);
		} else {
			/* pulse remains off */
			p_m_s_recog += SS5_DECODER_NPOINTS;
			/* not recognized end of digit, we wait... */
			if (p_m_s_recog < BELL_TIMER_RECOG_END)
				return;
			PDEBUG(DEBUG_SS5, "%s: pulse remains off, counted %d pulses\n", p_name, p_m_s_pulsecount);
			if (p_m_s_pulsecount >= 12)
				dial[1] = '#';
			else if (p_m_s_pulsecount == 11)
				dial[1] = '*';
			else if (p_m_s_pulsecount == 10)
				dial[1] = '0';
			else
				dial[1] = p_m_s_pulsecount + '0';
			ss5_trace_header(p_m_mISDNport, this, SS5_DIALING_IND, p_m_b_channel);
			add_trace("digit", NULL, "%s", dial+1);
			add_trace("pulses", NULL, "%d", p_m_s_pulsecount);
			end_trace();
			/* special star release feature */
			if ((p_m_mISDNport->ss5 & SS5_FEATURE_STAR_RELEASE) && dial[1] == '*') {
				ss5_trace_header(p_m_mISDNport, this, SS5_DIALING_IND, p_m_b_channel);
				add_trace("star", NULL, "releases call");
				end_trace();
				goto star_release;
			}
			if (p_state == PORT_STATE_IN_SETUP) {
				/* sending digit as setup */
				do_setup(dial, 0); /* include 'a' == KP1 */
				new_state(PORT_STATE_IN_OVERLAP);
			} else {
				/* sending digit as information */
				message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_INFORMATION);
				SCPY(message->param.information.id, dial+1);
				message_put(message);
			}
				new_ss5_state(SS5_STATE_IDLE);
			/* done rx pulses, return to idle */
			new_ss5_state(SS5_STATE_IDLE);
		}
	} else {
		if (on) {
			/* pulse remains on */
			p_m_s_recog += SS5_DECODER_NPOINTS;
		} else {
			/* pulse turns off */
			if (p_m_s_recog >= BELL_TIMER_RECOG_HANGUP) {
				PDEBUG(DEBUG_SS5, "%s: long pulse turns off, releasing\n", p_name);
				ss5_trace_header(p_m_mISDNport, this, SS5_DIALING_IND, p_m_b_channel);
				add_trace("longtone", NULL, "releases call");
				end_trace();
				star_release:
				/* long pulse is gone, release current connection, if any */
				while(p_epointlist) {
					message = message_create(p_serial, p_epointlist->epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
					message->param.disconnectinfo.location = LOCATION_BEYOND;
					message->param.disconnectinfo.cause = CAUSE_NORMAL;
					message_put(message);
					free_epointlist(p_epointlist);
				}
				set_tone("", NULL);
				/* return to setup state */
				new_state(PORT_STATE_IN_SETUP);
				new_ss5_state(SS5_STATE_IDLE);
				return;
			}
			PDEBUG(DEBUG_SS5, "%s: short pulse turns off, releasing\n", p_name);
			p_m_s_recog = 0;
			new_ss5_signal(SS5_SIGNAL_PULSE_OFF);
		}
	}
}

void Pss5::proceed_to_send_ind(void)
{
	ss5_trace_header(p_m_mISDNport, this, SS5_PROCEED_TO_SEND_IND, p_m_b_channel);
	end_trace();

	SCPY(p_m_s_dial, p_dialinginfo.id);
	start_signal(SS5_STATE_DIAL_OUT);

	new_state(PORT_STATE_OUT_OVERLAP);
}

void Pss5::busy_flash_ind(void)
{
	struct lcr_msg *message;

	ss5_trace_header(p_m_mISDNport, this, SS5_BUSY_FLASH_IND, p_m_b_channel);
	end_trace();

	/* busy before dialing ? */
	if (!p_epointlist)
		return;

	if (!(p_m_mISDNport->ss5 & SS5_FEATURE_NODISCONNECT)) {
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_DISCONNECT);
		message->param.disconnectinfo.location = LOCATION_BEYOND;
		message->param.disconnectinfo.cause = CAUSE_BUSY;
		message_put(message);
	}

	new_state(PORT_STATE_IN_DISCONNECT);
}

void Pss5::answer_ind(void)
{
	struct lcr_msg *message;

	ss5_trace_header(p_m_mISDNport, this, SS5_ANSWER_IND, p_m_b_channel);
	end_trace();

	/* answer before dialing ? */
	if (!p_epointlist)
		return;

	/* already connected */
	if (!(p_m_mISDNport->ss5 & SS5_FEATURE_CONNECT)) {
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_CONNECT);
		message_put(message);
	}

	new_state(PORT_STATE_CONNECT);
}

void Pss5::forward_transfer_ind(void)
{
//	struct lcr_msg *message;

	ss5_trace_header(p_m_mISDNport, this, SS5_FORWARD_TRANSFER_IND, p_m_b_channel);
	end_trace();

#if 0
	/* if BELL flavor bluebox flag is set, use it to seize a new line */
	if (!(p_m_mISDNport->ss5 & SS5_FEATURE_BELL))
		return;

	/* special BELL flavor hack to clear a line and seize a new one */
	while(p_epointlist) {
		message = message_create(p_serial, p_epointlist->epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.location = LOCATION_BEYOND;
		message->param.disconnectinfo.cause = CAUSE_NORMAL;
		message_put(message);
		free_epointlist(p_epointlist);
	}
	set_tone("", NULL);
	new_state(PORT_STATE_IN_SETUP);
#endif
}

void Pss5::clear_back_ind(void)
{
	struct lcr_msg *message;

	ss5_trace_header(p_m_mISDNport, this, SS5_CLEAR_BACK_IND, p_m_b_channel);
	end_trace();

	/* nobody? */
	if (!p_epointlist)
		return;

	if (!(p_m_mISDNport->ss5 & SS5_FEATURE_NODISCONNECT)) {
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_DISCONNECT);
		message->param.disconnectinfo.location = LOCATION_BEYOND;
		message->param.disconnectinfo.cause = CAUSE_NORMAL;
		message_put(message);
	}

	new_state(PORT_STATE_IN_DISCONNECT);
}

void Pss5::clear_forward_ind(void)
{
	struct lcr_msg *message;

	ss5_trace_header(p_m_mISDNport, this, SS5_CLEAR_FORWARD_IND, p_m_b_channel);
	end_trace();

	new_state(PORT_STATE_IDLE);
	set_tone("", NULL);
	p_type = PORT_TYPE_SS5_IDLE;

	/* someone ? */
	if (!p_epointlist)
		return;

	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_RELEASE);
	message->param.disconnectinfo.location = LOCATION_BEYOND;
	message->param.disconnectinfo.cause = CAUSE_NORMAL;
	message_put(message);
	free_epointlist(p_epointlist);

}

void Pss5::release_guard_ind(void)
{
	struct lcr_msg *message;

	ss5_trace_header(p_m_mISDNport, this, SS5_RELEASE_GUARD_IND, p_m_b_channel);
	end_trace();

	set_tone("", NULL);

	/* someone ? */
	if (!p_epointlist)
		return;

	message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_RELEASE);
	message->param.disconnectinfo.location = LOCATION_BEYOND;
	message->param.disconnectinfo.cause = CAUSE_NORMAL;
	message_put(message);
	free_epointlist(p_epointlist);
}

void Pss5::double_seizure_ind(void)
{
	ss5_trace_header(p_m_mISDNport, this, SS5_DOUBLE_SEIZURE_IND, p_m_b_channel);
	end_trace();
	ss5_trace_header(p_m_mISDNport, this, SS5_DOUBLE_SEIZURE_REQ, p_m_b_channel);
	end_trace();

	/* start double seizure sequence, so remote exchange will recognize it */
	start_signal(SS5_STATE_DOUBLE_SEIZE);
}


/*
 * shuts down by sending a clear forward and releasing endpoint
 */
void Pss5::do_release(int cause, int location)
{
	struct lcr_msg *message;

	p_m_s_timer = 0.0;

	/* sending release to endpoint */
	while(p_epointlist) {
		message = message_create(p_serial, p_epointlist->epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.location = location;
		message->param.disconnectinfo.cause = cause;
		message_put(message);
		free_epointlist(p_epointlist);
	}

	/* start clear-forward */
	ss5_trace_header(p_m_mISDNport, this, SS5_CLEAR_FORWARD_REQ, p_m_b_channel);
	end_trace();
	start_signal(SS5_STATE_CLEAR_FORWARD);

	new_state(PORT_STATE_RELEASE);
}


/*
 * create endpoint and send setup
 */
void Pss5::do_setup(char *dial, int complete)
{
	class Endpoint *epoint;
	struct lcr_msg *message;

	SCPY(p_dialinginfo.id, dial);
	p_dialinginfo.sending_complete = complete;
	p_callerinfo.present = INFO_PRESENT_NOTAVAIL;
	p_callerinfo.screen = INFO_SCREEN_NETWORK;
	p_callerinfo.ntype = INFO_NTYPE_NOTPRESENT;
	p_callerinfo.isdn_port = p_m_portnum;
	SCPY(p_callerinfo.interface, p_m_mISDNport->ifport->interface->name);

	p_capainfo.bearer_capa = INFO_BC_AUDIO;
	p_capainfo.bearer_info1 = (options.law=='a')?3:2;
	p_capainfo.bearer_mode = INFO_BMODE_CIRCUIT;
	p_capainfo.hlc = INFO_HLC_NONE;
	p_capainfo.exthlc = INFO_HLC_NONE;
	p_capainfo.source_mode = B_MODE_TRANSPARENT;

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
	message_put(message);

}


/*
 * handler
 */
int Pss5::handler(void)
{
	int ret;

	if ((ret = PmISDN::handler()))
		return(ret);

	/* handle timer */
	if (p_m_s_timer && p_m_s_timer < now) {
		p_m_s_timer = 0.0;
		(this->*(p_m_s_timer_fn))();
	}

	/* if answer signal is queued */
	if (p_m_s_answer && p_m_s_state == SS5_STATE_IDLE) {
		p_m_s_answer = 0;
		/* start answer */
		ss5_trace_header(p_m_mISDNport, this, SS5_ANSWER_REQ, p_m_b_channel);
		end_trace();
		start_signal(SS5_STATE_ANSWER);
	}

	/* if busy-flash signal is queued */
	if (p_m_s_busy_flash && p_m_s_state == SS5_STATE_IDLE) {
		p_m_s_busy_flash = 0;
		/* start busy-flash */
		ss5_trace_header(p_m_mISDNport, this, SS5_BUSY_FLASH_REQ, p_m_b_channel);
		end_trace();
		start_signal(SS5_STATE_BUSY_FLASH);
	}

	/* if clear-back signal is queued */
	if (p_m_s_clear_back && p_m_s_state == SS5_STATE_IDLE) {
		p_m_s_clear_back = 0;
		/* start clear-back */
		ss5_trace_header(p_m_mISDNport, this, SS5_CLEAR_BACK_REQ, p_m_b_channel);
		end_trace();
		start_signal(SS5_STATE_CLEAR_BACK);
	}

	return(0);
}


/*
 * handles all messages from endpoint
 */

/* MESSAGE_SETUP */
void Pss5::message_setup(unsigned int epoint_id, int message_id, union parameter *param)
{
	struct lcr_msg *message;
	int i;
	char string[128] = "", dial[128] = "";
	int dash, first_digit, last_was_digit;

	if (p_epointlist) {
		PERROR("endpoint already exist.\n");
		message = message_create(p_serial, epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		message->param.disconnectinfo.cause = CAUSE_UNSPECIFIED;
		message_put(message);
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

	/* parse dial string  */
	dash = 0; /* dash must be used next time */
	first_digit = 1;
	last_was_digit = 0;
	for (i = 0; p_dialinginfo.id[i]; i++) {
		if (dash || (last_was_digit && (p_dialinginfo.id[i]<'0' || p_dialinginfo.id[i]>'9')))
			SCCAT(string, '-');
		dash = 0;
		last_was_digit = 0;
		switch(p_dialinginfo.id[i]) {
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
			case '0':
			if (i && first_digit)
				dash = 1;
			first_digit = 0;
			last_was_digit = 1;
			SCCAT(string, p_dialinginfo.id[i]);
			SCCAT(dial, p_dialinginfo.id[i]);
			break;
			case '*':
			SCAT(string, "C11");
			SCCAT(dial, '*');
			dash = 1;
			break;
			case '#':
			SCAT(string, "C12");
			SCCAT(dial, '#');
			dash = 1;
			break;
			case 'a':
			SCAT(string, "KP1");
			SCCAT(dial, 'a');
			dash = 1;
			break;
			case 'b':
			SCAT(string, "KP2");
			SCCAT(dial, 'b');
			dash = 1;
			break;
			case 'c':
			SCAT(string, "ST");
			SCCAT(dial, 'c');
			dash = 1;
			case 'K':
			i++;
			if (p_dialinginfo.id[i] != 'P')
				goto dial_error;
			i++;
			if (p_dialinginfo.id[i] == '1') {
				SCAT(string, "KP1");
				SCCAT(dial, 'a');
				dash = 1;
				break;
			}
			if (p_dialinginfo.id[i] == '2') {
				SCAT(string, "KP2");
				SCCAT(dial, 'b');
				dash = 1;
				break;
			}
			goto dial_error;
			case 'C':
			i++;
			if (p_dialinginfo.id[i] != '1')
				goto dial_error;
			i++;
			if (p_dialinginfo.id[i] == '1') {
				SCAT(string, "C11");
				SCCAT(dial, 'a');
				dash = 1;
				break;
			}
			if (p_dialinginfo.id[i] == '2') {
				SCAT(string, "C12");
				SCCAT(dial, 'b');
				dash = 1;
				break;
			}
			goto dial_error;
			case 'S':
			i++;
			if (p_dialinginfo.id[i] != 'T')
				goto dial_error;
			SCAT(string, "ST");
			SCCAT(dial, 'c');
			dash = 1;
			break;
			default:
			break;
		}
		/* stop, if ST */
		if (dial[0] && dial[strlen(dial)-1] == 'c')
			break;
	}
	/* terminate */
	if (dial[0] && dial[strlen(dial)-1]!='c') {
		SCCAT(string, '-');
		SCAT(string, "ST");
		SCCAT(dial, 'c');
	}

	/* error in dial string */
	if (!dial[0]) {
		dial_error:
		ss5_trace_header(p_m_mISDNport, this, SS5_DIALING_REQ, p_m_b_channel);
		add_trace("string", NULL, "%s", p_dialinginfo.id);
		if (!dial[0])
			add_trace("error", NULL, "no number", dial);
		else if (dial[0]!='a' && dial[0]!='b')
			add_trace("error", NULL, "number must start with KP1/KP2", dial);
		else
			add_trace("error", NULL, "illegal format", dial);
		end_trace();
		message = message_create(p_serial, epoint_id, PORT_TO_EPOINT, MESSAGE_RELEASE);
		message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		message->param.disconnectinfo.cause = CAUSE_INVALID;
		message_put(message);
		return;
	}

	/* copy new dial string */
	SCPY(p_dialinginfo.id, dial);

	/* attach only if not already */
	epointlist_new(epoint_id);

	ss5_trace_header(p_m_mISDNport, this, SS5_DIALING_REQ, p_m_b_channel);
	add_trace("string", NULL, "%s", string);
	add_trace("type", NULL, "%s", (dial[0]=='b')?"international":"national");
	add_trace("number", NULL, "%s", dial);
	end_trace();
	/* connect auto path */
	if ((p_m_mISDNport->ss5 & SS5_FEATURE_CONNECT)) {
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_CONNECT);
		message_put(message);
	} else {
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_PROCEEDING);
		message_put(message);
	}

	/* start seizing */
	ss5_trace_header(p_m_mISDNport, this, SS5_SEIZING_REQ, p_m_b_channel);
	end_trace();
	new_ss5_state(SS5_STATE_SEIZING);
	new_ss5_signal(SS5_SIGNAL_SEND_ON);
	p_m_s_sample_nr = 0;
	inband_send_on();

	p_type = PORT_TYPE_SS5_OUT;
	new_state(PORT_STATE_OUT_SETUP);
}

/* MESSAGE_CONNECT */
void Pss5::message_connect(unsigned int epoint_id, int message_id, union parameter *param)
{
	memcpy(&p_connectinfo, &param->connectinfo, sizeof(p_connectinfo));

	if (p_state != PORT_STATE_CONNECT) {
		new_state(PORT_STATE_CONNECT);
		p_m_s_answer = 1;
	}

	set_tone("", NULL);
}

/* MESSAGE_DISCONNECT */
void Pss5::message_disconnect(unsigned int epoint_id, int message_id, union parameter *param)
{
	/* disconnect and clear forward (release guard) */
//	if ((p_type==PORT_TYPE_SS5_IN && !p_m_mISDNport->tones) /* incomming exchange with no tones */
if (0	 || p_type==PORT_TYPE_SS5_OUT) { /* outgoing exchange */
		do_release(param->disconnectinfo.cause, param->disconnectinfo.location);
		return;
	}

	ss5_trace_header(p_m_mISDNport, this, SS5_CLEAR_BACK_REQ, p_m_b_channel);
	end_trace();
	start_signal(SS5_STATE_CLEAR_BACK);

	new_state(PORT_STATE_OUT_DISCONNECT);
//	p_m_s_timer_fn = &Pss5::register_timeout;
//	p_m_s_timer = now + 30.0;
}

/* MESSAGE_RELEASE */
void Pss5::message_release(unsigned int epoint_id, int message_id, union parameter *param)
{
	do_release(param->disconnectinfo.cause, param->disconnectinfo.location);
}

void Pss5::register_timeout(void)
{
	do_release(CAUSE_NORMAL, LOCATION_BEYOND);
}

/*
 * endpoint sends messages to the port
 */
int Pss5::message_epoint(unsigned int epoint_id, int message_id, union parameter *param)
{
	if (PmISDN::message_epoint(epoint_id, message_id, param))
		return(1);

	switch(message_id) {
		case MESSAGE_SETUP: /* dial-out command received from epoint */
		if (p_state!=PORT_STATE_IDLE) {
			PERROR("Pss5(%s) ignoring setup because isdn port is not in idle state (or connected for sending display info).\n", p_name);
			break;
		}
		if (p_epointlist && p_state==PORT_STATE_IDLE)
			FATAL("Pss5(%s): epoint pointer is set in idle state, how bad!!\n", p_name);
		message_setup(epoint_id, message_id, param);
		break;

		case MESSAGE_CONNECT: /* call of endpoint is connected */
		message_connect(epoint_id, message_id, param);
		break;

		case MESSAGE_DISCONNECT: /* call has been disconnected */
		message_disconnect(epoint_id, message_id, param);
		break;

		case MESSAGE_RELEASE: /* release isdn port */
		message_release(epoint_id, message_id, param);
		break;

		default:
		PDEBUG(DEBUG_SS5, "Pss5(%s) ss5 port with (caller id %s) received an unhandled message: %d\n", p_name, p_callerinfo.id, message_id);
	}

	return(1);
}

