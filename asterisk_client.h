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
	unsigned long addr;	/* reference to bchannel, if set */
};

/* structure of all bchannels (that are assinged by lcr) */
struct chan_bchannel {
	struct chan_bchannel *next;
	unsigned long addr;	/* stack address */
	unsigned long ref;	/* if linked with a call, ref is set */
};

