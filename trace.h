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

#define MAX_NESTED_TRACES	1

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
	char category[32];
	char name[64];

	/* elements */
	int elements;
	struct trace_element element[MAX_TRACE_ELEMENTS];
};


#define	CATEGORY_L1	0x01
#define	CATEGORY_L2	0x02
#define	CATEGORY_L3	0x04
#define	CATEGORY_CH	0x08
#define	CATEGORY_EP	0x10
#define	CATEGORY_AP	0x20
#define	CATEGORY_RO	0x40


void start_trace(int port, char *interface, char *caller, char *dialing, int direction, char *category, char *name);
void add_trace(char *name, char *sub, char *value);
void end_trace(void);
//char *print_trace(int port, char *interface, char *caller, char *dialing, int direction, char *category, char *name);
