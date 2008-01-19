/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** Asterisk socket client header                                             **
**                                                                           **
\*****************************************************************************/

/* structure for all calls */
struct chan_call {
	struct chan_call *next;
	unsigned long ref;	/* callref, is 0, if not yet set */
	unsigned long bchannel_handle;	/* reference to bchannel, if set */
};


