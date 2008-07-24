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

/* definitions of commands */
#define L1_ACTIVATE_REQ			0x0001f000
#define L1_ACTIVATE_CON			0x0001f001
#define L1_ACTIVATE_IND			0x0001f002
#define L1_ACTIVATE_RES			0x0001f003
#define L1_DEACTIVATE_REQ		0x0001f100
#define L1_DEACTIVATE_CON		0x0001f101
#define L1_DEACTIVATE_IND		0x0001f102
#define L1_DEACTIVATE_RES		0x0001f103
#define L2_ESTABLISH_REQ		0x0002f000
#define L2_ESTABLISH_CON		0x0002f001
#define L2_ESTABLISH_IND		0x0002f002
#define L2_ESTABLISH_RES		0x0002f003
#define L2_RELEASE_REQ			0x0002f100
#define L2_RELEASE_CON			0x0002f101
#define L2_RELEASE_IND			0x0002f102
#define L2_RELEASE_RES			0x0002f103
#define L3_ALERTING_REQ			0x00030100
#define L3_ALERTING_IND			0x00030102
#define L3_PROCEEDING_REQ		0x00030200
#define L3_PROCEEDING_IND		0x00030202
#define L3_CONNECT_REQ			0x00030700
#define L3_CONNECT_IND			0x00030702
#define L3_CONNECT_RES			0x00030703
#define L3_CONNECT_ACKNOWLEDGE_REQ	0x00030f00
#define L3_CONNECT_ACKNOWLEDGE_IND	0x00030f02
#define L3_PROGRESS_REQ			0x00030300
#define L3_PROGRESS_IND			0x00030302
#define L3_SETUP_REQ			0x00030500
#define L3_SETUP_IND			0x00030502
#define L3_SETUP_ACKNOWLEDGE_REQ	0x00030d00
#define L3_SETUP_ACKNOWLEDGE_IND	0x00030d02
#define L3_RESUME_REQ			0x00032600
#define L3_RESUME_IND			0x00032602
#define L3_RESUME_ACKNOWLEDGE_REQ	0x00032e00
#define L3_RESUME_ACKNOWLEDGE_IND	0x00032e02
#define L3_RESUME_REJECT_REQ		0x00032200
#define L3_RESUME_REJECT_IND		0x00032202
#define L3_SUSPEND_REQ			0x00032500
#define L3_SUSPEND_IND			0x00032502
#define L3_SUSPEND_ACKNOWLEDGE_REQ	0x00032d00
#define L3_SUSPEND_ACKNOWLEDGE_IND	0x00032d02
#define L3_SUSPEND_REJECT_REQ		0x00032100
#define L3_SUSPEND_REJECT_IND		0x00032102
#define L3_USER_INFORMATION_REQ		0x00032000
#define L3_USER_INFORMATION_IND		0x00032002
#define L3_DISCONNECT_REQ		0x00034500
#define L3_DISCONNECT_IND		0x00034502
#define L3_RELEASE_REQ			0x00034d00
#define L3_RELEASE_IND			0x00034d02
#define L3_RELEASE_COMPLETE_REQ		0x00035a00
#define L3_RELEASE_COMPLETE_IND		0x00035a02
#define L3_RESTART_REQ			0x00034600
#define L3_RESTART_IND			0x00034602
#define L3_RESTART_ACKNOWLEDGE_REQ	0x00034e00
#define L3_RESTART_ACKNOWLEDGE_IND	0x00034e02
#define L3_SEGMENT_REQ			0x00036000
#define L3_SEGMENT_IND			0x00036002
#define L3_CONGESTION_CONTROL_REQ	0x00037900
#define L3_CONGESTION_CONTROL_IND	0x00037902
#define L3_INFORMATION_REQ		0x00037b00
#define L3_INFORMATION_IND		0x00037b02
#define L3_FACILITY_REQ			0x00036200
#define L3_FACILITY_IND			0x00036202
#define L3_NOTIFY_REQ			0x00036e00
#define L3_NOTIFY_IND			0x00036e02
#define L3_STATUS_REQ			0x00037d00
#define L3_STATUS_IND			0x00037d02
#define L3_STATUS_ENQUIRY_REQ		0x00037500
#define L3_STATUS_ENQUIRY_IND		0x00037502
#define L3_HOLD_REQ			0x00032400
#define L3_HOLD_IND			0x00032402
#define L3_HOLD_ACKNOWLEDGE_REQ		0x00032800
#define L3_HOLD_ACKNOWLEDGE_IND		0x00032802
#define L3_HOLD_REJECT_REQ		0x00033000
#define L3_HOLD_REJECT_IND		0x00033002
#define L3_RETRIEVE_REQ			0x00033100
#define L3_RETRIEVE_IND			0x00033102
#define L3_RETRIEVE_ACKNOWLEDGE_REQ	0x00033300
#define L3_RETRIEVE_ACKNOWLEDGE_IND	0x00033302
#define L3_RETRIEVE_REJECT_REQ		0x00033700
#define L3_RETRIEVE_REJECT_IND		0x00033702
#define L3_NEW_L3ID_REQ			0x0003f000
#define L3_NEW_L3ID_IND			0x0003f002
#define L3_RELEASE_L3ID_REQ		0x0003f100
#define L3_RELEASE_L3ID_IND		0x0003f102
#define L3_TIMEOUT_REQ			0x0003f200
#define L3_TIMEOUT_IND			0x0003f202
#define L3_UNKNOWN			0x0003ff00


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
	unsigned int sec, usec;
	
	/* type */
	int category;
	unsigned int serial;
	char name[64];

	/* elements */
	int elements;
	struct trace_element element[MAX_TRACE_ELEMENTS];
};



#define	CATEGORY_CH	0x01
#define	CATEGORY_EP	0x02
//#define CATEGORY_BC	0x04 check lcradmin help


#define start_trace(port, interface, caller, dialing, direction, category, serial, name) _start_trace(__FILE__, __LINE__, port, interface, caller, dialing, direction, category, serial, name)
#define add_trace(name, sub, fmt, arg...) _add_trace(__FILE__, __LINE__, name, sub, fmt, ## arg)
#define end_trace() _end_trace(__FILE__, __LINE__)
void _start_trace(const char *__file, int line, int port, struct interface *interface, char *caller, char *dialing, int direction, int category, int serial, char *name);
void _add_trace(const char *__file, int line, char *name, char *sub, const char *fmt, ...);
void _end_trace(const char *__file, int line);
//char *print_trace(int port, char *interface, char *caller, char *dialing, int direction, char *category, char *name);


