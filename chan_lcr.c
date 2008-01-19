/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** Asterisk socket client                                                    **
**                                                                           **
\*****************************************************************************/

/*

How does it work:

To connect, open a socket and send a MESSAGE_HELLO to admin socket with
the application name. This name is unique an can be used for routing calls.

To make a call, send a MESSAGE_NEWREF and a new reference is received.
When receiving a call, a new reference is received.
The reference is received with MESSAGE_NEWREF.

Make a MESSAGE_SETUP or receive a MESSAGE_SETUP with the reference.

To release call and reference, send or receive MESSAGE_RELEASE.
From that point on, the ref is not valid, so no other message may be sent
with that reference.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "extension.h"
#include "message.h"
#include "admin.h"
#include "cause.h"
#include "bchannel.h"
#include "chan_lcr.h"

int sock;

struct admin_list {
	struct admin_list *next;
	struct admin_msg msg;
} *admin_first = NULL;

/*
 * channel and call instances
 */
struct chan_call *call_first;

struct chan_call *find_call_ref(unsigned long ref)
{
	struct chan_call *call = call_first;

	while(call)
	{
		if (call->ref == ref)
			break;
		call = call->next;
	}
	return(call);
}

struct chan_call *find_call_handle(unsigned long handle)
{
	struct chan_call *call = call_first;

	while(call)
	{
		if (call->bchannel_handle == handle)
			break;
		call = call->next;
	}
	return(call);
}

struct chan_call *alloc_call(void)
{
	struct chan_call **callp = &call_first;

	while(*callp)
		callp = &((*callp)->next);

	*callp = (struct chan_call *)malloc(sizeof(struct chan_call));
	return(*callp);
}

void free_call(struct chan_call *call)
{
	struct chan_call **temp = &call_first;

	while(*temp)
	{
		if (*temp == call)
		{
			*temp = (*temp)->next;
			free(call);
			return;
		}
		temp = &((*temp)->next);
	}
}


/*
 * receive bchannel data
 */
void rx_data(struct bchannel *bchannel, unsigned char *data, int len)
{
}

void rx_dtmf(struct bchannel *bchannel, char tone)
{
}

/*
 * enque message to LCR
 */
int send_message(int message_type, unsigned long ref, union parameter *param)
{
	struct admin_list *admin, **adminp;

	adminp = &admin_first;
	while(*adminp)
		adminp = &((*adminp)->next);
	admin = (struct admin_list *)malloc(sizeof(struct admin_list));
	*adminp = admin;

	admin->msg.type = message_type;
	admin->msg.ref = ref;
	memcpy(&admin->msg.param, param, sizeof(union parameter));

	return(0);
}

/*
 * message received from LCR
 */
int receive_message(int message_type, unsigned long ref, union parameter *param)
{
	union parameter newparam;
	struct bchannel *bchannel;
	struct chan_call *call;

	memset(&newparam, 0, sizeof(union parameter));

	/* handle bchannel message*/
	if (message_type == MESSAGE_BCHANNEL)
	{
		switch(param->bchannel.type)
		{
			case BCHANNEL_ASSIGN:
			if ((bchannel = find_bchannel_handle(param->bchannel.handle)))
			{
				fprintf(stderr, "error: bchannel handle %x already assigned.\n", param->bchannel.handle);
				return(-1);
			}
			/* create bchannel */
			bchannel = alloc_bchannel(param->bchannel.handle);
			if (!bchannel)
			{
				fprintf(stderr, "error: alloc bchannel handle %x failed.\n", param->bchannel.handle);
				return(-1);
			}

			/* configure channel */
			bchannel->b_tx_gain = param->bchannel.tx_gain;
			bchannel->b_rx_gain = param->bchannel.rx_gain;
			strncpy(bchannel->b_pipeline, param->bchannel.pipeline, sizeof(bchannel->b_pipeline)-1);
			if (param->bchannel.crypt_len)
			{
				bchannel->b_crypt_len = param->bchannel.crypt_len;
				bchannel->b_crypt_type = param->bchannel.crypt_type;
				memcpy(bchannel->b_crypt_key, param->bchannel.crypt, param->bchannel.crypt_len);
			}
			bchannel->b_txdata = 0;
			bchannel->b_dtmf = 1;
			bchannel->b_tx_dejitter = 1;

			/* in case, ref is not set, this bchannel instance must
			 * be created until it is removed again by LCR */
			/* link to call */
			if ((call = find_call_ref(ref)))
			{
				bchannel->ref = ref;
				call->bchannel_handle = param->bchannel.handle;
			}
			if (bchannel_create(bchannel))
				bchannel_activate(bchannel, 1);

			/* acknowledge */
			newparam.bchannel.type = BCHANNEL_ASSIGN_ACK;
			newparam.bchannel.handle = param->bchannel.handle;
			send_message(MESSAGE_BCHANNEL, 0, &newparam);
			break;

			case BCHANNEL_REMOVE:
			if (!(bchannel = find_bchannel_handle(param->bchannel.handle)))
			{
				fprintf(stderr, "error: bchannel handle %x not assigned.\n", param->bchannel.handle);
				return(-1);
			}
			/* unlink from call */
			if ((call = find_call_ref(bchannel->ref)))
			{
				call->bchannel_handle = 0;
			}
			/* destroy and remove bchannel */
			free_bchannel(bchannel);

			/* acknowledge */
			newparam.bchannel.type = BCHANNEL_REMOVE_ACK;
			newparam.bchannel.handle = param->bchannel.handle;
			send_message(MESSAGE_BCHANNEL, 0, &newparam);
			
			break;

			default:
			fprintf(stderr, "received unknown bchannel message %d\n", param->bchannel.type);
		}
		return(0);
	}

	/* handle new ref */
	if (message_type == MESSAGE_NEWREF)
	{
		if (param->direction)
		{
			/* new ref from lcr */
			if (!ref || find_call_ref(ref))
			{
				fprintf(stderr, "illegal new ref %d received\n", ref);
				return(-1);
			}
			call = alloc_call();
			call->ref = ref;
		} else
		{
			/* new ref, as requested from this remote application */
			call = find_call_ref(0);
			if (!call)
			{
				/* send release, if ref does not exist */
				newparam.disconnectinfo.cause = CAUSE_NORMAL;
				newparam.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
				send_message(MESSAGE_RELEASE, ref, &newparam);
				return(0);
			}
			call->ref = ref;
#warning process call (send setup, if pending)
		}
		return(0);
	}

	/* check ref */
	if (!ref)
	{
		fprintf(stderr, "received message %d without ref\n", message_type);
		return(-1);
	}
	call = find_call_ref(ref);
	if (!call)
	{
		/* ignore ref that is not used (anymore) */
		return(0);
	}

	/* handle messages */
	switch(message_type)
	{
#warning we must see if ref is a reply or a request, do we??
		case MESSAGE_RELEASE:
#warning release call
		free_call(call);
		return(0);

		case MESSAGE_SETUP:
#warning handle incoming setup, send to asterisk
		break;
	}
	return(0);
}


/* asterisk handler
 * warning! not thread safe
 * returns -1 for socket error, 0 for no work, 1 for work
 */
int handle_socket(void)
{
	int work = 0;
	int len;
	struct admin_message msg;
	struct admin_list *admin;

	/* read from socket */
	len = read(sock, &msg, sizeof(msg));
	if (len == 0)
	{
		printf("Socket closed\n");
		return(-1); // socket closed
	}
	if (len > 0)
	{
		if (len != sizeof(msg))
		{
			fprintf(stderr, "Socket short read (%d)\n", len);
			return(-1); // socket error
		}
		if (msg.message != ADMIN_MESSAGE)
		{
			fprintf(stderr, "Socket received illegal message %d\n", msg.message);
			return(-1); // socket error
		}
		receive_message(msg.u.msg.type, msg.u.msg.ref, &msg.u.msg.param);
		printf("message received %d\n", msg.u.msg.type);
		work = 1;
	} else
	{
		if (errno != EWOULDBLOCK)
		{
			fprintf(stderr, "Socket error %d\n", errno);
			return(-1);
		}
	}

	/* write to socket */
	if (!admin_first)
		return(work);
	admin = admin_first;
	len = write(sock, &admin->msg, sizeof(msg));
	if (len == 0)
	{
		printf("Socket closed\n");
		return(-1); // socket closed
	}
	if (len > 0)
	{
		if (len != sizeof(msg))
		{
			fprintf(stderr, "Socket short write (%d)\n", len);
			return(-1); // socket error
		}
		/* free head */
		admin_first = admin->next;
		free(admin);

		work = 1;
	} else
	{
		if (errno != EWOULDBLOCK)
		{
			fprintf(stderr, "Socket error %d\n", errno);
			return(-1);
		}
	}

	return(work);
}

/*
 * main function
 */
int main(int argc, char *argv[])
{
	char *socket_name = SOCKET_NAME;
	int conn;
	struct sockaddr_un sock_address;
	int ret;
	unsigned long on = 1;
	union parameter param;
	int work;

	/* open socket */
	if ((sock = socket(PF_UNIX, SOCK_STREAM, 0)) < 0)
	{
		fprintf(stderr, "Failed to create socket.\n");
		exit(EXIT_FAILURE);
	}

	/* set socket address and name */
	memset(&sock_address, 0, sizeof(sock_address));
	sock_address.sun_family = PF_UNIX;
	strcpy(sock_address.sun_path, socket_name);

	/* connect socket */
	if ((conn = connect(sock, (struct sockaddr *)&sock_address, SUN_LEN(&sock_address))) < 0)
	{
		close(sock);
		fprintf(stderr, "Failed to connect to socket \"%s\".\nIs LCR running?\n", sock_address.sun_path);
		exit(EXIT_FAILURE);
	}

	/* set non-blocking io */
	if (ioctl(sock, FIONBIO, (unsigned char *)(&on)) < 0)
	{
		close(sock);
		fprintf(stderr, "Failed to set socket into non-blocking IO.\n");
		exit(EXIT_FAILURE);
	}

	/* enque hello message */
	memset(&param, 0, sizeof(param));
	strcpy(param.hello.application, "asterisk");
	send_message(MESSAGE_HELLO, 0, &param);

	/* bchannel */
	if (!bchannel_initialize())
		goto bchannel_failed;
	
	while(42)
	{
		work = 0;

		/* handle socket */
		ret = handle_socket();
		if (ret < 0)
			break;
		if (ret)
			work = 1;

		/* handle mISDN */
		ret = bchannel_handle();
		if (ret)
			work = 1;
		
		if (!work)
			usleep(30000);
	}

	bchannel_deinitialize();
	bchannel_failed:
	
	/* close socket */	
	close(sock);
	/* now we say good bye */
	if (ret)
	{
		printf("%s\n", ret);
		exit(EXIT_FAILURE);
	}
}





