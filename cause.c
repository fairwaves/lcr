/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** cause database                                                            **
**                                                                           **
\*****************************************************************************/ 

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "macro.h"
#include "cause.h"
#include "extension.h"

struct isdn_cause isdn_cause[128] = {
			/********************************* - **/ /*38*/
	{ /*0*/		"<No cause>",
			"<Kein Grund>" },
	{ /*1*/		"Unallocated number",
			"Nummer nicht vergeben" },
	{ /*2*/		"No route to transit network",
			"Keine Verbindung zum Netz" },
	{ /*3*/		"No route to destination",
			"Zielnummer nicht erreichbar" },
	{ /*4*/		"<Listen to announcement...>",
			"<Ansage hoeren...>" },
	{ /*5*/		"Misdialed trunk prefix.",
			"Falscher Carrier-Code" },
	{ /*6*/		"Channel unacceptable",
			"Kanal nicht akzeptiert" },
	{ /*7*/		"",
			"" },
	{ /*8*/		"Preemption",
			"Vorkauf" },
	{ /*9*/		"Preemption - circuit reserved",
			"Vorkauf - Gasse reserviert" },
	{ /*10*/	"",
			"" },
	{ /*11*/	"",
			"" },
	{ /*12*/	"",
			"" },
	{ /*13*/	"",
			"" },
	{ /*14*/	"",
			"" },
	{ /*15*/	"",
			"" },
	{ /*16*/	"Normal call clearing",
			"Normaler Verbindungsabbau" },
	{ /*17*/	"User busy",
			"Teilnehmer besetzt" },
	{ /*18*/	"No user responding",
			"Teilnehmer antwortet nicht" },
	{ /*19*/	"No answer from user",
			"Teilnehmer nimmt nicht ab" },
	{ /*20*/	"Subscriber absent",
			"Teilnehmer nicht anwesend" },
	{ /*21*/	"Call rejected",
			"Gespraech abgewiesen" },
	{ /*22*/	"Number changed",
			"Rufnummer hat sich geaendert" },
	{ /*23*/	"",
			"" },
	{ /*24*/	"",
			"" },
	{ /*25*/	"",
			"" },
	{ /*26*/	"Non-selected user clearing",
			"Gespraech woanders angenommen" },
	{ /*27*/	"Destination out of order",
			"Gegenstelle ausser Betrieb" },
	{ /*28*/	"Invalid number (incomplete)",
			"Fehlerhafte Nummer (n. komplett)" },
	{ /*29*/	"Facility rejected",
			"Funktion nicht verfuegbar" },
	{ /*30*/	"",
			"" },
	{ /*31*/	"Normal, unspecified",
			"Normal, unspezifisch" },
	{ /*32*/	"",
			"" },
	{ /*33*/	"",
			"" },
	{ /*34*/	"No circuit/channel available",
			"Keine Gasse/Kanal verfuegbar" },
	{ /*35*/	"",
			"" },
	{ /*36*/	"",
			"" },
	{ /*37*/	"",
			"" },
	{ /*38*/	"Network out of order",
			"Netz ausser Betrieb" },
	{ /*39*/	"",
			"" },
	{ /*40*/	"",
			"" },
	{ /*41*/	"Temporary failure",
			"Vorruebergehende Fehlfunktion" },
	{ /*42*/	"Switchting equipment congestion",
			"Vermittlungstelle ueberlastet" },
	{ /*43*/	"Access informationen discarded",
			"Zugriffsinformationen geloescht" },
	{ /*44*/	"No requested circuit/channel",
			"Keine angeforderte Gasse/Kanal" },
	{ /*45*/	"",
			"" },
	{ /*46*/	"Precedence call blocked",
			"Vorverkaufanruf gesperrt" },
	{ /*47*/	"Resource unavailable, unspecified",
			"" },
	{ /*48*/	"",
			"" },
	{ /*49*/	"Quality of service not available",
			"Qualitaetsmerkmal nicht verfuegbar" },
	{ /*50*/	"Requested facility not subscribed",
			"Funktion nicht freigeschaltet" },
	{ /*51*/	"",
			"" },
	{ /*52*/	"",
			"" },
	{ /*53*/	"Outgoing calls barred within CUG",
			"CUG erlaubt keine gehenden Rufe" },
	{ /*54*/	"",
			"" },
	{ /*55*/	"Incoming calls barred within CUG",
			"CUG erlaubt keine kommenden Rufe" },
	{ /*56*/	"",
			"" },
	{ /*57*/	"Bearer capability not authorized",
			"Verbindungseigenschaft verboten" },
	{ /*58*/	"Bearer capability not present",
			"Verb.eigenschaft n. verfuegbar" },
	{ /*59*/	"",
			"" },
	{ /*60*/	"",
			"" },
	{ /*61*/	"",
			"" },
	{ /*62*/	"",
			"" },
	{ /*63*/	"Service or option not available",
			"Dienst oder Merkmal nicht verf." },
	{ /*64*/	"",
			"" },
	{ /*65*/	"Bearer capability not implement.",
			"Verb.eigenschaft nicht unterstue." },
	{ /*66*/	"Channel type not implemented",
			"Kanalart nicht unterstuetzt" },
	{ /*67*/	"",
			"" },
	{ /*68*/	"",
			"" },
	{ /*69*/	"Requested facility not implement.",
			"Funktion nicht unterstuetzt" },
	{ /*70*/	"restricted digital informat. only",
			"Nur eingeschraenkte digitale inf." },
	{ /*71*/	"",
			"" },
	{ /*72*/	"",
			"" },
	{ /*73*/	"",
			"" },
	{ /*74*/	"",
			"" },
	{ /*75*/	"",
			"" },
	{ /*76*/	"",
			"" },
	{ /*77*/	"",
			"" },
	{ /*78*/	"",
			"" },
	{ /*79*/	"Service or option not implemented",
			"Dienst oder Merkmal n. unterstue." },
	{ /*80*/	"",
			"" },
	{ /*81*/	"Invalid call reference value",
			"Falsche call reference" },
	{ /*82*/	"Identified channel does not exist",
			"Erkannter Kanal existiert nicht" },
	{ /*83*/	"No suspended call with this id",
			"Kein geparktes Gespr. f. diese ID" },
	{ /*84*/	"Call identity in use",
			"ID in gebrauch" },
	{ /*85*/	"No call suspended",
			"Kein geparktes Gespraech" },
	{ /*86*/	"Suspended call has been cleared",
			"Geparktes Gespraech wurde beendet" },
	{ /*87*/	"User not member of CUG",
			"Teilnehmer nicht in der CUG" },
	{ /*88*/	"Incompatibel destination",
			"Gegenstelle nicht kompatibel" },
	{ /*89*/	"",
			"" },
	{ /*90*/	"Non-existent CUG",
			"CUG existiert nicht" },
	{ /*91*/	"Invalid transit network selection",
			"Falscher Carrier-Code" },
	{ /*92*/	"",
			"" },
	{ /*93*/	"",
			"" },
	{ /*94*/	"",
			"" },
	{ /*95*/	"Invalid message, unspecified",
			"Fehlerhafte Daten, unbekannt" },
	{ /*96*/	"Information element missing",
			"Information wird vermisst" },
	{ /*97*/	"Message type non-existent",
			"Message exisitiert nicht" },
	{ /*98*/	"Message not compatible with state",
			"Message nicht komatibel" },
	{ /*99*/	"Information element not impl.",
			"Information nicht implementiert" },
	{ /*100*/	"Invalid info element contents",
			"Fehlerhafterhafte Information" },
	{ /*101*/	"Message not compatible with state",
			"Message not kompatibel" },
	{ /*102*/	"Recovery on timer expiry",
			"Fehler durch Zeitueberschreitung" },
	{ /*103*/	"Parameter non-existent",
			"Parameter fehlt" },
	{ /*104*/	"",
			"" },
	{ /*105*/	"",
			"" },
	{ /*106*/	"",
			"" },
	{ /*107*/	"",
			"" },
	{ /*108*/	"",
			"" },
	{ /*109*/	"",
			"" },
	{ /*110*/	"",
			"" },
	{ /*111*/	"Protocol error, unspecified",
			"Protokollfehler, unbekannt" },
	{ /*112*/	"",
			"" },
	{ /*113*/	"",
			"" },
	{ /*114*/	"",
			"" },
	{ /*115*/	"",
			"" },
	{ /*116*/	"",
			"" },
	{ /*117*/	"",
			"" },
	{ /*118*/	"",
			"" },
	{ /*119*/	"",
			"" },
	{ /*120*/	"",
			"" },
	{ /*121*/	"",
			"" },
	{ /*122*/	"",
			"" },
	{ /*123*/	"",
			"" },
	{ /*124*/	"",
			"" },
	{ /*125*/	"",
			"" },
	{ /*126*/	"",
			"" },
	{ /*127*/	"Interworking, unspecified",
			"Zusammenspiel, unbekannt" },
};

struct isdn_cause isdn_cause_class[8] = {
			/********************************* - **/ /*38*/
	{ /*0*/		"(Normal class)",
			"(Normale Fehlerklasse)" },
	{ /*16*/	"(Normal class)",
			"(Normale Fehlerklasse)" },
	{ /*32*/	"(Resource unavailable class)",
			"(Ressourcen nicht verfuegbar)" },
	{ /*48*/	"(Service or option unavailable)",
			"(Dienst oder Merkmal n. verfueg.)" },
	{ /*64*/	"(Service or option n.implemented)",
			"(Dienst oder Merkmal n. vorhand.)" },
	{ /*80*/	"(Invalid message class)",
			"(Fehlerhafte Message)" },
	{ /*96*/	"(Protocol error class)",
			"(Klasse der Protokollfehler)" },
	{ /*112*/	"(Interworking class)",
			"(Klasse des Zusammenspiels)" },
};

struct isdn_location isdn_location[16] = {
	{ /*0*/		"User",
			"Endgerät" },
	{ /*1*/		"Private (Local)",
			"Anlage (Lokal)" },
	{ /*2*/		"Public (Local)",
			"Vermittlung (Lokal)" },
	{ /*3*/		"Transit",
			"Knotenvermittlung" },
	{ /*4*/		"Public (Remote)",
			"Vermittlung (Gegenstelle)" },
	{ /*5*/		"Private (Remote)",
			"Anlage (Gegenstelle)" },
	{ /*6*/		"",
			"" },
	{ /*7*/		"International",
			"Fernvermittlung" },
	{ /*8*/		"",
			"" },
	{ /*9*/		"",
			"" },
	{ /*10*/	"Beyond Interworking",
			"Nicht verfuegbar" },
	{ /*11*/	"",
			"" },
	{ /*12*/	"",
			"" },
	{ /*13*/	"",
			"" },
	{ /*14*/	"",
			"" },
	{ /*15*/	"",
			"" },
};

char *get_isdn_cause(int cause, int location, int type)
{
	static char result[128];

	/* protect us */
	if (cause<0 || cause>127)
		cause = 0;

	switch(type) {
		case DISPLAY_CAUSE_NUMBER:
		SPRINT(result, "Cause %d", cause);
		break;

		case DISPLAY_CAUSE_ENGLISH:
		if (isdn_cause[cause].english[0])
			SPRINT(result, "%d - %s", cause, isdn_cause[cause].english);
		else	SPRINT(result, "%d - %s", cause, isdn_cause_class[cause>>4].english);
		break;

		case DISPLAY_CAUSE_GERMAN:
		if (isdn_cause[cause].german[0])
			SPRINT(result, "%d - %s", cause, isdn_cause[cause].german);
		else	SPRINT(result, "%d - %s", cause, isdn_cause_class[cause>>4].german);
		break;

		case DISPLAY_LOCATION_ENGLISH:
		if (isdn_location[location].english[0])
			SPRINT(result, "%d - %s", cause, isdn_location[location].english);
		else	SPRINT(result, "%d - Location code %d", cause, location);
		break;

		case DISPLAY_LOCATION_GERMAN:
		if (isdn_location[location].german[0])
			SPRINT(result, "%d - %s", cause, isdn_location[location].german);
		else	SPRINT(result, "%d - Lokationscode %d", cause, location);
		break;

		default:
		result[0] = '\0';
	}

	return(result);
}

/*
 * collect cause for multipoint
 * used by Process, Endpoint and Join instance when multiplexing
 */
void collect_cause(int *multicause, int *multilocation, int newcause, int newlocation)
{
	if (newcause == CAUSE_REJECTED) { /* call rejected */
		*multicause = newcause;
		*multilocation = newlocation;
	} else
	if (newcause==CAUSE_NORMAL && *multicause!=CAUSE_REJECTED) { /* reject via hangup */
		*multicause = newcause;
		*multilocation = newlocation;
	} else
	if (newcause==CAUSE_BUSY && *multicause!=CAUSE_REJECTED && *multicause!=CAUSE_NORMAL) { /* busy */
		*multicause = newcause;
		*multilocation = newlocation;
	} else
	if (newcause==CAUSE_OUTOFORDER && *multicause!=CAUSE_BUSY && *multicause!=CAUSE_REJECTED && *multicause!=CAUSE_NORMAL) { /* no L1 */
		*multicause = newcause;
		*multilocation = newlocation;
	} else
	if (newcause!=CAUSE_NOUSER && *multicause!=CAUSE_OUTOFORDER && *multicause!=CAUSE_BUSY && *multicause!=CAUSE_REJECTED && *multicause!=CAUSE_NORMAL) { /* anything but not 18 */
		*multicause = newcause;
		*multilocation = newlocation;
	} else
	if (newcause==CAUSE_NOUSER && *multicause==CAUSE_NOUSER) { /* cause 18, use the location */
		*multilocation = newlocation;
	} else
	if (*multicause==0) { /* no cause yet, use newcause (should be 18) */
		*multicause = newcause;
		*multilocation = newlocation;
	}
}

