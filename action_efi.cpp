/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** elektronische fernmelder identifikation                                   **
**                                                                           **
\*****************************************************************************/ 

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "main.h"

enum {
	EFI_STATE_DIE,
	EFI_STATE_BENUTZERDEFINIERTE,
	EFI_STATE_UNTERDRUECKTE,
	EFI_STATE_RUFNUMMER_LAUTET,
	EFI_STATE_DIGIT,
	EFI_STATE_ICH_WIEDERHOLE,
	EFI_STATE_STOP,
};

void EndpointAppPBX::action_init_efi(void)
{
//	int			language = e_ext.vbox_language;
//	struct route_param	*rparam;
	struct message		*message;
	struct port_list	*portlist = ea_endpoint->ep_portlist;

	/* if no caller id */
	if (e_callerinfo.id[0] == '\0')
	{
		/* facility rejected */
		message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_DISCONNECT);
		message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
		message->param.disconnectinfo.cause = CAUSE_FACILITYREJECTED;
		message_put(message);
		logmessage(message);
		new_state(EPOINT_STATE_OUT_DISCONNECT);
		set_tone(portlist,"cause_22");
		return;
	}

	/* connect */
	new_state(EPOINT_STATE_CONNECT);

	/* initialize the vbox */
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) initializing efi\n", ea_endpoint->ep_serial);

	e_efi_state = EFI_STATE_DIE;
	set_tone_efi("die");

	e_efi_digit = 0;
}

/*
 * the audio file has ended
 * this is called by Endpoint::message_port(), whenever an audio of has been received
 */
void EndpointAppPBX::efi_message_eof(void)
{
//	char buffer[32];
	char digit[] = "number_00";
	struct message		*message;
	struct port_list	*portlist = ea_endpoint->ep_portlist;

	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) terminal %s end of file during state: %d\n", ea_endpoint->ep_serial, e_ext.number, e_vbox_state);

	switch(e_efi_state)
	{
		case EFI_STATE_DIE:
		if (e_callerinfo.screen==INFO_SCREEN_USER)
		{
			e_efi_state = EFI_STATE_BENUTZERDEFINIERTE;
			set_tone_efi("benutzerdefinierte");
			break;
		}
		// fall through
		case EFI_STATE_BENUTZERDEFINIERTE:
		if (e_callerinfo.present==INFO_PRESENT_RESTRICTED)
		{
			e_efi_state = EFI_STATE_UNTERDRUECKTE;
			set_tone_efi("unterdrueckte");
			break;
		}
		// fall through
		case EFI_STATE_UNTERDRUECKTE:
		e_efi_state = EFI_STATE_RUFNUMMER_LAUTET;
		set_tone_efi("rufnummer_lautet");
		break;

		case EFI_STATE_RUFNUMMER_LAUTET:
		e_efi_state = EFI_STATE_DIGIT;
		e_efi_digit = 0;
		// fall through
		case EFI_STATE_DIGIT:
		digit[8] = numberrize_callerinfo(e_callerinfo.id,e_callerinfo.ntype)[e_efi_digit];
		if (digit[8])
		{
			set_tone_efi(digit);
			e_efi_digit++;
		} else
		{
			e_efi_state = EFI_STATE_STOP; //EFI_STATE_ICH_WIEDERHOLE;
			message = message_create(ea_endpoint->ep_serial, portlist->port_id, EPOINT_TO_PORT, MESSAGE_DISCONNECT);
			message->param.disconnectinfo.location = LOCATION_PRIVATE_LOCAL;
			message->param.disconnectinfo.cause = CAUSE_NORMAL;
			message_put(message);
			logmessage(message);
			new_state(EPOINT_STATE_OUT_DISCONNECT);
			set_tone(portlist,"cause_10");
//			set_tone_efi("ich_wiederhole");
		}
		break;

		case EFI_STATE_ICH_WIEDERHOLE:
		e_efi_state = EFI_STATE_DIE;
		set_tone_efi("die");
		break;

		case EFI_STATE_STOP:
		break;

		default:
		PERROR("efi_message_eof(ep%d): terminal %s unknown state: %d\n", ea_endpoint->ep_serial, e_ext.number, e_vbox_state);
	}
}



/*
 * set the given vbox-tone with full path (without appending)
 * the tone is played and after eof, a message is received
 */
void EndpointAppPBX::set_tone_efi(char *tone)
{
	struct message *message;

	if (tone == NULL)
		tone = "";

	if (!ea_endpoint->ep_portlist)
	{
		PERROR("EPOINT(%d) no portlist\n", ea_endpoint->ep_serial);
	}
	message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_portlist->port_id, EPOINT_TO_PORT, MESSAGE_VBOX_TONE);
	SCPY(message->param.tone.dir, (char *)"tones_efi");
	SCPY(message->param.tone.name, tone);
	message_put(message);

	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) terminal %s set tone '%s'\n", ea_endpoint->ep_serial, e_ext.number, tone);
}

