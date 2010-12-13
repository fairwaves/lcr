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
	struct lcr_fd fd;
	int sockserial;
	char remote_name[32]; /* socket is connected remote application */
	struct admin_trace_req trace; /* stores trace, if detail != 0 */
	unsigned int epointid;
	struct admin_queue *response;
};

extern struct admin_list *admin_first;
int admin_init(void);
void admin_cleanup(void);
void admin_call_response(int adminid, int message, const char *connected, int cause, int location, int notify);
int admin_message_to_lcr(struct admin_message *msg, int remote_id);
int admin_message_from_lcr(int remote_id, unsigned int ref, int message_type, union parameter *param);
void message_bchannel_to_remote(unsigned int remote_id, unsigned int ref, int type, unsigned int handle, int tx_gain, int rx_gain, char *pipeline, unsigned char *crypt, int crypt_len, int crypt_type, int isloopback);



