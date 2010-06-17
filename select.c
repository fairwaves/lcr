/* based on code from OpenBSC */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/time.h>
#include "macro.h"
#include "select.h"

static int maxfd = 0;
static int unregistered;
static struct lcr_fd *fd_first = NULL;
static struct timeval *nearest_timer(struct timeval *select_timer, int *work);
static int next_work(void);

int _register_fd(struct lcr_fd *fd, int when, int (*cb)(struct lcr_fd *fd, unsigned int what, void *instance, int index), void *instance, int index, const char *func)
{
	int flags;

	if (fd->inuse)
		FATAL("FD that is registered in function %s is already in use\n", func);
//	printf("registering fd %d  %s\n", fd->fd, func);

	/* make FD nonblocking */
	flags = fcntl(fd->fd, F_GETFL);
	if (flags < 0)
		FATAL("Failed to F_GETFL\n");
	flags |= O_NONBLOCK;
	flags = fcntl(fd->fd, F_SETFL, flags);
	if (flags < 0)
		FATAL("Failed to F_SETFL O_NONBLOCK\n");

	/* Register FD */
	if (fd->fd > maxfd)
		maxfd = fd->fd;

	/* append to list */
	fd->inuse = 1;
	fd->when = when;
	fd->cb = cb;
	fd->cb_instance = instance;
	fd->cb_index = index;
	fd->next = fd_first;
	fd_first = fd;

	return 0;
}

void _unregister_fd(struct lcr_fd *fd, const char *func)
{
	struct lcr_fd **lcr_fdp;

	/* find pointer to fd */
	lcr_fdp = &fd_first;
	while(*lcr_fdp) {
		if (*lcr_fdp == fd)
			break;
		lcr_fdp = &((*lcr_fdp)->next);
	}
	if (!*lcr_fdp) {
		FATAL("FD unregistered in function %s not in list\n", func);
	}

	/* remove fd from list */
	fd->inuse = 0;
	*lcr_fdp = fd->next;
	unregistered = 1;
}


int select_main(int polling, int *global_change, void (*lock)(void), void (*unlock)(void))
{
	struct lcr_fd *lcr_fd;
	fd_set readset, writeset, exceptset;
	int work = 0, temp, rc;
	struct timeval no_time = {0, 0};
	struct timeval select_timer, *timer;

	/* goto again;
	 *
	 * this ensures that select is only called until:
	 * - no work event exists
	 * - and no timeout occurred
	 *
	 * if no future timeout exists, select will wait infinit.
	 */

printf("-"); fflush(stdout);
again:
printf("1"); fflush(stdout);
	/* process all work events */
	if (next_work()) {
printf("2"); fflush(stdout);
		work = 1;
		goto again;
	}

	/* process timer events and get timeout for next timer event */
	temp = 0;
	timer = nearest_timer(&select_timer, &temp);
printf("3"); fflush(stdout);
	if (temp) {
printf("4"); fflush(stdout);
		work = 1;
		goto again;
	}
	if (polling)
		timer = &no_time;
//#warning TESTING
//	if (!timer)
//		printf("wait till infinity ..."); fflush(stdout);

	FD_ZERO(&readset);
	FD_ZERO(&writeset);
	FD_ZERO(&exceptset);

printf("5"); fflush(stdout);
	/* prepare read and write fdsets */
	lcr_fd = fd_first;
	while(lcr_fd) {
		if (lcr_fd->when & LCR_FD_READ)
			FD_SET(lcr_fd->fd, &readset);
		if (lcr_fd->when & LCR_FD_WRITE)
			FD_SET(lcr_fd->fd, &writeset);
		if (lcr_fd->when & LCR_FD_EXCEPT)
			FD_SET(lcr_fd->fd, &exceptset);
		lcr_fd = lcr_fd->next;
	}
printf("6"); fflush(stdout);

	if (unlock)
		unlock();
	rc = select(maxfd+1, &readset, &writeset, &exceptset, timer);
	if (lock)
		lock();
//#warning TESTING
//	if (!timer)
//		printf("interrupted.\n");
	if (rc < 0)
		return 0;
	if (global_change && *global_change) {
		*global_change = 0;
		return 1;
	}
printf("7"); fflush(stdout);

	/* fire timers */
#if 0
	bsc_update_timers();
#endif

	/* call registered callback functions */
restart:
printf("8"); fflush(stdout);
	unregistered = 0;
	lcr_fd = fd_first;
	while(lcr_fd) {
		int flags = 0;

		if (FD_ISSET(lcr_fd->fd, &readset)) {
			flags |= LCR_FD_READ;
			FD_CLR(lcr_fd->fd, &readset);
		}
		if (FD_ISSET(lcr_fd->fd, &writeset)) {
			flags |= LCR_FD_WRITE;
			FD_CLR(lcr_fd->fd, &writeset);
		}
		if (FD_ISSET(lcr_fd->fd, &exceptset)) {
			flags |= LCR_FD_EXCEPT;
			FD_CLR(lcr_fd->fd, &exceptset);
		}
		if (flags) {
printf("9"); fflush(stdout);
			work = 1;
			lcr_fd->cb(lcr_fd, flags, lcr_fd->cb_instance, lcr_fd->cb_index);
			if (unregistered)
				goto restart;
printf("-"); fflush(stdout);
			return 1;
		}
		lcr_fd = lcr_fd->next;
	}
	return work;
}


static struct lcr_timer *timer_first = NULL;

int _add_timer(struct lcr_timer *timer, int (*cb)(struct lcr_timer *timer, void *instance, int index), void *instance, int index, const char *func)
{
	if (timer->inuse) {
		FATAL("timer that is registered in function %s is already in use\n", func);
	}

#if 0
	struct lcr_timer *test = timer_first;
	while(test) {
		if (test == timer)
			FATAL("Timer already in list %s\n", func);
		test = test->next;
	}
#endif

	timer->inuse = 1;
	timer->active = 0;
	timer->timeout.tv_sec = 0;
	timer->timeout.tv_usec = 0;
	timer->cb = cb;
	timer->cb_instance = instance;
	timer->cb_index = index;
	timer->next = timer_first;
	timer_first = timer;

	return 0;
}

void _del_timer(struct lcr_timer *timer, const char *func)
{
	struct lcr_timer **lcr_timerp;

	/* find pointer to timer */
	lcr_timerp = &timer_first;
	while(*lcr_timerp) {
		if (*lcr_timerp == timer)
			break;
		lcr_timerp = &((*lcr_timerp)->next);
	}
	if (!*lcr_timerp) {
		FATAL("timer deleted in function %s not in list\n", func);
	}

	/* remove timer from list */
	timer->inuse = 0;
	*lcr_timerp = timer->next;
}

void schedule_timer(struct lcr_timer *timer, int seconds, int microseconds)
{
	struct timeval current_time;

	if (!timer->inuse) {
		FATAL("Timer not added\n");
	}

	gettimeofday(&current_time, NULL);
	unsigned long long currentTime = current_time.tv_sec * MICRO_SECONDS + current_time.tv_usec;
	currentTime += seconds * MICRO_SECONDS + microseconds;
	timer->timeout.tv_sec = currentTime / MICRO_SECONDS;
	timer->timeout.tv_usec = currentTime % MICRO_SECONDS;
	timer->active = 1;
}

void unsched_timer(struct lcr_timer *timer)
{
	timer->active = 0;
}

/* if a timeout is reached, process timer, if not, return timer value for select */
static struct timeval *nearest_timer(struct timeval *select_timer, int *work)
{
	struct timeval current;
	struct timeval *nearest = NULL;
	struct lcr_timer *lcr_timer, *lcr_nearest = NULL;

	/* find nearest timer, or NULL, if no timer active */
	lcr_timer = timer_first;
	while(lcr_timer) {
		if (lcr_timer->active && (!nearest || TIME_SMALLER(&lcr_timer->timeout, nearest))) {
			nearest = &lcr_timer->timeout;
			lcr_nearest = lcr_timer;
		}
		lcr_timer = lcr_timer->next;
	}

	select_timer->tv_sec = 0;
	select_timer->tv_usec = 0;

	if (!nearest)
		return NULL; /* wait until infinity */

	gettimeofday(&current, NULL);
	unsigned long long nearestTime = nearest->tv_sec * MICRO_SECONDS + nearest->tv_usec;
	unsigned long long currentTime = current.tv_sec * MICRO_SECONDS + current.tv_usec;

	if (nearestTime > currentTime) {
		select_timer->tv_sec = (nearestTime - currentTime) / MICRO_SECONDS;
		select_timer->tv_usec = (nearestTime - currentTime) % MICRO_SECONDS;
		return select_timer;
	} else {
		lcr_nearest->active = 0;
		(*lcr_nearest->cb)(lcr_nearest, lcr_nearest->cb_instance, lcr_nearest->cb_index);
		/* don't wait so we can process the queues, indicate "work=1" */
		select_timer->tv_sec = 0;
		select_timer->tv_usec = 0;
		*work = 1;
		return select_timer;
	}
}


static struct lcr_work *work_first = NULL; /* chain of work */
static struct lcr_work *first_event = NULL, *last_event = NULL; /* chain of active events */

#ifdef DEBUG_WORK
void show_chain(const char *func)
{
	struct lcr_work *work = first_event;
	printf("chain:%s\n", func);
	while(work) {
		printf("%p - %p - %p\n", work->prev_event, work, work->next_event);
		work = work->next_event;
	}
}
#endif

int _add_work(struct lcr_work *work, int (*cb)(struct lcr_work *work, void *instance, int index), void *instance, int index, const char *func)
{
	if (work->inuse) {
		FATAL("work that is registered in function %s is already in use\n", func);
	}

#ifdef DEBUG_WORK
	printf("add work %p from function %s\n", work, func);
	show_chain("before add");
#endif
	work->inuse = 1;
	work->active = 0;
	work->cb = cb;
	work->cb_instance = instance;
	work->cb_index = index;
	work->next = work_first;
	work_first = work;
#ifdef DEBUG_WORK
	show_chain("after add");
#endif

	return 0;
}

void _del_work(struct lcr_work *work, const char *func)
{
	struct lcr_work **lcr_workp;

#ifdef DEBUG_WORK
	show_chain("before detach");
#endif
	if (work->active) {
		/* first event removed */
		if (!work->prev_event)
			first_event = work->next_event;
		else
			work->prev_event->next_event = work->next_event;
		/* last event removed */
		if (!work->next_event)
			last_event = work->prev_event;
		else
			work->next_event->prev_event = work->prev_event;
	}
#ifdef DEBUG_WORK
	show_chain("after detach");
#endif

	/* find pointer to work */
	lcr_workp = &work_first;
	while(*lcr_workp) {
		if (*lcr_workp == work)
			break;
		lcr_workp = &((*lcr_workp)->next);
	}
	if (!*lcr_workp) {
		FATAL("work deleted by '%s' not in list\n", func);
	}

	/* remove work from list */
	work->inuse = 0;
	*lcr_workp = work->next;
#ifdef DEBUG_WORK
	show_chain("after delete");
#endif
}

void trigger_work(struct lcr_work *work)
{
	if (!work->inuse) {
		FATAL("Work not added\n");
	}

	/* event already triggered */
	if (work->active)
		return;

#ifdef DEBUG_WORK
	show_chain("before trigger");
#endif
	/* append to tail of chain */
	if (last_event)
		last_event->next_event = work;
	work->prev_event = last_event;
	work->next_event = NULL;
	last_event = work;
	if (!first_event)
		first_event = work;
#ifdef DEBUG_WORK
	show_chain("after trigger");
#endif

	work->active = 1;
}

/* get first work and remove from event chain */
static int next_work(void)
{
	struct lcr_work *lcr_work;

	if (!first_event)
		return 0;

#ifdef DEBUG_WORK
	show_chain("before next_work");
#endif
	if (!first_event->inuse) {
		FATAL("Work not added\n");
	}

	/* detach from event chain */
	lcr_work = first_event;
	first_event = lcr_work->next_event;
	if (!first_event)
		last_event = NULL;
	else
		first_event->prev_event = NULL;

#ifdef DEBUG_WORK
	show_chain("after next_work");
#endif
	lcr_work->active = 0;

	(*lcr_work->cb)(lcr_work, lcr_work->cb_instance, lcr_work->cb_index);

	return 1;
}

