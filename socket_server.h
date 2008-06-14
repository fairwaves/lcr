/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** Administration tool header file (server)                                  **
**                                                                           **
\*****************************************************************************/

#include "lcrsocket.h"

struct admin_queue {
	struct admin_queue	*next;
	unsigned int		offset; /* current offset writing */
	unsigned int		num; /* number of admin messages */
	struct admin_message	am[0];
};

struct admin_list {
	struct admin_list *next;
	int sock;
	int sockserial;
	char remote_name[32]; /* socket is connected remote application */
	struct admin_trace_req trace; /* stores trace, if detail != 0 */
	unsigned int epointid;
	struct admin_queue *response;
};

extern struct admin_list *admin_first;
int admin_init(void);
void admin_cleanup(void);
int admin_handle(void);
void admin_call_response(int adminid, int message, char *connected, int cause, int location, int notify);
int admin_message_to_join(struct admin_message *msg, int remote_id);
int admin_message_from_join(int remote_id, unsigned int ref, int message_type, union parameter *param);



