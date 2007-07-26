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
#include "macro.h"
#include "extension.h"
#include "message.h"
#include "admin.h"

int sock;

struct admin_list {
	struct admin_list *next;
	struct admin_msg msg;
} *admin_first = NULL;

/*
 * enque message from asterisk
 */
int admin_asterisk(int message_type, union parameter *param)
{
	struct admin_list *admin, **adminp;

	adminp = &admin_first;
	while(*adminp)
		adminp = &((*adminp)->next);
	admin = (struct admin_list *)MALLOC(sizeof(struct admin_list));
	*adminp = admin;

	admin->msg.type = message_type;
	memcpy(&admin->msg.param, param, sizeof(union parameter));

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
		FREE(admin, 0);

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

	/* open socket */
	if ((sock = socket(PF_UNIX, SOCK_STREAM, 0)) < 0)
	{
		fprintf(stderr, "Failed to create socket.\n");
		exit(EXIT_FAILURE);
	}

	/* set socket address and name */
	memset(&sock_address, 0, sizeof(sock_address));
	sock_address.sun_family = PF_UNIX;
	UCPY(sock_address.sun_path, socket_name);

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
	SCPY(param.hello.application, "asterisk");
	admin_asterisk(MESSAGE_HELLO, &param);

	while(42)
	{
		ret = handle_socket();
		if (ret < 0)
			break;
		if (!ret)
			usleep(30000);
	}
	
	/* close socket */	
	close(sock);
	/* now we say good bye */
	if (ret)
	{
		printf("%s\n", ret);
		exit(EXIT_FAILURE);
	}
}





