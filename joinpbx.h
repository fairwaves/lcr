/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** join header file for pbx joins                                            **
**                                                                           **
\*****************************************************************************/ 


/* join
 *
 * joins connect interfaces together
 * joins are linked in a chain
 * interfaces can have 0, 1 or more references to a join
 * the join can have many references to interfaces
 * joins receive and send messages
 */

#define RECORD_BUFFER_SIZE	16000

enum { /* relation types */
	RELATION_TYPE_CALLING,	/* initiator of a join */
	RELATION_TYPE_SETUP,	/* interface which is to be set up */
	RELATION_TYPE_CONNECT,	/* interface is connected */
};

enum { /* states that results from last notification */
	NOTIFY_STATE_ACTIVE, /* just the normal case, the party is active */
	NOTIFY_STATE_SUSPEND, /* the party is inactive, because she has parked */
	NOTIFY_STATE_HOLD, /* the party is inactive, because she holds the line */
	NOTIFY_STATE_CONFERENCE, /* the parties joined a conference */
};


struct join_relation { /* relation to an interface */
	struct join_relation *next;	/* next node */
	int type;			/* type of relation */
	unsigned int epoint_id;	/* interface to link join to */
	int channel_state;		/* if audio is available */
	int rx_state;			/* current state of what we received from endpoint */
	int tx_state;			/* current state of what we sent to endpoint */
};

class JoinPBX : public Join
{
	public:
	JoinPBX(class Endpoint *epoint);
	~JoinPBX();
	void message_epoint(unsigned int epoint_id, int message, union parameter *param);
	int release(struct join_relation *relation, int location, int cause);

	char j_caller[32];		/* caller number */
	char j_caller_id[32];		/* caller id to signal */
	char j_dialed[1024];		/* dial string of (all) number(s) */
	char j_todial[32];		/* overlap dialing (part not signalled yet) */
	int j_multicause, j_multilocation;
	
	int j_pid;			/* pid of join to generate bridge id */
	struct lcr_work j_updatebridge;		/* bridge must be updated */
	struct join_relation *j_relation; /* list of endpoints that are related to the join */

	int j_partyline;		/* if set, join is conference room */
	int j_partyline_jingle;		/* also play jingle on join/leave */

	unsigned int j_3pty;		/* other join if a 3pty-bridge is requested */

	void bridge(void);
	void remove_relation(struct join_relation *relation);
	struct join_relation *add_relation(void);
	int out_setup(unsigned int epoint_id, int message, union parameter *param, char *newnumber, char *newkeypad);
	void play_jingle(int in);
}; 

void joinpbx_debug(class JoinPBX *joinpbx, const char *function);
int joinpbx_countrelations(unsigned int join_id);
int track_notify(int oldstate, int notify);

