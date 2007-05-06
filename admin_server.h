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

#include "admin.h"

struct admin_queue {
	struct admin_queue	*next;
	ulong			offset; /* current offset writing */
	ulong			num; /* number of admin messages */
	struct admin_message	am[0];
};

struct admin_list {
	struct admin_list *next;
	int sock;
	int sockserial;
	unsigned long epointid;
	struct admin_queue *response;
};

int admin_init(void);
void admin_cleanup(void);
int admin_handle(void);
void admin_call_response(int adminid, int message, char *connected, int cause, int location, int notify);



