/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** Socket link server                                                        **
**                                                                           **
\*****************************************************************************/

#include "main.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <curses.h>


char socket_name[128];
int sock = -1;
struct sockaddr_un sock_address;

struct admin_list *admin_first = NULL;
static struct lcr_fd admin_fd;

int admin_handle(struct lcr_fd *fd, unsigned int what, void *instance, int index);

/*
 * initialize admin socket 
 */
int admin_init(void)
{
	/* open and bind socket */
	if ((sock = socket(PF_UNIX, SOCK_STREAM, 0)) < 0) {
		PERROR("Failed to create admin socket. (errno=%d)\n", errno);
		return(-1);
	}
	fhuse++;
	memset(&sock_address, 0, sizeof(sock_address));
	SPRINT(socket_name, SOCKET_NAME, options.lock);
	sock_address.sun_family = AF_UNIX;
	UCPY(sock_address.sun_path, socket_name);
	unlink(socket_name);
	if (bind(sock, (struct sockaddr *)(&sock_address), SUN_LEN(&sock_address)) < 0) {
		close(sock);
		unlink(socket_name);
		fhuse--;
		sock = -1;
		PERROR("Failed to bind admin socket to \"%s\". (errno=%d)\n", sock_address.sun_path, errno);
		return(-1);
	}
	if (listen(sock, 5) < 0) {
		close(sock);
		unlink(socket_name);
		fhuse--;
		sock = -1;
		PERROR("Failed to listen to socket \"%s\". (errno=%d)\n", sock_address.sun_path, errno);
		return(-1);
	}
	memset(&admin_fd, 0, sizeof(admin_fd));
	admin_fd.fd = sock;
	register_fd(&admin_fd, LCR_FD_READ | LCR_FD_EXCEPT, admin_handle, NULL, 0);
	if (chmod(socket_name, options.socketrights) < 0) {
		PERROR("Failed to change socket rights to %d. (errno=%d)\n", options.socketrights, errno);
	}
	if (chown(socket_name, options.socketuser, options.socketgroup) < 0) {
		PERROR("Failed to change socket user/group to %d/%d. (errno=%d)\n", options.socketuser, options.socketgroup, errno);
	}

	return(0);
}


/*
 * free connection
 * also releases all remote joins
 */
void free_connection(struct admin_list *admin)
{
	struct admin_queue *response;
	void *temp;
	union parameter param;
	class Join *join, *joinnext;
	struct mISDNport *mISDNport;
	int i, ii;
	struct admin_list **adminp;

	/* free remote joins */
	if (admin->remote_name[0]) {
		start_trace(-1,
			NULL,
			NULL,
			NULL,
			DIRECTION_NONE,
	   		0,
			0,
			"REMOTE APP release");
		add_trace("app", "name", "%s", admin->remote_name);
		end_trace();
		/* release all exported channels */
		mISDNport = mISDNport_first;
		while(mISDNport) {
			i = 0;
			ii = mISDNport->b_num;
			while(i < ii) {
				if (mISDNport->b_remote_id[i] == admin->sock) {
					mISDNport->b_state[i] = B_STATE_IDLE;
					unsched_timer(&mISDNport->b_timer[i]);
					mISDNport->b_remote_id[i] = 0;
					mISDNport->b_remote_ref[i] = 0;
				}
				i++;
			}
			mISDNport = mISDNport->next;
		}
		/* release join */
		join = join_first;
		while(join) {
			joinnext = join->next;
			if (join->j_type==JOIN_TYPE_REMOTE) if (((class JoinRemote *)join)->j_remote_id == admin->sock) {
				memset(&param, 0, sizeof(param));
				param.disconnectinfo.cause = CAUSE_OUTOFORDER;
				param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
				((class JoinRemote *)join)->message_remote(MESSAGE_RELEASE, &param);
				/* join is now destroyed, so we go to next join */
			}
			join = joinnext;
		}
	}

	if (admin->sock >= 0) {
		unregister_fd(&admin->fd);
		close(admin->sock);
		fhuse--;
	}
	response = admin->response;
	while (response) {
		temp = response->next;
		FREE(response, 0);
		memuse--;
		response = (struct admin_queue *)temp;
	}

	adminp = &admin_first;
	while(*adminp) {
		if (*adminp == admin)
			break;
		adminp = &((*adminp)->next);
	}
	if (*adminp)
		*adminp = (*adminp)->next;

	FREE(admin, 0);
	memuse--;
}


/*
 * cleanup admin socket 
 */
void admin_cleanup(void)
{
	struct admin_list *admin, *next;;

	admin = admin_first;
	while(admin) {
		next = admin->next;
		free_connection(admin);
		admin = next;
	}

	if (sock >= 0) {
		unregister_fd(&admin_fd);
		close(sock);
		fhuse--;
	}

	unlink(socket_name);
}


/*
 * do interface reload
 */
int admin_interface(struct admin_queue **responsep)
{
	struct admin_queue	*response;	/* response pointer */
	const char		*err_txt = "";
	int			err = 0;

        if (read_interfaces()) {
       		relink_interfaces();
		free_interfaces(interface_first);
		interface_first = interface_newlist;
		interface_newlist = NULL;
	} else {
		err_txt = interface_error;
		err = -1;
	}
	/* create state response */
	response = (struct admin_queue *)MALLOC(sizeof(struct admin_queue)+sizeof(admin_message));
	memuse++;
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
	while(apppbx) {
		n++;
		apppbx = apppbx->next;
	}
	if (apppbx_first) {
		SPRINT(err_txt, "Cannot reload routing, because %d endpoints active\n", n);
		err = -1;
		goto response;
	}
#endif
        if (!(ruleset_new = ruleset_parse())) {
		SPRINT(err_txt, ruleset_error);
		err = -1;
		goto response;
	}
	ruleset_free(ruleset_first);
	ruleset_first = ruleset_new;
	ruleset_main = getrulesetbyname("main");
	if (!ruleset_main) {
		SPRINT(err_txt, "Ruleset reloaded, but rule 'main' not found.\n");
		err = -1;
	}
	apppbx = apppbx_first;
	while(apppbx) {
		if (apppbx->e_action) {
			switch(apppbx->e_action->index) {
				case ACTION_INTERNAL:
				apppbx->e_action = &action_internal;
				break;
				case ACTION_EXTERNAL:
				apppbx->e_action = &action_external;
				break;
				case ACTION_REMOTE:
				apppbx->e_action = &action_remote;
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
		} else if (apppbx->e_state != EPOINT_STATE_CONNECT) {
			release:
			unsched_timer(&apppbx->e_callback_timeout);
			apppbx->e_action = NULL;
			apppbx->release(RELEASE_ALL, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL);
			start_trace(-1,
				NULL,
				numberrize_callerinfo(apppbx->e_callerinfo.id, apppbx->e_callerinfo.ntype, options.national, options.international),
				apppbx->e_dialinginfo.id,
				DIRECTION_NONE,
		   		CATEGORY_EP,
				apppbx->ea_endpoint->ep_serial,
				"KICK (reload routing)");
			end_trace();
		}

		unsched_timer(&apppbx->e_action_timeout);
		apppbx->e_rule = NULL;
		apppbx->e_ruleset = NULL;

		apppbx = apppbx->next;
	}

	response:
	/* create state response */
	response = (struct admin_queue *)MALLOC(sizeof(struct admin_queue)+sizeof(admin_message));
	memuse++;
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
	response = (struct admin_queue *)MALLOC(sizeof(struct admin_queue)+sizeof(admin_message));
	memuse++;
	response->num = 1;
	/* message */
	response->am[0].message = ADMIN_RESPONSE_CMD_DIAL;

	/* process request */
	if (!(p = strchr(message,':'))) {
		response->am[0].u.x.error = -EINVAL;
		SPRINT(response->am[0].u.x.message, "no seperator ':' in message to seperate number from extension");
		goto out;
	}
	*p++ = 0;

	/* modify extension */
	if (!read_extension(&ext, message)) {
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
 * do tracing
 */
int admin_trace(struct admin_list *admin, struct admin_trace_req *trace)
{
	memcpy(&admin->trace, trace, sizeof(struct admin_trace_req));
	return(0);
}


/*
 * do blocking
 * 
 * 0 = make port available
 * 1 = make port administratively blocked
 * 2 = unload port
 * the result is returned:
 * 0 = port is now available
 * 1 = port is now blocked
 * 2 = port cannot be loaded or has been unloaded
 * -1 = port doesn't exist
 */
int admin_block(struct admin_queue **responsep, int portnum, int block)
{
	struct admin_queue	*response;	/* response pointer */
	struct interface	*interface;
	struct interface_port	*ifport;

	/* create block response */
	response = (struct admin_queue *)MALLOC(sizeof(struct admin_queue)+sizeof(admin_message));
	memuse++;
	response->num = 1;
	/* message */
	response->am[0].message = ADMIN_RESPONSE_CMD_BLOCK;
	response->am[0].u.x.portnum = portnum;

	/* search for port */
	ifport = NULL;
	interface = interface_first;
	while(interface) {
		ifport = interface->ifport;
		while(ifport) {
			if (ifport->portnum == portnum)
				break;
			ifport = ifport->next;
		}
		if (ifport)
			break;
		interface = interface->next;
	}
	/* not found, we return -1 */
	if (!ifport) {
		response->am[0].u.x.block = -1;
		response->am[0].u.x.error = 1;
		SPRINT(response->am[0].u.x.message, "Port %d does not exist.", portnum);
		goto out;
	}

	/* no interface */
	if (!ifport->mISDNport) {
		/* not loaded anyway */
		if (block >= 2) {
			response->am[0].u.x.block = 2;
			goto out;
		}

		/* try loading interface */
		ifport->block = block;
		load_port(ifport);

		/* port cannot load */
		if (ifport->block >= 2) {
			response->am[0].u.x.block = 2;
			response->am[0].u.x.error = 1;
			SPRINT(response->am[0].u.x.message, "Port %d will not load.", portnum);
			goto out;
		}

		/* port loaded */
		response->am[0].u.x.block = ifport->block;
		goto out;
	}

	/* if we shall unload interface */
	if (block >= 2) {
		mISDNport_close(ifport->mISDNport);
		ifport->mISDNport = 0;
		ifport->block = 2;
		goto out;
	}
	
	/* port new blocking state */
	ifport->block = response->am[0].u.x.block = block;

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
	unsigned int		id;
	struct admin_queue	*response;	/* response pointer */
	class EndpointAppPBX	*apppbx;

	/* create state response */
	response = (struct admin_queue *)MALLOC(sizeof(struct admin_queue)+sizeof(admin_message));
	memuse++;
	response->num = 1;
	/* message */
	response->am[0].message = ADMIN_RESPONSE_CMD_RELEASE;

	id = atoi(message);
	apppbx = apppbx_first;
	while(apppbx) {
		if (apppbx->ea_endpoint->ep_serial == id)
			break;
		apppbx = apppbx->next;
	}
	if (!apppbx) {
		response->am[0].u.x.error = -EINVAL;
		SPRINT(response->am[0].u.x.message, "Given endpoint %d doesn't exist.", id);
		goto out;
	}

	unsched_timer(&apppbx->e_callback_timeout);
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

	if (!(epoint = new Endpoint(0, 0)))
		FATAL("No memory for Endpoint instance\n");
	if (!(epoint->ep_app = apppbx = new DEFAULT_ENDPOINT_APP(epoint, 1))) // outgoing
		FATAL("No memory for Endpoint Application instance\n");
	apppbx->e_adminid = admin->sockserial;
	admin->epointid = epoint->ep_serial;
	SCPY(apppbx->e_callerinfo.id, nationalize_callerinfo(msg->u.call.callerid, &apppbx->e_callerinfo.ntype, options.national, options.international));
	if (msg->u.call.present)
		apppbx->e_callerinfo.present = INFO_PRESENT_ALLOWED;
	else
		apppbx->e_callerinfo.present = INFO_PRESENT_RESTRICTED;
	apppbx->e_callerinfo.screen = INFO_SCREEN_NETWORK;


	apppbx->e_capainfo.bearer_capa = msg->u.call.bc_capa;
	apppbx->e_capainfo.bearer_mode = msg->u.call.bc_mode;
	apppbx->e_capainfo.bearer_info1 = msg->u.call.bc_info1;
	apppbx->e_capainfo.hlc = msg->u.call.hlc;
	apppbx->e_capainfo.exthlc = msg->u.call.exthlc;
	SCPY(apppbx->e_dialinginfo.id, msg->u.call.dialing);
	SCPY(apppbx->e_dialinginfo.interfaces, msg->u.call.interface);
	apppbx->e_dialinginfo.sending_complete = 1;

	apppbx->new_state(PORT_STATE_OUT_SETUP);
	apppbx->out_setup();
	return(0);
}


/*
 * this function is called for response whenever a call state changes.
 */
void admin_call_response(int adminid, int message, const char *connected, int cause, int location, int notify)
{
	struct admin_list	*admin;
	struct admin_queue	*response, **responsep;	/* response pointer */

	/* searching for admin id
	 * maybe there is no admin instance, because the calling port was not
	 * initiated by admin_call */
	admin = admin_first;
	while(admin) {
		if (adminid == admin->sockserial)
			break;
		admin = admin->next;
	}
	if (!admin)
		return;

	/* seek to end of response list */
	response = admin->response;
	responsep = &admin->response;
	while(response) {
		responsep = &response->next;
		response = response->next;
	}

	/* create state response */
	response = (struct admin_queue *)MALLOC(sizeof(struct admin_queue)+sizeof(admin_message));
	memuse++;
	response->num = 1;
	/* message */
	response->am[0].message = message;

	SCPY(response->am[0].u.call.callerid, connected);
	response->am[0].u.call.cause = cause;
	response->am[0].u.call.location = location;
	response->am[0].u.call.notify = notify;

	/* attach to response chain */
	*responsep = response;
	responsep = &response->next;
	admin->fd.when |= LCR_FD_WRITE;
}


/*
 * send data to the remote socket join instance
 */
int admin_message_to_join(struct admin_msg *msg, struct admin_list *admin)
{
	class Join			*join;
	struct admin_list		*temp;

	/* hello message */
	if (msg->type == MESSAGE_HELLO) {
		if (admin->remote_name[0]) {
			PERROR("Remote application repeats hello message.\n");
			return(-1);
		}
		/* look for second application */
		temp = admin_first;
		while(temp) {
			if (!strcmp(temp->remote_name, msg->param.hello.application))
				break;
			temp = temp->next;
		}
		if (temp) {
			PERROR("Remote application connects twice??? (ignoring)\n");
			return(-1);
		}
		/* set remote socket instance */
		SCPY(admin->remote_name, msg->param.hello.application);
		start_trace(-1,
			NULL,
			NULL,
			NULL,
			DIRECTION_NONE,
	   		0,
			0,
			"REMOTE APP registers");
		add_trace("app", "name", "%s", admin->remote_name);
		end_trace();
		return(0);
	}

	/* check we have no application name */
	if (!admin->remote_name[0]) {
		PERROR("Remote application did not send us a hello message.\n");
		return(-1);
	}

	/* new join */
	if (msg->type == MESSAGE_NEWREF) {
		/* create new join instance */
		join = new JoinRemote(0, admin->remote_name, admin->sock); // must have no serial, because no endpoint is connected
		if (!join) {
			FATAL("No memory for remote join instance\n");
			return(-1);
		}
		return(0);
	}

	/* bchannel message
	 * no ref given for *_ack */
	if (msg->type == MESSAGE_BCHANNEL)
	if (msg->param.bchannel.type == BCHANNEL_ASSIGN_ACK
	 || msg->param.bchannel.type == BCHANNEL_REMOVE_ACK
	 || msg->param.bchannel.type == BCHANNEL_RELEASE) {
		/* no ref, but address */
		message_bchannel_from_remote(NULL, msg->param.bchannel.type, msg->param.bchannel.handle);
		return(0);
	}
	
	/* check for ref */
	if (!msg->ref) {
		PERROR("Remote application did not send us a valid ref with a message.\n");
		return(-1);
	}

	/* find join instance */
	join = join_first;
	while(join) {
		if (join->j_serial == msg->ref)
			break;
		join = join->next;
	}
	if (!join) {
		PDEBUG(DEBUG_LOG, "No join found with serial %d. (May have been already released.)\n", msg->ref);
		return(0);
	}

	/* check application */
	if (join->j_type != JOIN_TYPE_REMOTE) {
		PERROR("Ref %d does not belong to a remote join instance.\n", msg->ref);
		return(-1);
	}
	if (admin->sock != ((class JoinRemote *)join)->j_remote_id) {
		PERROR("Ref %d belongs to remote application %s, but not to sending application %s.\n", msg->ref, ((class JoinRemote *)join)->j_remote_name, admin->remote_name);
		return(-1);
	}

	/* send message */
	((class JoinRemote *)join)->message_remote(msg->type, &msg->param);

	return(0);
}


/*
 * this function is called for every message to remote socket
 */
int admin_message_from_join(int remote_id, unsigned int ref, int message_type, union parameter *param)
{
	struct admin_list	*admin;
	struct admin_queue	**responsep;	/* response pointer */

	/* searching for admin id
	 * maybe there is no given remote application
	 */
	admin = admin_first;
	while(admin) {
		if (admin->remote_name[0] && admin->sock==remote_id)
			break;
		admin = admin->next;
	}
	/* no given remote application connected */
	if (!admin)
		return(-1);

	/* seek to end of response list */
	responsep = &admin->response;
	while(*responsep) {
		responsep = &(*responsep)->next;
	}

	/* create state response */
	*responsep = (struct admin_queue *)MALLOC(sizeof(struct admin_queue)+sizeof(admin_message));
	memuse++;
	(*responsep)->num = 1;

	/* message */
	(*responsep)->am[0].message = ADMIN_MESSAGE;
	(*responsep)->am[0].u.msg.type = message_type;
	(*responsep)->am[0].u.msg.ref = ref;
	memcpy(&(*responsep)->am[0].u.msg.param, param, sizeof(union parameter));
	admin->fd.when |= LCR_FD_WRITE;
	return(0);
}


/*
 * do state debugging
 */
int admin_state(struct admin_queue **responsep)
{
	class Port		*port;
	class EndpointAppPBX	*apppbx;
	class Join		*join;
	class Pdss1		*pdss1;
	struct interface	*interface;
	struct interface_port	*ifport;
	struct mISDNport	*mISDNport;
	int			i;
	int			num;
	int			anybusy;
	struct admin_queue	*response;
	struct admin_list	*admin;
	struct tm		*now_tm;
	time_t			now;

	/* create state response */
	response = (struct admin_queue *)MALLOC(sizeof(struct admin_queue)+sizeof(admin_message));
	memuse++;
	response->num = 1;
	/* message */
	response->am[0].message = ADMIN_RESPONSE_STATE;
	/* version */
	SCPY(response->am[0].u.s.version_string, VERSION_STRING);
	/* time */
	time(&now);
	now_tm = localtime(&now);
	memcpy(&response->am[0].u.s.tm, now_tm, sizeof(struct tm));
	/* log file */
	SCPY(response->am[0].u.s.logfile, options.log);
	/* interface count */
	i = 0;
	interface = interface_first;
	while(interface) {
		ifport = interface->ifport;
		while(ifport) {
			i++;
			ifport = ifport->next;
		}
		interface = interface->next;
	}
	response->am[0].u.s.interfaces = i;
	/* remote connection count */
	i = 0;
	admin = admin_first;
	while(admin) {
		if (admin->remote_name[0])
			i++;
		admin = admin->next;
	}
	response->am[0].u.s.remotes = i;
	/* join count */
	join = join_first;
	i = 0;
	while(join) {
		i++;
		join = join->next;
	}
	response->am[0].u.s.joins = i;
	/* apppbx count */
	apppbx = apppbx_first;
	i = 0;
	while(apppbx) {
		i++;
		apppbx = apppbx->next;
	}
	response->am[0].u.s.epoints = i;
	/* port count */
	i = 0;
	port = port_first;
	while(port) {
		i++;
		port = port->next;
	}
	response->am[0].u.s.ports = i;
	/* attach to response chain */
	*responsep = response;
	responsep = &response->next;

	/* create response for all instances */
	num = (response->am[0].u.s.interfaces)
	    + (response->am[0].u.s.remotes)
	    + (response->am[0].u.s.joins)
	    + (response->am[0].u.s.epoints)
	    + (response->am[0].u.s.ports);
	if (num == 0)
		return(0);
	response = (struct admin_queue *)MALLOC(sizeof(admin_queue)+(num*sizeof(admin_message)));
	memuse++;
	response->num = num;
	*responsep = response;
	responsep = &response->next;
	interface = interface_first;
	num = 0;
	while(interface) {
		ifport = interface->ifport;
		while(ifport) {
			/* message */
			response->am[num].message = ADMIN_RESPONSE_S_INTERFACE;
			/* interface */
			SCPY(response->am[num].u.i.interface_name, interface->name);
			/* portnum */
			response->am[num].u.i.portnum = ifport->portnum;
			/* portname */
			SCPY(response->am[num].u.i.portname, ifport->portname);
			/* iftype */
			response->am[num].u.i.extension = interface->extension;
			/* block */
			response->am[num].u.i.block = ifport->block;
			if (ifport->mISDNport) {
				mISDNport = ifport->mISDNport;

				/* ptp */
				response->am[num].u.i.ptp = mISDNport->ptp;
				/* l1hold */
				response->am[num].u.i.l1hold = mISDNport->l1hold;
				/* l2hold */
				response->am[num].u.i.l2hold = mISDNport->l2hold;
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
				memcpy(response->am[num].u.i.l2mask, mISDNport->l2mask, 16);
				/* los */
				response->am[num].u.i.los = mISDNport->los;
				/* ais */
				response->am[num].u.i.ais = mISDNport->ais;
				/* rdi */
				response->am[num].u.i.rdi = mISDNport->rdi;
				/* slip */
				response->am[num].u.i.slip_tx = mISDNport->slip_tx;
				response->am[num].u.i.slip_rx = mISDNport->slip_rx;
				/* channels */
				response->am[num].u.i.channels = mISDNport->b_num;
				/* channel info */
				i = 0;
				anybusy = 0;
				while(i < mISDNport->b_num) {
					response->am[num].u.i.busy[i] = mISDNport->b_state[i];
					if (mISDNport->b_port[i])
						response->am[num].u.i.port[i] = mISDNport->b_port[i]->p_serial;
					response->am[num].u.i.mode[i] = mISDNport->b_mode[i];
					i++;
				}
			}
			num++;

			ifport = ifport->next;
		}
		interface = interface->next;
	}

	/* create response for all remotes */
	admin = admin_first;
	while(admin) {
		if (admin->remote_name[0]) {
			/* message */
			response->am[num].message = ADMIN_RESPONSE_S_REMOTE;
			/* name */
			SCPY(response->am[num].u.r.name, admin->remote_name);
			/* */
			num++;
		}
		admin = admin->next;
	}

	/* create response for all joins */
	join = join_first;
	while(join) {
		/* message */
		response->am[num].message = ADMIN_RESPONSE_S_JOIN;
		/* serial */
		response->am[num].u.j.serial = join->j_serial;
		/* partyline */
		if (join->j_type == JOIN_TYPE_PBX)
			response->am[num].u.j.partyline = ((class JoinPBX *)join)->j_partyline;
		/* remote application */
		if (join->j_type == JOIN_TYPE_REMOTE)
			SCPY(response->am[num].u.j.remote, ((class JoinRemote *)join)->j_remote_name);
		/* */
		join = join->next;
		num++;
	}

	/* create response for all endpoint */
	apppbx = apppbx_first;
	while(apppbx) {
		/* message */
		response->am[num].message = ADMIN_RESPONSE_S_EPOINT;
		/* serial */
		response->am[num].u.e.serial = apppbx->ea_endpoint->ep_serial;
		/* join */
		response->am[num].u.e.join = apppbx->ea_endpoint->ep_join_id;
		/* rx notification */
		response->am[num].u.e.rx_state = apppbx->e_rx_state;
		/* tx notification */
		response->am[num].u.e.tx_state = apppbx->e_tx_state;
		/* state */
		switch(apppbx->e_state) {
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
		SCPY(response->am[num].u.e.terminal, apppbx->e_ext.number);
		/* callerid */
		SCPY(response->am[num].u.e.callerid, apppbx->e_callerinfo.id);
		/* dialing */
		SCPY(response->am[num].u.e.dialing, apppbx->e_dialinginfo.id);
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
	while(port) {
		/* message */
		response->am[num].message = ADMIN_RESPONSE_S_PORT;
		/* serial */
		response->am[num].u.p.serial = port->p_serial;
		/* name */
		SCPY(response->am[num].u.p.name, port->p_name);
		/* epoint */
		response->am[num].u.p.epoint = ACTIVE_EPOINT(port->p_epointlist);
		/* state */
		switch(port->p_state) {
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
			case PORT_STATE_RELEASE:
			response->am[num].u.p.state = ADMIN_STATE_RELEASE;
			break;
			default:
			response->am[num].u.p.state = ADMIN_STATE_IDLE;
		}
		/* isdn */
		if ((port->p_type&PORT_CLASS_mISDN_MASK) == PORT_CLASS_mISDN_DSS1) {
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
int admin_handle_con(struct lcr_fd *fd, unsigned int what, void *instance, int index);

int admin_handle(struct lcr_fd *fd, unsigned int what, void *instance, int index)
{
	int			new_sock;
	socklen_t		sock_len = sizeof(sock_address);
	struct admin_list	*admin;

	/* check for new incoming connections */
	if ((new_sock = accept(sock, (struct sockaddr *)&sock_address, &sock_len)) >= 0) {
		/* insert new socket */
		admin = (struct admin_list *)MALLOC(sizeof(struct admin_list));
		memuse++;
		fhuse++;
		admin->sockserial = sockserial++;
		admin->next = admin_first;
		admin_first = admin;
		admin->sock = new_sock;
		admin->fd.fd = new_sock;
		register_fd(&admin->fd, LCR_FD_READ | LCR_FD_EXCEPT, admin_handle_con, admin, 0);
	} else {
		if (errno != EWOULDBLOCK) {
			PERROR("Failed to accept connection from socket \"%s\". (errno=%d) Closing socket.\n", sock_address.sun_path, errno);
			admin_cleanup();
			return 0;
		}
	}

	return 0;
}

int admin_handle_con(struct lcr_fd *fd, unsigned int what, void *instance, int index)
{
	struct admin_list *admin = (struct admin_list *)instance;
	void			*temp;
	struct admin_message	msg;
	int			len;
	struct Endpoint		*epoint;

	if ((what & LCR_FD_READ)) {
		/* read command */
		len = read(admin->sock, &msg, sizeof(msg));
		if (len < 0) {
			brokenpipe:
			PDEBUG(DEBUG_LOG, "Broken pipe on socket %d. (errno=%d).\n", admin->sock, errno);
			free_connection(admin);
			return 0;
		}
		if (len == 0) {
			end:

			/*release endpoint if exists */
			if (admin->epointid) {
				epoint = find_epoint_id(admin->epointid);
				if (epoint) {
					((class DEFAULT_ENDPOINT_APP *)epoint->ep_app)->
						release(RELEASE_ALL, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL, LOCATION_PRIVATE_LOCAL, CAUSE_NORMAL);
				}
			}

			free_connection(admin);
			return 0;
		}
		if (len != sizeof(msg)) {
			PERROR("Short/long read on socket %d. (len=%d != size=%d).\n", admin->sock, len, sizeof(msg));
			free_connection(admin);
			return 0;
		}
		/* process socket command */
		if (admin->response && msg.message != ADMIN_MESSAGE) {
			PERROR("Data from socket %d while sending response.\n", admin->sock);
			free_connection(admin);
			return 0;
		}
		switch (msg.message) {
			case ADMIN_REQUEST_CMD_INTERFACE:
			if (admin_interface(&admin->response) < 0) {
				PERROR("Failed to create dial response for socket %d.\n", admin->sock);
				goto response_error;
			}
			admin->fd.when |= LCR_FD_WRITE;
			break;

			case ADMIN_REQUEST_CMD_ROUTE:
			if (admin_route(&admin->response) < 0) {
				PERROR("Failed to create dial response for socket %d.\n", admin->sock);
				goto response_error;
			}
			admin->fd.when |= LCR_FD_WRITE;
			break;

			case ADMIN_REQUEST_CMD_DIAL:
			if (admin_dial(&admin->response, msg.u.x.message) < 0) {
				PERROR("Failed to create dial response for socket %d.\n", admin->sock);
				goto response_error;
			}
			admin->fd.when |= LCR_FD_WRITE;
			break;

			case ADMIN_REQUEST_CMD_RELEASE:
			if (admin_release(&admin->response, msg.u.x.message) < 0) {
				PERROR("Failed to create release response for socket %d.\n", admin->sock);
				goto response_error;
			}
			admin->fd.when |= LCR_FD_WRITE;
			break;

			case ADMIN_REQUEST_STATE:
			if (admin_state(&admin->response) < 0) {
				PERROR("Failed to create state response for socket %d.\n", admin->sock);
				goto response_error;
			}
			admin->fd.when |= LCR_FD_WRITE;
			break;

			case ADMIN_TRACE_REQUEST:
			if (admin_trace(admin, &msg.u.trace_req) < 0) {
				PERROR("Failed to create trace response for socket %d.\n", admin->sock);
				goto response_error;
			}
			admin->fd.when |= LCR_FD_WRITE;
			break;

			case ADMIN_REQUEST_CMD_BLOCK:
			if (admin_block(&admin->response, msg.u.x.portnum, msg.u.x.block) < 0) {
				PERROR("Failed to create block response for socket %d.\n", admin->sock);
				goto response_error;
			}
			admin->fd.when |= LCR_FD_WRITE;
			break;

			case ADMIN_MESSAGE:
			if (admin_message_to_join(&msg.u.msg, admin) < 0) {
				PERROR("Failed to deliver message for socket %d.\n", admin->sock);
				goto response_error;
			}
			break;

			case ADMIN_CALL_SETUP:
			if (admin_call(admin, &msg) < 0) {
				PERROR("Failed to create call for socket %d.\n", admin->sock);
				response_error:
				free_connection(admin);
				return 0;
			}
			break;

			default:
			PERROR("Invalid message %d from socket %d.\n", msg.message, admin->sock);
			free_connection(admin);
			return 0;
		}
	}

	if ((what & LCR_FD_WRITE)) {
		/* write queue */
		if (admin->response) {
			len = write(admin->sock, ((unsigned char *)(admin->response->am))+admin->response->offset, sizeof(struct admin_message)*(admin->response->num)-admin->response->offset);
			if (len < 0) {
				goto brokenpipe;
			}
			if (len == 0)
				goto end;
			if (len < (int)(sizeof(struct admin_message)*(admin->response->num) - admin->response->offset)) {
				admin->response->offset+=len;
				return 0;
			} else {
				temp = admin->response;
				admin->response = admin->response->next;
				FREE(temp, 0);
				memuse--;
			}
		} else
			admin->fd.when &= ~LCR_FD_WRITE;
	}

	return 0;
}

