/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/8/28
 *
 * timer using timerfd
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_WITH_TIMERFD

#include "hp/hp_timerfd.h"  /* hp_timerfd */
#include "hp/hp_log.h"     /* hp_log */
#include <sys/timerfd.h> /* timerfd_create */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>       /* errno */
#include <string.h> 	 /* strerror */
#include <assert.h>      /* define NDEBUG to disable assertion */
/////////////////////////////////////////////////////////////////////////////////////

static int hp_timerfd_handle_tfd(struct epoll_event * ev)
{
	int fd = hp_epoll_fd(ev);

	hp_timerfd * timerfd = (hp_timerfd * )hp_epoll_arg(ev);
	assert(timerfd);
//
//#ifndef NDEBUG
//	if(hp_log_level > 5){
//		char buf[64];
//		hp_log(stdout, "%s: fd=%d, arg=%p, events='%s'\n", __FUNCTION__
//				, fd, timerfd, hp_epoll_e2str(ev->events, buf, sizeof(buf)));
//	}
//#endif /* NDEBUG */

	uint64_t exp;
	ssize_t s = read(fd, &exp, sizeof(uint64_t));
	assert(s == sizeof(uint64_t));

	++timerfd->n;
	return timerfd->handle(timerfd);
}

int hp_timerfd_reset(hp_timerfd * timerfd, int interval)
{
	if(!timerfd)
		return -1;

    struct itimerspec new_value;
    if(interval <= 0)
    	memset(&new_value, 0, sizeof(struct itimerspec));
    else {
        if (clock_gettime(CLOCK_REALTIME, &new_value.it_value) == -1){
        	hp_log(stderr, "%s: clock_gettime failed, errno=%d/'%s'\n"
        			, __FUNCTION__, errno, strerror(errno));
        	return -1;
        }

        new_value.it_interval.tv_sec = interval / 1000;
        new_value.it_interval.tv_nsec = (((size_t)interval  * 1000000) % (size_t)1000000000);
    }

    if (timerfd_settime(timerfd->fd, TFD_TIMER_ABSTIME, &new_value, NULL) == -1){
    	hp_log(stderr, "%s: timerfd_settime failed, interval=%dms, errno=%d/'%s'\n"
    			, __FUNCTION__, interval, errno, strerror(errno));
    	return -1;
    }

    return 0;
}

int hp_timerfd_init(hp_timerfd * timerfd, hp_epoll * efds,
	  int (*handle)(hp_timerfd * timerfd), int interval
	  , void * arg)
{
	if(!(timerfd && efds))
		return -1;

	int fd = timerfd_create(CLOCK_REALTIME, 0);
    if (fd == -1){
    	hp_log(stderr, "%s: timerfd_create failed, errno=%d/'%s'\n"
    			, __FUNCTION__, errno, strerror(errno));
    	return -1;
    }

    memset(timerfd, 0, sizeof(hp_timerfd));
	timerfd->fd = fd;
	timerfd->handle = handle;
	timerfd->arg = arg;
	timerfd->efds = efds;

	hp_epolld_set(&timerfd->ed, timerfd->fd, hp_timerfd_handle_tfd,
			timerfd);

	if(hp_epoll_add(efds, timerfd->fd, EPOLLIN | EPOLLET, &timerfd->ed) != 0)
		return -3;

	if(hp_timerfd_reset(timerfd, interval) != 0)
		return -1;

	return 0;
}

int hp_timerfd_init2(hp_timerfd * timerfd, hp_epoll * efds, int fd,
	  int (*handle)(hp_timerfd * timerfd), int interval
	  , void * arg)
{
	if(!(timerfd))
		return -1;

    memset(timerfd, 0, sizeof(hp_timerfd));
	timerfd->fd = fd;
	timerfd->handle = handle;
	timerfd->arg = arg;

	hp_epolld_set(&timerfd->ed, timerfd->fd, hp_timerfd_handle_tfd,
			timerfd);

	if(efds){
		if(hp_epoll_add(efds, timerfd->fd, EPOLLIN | EPOLLET, &timerfd->ed) != 0)
			return -3;
	}

	if(hp_timerfd_reset(timerfd, interval) != 0)
		return -1;

	return 0;
}

void hp_timerfd_uninit(hp_timerfd * timerfd)
{
	if(!timerfd)
		return;
	if(timerfd->efds){
		int rc = hp_epoll_del(timerfd->efds, timerfd->fd, EPOLLIN | EPOLLET, &timerfd->ed);
		assert(rc == 0);

		close(timerfd->fd);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

/* tests */
#ifndef NDEBUG
#include "hp/hp_epoll.h"

static hp_epoll efds_obj, *efds = &efds_obj;
static int n = 2;
static int time_out = 0;

static hp_timerfd * timer = 0;
static time_t stop_time = 0;

static int test_stopped_timerfd(hp_timerfd * timerfd)
{
	++n;
	return 0;
}

static int test_stopped_timerfd_before_wait(struct hp_epoll * efds)
{
	assert(n == 0);
	if(difftime(time(0), stop_time) > 4){
		efds->stop = 1;
	}
	return 0;
}

static int test_stop_timerfd(hp_timerfd * timerfd)
{
	assert(timerfd == timer);
	--n;

	fprintf(stdout, "%s: timer ticked, left=%d, time_out=%d\n", __FUNCTION__, n, time_out);

	if(n == 0){
		hp_timerfd_reset(timerfd, 0);
		stop_time = time(0);
	}
	return 0;
}

static int test_stop_timerfd_before_wait(struct hp_epoll * efds)
{
	assert(efds);
	if(stop_time > 0){
		if(difftime(time(0), stop_time) > 4){
			efds->stop = 1;
		}
	}
	return 0;
}

static int test_handle_timerfd(hp_timerfd * timerfd)
{
	assert(timerfd);
	assert(timerfd == timer);
	assert(timerfd->arg == efds);

	--n;
	if(n <= 0)
		efds->stop = 1;

	fprintf(stdout, "%s: timer ticked, left=%d, time_out=%d\n", __FUNCTION__, n, time_out);

	return 0;
}

static int test_start_stop_timerfd(hp_timerfd * timerfd)
{
	assert(timerfd);
	assert(timerfd == timer);
	assert(timerfd->arg == efds);

	++n;

	if(n == 2){
		fprintf(stdout, "%s: timer ticked for count=%d, test stop and restart timer\n", __FUNCTION__, n);

		hp_timerfd_reset(timerfd, 0);

		stop_time = time(0);
	}
	if(n == 4)
		efds->stop = 1;

	fprintf(stdout, "%s: timer ticked, count=%d, time_out=%d\n", __FUNCTION__, n, time_out);
	return 0;
}

static int test_start_stop_timerfd_before_wait(struct hp_epoll * efds)
{
	if(stop_time > 0){
		/* timer not tick in this period */
		assert( n == 2);

		if(difftime(time(0), stop_time) > 4){
			stop_time = 0;
			/* restore timer */
			time_out = 200;
			hp_timerfd_reset(timer, time_out);
		}
	}

	return 0;
}

int test_hp_timerfd_main(int argc, char ** argv)
{
	int rc;

	fprintf(stdout, "%s: running timer test, please wait ...\n", __FUNCTION__);

	/* hp_timerfd_init2 */
	{
		time_out = 100;

		rc = hp_epoll_init(efds, 2);
		assert(rc == 0);

		hp_timerfd  tfd_obj;
		timer = &tfd_obj;

		int fd = timerfd_create(CLOCK_REALTIME, 0);
		rc = hp_timerfd_init2(timer, efds, fd, test_stopped_timerfd, time_out, efds);
		assert(rc == 0);

		hp_timerfd_uninit(timer);
		close(fd);

		hp_epoll_uninit(efds);
}
	/* timer that first stopped */
	{
		n = 0;
		time_out = 0;
		stop_time = time(0);

		rc = hp_epoll_init(efds, 2);
		assert(rc == 0);

		hp_timerfd  tfd_obj;
		timer = &tfd_obj;

		rc = hp_timerfd_init(timer, efds, test_stopped_timerfd, time_out, efds);
		assert(rc == 0);

		hp_epoll_run(efds, 100, test_stopped_timerfd_before_wait);

		hp_timerfd_uninit(timer);
		hp_epoll_uninit(efds);
	}

	/* test to stop timer */
	{
		n = 4;
		time_out = 100;
		stop_time = 0;

		rc = hp_epoll_init(efds, 2);
		assert(rc == 0);

		hp_timerfd  tfd_obj;
		timer = &tfd_obj;

		rc = hp_timerfd_init(timer, efds, test_stop_timerfd, time_out, efds);
		assert(rc == 0);

		hp_epoll_run(efds, 100, test_stop_timerfd_before_wait);

		assert(n == 0);

		hp_timerfd_uninit(timer);
		hp_epoll_uninit(efds);
	}

	/* timer in ms */
	{
		n = 3;
		time_out = 100;

		rc = hp_epoll_init(efds, 2);
		assert(rc == 0);

		hp_timerfd  tfd_obj;
		timer = &tfd_obj;
		rc = hp_timerfd_init(timer, efds, test_handle_timerfd, time_out, efds);
		assert(rc == 0);

		hp_epoll_run(efds, 100, 0);

		hp_timerfd_uninit(timer);
		hp_epoll_uninit(efds);
	}
	/* timer > 1s */
	{
		// n = 3; 
		// time_out = 1200;

		// rc = hp_epoll_init(efds, 2);
		// assert(rc == 0);

		// hp_timerfd  tfd_obj;
		// timer = &tfd_obj;
		// rc = hp_timerfd_init(timer, efds, test_handle_timerfd, time_out, efds);
		// assert(rc == 0);

		// hp_epoll_run(efds, 200, 0);

		// hp_timerfd_uninit(timer);
		// hp_epoll_uninit(efds);
	}
	/* test start/stop timer */
	{
		n = 0;
		time_out = 200;
		stop_time = 0;

		rc = hp_epoll_init(efds, 2);
		assert(rc == 0);

		hp_timerfd  tfd_obj;
		timer = &tfd_obj;

		rc = hp_timerfd_init(timer, efds, test_start_stop_timerfd, time_out, efds);
		assert(rc == 0);

		hp_epoll_run(efds, 200, test_start_stop_timerfd_before_wait);

		hp_timerfd_uninit(timer);
		hp_epoll_uninit(efds);
	}

	return 0;
}

#endif /* NDEBUG */
#endif /* LIBHP_WITH_TIMERFD */
