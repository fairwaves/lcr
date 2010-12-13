
struct mISDNloop {
	int		sock;		/* loopback interface external side */
	int		port;		/* port number for external side */
};

extern mISDNloop mISDNloop;

void mISDNloop_close(void);
int mISDNloop_open();
int loop_hunt_bchannel(class PmISDN *port, struct mISDNport *mISDNport);

