/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2022/1/8
 *
 * the poll event system
 *
 * */

#ifndef LIBHP_POLL_H__
#define LIBHP_POLL_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#if !defined(__linux__) && !defined(_MSC_VER)
#include <poll.h>  /* poll */

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////
typedef struct hp_poll hp_poll;
typedef struct hp_polld hp_polld;
typedef struct hp_bwait hp_bwait;
typedef int (* hp_poll_cb_t)(struct pollfd * ev);

struct hp_polld {
	int                  fd;
	hp_poll_cb_t        fn;
	void *               arg;
	int                  n;    /* index when fired */
};

struct hp_poll {
	struct pollfd * 	 fd;
	int                  nfd;

	hp_bwait **          bwaits;

	int                  stop; /* stop loop? */
	void *               arg;  /* ignored by hp_poll */
};

int hp_poll_init(struct hp_poll * fds, int n);
void hp_poll_uninit(struct hp_poll * fds);
int hp_poll_add(struct hp_poll * fds, int fd, int events, struct hp_polld * ed);
int hp_poll_del(struct hp_poll * fds, int fd, int events, struct hp_polld * ed);

int hp_poll_add_before_wait(struct hp_poll * fds
		, int (* before_wait)(struct hp_poll * fds, void * arg), void * arg);

int hp_poll_run(hp_poll * fds, int timeout, int (* before_wait)(struct hp_poll * fds));
char * hp_poll_e2str(int events, char * buf, int len);

#define hp_poll_arg(ev) (((struct hp_polld *)(ev)->data.ptr)->arg)
#define hp_poll_fd(ev)  (((struct hp_polld *)(ev)->data.ptr)->fd)

#define hp_polld_set(ed, _fd, _fn, _arg)  \
	(ed)->fd = _fd; (ed)->fn = _fn; (ed)->arg = _arg; (ed)->n = 0

#ifndef NDEBUG
int test_hp_poll_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif

#endif /*_MSC_VER*/
#endif /* LIBHP_POLL_H__ */
