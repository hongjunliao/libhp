/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/5/18
 *
 * the epoll event system
 *
 * NOTE:
 * linux ONLY
 * */

#ifndef LIBHP_EPOLL_H__
#define LIBHP_EPOLL_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifndef _MSC_VER

#include <sys/epoll.h>  /* epoll_event */

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////
typedef struct hp_epoll hp_epoll;
typedef struct hp_epolld hp_epolld;
typedef struct hp_bwait hp_bwait;
typedef int (* hp_epoll_cb_t)(struct epoll_event * ev);

struct hp_epolld {
	int                  fd;
	hp_epoll_cb_t        fn;
	void *               arg;
	int                  n;    /* index when fired */
};

struct hp_epoll {
	int                  fd;
	struct epoll_event * ev;
	int                  ev_len;

	hp_bwait **          bwaits;

	int                  stop; /* stop loop? */
	void *               arg;  /* ignored by hp_epoll */
};

int hp_epoll_init(struct hp_epoll * efds, int n);
void hp_epoll_uninit(struct hp_epoll * efds);
int hp_epoll_add(struct hp_epoll * efds, int fd, int events, struct hp_epolld * ed);
int hp_epoll_del(struct hp_epoll * efds, int fd, int events, struct hp_epolld * ed);

int hp_epoll_add_before_wait(struct hp_epoll * efds
		, int (* before_wait)(struct hp_epoll * efds, void * arg), void * arg);

int hp_epoll_run(hp_epoll * efds, int timeout, int (* before_wait)(struct hp_epoll * efds));
char * hp_epoll_e2str(int events, char * buf, int len);

#define hp_epoll_arg(ev) (((struct hp_epolld *)(ev)->data.ptr)->arg)
#define hp_epoll_fd(ev)  (((struct hp_epolld *)(ev)->data.ptr)->fd)

#define hp_epolld_set(ed, _fd, _fn, _arg)  \
	(ed)->fd = _fd; (ed)->fn = _fn; (ed)->arg = _arg; (ed)->n = 0

#ifndef NDEBUG
int test_hp_epoll_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif

#endif /*_MSC_VER*/
#endif /* LIBHP_EPOLL_H__ */
