/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** The EndpointApp represents the application for the Endpoint.              **
**                                                                           **
\*****************************************************************************/ 


#include <stdio.h>
#include "main.h"

/*
 * EndpointApp constructor
 */
EndpointApp::EndpointApp(class Endpoint *epoint)
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

void EndpointApp::ea_message_port(unsigned long port_id, int message_type, union parameter *param)
{
	PDEBUG(DEBUG_EPOINT, "%s: Spare function.\n", __FUNCTION__);
}

void EndpointApp::ea_message_call(unsigned long port_id, int message_type, union parameter *param)
{
	PDEBUG(DEBUG_EPOINT, "%s: Spare function.\n", __FUNCTION__);
}

