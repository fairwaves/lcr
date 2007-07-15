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

	message = (struct message *)MALLOC(sizeof(struct message));
	if (!message)
		FATAL("No memory for message.\n");
	mmemuse++;

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

void message_forward(int id_from, int id_to, int flow, union parameter *param)
{
	struct message *message;

	/* get point to message */
	message = (struct message *)((unsigned long)param - ((unsigned long)(&message->param) - (unsigned long)message));

	/* protect, so forwarded messages are not freed after handling */
	message->keep = 1;

	message->id_from = id_from;
	message->id_to = id_to;
	message->flow = flow;
	message_put(message);
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

	message->keep = 0;

	if ((options.deb&DEBUG_MSG) && message->type != MESSAGE_DATA)

		PDEBUG(DEBUG_MSG, "message %s reading from %ld to %ld (memory %x)\n", messages_txt[message->type], message->id_from, message->id_to, message);

	return(message);
}

/* free a message */
void message_free(struct message *message)
{
	if (message->keep)
		return;
	FREE(message, sizeof(struct message));
	mmemuse--;
}


