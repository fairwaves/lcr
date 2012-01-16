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
EndpointApp::EndpointApp(class Endpoint *epoint, int origin, int type)
{
	ea_endpoint = epoint;
	ea_type = type;
	classuse++;
}

/*
 * endpoint destructor
 */
EndpointApp::~EndpointApp(void)
{
	classuse--;
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


/* create endpoint app */
class EndpointApp *new_endpointapp(class Endpoint *epoint, int origin, int type)
{
	class EndpointApp *app = NULL;

	switch (type) {
	case EAPP_TYPE_PBX:
		app = new EndpointAppPBX(epoint, origin);
		break;
	case EAPP_TYPE_BRIDGE:
		app = new EndpointAppBridge(epoint, origin);
		break;
	}

	if (!app)
		FATAL("Failed to create endpoint APP (type %d)\n", type);

	epoint->ep_app_type = type;
	epoint->ep_app = app;

	return app;
}
