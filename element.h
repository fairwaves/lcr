/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** call trace header                                                         **
**                                                                           **
\*****************************************************************************/ 

#define TRACE_DIR_IN   1
#define TRACE_DIR_OUT  2

struct trace_subelement {
	struct trace_subelement *next;
	char name[32];
	char value[128];
};

struct trace_element {
	struct trace_element *next;
	char name[32];
	char value[128];
	struct trace_subelement *subelement;
};

struct trace_head {
	unsigned long sec, usec;
	char interface[64];
	int port, nt, pri, ptp;
	int direction;
	char caller[64];
	char dialing[64];
	int layer;
	char message[64];
};

void trace_start(char *interface, int port, int nt, int pri, int ptp, int direction, char *caller, char *dialing, int layer, char *message);
void trace_element(char *name, value);
void trace_subelement(char *name, value);
void trace_finish(void);

void trace_show(FILE *fp, struct trace_head *trace_head, char *interface, int port, char *caller, char *dialing, unsigned long layermask);

