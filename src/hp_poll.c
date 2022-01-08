/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2022/1/8
 *
 * the poll event system
 *
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#if !defined(__linux__) && !defined(_MSC_VER)

#include "hp_poll.h"   /*  */
#include "hp_log.h"     /* hp_log */
#include <poll.h>  /* poll */
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
	int (* fn)(struct hp_poll * fds, void * arg);
	void * arg;
};



int hp_poll_run(hp_poll * fds, int timeout
		, int (* before_wait)(struct hp_poll * fds))
{
	int i, n;

	int runed = 0;
	for(;!fds->stop && !runed;){
		if(before_wait){
			if(before_wait != (void *)-1){
				int rc = before_wait(fds);
				if(rc != 0) return rc;
			}
			else runed = 1;
		}

		for(i = 0; i < (int)cvector_size(fds->bwaits); ++i){
			assert(fds->bwaits[i]->fn);
			if(fds->bwaits[i]->fn){
				int rc = fds->bwaits[i]->fn(fds, fds->bwaits[i]->arg);
				if(rc != 0) return rc;
			}
		}

		/* timeout: @see redis.conf/hz */
		n = poll(fds->fd, fds->nfd, timeout);
		if(n < 0){
			if(errno == EINTR || errno == EAGAIN)
				continue;

			hp_log(stderr, "%s: poll failed, errno=%d, error='%s'\n"
					, __FUNCTION__, errno, strerror(errno));
			return -7;
		}

		for (i = 0; i < n; ++i) {
			struct epoll_event * ev = fds->fd + i;
			if(ev->data.ptr && ((struct hp_polld  *)ev->data.ptr)->fn){
				((struct hp_polld  *)ev->data.ptr)->fn(ev);
				if(ev->data.ptr)
					((struct hp_polld  *)ev->data.ptr)->n = 0;
			}
		} /* for each epoll-ed fd */
	}

	return 0;
}

int hp_poll_init(struct hp_poll * fds, int n)
{
	if(!fds) return -1;

	memset(fds, 0, sizeof(hp_poll));

	int epollfd = poll(0);;
	if (epollfd == -1) {
		hp_log(stderr, "%s: epoll_create1 failed, errno=%d, error='%s'\n"
				, __FUNCTION__, errno, strerror(errno));
		return -2;
	}
	fds->fd = epollfd;

	fds->fd = (struct epoll_event *)calloc(n, sizeof(struct epoll_event));
	fds->nfd = n;

	cvector_init(fds->bwaits, 1);

	return 0;
}

void hp_poll_uninit(struct hp_poll * fds)
{
	if(!fds) return;

	free(fds->fd);
	close(fds->fd);

	size_t i;
	for(i = 0; i < cvector_size(fds->bwaits); ++i){
		free(fds->bwaits[i]);
	}
	cvector_free(fds->bwaits);
}

/*
 * NOTE: use the same @param ed when you add and modify the same fd
 * */
int hp_poll_add(struct hp_poll * fds, int fd, int events, struct hp_polld * ed)
{
	if(!(fds && ed)) return -1;

	if(!ed->fn)
		hp_log(stderr, "%s: warning: callback NULL, fd=%d/%d\n", __FUNCTION__, fd, ed->fd);

	int ret = 0;

	struct epoll_event evobj;
	evobj.events = events;
	evobj.data.ptr = ed;

	if (epoll_ctl(fds->fd, POLL_CTL_ADD, fd, &evobj) == 0){
		return 0;
	}
	else {
		if(errno == EEXIST){
			if (epoll_ctl(fds->fd, POLL_CTL_MOD, fd, &evobj) == 0)
				return 0;
		}

		hp_log(stderr, "%s: epoll_ctl failed, epollfd=%d, fd=%d, errno=%d, error='%s'\n"
				, __FUNCTION__, fds->fd, fd, errno, strerror(errno));
		ret = -1;
	}
	return ret;
}

int hp_poll_del(struct hp_poll * fds, int fd, int events, struct hp_polld * ed)
{
	if(!(fds && ed)) return -1;

	int ret = 0;

	struct epoll_event evobj;
	evobj.events = events;
	evobj.data.ptr = ed;

	if (epoll_ctl(fds->fd, POLL_CTL_DEL, fd, &evobj) != 0){
		hp_log(stderr, "%s: epoll_ctl failed, fd=%d, errno=%d, error='%s'\n"
				, __FUNCTION__, fd, errno, strerror(errno));
		ret = -1;
	}

	if(ed->n > 0){
		fds->fd[ed->n - 1].data.ptr = 0;
		ed->n = 0;
	}

	return ret;
}

int hp_poll_add_before_wait(struct hp_poll * fds
		, int (* before_wait)(struct hp_poll * fds, void * arg), void * arg)
{
	if(!(fds && before_wait))
		return -1;

	hp_bwait * bwait = calloc(1, sizeof(hp_bwait));
	bwait->fn = before_wait;
	bwait->arg = arg;
	cvector_push_back(fds->bwaits, bwait);

	return 0;
}

char * hp_poll_e2str(int events, char * buf, int len)
{
	if(!buf && len > 0) return 0;

	buf[0] = '\0';
	int n = snprintf(buf, len, "%s%s%s%s"
			, (events & POLLERR?   "POLLERR | " : "")
			, (events & POLLIN?    "POLLIN | " : "")
			, (events & POLLOUT?   "POLLOUT | " : "")
			);
	if(buf[0] != '\0' && n >= 3 )
		buf[n - 3] = '\0';

	int left = events & (~(POLLERR  | POLLIN | POLLOUT));
	if(!(left == 0))
		snprintf(buf + n, len - n, " | %d", left);

	return buf;
}

/////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
int test_hp_poll_main(int argc, char ** argv)
{
	char buf[128] = "";
	assert(strcmp(hp_poll_e2str(0, buf, sizeof(buf)), "") == 0);
	assert(strcmp(hp_poll_e2str(POLLERR, buf, sizeof(buf)), "POLLERR") == 0);
	assert(strcmp(hp_poll_e2str(POLLET, buf, sizeof(buf)), "POLLET") == 0);
	assert(strcmp(hp_poll_e2str(POLLIN, buf, sizeof(buf)), "POLLIN") == 0);
	assert(strcmp(hp_poll_e2str(POLLOUT, buf, sizeof(buf)), "POLLOUT") == 0);

	assert(strcmp(hp_poll_e2str(POLLERR , buf, sizeof(buf)), "POLLERR ") == 0);
	assert(strcmp(hp_poll_e2str(POLLET | POLLIN, buf, sizeof(buf)), "POLLET | POLLIN") == 0);

	assert(strcmp(hp_poll_e2str(POLLERR  | POLLIN, buf, sizeof(buf)), "POLLERR  | POLLIN") == 0);

	assert(strcmp(hp_poll_e2str(POLLERR  | POLLIN | POLLOUT, buf, sizeof(buf)), "POLLERR  | POLLIN | POLLOUT") == 0);

	hp_poll ghp_fdsobj = { 0 }, * fds = &ghp_fdsobj;
	hp_poll_init(fds, 100);
	hp_poll_uninit(fds);
	hp_poll_init(fds, 4000);
	hp_poll_uninit(fds);

	return 0;
}
#endif /* NDEBUG */

#endif /**/
