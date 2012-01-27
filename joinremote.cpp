/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** join functions for remote application                                     **
**                                                                           **
\*****************************************************************************/ 

#include "main.h"
//#define __u8 unsigned char
//#define __u16 unsigned short
//#define __u32 unsigned int

extern unsigned int new_remote;

/*
 * constructor for a new join 
 * the join will have a relation to the calling endpoint
 */
JoinRemote::JoinRemote(unsigned int serial, char *remote_name, int remote_id) : Join()
{
	union parameter param;

	SCPY(j_remote_name, remote_name);
	j_remote_id = remote_id;
	j_type = JOIN_TYPE_REMOTE;
	j_remote_ref = new_remote++;

	PDEBUG(DEBUG_JOIN, "Constructor(new join) ref=%d\n", j_remote_ref);

	j_epoint_id = serial; /* this is the endpoint, if created by epoint */
	if (j_epoint_id)
		PDEBUG(DEBUG_JOIN, "New remote join connected to endpoint id %lu and application %s (ref=%d)\n", j_epoint_id, remote_name, j_remote_ref);

	/* send new ref to remote socket */
	memset(&param, 0, sizeof(union parameter));
	if (serial)
		param.newref.direction = 1; /* new ref from lcr */
	if (admin_message_from_lcr(j_remote_id, j_remote_ref, MESSAGE_NEWREF, &param)<0)
		FATAL("No socket with remote application '%s' found, this shall not happen. because we already created one.\n", j_remote_name);
}


/*
 * join descructor
 */
JoinRemote::~JoinRemote()
{
}

void JoinRemote::message_epoint(unsigned int epoint_id, int message_type, union parameter *param)
{
	/* if endpoint has just been removed, but still a message in the que */
	if (epoint_id != j_epoint_id)
		return;
	
	PDEBUG(DEBUG_JOIN, "Message %d of endpoint %d from LCR to remote (ref=%d)\n", message_type, j_epoint_id, j_remote_ref);

	/* look for Remote's interface */
	if (admin_message_from_lcr(j_remote_id, j_remote_ref, message_type, param)<0) {
		PERROR("No socket with remote application '%s' found, this shall not happen. Closing socket shall cause release of all joins.\n", j_remote_name);
		return;		
	}

	if (message_type == MESSAGE_RELEASE) {
		delete this;
		return;
	}
}

void JoinRemote::message_remote(int message_type, union parameter *param)
{
	struct lcr_msg *message;

	PDEBUG(DEBUG_JOIN, "Message %d of endpoint %d from remote to LCR (ref=%d)\n", message_type, j_epoint_id, j_remote_ref);

	/* create relation if no relation exists */
	if (!j_epoint_id) {
		class Endpoint		*epoint;

		if (!(epoint = new Endpoint(0, j_serial)))
			FATAL("No memory for Endpoint instance\n");
		j_epoint_id = epoint->ep_serial;
		PDEBUG(DEBUG_JOIN, "Created endpoint %d\n", j_epoint_id);
		epoint->ep_app = new_endpointapp(epoint, 1, EAPP_TYPE_PBX); // outgoing
	}

#ifdef WITH_MISDN
	/* set serial on bchannel message
	 * also ref is given, so we send message with ref */
	if (message_type == MESSAGE_BCHANNEL) {
		message_bchannel_from_remote(this, param->bchannel.type, param->bchannel.handle);
		return;
	}
#endif
	
	/* cannot just forward, because param is not of container "struct lcr_msg" */
	message = message_create(j_serial, j_epoint_id, JOIN_TO_EPOINT, message_type);
	memcpy(&message->param, param, sizeof(message->param));
	message_put(message);

	if (message_type == MESSAGE_RELEASE) {
		delete this;
		return;
	}
}



