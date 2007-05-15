/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** message handling                                                          **
**                                                                           **
\*****************************************************************************/ 

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "main.h"

MESSAGES

struct message *message_first = NULL;
struct message **messagepointer_end = &message_first;

/* creates a new message with the given attributes. the message must be filled then. after filling, the message_put must be called */
struct message *message_create(int id_from, int id_to, int flow, int type)
{
	struct message *message;
	int i = 0;

	while(i < 10)
	{
		message = (struct message *)calloc(1, sizeof(struct message));
		if (message)
			break;

		if (!i)
			PERROR("no mem for message, retrying...\n");
		i++;
		usleep(300000);
	}
	if (!message)
	{
		PERROR("***Fatal error: no mem for message!!! exitting.\n");
		exit(-1);
	}
	mmemuse++;

	memset(message, 0, sizeof(struct message));

	message->id_from = id_from;
	message->id_to = id_to;
	message->flow = flow;
	message->type = type;

	return(message);
}

/* attaches a message to the end of the message chain */
void message_put(struct message *message)
{
	if (message->id_to == 0)
	{
		PDEBUG(DEBUG_MSG, "message %s not written, because destination is 0.\n", messages_txt[message->type]);
		message_free(message);
		return;
	}
	
	if ((options.deb&DEBUG_MSG) && message->type != MESSAGE_DATA)
		PDEBUG(DEBUG_MSG, "message %s written from %ld to %ld (memory %x)\n", messages_txt[message->type], message->id_from, message->id_to, message);

	*messagepointer_end = message;
	messagepointer_end = &(message->next);
}


/* detaches the first messages from the message chain */
struct message *message_get(void)
{
	struct message *message;

	if (!message_first)
	{
		return(0);
	}

	message = message_first;
	message_first = message->next;
	if (!message_first)
		messagepointer_end = &message_first;

	if ((options.deb&DEBUG_MSG) && message->type != MESSAGE_DATA)
		PDEBUG(DEBUG_MSG, "message %s reading from %ld to %ld (memory %x)\n", messages_txt[message->type], message->id_from, message->id_to, message);

	return(message);
}

/* free a message */
void message_free(struct message *message)
{
	memset(message, 0, sizeof(struct message));
	free(message);
	mmemuse--;
}


