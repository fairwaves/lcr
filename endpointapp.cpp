/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** The EndpointApp represents the application for the Endpoint.              **
**                                                                           **
\*****************************************************************************/ 

#include "main.h"

/*
 * EndpointApp constructor
 */
EndpointApp::EndpointApp(class Endpoint *epoint, int origin)
{
	ea_endpoint = epoint;
	classuse++;
}

/*
 * endpoint destructor
 */
EndpointApp::~EndpointApp(void)
{
	classuse--;
}

int EndpointApp::handler(void)
{
	return(0);
}

/* mini application for test purpose only */

void EndpointApp::ea_message_port(unsigned int port_id, int message_type, union parameter *param)
{
	PDEBUG(DEBUG_EPOINT, "%s: Spare function.\n", __FUNCTION__);
}

void EndpointApp::ea_message_join(unsigned int join_id, int message_type, union parameter *param)
{
	PDEBUG(DEBUG_EPOINT, "%s: Spare function.\n", __FUNCTION__);
}

