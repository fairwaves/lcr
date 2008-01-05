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
	char name[11];
	char sub[11];
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
	int category;
	unsigned long serial;
	char name[64];

	/* elements */
	int elements;
	struct trace_element element[MAX_TRACE_ELEMENTS];
};



#define	CATEGORY_CH	0x01
#define	CATEGORY_EP	0x02
//#define CATEGORY_BC	0x04 check lcradmin help


#define start_trace(port, interface, caller, dialing, direction, category, serial, name) _start_trace(__FUNCTION__, __LINE__, port, interface, caller, dialing, direction, category, serial, name)
#define add_trace(name, sub, fmt, arg...) _add_trace(__FUNCTION__, __LINE__, name, sub, fmt, ## arg)
#define end_trace() _end_trace(__FUNCTION__, __LINE__)
void _start_trace(const char *__file, int line, int port, struct interface *interface, char *caller, char *dialing, int direction, int category, int serial, char *name);
void _add_trace(const char *__file, int line, char *name, char *sub, const char *fmt, ...);
void _end_trace(const char *__file, int line);
//char *print_trace(int port, char *interface, char *caller, char *dialing, int direction, char *category, char *name);


