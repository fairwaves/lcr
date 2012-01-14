
#define LCR_FD_READ	1
#define LCR_FD_WRITE	2
#define LCR_FD_EXCEPT	4

#define MICRO_SECONDS  1000000LL

#define TIME_SMALLER(left, right) \
        (((left)->tv_sec*MICRO_SECONDS+(left)->tv_usec) <= ((right)->tv_sec*MICRO_SECONDS+(right)->tv_usec))

struct lcr_fd {
	struct lcr_fd	*next;	/* pointer to next element in list */
	int		inuse;	/* if in use */
	int		fd;	/* file descriptior if in use */
	int		when;	/* select on what event */
	int		(*cb)(struct lcr_fd *fd, unsigned int what, void *instance, int index); /* callback */
	void		*cb_instance;
	int		cb_index;
};

#define register_fd(a, b, c, d, e) _register_fd(a, b, c, d, e, __func__);
int _register_fd(struct lcr_fd *fd, int when, int (*cb)(struct lcr_fd *fd, unsigned int what, void *instance, int index), void *instance, int index, const char *func);
#define unregister_fd(a) _unregister_fd(a, __func__);
void _unregister_fd(struct lcr_fd *fd, const char *func);
int select_main(int polling, int *global_change, void (*lock)(void), void (*unlock)(void));


struct lcr_timer {
	struct lcr_timer *next;	/* pointer to next element in list */
	int		inuse;	/* if in use */
	int		active;	/* if timer is currently active */
	struct timeval	timeout; /* timestamp when to timeout */
	int		(*cb)(struct lcr_timer *timer, void *instance, int index); /* callback */
	void		*cb_instance;
	int		cb_index;
};

#define add_timer(a, b, c, d) _add_timer(a, b, c, d, __func__);
int _add_timer(struct lcr_timer *timer, int (*cb)(struct lcr_timer *timer, void *instance, int index), void *instance, int index, const char *func);
#define del_timer(a) _del_timer(a, __func__);
void _del_timer(struct lcr_timer *timer, const char *func);
void schedule_timer(struct lcr_timer *timer, int seconds, int microseconds);
void unsched_timer(struct lcr_timer *timer);


struct lcr_work {
	struct lcr_work *next;	/* pointer to next element in list */
	struct lcr_work *prev_event, *next_event; /* pointer to previous/next event, if triggered */
	int		inuse;	/* if in use */
	int		active;	/* if timer is currently active */
	int		(*cb)(struct lcr_work *work, void *instance, int index); /* callback */
	void		*cb_instance;
	int		cb_index;
};

#define add_work(a, b, c, d) _add_work(a, b, c, d, __func__);
int _add_work(struct lcr_work *work, int (*cb)(struct lcr_work *work, void *instance, int index), void *instance, int index, const char *func);
#define del_work(a) _del_work(a, __func__);
void _del_work(struct lcr_work *work, const char *func);
#define trigger_work(a) _trigger_work(a, __func__);
void _trigger_work(struct lcr_work *work, const char *func);


