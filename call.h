/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** call header file                                                          **
**                                                                           **
\*****************************************************************************/ 

enum { CALL_TYPE_NONE, CALL_TYPE_PBX, CALL_TYPE_ASTERISK};

/* call
 *
 * abstraction for pbx calls and asterisk calls
 */


class Call
{
	public:
	Call(class Endpoint *epoint);
	virtual ~Call();
	class Call *next;		/* next node in list of calls */
	virtual void message_epoint(unsigned long epoint_id, int message, union parameter *param);
	virtual int handler(void);
	virtual void release(unsigned long epoint_id, int hold, int location, int cause);

	unsigned long c_type;		/* call type (pbx or asterisk) */
	unsigned long c_serial;		/* serial/unique number of call */
}; 

void call_free(void);

extern class Call *call_first;

class Call *find_call_id(unsigned long call_id);

