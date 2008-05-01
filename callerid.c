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

#include "main.h"

/* create caller id from digits by comparing with national and international
 * prefixes.
 */
char *nationalize_callerinfo(char *string, int *ntype, char *national, char *international)
{
	if (!strncmp(options.international, string, strlen(options.international)))
	{
		*ntype = INFO_NTYPE_INTERNATIONAL;
		return(string+strlen(international)); 
	}
	if (!strncmp(options.national, string, strlen(options.national)))
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
char *numberrize_callerinfo(char *string, int ntype, char *national, char *international)
{
	static char result[256];

	switch(ntype)
	{
		case INFO_NTYPE_INTERNATIONAL:
		UCPY(result, international);
		SCAT(result, string);
		return(result);
		break;

		case INFO_NTYPE_NATIONAL:
		UCPY(result, national);
		SCAT(result, string);
		return(result);
		break;

		default:
		return(string);
	}
}



