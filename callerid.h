/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** callerid header file                                                      **
**                                                                           **
\*****************************************************************************/ 

char *nationalize_callerinfo(char *string, int *type, char *national, char *international);
char *numberrize_callerinfo(char *string, int type, char *national, char *international);

