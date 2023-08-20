/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/5/18
 *
 * the epoll event system
 * 2023/8/5 updated: add hp_epoll::ed, hp_epoll_add(arg)
 * */
/////////////////////////////////////////////////////////////////////////////////////

#ifndef LIBHP_EPOLL_H__
#define LIBHP_EPOLL_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef HAVE_SYS_EPOLL_H
#include <sys/epoll.h>  /* epoll_event */
#include "hp/hp_net.h"	//hp_sock_t
#include "hp_stdlib.h" //hp_cmp_fn_t

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////
typedef struct epoll_event epoll_event;
typedef struct hp_epoll hp_epoll;
typedef struct hp_epolld hp_epolld;
typedef int (* hp_epoll_cb_t)(epoll_event * ev, void * arg);
typedef int (* hp_epoll_loop_t)(void * arg);

struct hp_epoll {
	int fd;
	epoll_event * ev;
	hp_epolld **  ed;
	int ev_len;
	int max_ev_len;
	int timeout;
	int (* before_wait)(hp_epoll * epo);
	void * arg;  /* ignored by hp_epoll */
	int flags;
};

int hp_epoll_init(hp_epoll * epo, int max_ev_len, int timeout
		, int (* before_wait)(hp_epoll * epo), void * arg);
int hp_epoll_add(hp_epoll * epo, hp_sock_t fd, int events,
		hp_epoll_cb_t  fn, hp_epoll_loop_t on_loop, void * arg);
int hp_epoll_rm(hp_epoll * epo, int fd);
int hp_epoll_run(hp_epoll * epo, int mode);
#define hp_epoll_size(epo) (epo)->ev_len

void * hp_epoll_find(hp_epoll * epo, void * key, hp_cmp_fn_t on_cmp);
void hp_epoll_uninit(hp_epoll * epo);

/////////////////////////////////////////////////////////////////////////////////////

char * hp_epoll_e2str(int events, char * buf, int len);

#ifndef NDEBUG
int test_hp_epoll_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif

#endif /*HAVE_SYS_EPOLL_H*/
#endif /* LIBHP_EPOLL_H__ */
