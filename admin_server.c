/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** Socket link                                                               **
**                                                                           **
\*****************************************************************************/

#include <stdio.h>
//#include <stdlib.h>
//#include <string.h>
#include <sys/types.h>
//#include <sys/stat.h>
//#include <unistd.h>
//#include <signal.h>
//#include <stdarg.h>
//#include <fcntl.h>
#include <sys/ioctl.h>
//#include <sys/file.h>
//#include <errno.h>
//#include <sys/mman.h>
//#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <curses.h>
#include "main.h"


char *socket_name = SOCKET_NAME;
int sock = -1;
struct sockaddr_un sock_address;

struct admin_list *admin_list = NULL;

/*
 * initialize admin socket 
 */
int admin_init(void)
{
	unsigned long on = 1;

	/* open and bind socket */
	if ((sock = socket(PF_UNIX, SOCK_STREAM, 0)) < 0)
	{
		PERROR("Failed to create admin socket. (errno=%d)\n", errno);
		return(-1);
	}
	fhuse++;
	memset(&sock_address, 0, sizeof(sock_address));
	sock_address.sun_family = AF_UNIX;
	UCPY(sock_address.sun_path, socket_name);
	unlink(socket_name);
	if (bind(sock, (struct sockaddr *)(&sock_address), SUN_LEN(&sock_address)) < 0)
	{
		close(sock);
		unlink(socket_name);
		fhuse--;
		sock = -1;
		PERROR("Failed to bind admin socket to \"%s\". (errno=%d)\n", sock_address.sun_path, errno);
		return(-1);
	}
	if (listen(sock, 5) < 0)
	{
		close(sock);
		unlink(socket_name);
		fhuse--;
		sock = -1;
		PERROR("Failed to listen to socket \"%s\". (errno=%d)\n", sock_address.sun_path, errno);
		return(-1);
	}
	if (ioctl(sock, FIONBIO, (unsigned char *)(&on)) < 0)
	{
		close(sock);
		unlink(socket_name);
		fhuse--;
		sock = -1;
		PERROR("Failed to set socket \"%s\" into non-blocking mode. (errno=%d)\n", sock_address.sun_path, errno);
		return(-1);
	}
	return(0);
}


/*
 * free connection
 */
void free_connection(struct admin_list *admin)
{
	struct admin_queue *response;
	void *temp;

	if (admin->sock >= 0)
	{
		close(admin->sock);
		fhuse--;
	}
//	printf("new\n", response);
	response = admin->response;
	while (response)
	{
//#warning
//	printf("%x\n", response);
		temp = response->next;
		free(response);
		memuse--;
		response = (struct admin_queue *)temp;
	}
//	printf("new2\n", response);
	free(admin);
//	printf("new3\n", response);
	memuse--;
}


/*
 * cleanup admin socket 
 */
void admin_cleanup(void)
{
	struct admin_list *admin, *next;;

	admin = admin_list;
	while(admin)
	{
//printf("clean\n");
		next = admin->next;
		free_connection(admin);
		admin = next;
	}

	if (sock >= 0)
	{
		close(sock);
		fhuse--;
	}
}


/*
 * do interface reload
 */
int admin_interface(struct admin_queue **responsep)
{
	struct admin_queue	*response;	/* response pointer */
	char			*err_txt = "";
	int			err = 0;

        if (read_interfaces())
	{
       		relink_interfaces();
		free_interfaces(interface_first);
		interface_first = interface_newlist;
		interface_newlist = NULL;
	} else
	{
		err_txt = interface_error;
		err = -1;
	}
	/* create state response */
	response = (struct admin_queue *)malloc(sizeof(struct admin_queue)+sizeof(admin_message));
	if (!response)
		return(-1);
	memuse++;
	memset(response, 0, sizeof(admin_queue));
	response->num = 1;
	/* message */
	response->am[0].message = ADMIN_RESPONSE_CMD_INTERFACE;
	/* error */
	response->am[0].u.x.error = err;
	/* message */
	SCPY(response->am[0].u.x.message, err_txt);
	/* attach to response chain */
	*responsep = response;
	responsep = &response->next;

	return(0);
}


/*
 * do route reload
 */
int admin_route(struct admin_queue **responsep)
{
	struct route_ruleset	*ruleset_new;
	struct admin_queue	*response;	/* response pointer */
	char			err_txt[256] = "";
	int			err = 0;
#if 0
	int			n;
#endif
	class EndpointAppPBX	*apppbx;

#if 0
	n = 0;
	apppbx = apppbx_first;
	while(apppbx)
	{
		n++;
		apppbx = apppbx->next;
	}
	if (apppbx_first)
	{
		SPRINT(err_txt, "Cannot reload routing, because %d endpoints active\n", n);
		err = -1;
		goto response;
	}
#endif
        if (!(ruleset_new = ruleset_parse()))
	{
		SPRINT(err_txt, ruleset_error);
		err = -1;
		goto response;
	}
	ruleset_free(ruleset_first);
	ruleset_first = ruleset_new;
	ruleset_main = getrulesetbyname("main");
	if (!ruleset_main)
	{
		SPRINT(err_txt, "Ruleset reloaded, but rule 'main' not found.\n");
		err = -1;
	}
	apppbx = apppbx_first;
	while(apppbx)
	{
		if (apppbx->e_action)
		{
			switch(apppbx->e_action->index)
			{
				case ACTION_INTERNAL:
				apppbx->e_action = &action_internal;
				break;
				case ACTION_EXTERNAL:
				apppbx->e_action = &action_external;
				break;
				case ACTION_CHAN:
				apppbx->e_action = &action_chan;
				break;
				case ACTION_VBOX_RECORD:
				apppbx->e_action = &action_vbox;
				break;
				case ACTION_PARTYLINE:
				apppbx->e_action = &action_partyline;
				break;
				default:
				goto release;
			}
		} else if (apppbx->e_state != EPOINT_STATE_CONNECT)
		{
			release:
			apppbx->e_callback = 0;
			apppbx->e_action = NULL;
			apppbx->release(RELEASE_ALL, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL);
			printlog("%3d  endpoint ADMIN Kicking due to reload of routing.\n", apppbx->ea_endpoint->ep_serial);
		}

		apppbx->e_action_timeout = NULL;
		apppbx->e_rule = NULL;
		apppbx->e_ruleset = NULL;

		apppbx = apppbx->next;
	}

	response:
	/* create state response */
	response = (struct admin_queue *)malloc(sizeof(struct admin_queue)+sizeof(admin_message));
	if (!response)
		return(-1);
	memuse++;
	memset(response, 0, sizeof(admin_queue));
	response->num = 1;
	/* message */
	response->am[0].message = ADMIN_RESPONSE_CMD_ROUTE;
	/* error */
	response->am[0].u.x.error = err;
	/* message */
	SCPY(response->am[0].u.x.message, err_txt);
	/* attach to response chain */
	*responsep = response;
	responsep = &response->next;

	return(0);
}


/*
 * do dialing
 */
int admin_dial(struct admin_queue **responsep, char *message)
{
	struct extension	ext;		/* temporary extension's settings */
	struct admin_queue	*response;	/* response pointer */
	char			*p;		/* pointer to dialing digits */

	/* create state response */
	response = (struct admin_queue *)malloc(sizeof(struct admin_queue)+sizeof(admin_message));
	if (!response)
		return(-1);
	memuse++;
	memset(response, 0, sizeof(admin_queue));
	response->num = 1;
	/* message */
	response->am[0].message = ADMIN_RESPONSE_CMD_DIAL;

	/* process request */
	if (!(p = strchr(message,':')))
	{
		response->am[0].u.x.error = -EINVAL;
		SPRINT(response->am[0].u.x.message, "no seperator ':' in message to seperate number from extension");
		goto out;
	}
	*p++ = 0;

	/* modify extension */
	if (!read_extension(&ext, message))
	{
		response->am[0].u.x.error = -EINVAL;
		SPRINT(response->am[0].u.x.message, "extension doesn't exist");
		goto out;
	}
	SCPY(ext.next, p);
	write_extension(&ext, message);

	out:
	/* attach to response chain */
	*responsep = response;
	responsep = &response->next;
	return(0);
}


/*
 * do release
 */
int admin_release(struct admin_queue **responsep, char *message)
{
	unsigned long		id;
	struct admin_queue	*response;	/* response pointer */
	class EndpointAppPBX	*apppbx;

	/* create state response */
	response = (struct admin_queue *)malloc(sizeof(struct admin_queue)+sizeof(admin_message));
	if (!response)
		return(-1);
	memuse++;
	memset(response, 0, sizeof(admin_queue));
	response->num = 1;
	/* message */
	response->am[0].message = ADMIN_RESPONSE_CMD_RELEASE;

	id = atoi(message);
	apppbx = apppbx_first;
	while(apppbx)
	{
		if (apppbx->ea_endpoint->ep_serial == id)
			break;
		apppbx = apppbx->next;
	}
	if (!apppbx)
	{
		response->am[0].u.x.error = -EINVAL;
		SPRINT(response->am[0].u.x.message, "Given endpoint %d doesn't exist.", id);
		goto out;
	}

	apppbx->e_callback = 0;
	apppbx->release(RELEASE_ALL, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL);

	out:
	/* attach to response chain */
	*responsep = response;
	responsep = &response->next;
	return(0);
}


/*
 * do call
 */
int admin_call(struct admin_list *admin, struct admin_message *msg)
{
	class Endpoint		*epoint;
	class EndpointAppPBX	*apppbx;

	if (!(epoint = new Endpoint(0,0)))
		return(-1);

        if (!(epoint->ep_app = apppbx = new DEFAULT_ENDPOINT_APP(epoint)))
        {
                PERROR("no memory for application\n");
                exit(-1);
        }
	apppbx->e_adminid = admin->sockserial;
	admin->epointid = epoint->ep_serial;
	SCPY(apppbx->e_callerinfo.id, nationalize_callerinfo(msg->u.call.callerid, &apppbx->e_callerinfo.ntype));
	if (msg->u.call.present)
		apppbx->e_callerinfo.present = INFO_PRESENT_ALLOWED;
	else
		apppbx->e_callerinfo.present = INFO_PRESENT_RESTRICTED;
	apppbx->e_callerinfo.screen = INFO_SCREEN_NETWORK;

//printf("hh=%d\n", apppbx->e_capainfo.hlc);

	apppbx->e_capainfo.bearer_capa = msg->u.call.bc_capa;
	apppbx->e_capainfo.bearer_mode = msg->u.call.bc_mode;
	apppbx->e_capainfo.bearer_info1 = msg->u.call.bc_info1;
	apppbx->e_capainfo.hlc = msg->u.call.hlc;
	apppbx->e_capainfo.exthlc = msg->u.call.exthlc;
	SCPY(apppbx->e_dialinginfo.number, msg->u.call.dialing);
	SCPY(apppbx->e_dialinginfo.interfaces, msg->u.call.interface);
	apppbx->e_dialinginfo.sending_complete = 1;

	apppbx->new_state(PORT_STATE_OUT_SETUP);
	apppbx->out_setup();
	return(0);
}


/*
 * this function is called for response whenever a call state changes.
 */
void admin_call_response(int adminid, int message, char *connected, int cause, int location, int notify)
{
	struct admin_list	*admin;
	struct admin_queue	*response, **responsep;	/* response pointer */

	/* searching for admin id
	 * maybe there is no admin instance, because the calling port was not
	 * initiated by admin_call */
	admin = admin_list;
	while(admin)
	{
		if (adminid == admin->sockserial)
			break;
		admin = admin->next;
	}
	if (!admin)
		return;

	/* seek to end of response list */
	response = admin->response;
	responsep = &admin->response;
	while(response)
	{
		responsep = &response->next;
		response = response->next;
	}

	/* create state response */
	response = (struct admin_queue *)malloc(sizeof(struct admin_queue)+sizeof(admin_message));
	if (!response)
		return;
	memuse++;
	memset(response, 0, sizeof(admin_queue));
	response->num = 1;
	/* message */
	response->am[0].message = message;
//	printf("MESSAGE: %d\n", message);

	SCPY(response->am[0].u.call.callerid, connected);
	response->am[0].u.call.cause = cause;
	response->am[0].u.call.location = location;
	response->am[0].u.call.notify = notify;

	/* attach to response chain */
	*responsep = response;
	responsep = &response->next;
}


/*
 * do state debugging
 */
int admin_state(struct admin_queue **responsep)
{

	class Port		*port;
	class EndpointAppPBX	*apppbx;
	class Call		*call;
	class Pdss1		*pdss1;
	struct mISDNport	*mISDNport;
	int			i;
	int			num;
	int			anybusy;
	struct admin_queue	*response;

	/* create state response */
	response = (struct admin_queue *)malloc(sizeof(struct admin_queue)+sizeof(admin_message));
	if (!response)
		return(-1);
	memuse++;
	memset(response, 0, sizeof(admin_queue));
	response->num = 1;
	/* message */
	response->am[0].message = ADMIN_RESPONSE_STATE;
	/* version */
	SCPY(response->am[0].u.s.version_string, VERSION_STRING);
	/* time */
	memcpy(&response->am[0].u.s.tm, now_tm, sizeof(struct tm));
	/* log file */
	SCPY(response->am[0].u.s.logfile, options.log);
	/* interface count */
	mISDNport = mISDNport_first;
	i = 0;
	while(mISDNport)
	{
		i++;
		mISDNport = mISDNport->next;
	}
	response->am[0].u.s.interfaces = i;
	/* call count */
	call = call_first;
	i = 0;
	while(call)
	{
		i++;
		call = call->next;
	}
	response->am[0].u.s.calls = i;
	/* apppbx count */
	apppbx = apppbx_first;
	i = 0;
	while(apppbx)
	{
		i++;
		apppbx = apppbx->next;
	}
	response->am[0].u.s.epoints = i;
	/* port count */
	port = port_first;
	i = 0;
	while(port)
	{
		i++;
		port = port->next;
	}
	response->am[0].u.s.ports = i;
	/* attach to response chain */
	*responsep = response;
	responsep = &response->next;

	/* create response for all interfaces */
	num = (response->am[0].u.s.interfaces)+(response->am[0].u.s.calls)+(response->am[0].u.s.epoints)+(response->am[0].u.s.ports);
	response = (struct admin_queue *)malloc(sizeof(admin_queue)+(num*sizeof(admin_message)));
	if (!response)
		return(-1);
	memuse++;
	memset(response, 0, sizeof(admin_queue)+(num*sizeof(admin_queue)));
	response->num = num;
	*responsep = response;
	responsep = &response->next;
	mISDNport = mISDNport_first;
	num = 0;
	while(mISDNport)
	{
		/* message */
		response->am[num].message = ADMIN_RESPONSE_S_INTERFACE;
		/* portnum */
		response->am[num].u.i.portnum = mISDNport->portnum;
		/* interface */
		SCPY(response->am[num].u.i.interface_name, mISDNport->interface_name);
		/* iftype */
		response->am[num].u.i.iftype = mISDNport->iftype;
		/* ptp */
		response->am[num].u.i.ptp = mISDNport->ptp;
		/* ntmode */
		response->am[num].u.i.ntmode = mISDNport->ntmode;
		/* pri */
		response->am[num].u.i.pri = mISDNport->pri;
		/* use */
		response->am[num].u.i.use = mISDNport->use;
		/* l1link */
		response->am[num].u.i.l1link = mISDNport->l1link;
		/* l2link */
		response->am[num].u.i.l2link = mISDNport->l2link;
		/* channels */
		response->am[num].u.i.channels = mISDNport->b_num;
		/* channel info */
		i = 0;
		anybusy = 0;
		while(i < mISDNport->b_num)
		{
			response->am[num].u.i.busy[i] = mISDNport->b_state[i];
			if (mISDNport->b_port[i])
				response->am[num].u.i.port[i] = mISDNport->b_port[i]->p_serial;
			i++;
		}
		mISDNport = mISDNport->next;
		num++;
	}

	/* create response for all calls */
	call = call_first;
	while(call)
	{
		/* message */
		response->am[num].message = ADMIN_RESPONSE_S_CALL;
		/* serial */
		response->am[num].u.c.serial = call->c_serial;
		/* partyline */
		if (call->c_type == CALL_TYPE_PBX)
			response->am[num].u.c.partyline = ((class CallPBX *)call)->c_partyline;
		/* */
		call = call->next;
		num++;
	}

	/* create response for all endpoint */
	apppbx = apppbx_first;
	while(apppbx)
	{
		/* message */
		response->am[num].message = ADMIN_RESPONSE_S_EPOINT;
		/* serial */
		response->am[num].u.e.serial = apppbx->ea_endpoint->ep_serial;
		/* call */
		response->am[num].u.e.call = apppbx->ea_endpoint->ep_call_id;
		/* rx notification */
		response->am[num].u.e.rx_state = apppbx->e_rx_state;
		/* tx notification */
		response->am[num].u.e.tx_state = apppbx->e_tx_state;
		/* state */
		switch(apppbx->e_state)
		{
			case EPOINT_STATE_IN_SETUP:
			response->am[num].u.e.state = ADMIN_STATE_IN_SETUP;
			break;
			case EPOINT_STATE_OUT_SETUP:
			response->am[num].u.e.state = ADMIN_STATE_OUT_SETUP;
			break;
			case EPOINT_STATE_IN_OVERLAP:
			response->am[num].u.e.state = ADMIN_STATE_IN_OVERLAP;
			break;
			case EPOINT_STATE_OUT_OVERLAP:
			response->am[num].u.e.state = ADMIN_STATE_OUT_OVERLAP;
			break;
			case EPOINT_STATE_IN_PROCEEDING:
			response->am[num].u.e.state = ADMIN_STATE_IN_PROCEEDING;
			break;
			case EPOINT_STATE_OUT_PROCEEDING:
			response->am[num].u.e.state = ADMIN_STATE_OUT_PROCEEDING;
			break;
			case EPOINT_STATE_IN_ALERTING:
			response->am[num].u.e.state = ADMIN_STATE_IN_ALERTING;
			break;
			case EPOINT_STATE_OUT_ALERTING:
			response->am[num].u.e.state = ADMIN_STATE_OUT_ALERTING;
			break;
			case EPOINT_STATE_CONNECT:
			response->am[num].u.e.state = ADMIN_STATE_CONNECT;
			break;
			case EPOINT_STATE_IN_DISCONNECT:
			response->am[num].u.e.state = ADMIN_STATE_IN_DISCONNECT;
			break;
			case EPOINT_STATE_OUT_DISCONNECT:
			response->am[num].u.e.state = ADMIN_STATE_OUT_DISCONNECT;
			break;
			default:
			response->am[num].u.e.state = ADMIN_STATE_IDLE;
		}
		/* terminal */
		SCPY(response->am[num].u.e.terminal, apppbx->e_terminal);
		/* callerid */
		SCPY(response->am[num].u.e.callerid, apppbx->e_callerinfo.id);
		/* dialing */
		SCPY(response->am[num].u.e.dialing, apppbx->e_dialinginfo.number);
		/* action string */
		if (apppbx->e_action)
			SCPY(response->am[num].u.e.action, action_defs[apppbx->e_action->index].name);
//		if (apppbx->e_action)
//		printf("action=%s\n",action_defs[apppbx->e_action->index].name);
		/* park */
		response->am[num].u.e.park = apppbx->ea_endpoint->ep_park;
		if (apppbx->ea_endpoint->ep_park && apppbx->ea_endpoint->ep_park_len && apppbx->ea_endpoint->ep_park_len<=(int)sizeof(response->am[num].u.e.park_callid))
			memcpy(response->am[num].u.e.park_callid, apppbx->ea_endpoint->ep_park_callid, apppbx->ea_endpoint->ep_park_len);
		response->am[num].u.e.park_len = apppbx->ea_endpoint->ep_park_len;
		/* crypt */
		if (apppbx->e_crypt == CRYPT_ON)
			response->am[num].u.e.crypt = 1;
		/* */
		apppbx = apppbx->next;
		num++;
	}

	/* create response for all ports */
	port = port_first;
	while(port)
	{
		/* message */
		response->am[num].message = ADMIN_RESPONSE_S_PORT;
		/* serial */
		response->am[num].u.p.serial = port->p_serial;
		/* name */
		SCPY(response->am[num].u.p.name, port->p_name);
		/* epoint */
		response->am[num].u.p.epoint = ACTIVE_EPOINT(port->p_epointlist);
		/* state */
		switch(port->p_state)
		{
			case PORT_STATE_IN_SETUP:
			response->am[num].u.p.state = ADMIN_STATE_IN_SETUP;
			break;
			case PORT_STATE_OUT_SETUP:
			response->am[num].u.p.state = ADMIN_STATE_OUT_SETUP;
			break;
			case PORT_STATE_IN_OVERLAP:
			response->am[num].u.p.state = ADMIN_STATE_IN_OVERLAP;
			break;
			case PORT_STATE_OUT_OVERLAP:
			response->am[num].u.p.state = ADMIN_STATE_OUT_OVERLAP;
			break;
			case PORT_STATE_IN_PROCEEDING:
			response->am[num].u.p.state = ADMIN_STATE_IN_PROCEEDING;
			break;
			case PORT_STATE_OUT_PROCEEDING:
			response->am[num].u.p.state = ADMIN_STATE_OUT_PROCEEDING;
			break;
			case PORT_STATE_IN_ALERTING:
			response->am[num].u.p.state = ADMIN_STATE_IN_ALERTING;
			break;
			case PORT_STATE_OUT_ALERTING:
			response->am[num].u.p.state = ADMIN_STATE_OUT_ALERTING;
			break;
			case PORT_STATE_CONNECT:
			response->am[num].u.p.state = ADMIN_STATE_CONNECT;
			break;
			case PORT_STATE_IN_DISCONNECT:
			response->am[num].u.p.state = ADMIN_STATE_IN_DISCONNECT;
			break;
			case PORT_STATE_OUT_DISCONNECT:
			response->am[num].u.p.state = ADMIN_STATE_OUT_DISCONNECT;
			break;
			default:
			response->am[num].u.p.state = ADMIN_STATE_IDLE;
		}
		/* isdn */
		if ((port->p_type&PORT_CLASS_mISDN_MASK) == PORT_CLASS_mISDN_DSS1)
		{
			response->am[num].u.p.isdn = 1;
			pdss1 = (class Pdss1 *)port;
			response->am[num].u.p.isdn_chan = pdss1->p_m_b_channel;
			response->am[num].u.p.isdn_hold = pdss1->p_m_hold;
			response->am[num].u.p.isdn_ces = pdss1->p_m_d_ces;
		}
		/* */
		port = port->next;
		num++;
	}
	return(0);
}

int sockserial = 1; // must start with 1, because 0 is used if no serial is set
/*
 * handle admin socket (non blocking)
 */
int admin_handle(void)
{
	struct admin_list	*admin, **adminp;
	void			*temp;
	struct admin_message	msg;
	int			len;
	int			new_sock;
	socklen_t		sock_len = sizeof(sock_address);
	unsigned long		on = 1;
	int			work = 0; /* if work was done */
	struct Endpoint		*epoint;

	if (sock < 0)
		return(0);

	/* check for new incomming connections */
	if ((new_sock = accept(sock, (struct sockaddr *)&sock_address, &sock_len)) >= 0)
	{
		work = 1;
		/* insert new socket */
		admin = (struct admin_list *)malloc(sizeof(struct admin_list));
		if (admin)
		{
			if (ioctl(new_sock, FIONBIO, (unsigned char *)(&on)) >= 0)
			{
//#warning
//	PERROR("DEBUG incomming socket %d, serial=%d\n", new_sock, sockserial);
				memuse++;
				fhuse++;
				memset(admin, 0, sizeof(struct admin_list));
				admin->sockserial = sockserial++;
				admin->next = admin_list;
				admin_list = admin;
				admin->sock = new_sock;
			} else {
				close(new_sock);
				free(admin);
			}
		} else
			close(new_sock);
	} else
	{
		if (errno != EWOULDBLOCK)
		{
			PERROR("Failed to accept connection from socket \"%s\". (errno=%d) Closing socket.\n", sock_address.sun_path, errno);
			admin_cleanup();
			return(1);
		}
	}

	/* loop all current socket connections */
	admin = admin_list;
	adminp = &admin_list;
	while(admin)
	{
		/* read command */
		len = read(admin->sock, &msg, sizeof(msg));
		if (len < 0)
		{
			if (errno != EWOULDBLOCK)
			{
				work = 1;
				brokenpipe:
				printf("Broken pipe on socket %d. (errno=%d).\n", admin->sock, errno);
				PDEBUG(DEBUG_LOG, "Broken pipe on socket %d. (errno=%d).\n", admin->sock, errno);
				*adminp = admin->next;
				free_connection(admin);
				admin = *adminp;
				continue;
			}
			goto send_data;
		}
		work = 1;
//#warning
//PERROR("DEBUG socket %d got data. serial=%d\n", admin->sock, admin->sockserial);
		if (len == 0)
		{
			end:

			/*release endpoint if exists */
			if (admin->epointid)
			{
				epoint = find_epoint_id(admin->epointid);
				if (epoint)
				{
					((class DEFAULT_ENDPOINT_APP *)epoint->ep_app)->
						release(RELEASE_ALL, CAUSE_NORMAL, LOCATION_PRIVATE_LOCAL, 0, 0);
				}
			}

//#warning
//PERROR("DEBUG socket %d closed by remote.\n", admin->sock);
			*adminp = admin->next;
			free_connection(admin);
			admin = *adminp;
//PERROR("DEBUG (admin_list=%x)\n", admin_list);
			continue;
		}
		if (len != sizeof(msg))
		{
			PERROR("Short/long read on socket %d. (len=%d != size=%d).\n", admin->sock, len, sizeof(msg));
			*adminp = admin->next;
			free_connection(admin);
			admin = *adminp;
			continue;
		}
		/* process socket command */
		if (admin->response)
		{
			PERROR("Data from socket %d while sending response.\n", admin->sock);
			*adminp = admin->next;
			free_connection(admin);
			admin = *adminp;
			continue;
		}
		switch (msg.message)
		{
			case ADMIN_REQUEST_CMD_INTERFACE:
			if (admin_interface(&admin->response) < 0)
			{
				PERROR("Failed to create dial response for socket %d.\n", admin->sock);
				goto response_error;
			}
			break;

			case ADMIN_REQUEST_CMD_ROUTE:
			if (admin_route(&admin->response) < 0)
			{
				PERROR("Failed to create dial response for socket %d.\n", admin->sock);
				goto response_error;
			}
			break;

			case ADMIN_REQUEST_CMD_DIAL:
			if (admin_dial(&admin->response, msg.u.x.message) < 0)
			{
				PERROR("Failed to create dial response for socket %d.\n", admin->sock);
				goto response_error;
			}
			break;

			case ADMIN_REQUEST_CMD_RELEASE:
			if (admin_release(&admin->response, msg.u.x.message) < 0)
			{
				PERROR("Failed to create release response for socket %d.\n", admin->sock);
				goto response_error;
			}
			break;

			case ADMIN_REQUEST_STATE:
			if (admin_state(&admin->response) < 0)
			{
				PERROR("Failed to create state response for socket %d.\n", admin->sock);
				goto response_error;
			}
			case ADMIN_REQUEST_MESSAGE:
			if (admin_message(&admin->response) < 0)
			{
				PERROR("Failed to create message response for socket %d.\n", admin->sock);
				response_error:
				*adminp = admin->next;
				free_connection(admin);
				admin = *adminp;
				continue;
			}
#if 0
#warning DEBUGGING
{
	struct admin_queue	*response;
	printf("Chain: ");
	response = admin->response;
	while(response)
	{
		printf("%c", '0'+response->am[0].message);
		response=response->next;
	}
	printf("\n");
}
#endif
			break;

			case ADMIN_CALL_SETUP:
			if (admin_call(admin, &msg))
			{
				PERROR("Failed to create call for socket %d.\n", admin->sock);
				goto response_error;
			}
			break;

			default:
			PERROR("Invalid message %d from socket %d.\n", msg.message, admin->sock);
			*adminp = admin->next;
			free_connection(admin);
			admin = *adminp;
			continue;
		}
		/* write queue */
		send_data:
		if (admin->response)
		{
//#warning
//PERROR("DEBUG socket %d sending data.\n", admin->sock);
			len = write(admin->sock, ((unsigned char *)(admin->response->am))+admin->response->offset, sizeof(struct admin_message)*(admin->response->num)-admin->response->offset);
			if (len < 0)
			{
				if (errno != EWOULDBLOCK)
				{
					work = 1;
					goto brokenpipe;
				}
				goto next;
			}
			work = 1;
			if (len == 0)
				goto end;
			if (len < (int)(sizeof(struct admin_message)*(admin->response->num)-admin->response->offset))
			{
				admin->response->offset+=len;
				goto next;
			} else
			{
				temp = admin->response;
				admin->response = admin->response->next;
				free(temp);
				memuse--;
			}
		}
		/* done with socket instance */
		next:
		adminp = &admin->next;
		admin = admin->next;
	}

	return(work);
}

