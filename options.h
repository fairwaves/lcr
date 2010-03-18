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
#ifdef __cplusplus
extern "C" {
#endif

struct options {
	char	log[128];		/* location of log file */
	int	deb;			/* debugging */
	char	law;			/* 'a' or 'u' law */

	char	national[10];		/* prefix for national calls */
	char	international[10];	/* prefix for international calls */

	char	tones_dir[64];		/* directory of all tones/patterns */
	char	fetch_tones[256];	/* directories of tones to fetch */
	char	dummyid[32];		/* caller id for external calls if not available */
	int	schedule;		/* run process in realtime @ given priority */
	char	email[128];		/* source email address */
	char	lock[128];		/* path of lock files */
	int	socketrights;		/* rights of lcr admin socket */
	int     socketuser;             /* socket chown to this user */
	int     socketgroup;            /* socket chgrp to this group */
	int	gsm;			/* enable gsm support */
	int	polling;
};	

extern struct options options;

int read_options(char *options_error);

#ifdef __cplusplus
}
#endif

