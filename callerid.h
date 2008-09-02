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

const char *nationalize_callerinfo(const char *string, int *type, const char *national, const char *international);
const char *numberrize_callerinfo(const char *string, int type, const char *national, const char *international);

