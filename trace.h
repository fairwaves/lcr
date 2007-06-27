/*****************************************************************************\
**                                                                           **
** Linux Call Router                                                         **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** trace header file                                                         **
**                                                                           **
\*****************************************************************************/ 

struct trace_element {
	char name[10];
	char sub[10];
	char value[64];
};

#define MAX_TRACE_ELEMENTS	32
struct trace {
	/* header */
	int port;
	char interface[32];
	char caller[64];
	char dialing[64];
	int direction;
	unsigned long sec, usec;
	
	/* type */
	char category[4];
	int serial;
	char name[64];

	/* elements */
	int elements;
	struct trace_element element[MAX_TRACE_ELEMENTS];
};



#define	CATEGORY_CH	0x01
#define	CATEGORY_EP	0x02


void start_trace(int port, struct interface *interface, char *caller, char *dialing, int direction, int category, int serial, char *name);
void add_trace(char *name, char *sub, const char *fmt, ...);
void end_trace(void);
//char *print_trace(int port, char *interface, char *caller, char *dialing, int direction, char *category, char *name);


