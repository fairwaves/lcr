/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** cause header file                                                         **
**                                                                           **
\*****************************************************************************/ 

/* location (equivalent to the q.850 coding) */
#define	LOCATION_USER		0
#define LOCATION_PRIVATE_LOCAL	1
#define LOCATION_PUBLIC_LOCAL	2
#define LOCATION_TRANSIT	3
#define LOCATION_PUBLIC_REMOTE	4
#define LOCATION_PRIVATE_REMOTE	5
#define LOCATION_INTERNATIONAL	7
#define LOCATION_BEYOND		10

/* some causes (equivalent to the q.850 coding) */
#define CAUSE_UNALLOCATED	1
#define	CAUSE_NORMAL		16
#define CAUSE_BUSY		17
#define CAUSE_NOUSER		18
#define CAUSE_NOANSWER		19
#define CAUSE_REJECTED		21
#define CAUSE_OUTOFORDER	27
#define CAUSE_INVALID		28
#define CAUSE_FACILITYREJECTED	29
#define CAUSE_UNSPECIFIED	31
#define CAUSE_NOCHANNEL		34
#define CAUSE_TEMPOFAIL		41
#define CAUSE_RESSOURCEUNAVAIL	47
#define CAUSE_SERVICEUNAVAIL	63
#define CAUSE_UNIMPLEMENTED	79

struct isdn_cause {
	char *english;
	char *german;
};

struct isdn_location {
	char *english;
	char *german;
};

extern struct isdn_cause isdn_cause[128];
extern struct isdn_location isdn_location[16];
char *get_isdn_cause(int cause, int location, int type);
