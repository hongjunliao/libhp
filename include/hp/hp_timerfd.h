/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/8/28
 *
 * timer using timerfd
 * */
#ifndef LIBHP_TIMERFD_H__
#define LIBHP_TIMERFD_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */
#ifdef LIBHP_WITH_TIMERFD

#include "hp_epoll.h"    		  /* hp_epoll */
#include <time.h>                /* itimerspec */
/////////////////////////////////////////////////////////////////////////////////////
#ifdef __cplusplus
extern "C" {
#endif

typedef struct hp_timerfd hp_timerfd;

struct hp_timerfd{
	hp_epoll *           efds;
	int                  fd;
	hp_epolld            ed;
	size_t               n;
	int (*handle)(hp_timerfd * timerfd);
	void *               arg;
};

/*
 * @param interval: <=0 for NOT start timer
 * */
int hp_timerfd_init(hp_timerfd * timerfd, hp_epoll * efds,
	  int (*handle)(hp_timerfd * timerfd), int interval
	  , void * arg);
/*
 * for mutl-process
 * */
int hp_timerfd_init2(hp_timerfd * timerfd, hp_epoll * efds, int tfd,
	  int (*handle)(hp_timerfd * timerfd), int interval
	  , void * arg);
/*
 * @param interval: <=0 for stop timer, else reset timer
 * */
int hp_timerfd_reset(hp_timerfd * timerfd, int interval);
void hp_timerfd_uninit(hp_timerfd * timerfd);

#ifndef NDEBUG
int test_hp_timerfd_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif
#endif /* LIBHP_WITH_TIMERFD */

#endif /* LIBHP_TIMERFD_H__ */
