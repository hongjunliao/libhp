/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/5/16
 *
 * the epoll event system
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifndef LIBXHH_WITHOUT_EPOLL

#ifdef __linux__

#include "hp_epoll.h"   /*  */
#include "hp_log.h"     /* hp_log */
#include <sys/epoll.h>  /* epoll_event */
#include <unistd.h>
#include <string.h> 	/* strlen */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>     /* memset, ... */
#include <errno.h>      /* errno */
#include <assert.h>     /* define NDEBUG to disable assertion */
#include "c-vector/cvector.h"

/////////////////////////////////////////////////////////////////////////////////////

struct hp_bwait{
	int (* fn)(struct hp_epoll * efds, void * arg);
	void * arg;
};



int hp_epoll_run(hp_epoll * efds, int timeout
		, int (* before_wait)(struct hp_epoll * efds))
{
	int i, n;

	int runed = 0;
	for(;!efds->stop && !runed;){
		if(before_wait){
			if(before_wait != (void *)-1){
				int rc = before_wait(efds);
				if(rc != 0) return rc;
			}
			else runed = 1;
		}

		for(i = 0; i < (int)cvector_size(efds->bwaits); ++i){
			assert(efds->bwaits[i]->fn);
			if(efds->bwaits[i]->fn){
				int rc = efds->bwaits[i]->fn(efds, efds->bwaits[i]->arg);
				if(rc != 0) return rc;
			}
		}

		/* timeout: @see redis.conf/hz */
		n = epoll_wait(efds->fd, efds->ev, efds->ev_len, timeout);
		if(n < 0){
			if(errno == EINTR || errno == EAGAIN)
				continue;

			hp_log(stderr, "%s: epoll_wait failed, errno=%d, error='%s'\n"
					, __FUNCTION__, errno, strerror(errno));
			return -7;
		}

		for (i = 0; i < n; ++i) {
			struct epoll_event * ev = efds->ev + i;
			if(!ev->data.ptr)
				continue;
			struct hp_epolld  * evdata = (struct hp_epolld  *)(ev->data.ptr);
			evdata->n = i + 1;
		}
		for (i = 0; i < n; ++i) {
			struct epoll_event * ev = efds->ev + i;
			if(ev->data.ptr && ((struct hp_epolld  *)ev->data.ptr)->fn){
				((struct hp_epolld  *)ev->data.ptr)->fn(ev);
				if(ev->data.ptr)
					((struct hp_epolld  *)ev->data.ptr)->n = 0;
			}
		} /* for each epoll-ed fd */
	}

	return 0;
}

int hp_epoll_init(struct hp_epoll * efds, int n)
{
	if(!efds) return -1;

	memset(efds, 0, sizeof(hp_epoll));

	int epollfd = epoll_create1(0);;
	if (epollfd == -1) {
		hp_log(stderr, "%s: epoll_create1 failed, errno=%d, error='%s'\n"
				, __FUNCTION__, errno, strerror(errno));
		return -2;
	}
	efds->fd = epollfd;

	efds->ev = (struct epoll_event *)calloc(n, sizeof(struct epoll_event));
	efds->ev_len = n;

	cvector_init(efds->bwaits, 1);

	return 0;
}

void hp_epoll_uninit(struct hp_epoll * efds)
{
	if(!efds) return;

	free(efds->ev);
	close(efds->fd);

	size_t i;
	for(i = 0; i < cvector_size(efds->bwaits); ++i){
		free(efds->bwaits[i]);
	}
	cvector_free(efds->bwaits);
}

/*
 * NOTE: use the same @param ed when you add and modify the same fd
 * */
int hp_epoll_add(struct hp_epoll * efds, int fd, int events, struct hp_epolld * ed)
{
	if(!(efds && ed)) return -1;

	if(!ed->fn)
		hp_log(stderr, "%s: warning: callback NULL, fd=%d/%d\n", __FUNCTION__, fd, ed->fd);

	int ret = 0;

	struct epoll_event evobj;
	evobj.events = events;
	evobj.data.ptr = ed;

	if (epoll_ctl(efds->fd, EPOLL_CTL_ADD, fd, &evobj) == 0){
		return 0;
	}
	else {
		if(errno == EEXIST){
			if (epoll_ctl(efds->fd, EPOLL_CTL_MOD, fd, &evobj) == 0)
				return 0;
		}

		hp_log(stderr, "%s: epoll_ctl failed, epollfd=%d, fd=%d, errno=%d, error='%s'\n"
				, __FUNCTION__, efds->fd, fd, errno, strerror(errno));
		ret = -1;
	}
	return ret;
}

int hp_epoll_del(struct hp_epoll * efds, int fd, int events, struct hp_epolld * ed)
{
	if(!(efds && ed)) return -1;

	int ret = 0;

	struct epoll_event evobj;
	evobj.events = events;
	evobj.data.ptr = ed;

	if (epoll_ctl(efds->fd, EPOLL_CTL_DEL, fd, &evobj) != 0){
		hp_log(stderr, "%s: epoll_ctl failed, fd=%d, errno=%d, error='%s'\n"
				, __FUNCTION__, fd, errno, strerror(errno));
		ret = -1;
	}

	if(ed->n > 0){
		efds->ev[ed->n - 1].data.ptr = 0;
		ed->n = 0;
	}

	return ret;
}

int hp_epoll_add_before_wait(struct hp_epoll * efds
		, int (* before_wait)(struct hp_epoll * efds, void * arg), void * arg)
{
	if(!(efds && before_wait))
		return -1;

	hp_bwait * bwait = calloc(1, sizeof(hp_bwait));
	bwait->fn = before_wait;
	bwait->arg = arg;
	cvector_push_back(efds->bwaits, bwait);

	return 0;
}

char * hp_epoll_e2str(int events, char * buf, int len)
{
	if(!buf && len > 0) return 0;

	buf[0] = '\0';
	int n = snprintf(buf, len, "%s%s%s%s"
			, (events & EPOLLERR?   "EPOLLERR | " : "")
			, (events & EPOLLET?    "EPOLLET | " : "")
			, (events & EPOLLIN?    "EPOLLIN | " : "")
			, (events & EPOLLOUT?   "EPOLLOUT | " : "")
			);
	if(buf[0] != '\0' && n >= 3 )
		buf[n - 3] = '\0';

	int left = events & (~(EPOLLERR | EPOLLET | EPOLLIN | EPOLLOUT));
	if(!(left == 0))
		snprintf(buf + n, len - n, " | %d", left);

	return buf;
}

/////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
int test_hp_epoll_main(int argc, char ** argv)
{
	char buf[128] = "";
	assert(strcmp(hp_epoll_e2str(0, buf, sizeof(buf)), "") == 0);
	assert(strcmp(hp_epoll_e2str(EPOLLERR, buf, sizeof(buf)), "EPOLLERR") == 0);
	assert(strcmp(hp_epoll_e2str(EPOLLET, buf, sizeof(buf)), "EPOLLET") == 0);
	assert(strcmp(hp_epoll_e2str(EPOLLIN, buf, sizeof(buf)), "EPOLLIN") == 0);
	assert(strcmp(hp_epoll_e2str(EPOLLOUT, buf, sizeof(buf)), "EPOLLOUT") == 0);

	assert(strcmp(hp_epoll_e2str(EPOLLERR | EPOLLET, buf, sizeof(buf)), "EPOLLERR | EPOLLET") == 0);
	assert(strcmp(hp_epoll_e2str(EPOLLET | EPOLLIN, buf, sizeof(buf)), "EPOLLET | EPOLLIN") == 0);

	assert(strcmp(hp_epoll_e2str(EPOLLERR | EPOLLET | EPOLLIN, buf, sizeof(buf)), "EPOLLERR | EPOLLET | EPOLLIN") == 0);

	assert(strcmp(hp_epoll_e2str(EPOLLERR | EPOLLET | EPOLLIN | EPOLLOUT, buf, sizeof(buf)), "EPOLLERR | EPOLLET | EPOLLIN | EPOLLOUT") == 0);

	static struct hp_epoll ghp_efdsobj = { 0 }, * efds = &ghp_efdsobj;
	hp_epoll_init(efds, 100);
	hp_epoll_uninit(efds);
	hp_epoll_init(efds, 4000);
	hp_epoll_uninit(efds);

	return 0;
}
#endif /* NDEBUG */

#endif /*_MSC_VER*/
#endif
