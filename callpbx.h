/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** call header file for pbx calls                                            **
**                                                                           **
\*****************************************************************************/ 


/* call
 *
 * calls connect interfaces together
 * calls are linked in a chain
 * interfaces can have 0, 1 or more references to a call
 * the call can have many references to interfaces
 * calls receive and send messages
 */

#define RECORD_BUFFER_SIZE	16000

enum { /* relation types */
	RELATION_TYPE_CALLING,	/* initiator of a call */
	RELATION_TYPE_SETUP,	/* interface which is to be set up */
	RELATION_TYPE_CONNECT,	/* interface is connected */
};

enum { /* relation audio state */
	CHANNEL_STATE_CONNECT,	/* endpoint is connected to the call voice transmission in both dirs */
	CHANNEL_STATE_HOLD,	/* endpoint is on hold state, no audio */
};

enum { /* states that results from last notification */
	NOTIFY_STATE_ACTIVE, /* just the normal case, the party is active */
	NOTIFY_STATE_SUSPEND, /* the party is inactive, because she has parked */
	NOTIFY_STATE_HOLD, /* the party is inactive, because she holds the line */
	NOTIFY_STATE_CONFERENCE, /* the parties joined a conference */
};


struct call_relation { /* relation to an interface */
	struct call_relation *next;	/* next node */
	int type;			/* type of relation */
	unsigned long epoint_id;	/* interface to link call to */
	int channel_state;		/* if audio is available */
	int rx_state;			/* current state of what we received from endpoint */
	int tx_state;			/* current state of what we sent to endpoint */
};

class CallPBX : public Call
{
	public:
	CallPBX(class Endpoint *epoint);
	~CallPBX();
	void message_epoint(unsigned long epoint_id, int message, union parameter *param);
	int handler(void);
	int release(struct call_relation *relation, int location, int cause);

	char c_caller[32];		/* caller number */
	char c_caller_id[32];		/* caller id to signal */
	char c_dialed[1024];		/* dial string of (all) number(s) */
	char c_todial[32];		/* overlap dialing (part not signalled yet) */
	int c_multicause, c_multilocation;
	
	int c_pid;			/* pid of call to generate bridge id */
	int c_updatebridge;		/* bridge must be updated */
	struct call_relation *c_relation; /* list of endpoints that are related to the call */

	int c_partyline;		/* if set, call is conference room */

	void bridge(void);
	void bridge_data(unsigned long epoint_from, struct call_relation *relation_from, union parameter *param);
	void remove_relation(struct call_relation *relation);
	struct call_relation *add_relation(void);
	int out_setup(unsigned long epoint_id, int message, union parameter *param, char *newnumber);
}; 

void callpbx_debug(class CallPBX *callpbx, char *function);
int callpbx_countrelations(unsigned long call_id);
int track_notify(int oldstate, int notify);

