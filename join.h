/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** join header file                                                          **
**                                                                           **
\*****************************************************************************/ 

enum { JOIN_TYPE_NONE, JOIN_TYPE_PBX, JOIN_TYPE_REMOTE};

/* join
 *
 * abstraction for pbx calls and asterisk calls
 */


class Join
{
	public:
	Join();
	virtual ~Join();
	class Join *next;		/* next node in list of joins */
	virtual void message_epoint(unsigned int epoint_id, int message, union parameter *param);

	unsigned int j_type;		/* join type (pbx or asterisk) */
	unsigned int j_serial;		/* serial/unique number of join */
}; 

void join_free(void);

extern class Join *join_first;

class Join *find_join_id(unsigned int join_id);

