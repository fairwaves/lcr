/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** caller id support file                                                    **
**                                                                           **
\*****************************************************************************/ 

#include <string.h>
#include <time.h>
#include "extension.h"
#include "message.h"
#include "callerid.h"

/* create caller id from digits by comparing with national and international
 * prefixes.
 */
const char *nationalize_callerinfo(const char *string, int *ntype, const char *national, const char *international)
{
	if (!strncmp(international, string, strlen(international)))
	{
		*ntype = INFO_NTYPE_INTERNATIONAL;
		return(string+strlen(international)); 
	}
	if (!strncmp(national, string, strlen(national)))
	{
		*ntype = INFO_NTYPE_NATIONAL;
		return(string+strlen(national)); 
	}
	*ntype = INFO_NTYPE_SUBSCRIBER;
	return(string);
}

/* create number (including access codes) from caller id
 * prefixes.
 */
const char *numberrize_callerinfo(const char *string, int ntype, const char *national, const char *international)
{
	static char result[256];

	switch(ntype)
	{
		case INFO_NTYPE_NOTPRESENT:
		return("");

		case INFO_NTYPE_INTERNATIONAL:
		strcpy(result, international);
		strncat(result, string, sizeof(result)-strlen(result)-1);
		result[sizeof(result)-1] = '\0';
		return(result);
		break;

		case INFO_NTYPE_NATIONAL:
		strcpy(result, national);
		strncat(result, string, sizeof(result)-strlen(result)-1);
		result[sizeof(result)-1] = '\0';
		return(result);
		break;

		default:
		return(string);
	}
}



