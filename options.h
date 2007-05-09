/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** options header file                                                       **
**                                                                           **
\*****************************************************************************/ 

struct options {
	char	log[128];		/* location of log file */
	char	ports[256];		/* use of ports */
//	int	ptp;			/* if layer 2 should be watched */
	int	deb;			/* debugging */
	char	law;			/* 'a' or 'u' law */

	char	national[10];		/* prefix for national calls */
	char	international[10];	/* prefix for international calls */

	char	tones_dir[64];		/* directory of all tones/patterns */
	char	fetch_tones[256];	/* directories of tones to fetch */
	char	extensions_dir[64];	/* directory of extensions */
	char	h323_name[128];		/* the name of h323 endpoint */
	int	h323_ringconnect;	/* connected when ringing */
	int	h323_gsm_pri;		/* priority to use of GSM codec (0 == don't use) */
	int	h323_gsm_opt;
	int	h323_g726_pri;		/* priority to use of G726 codec (0 == don't use) */
	int	h323_g726_opt;
	int	h323_g7231_pri;		/* priority to use of G7231 codec (0 == don't use) */
	int	h323_g729a_pri;		/* priority to use of G729a codec (0 == don't use) */
	int	h323_lpc10_pri;		/* priority to use of lpc-10 codec (0 == don't use) */
	int	h323_speex_pri;		/* priority to use of speex codec (0 == don't use) */
	int	h323_speex_opt;
	int	h323_xspeex_pri;	/* priority to use of xspeex codec (0 == don't use) */
	int	h323_xspeex_opt;
	int	h323_law_pri;		/* priority to use of law codec (0 == don't use) */
	int	h323_law_opt;
	int	h323_icall;		/* allow incoming h323 calls */
	char	h323_icall_prefix[32];	/* the prefix */
	int	h323_port;		/* port for incoming calls */
	int	h323_gatekeeper;	/* register with h323 gatekeeper */
	char	h323_gatekeeper_host[128];/* the gatekeeper host */
	int	sip_port;
	int	sip_maxqueue;
	int	nodtmf;			/* use dtmf detection */
	char	dummyid[32];		/* caller id for external calls if not available */
	int	dsptones;		/* tones will be generated via dsp.o 1=american 2=ger */
	int	schedule;		/* run process in realtime @ given priority */
	char	email[128];		/* source email address */
};	

extern struct options options;

int read_options(void);


