/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2022/1/8
 *
 * the poll event system
 *
 * 2023/8/1 updated:
 * update hp_poll::fds memory layout:
 * */

/////////////////////////////////////////////////////////////////////////////////////

#ifndef LIBHP_POLL_H__
#define LIBHP_POLL_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef HAVE_POLL_H
#include <poll.h>  	/* pollfd */
#include "hp_net.h" //hp_sock_t
#include "hp_stdlib.h" //hp_cmp_fn_t


#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////
typedef struct pollfd pollfd;
typedef struct hp_poll hp_poll;
/*!
 * NOTE: set pd->revents = 0 after processed
 * @return: 0 on OK, else hp_poll_del* called
 * */
typedef int (* hp_poll_cb_t)(pollfd * pd, void * arg);
typedef int (* hp_poll_loop_t)(void * arg);

struct hp_poll {
	pollfd * fds;
	int nfds;
	int max_fd;
	int timeout;
	int (* before_wait)(hp_poll * po);
	void * 	arg;	/* ignored by hp_poll */
};

int hp_poll_init(hp_poll * po, int max_fd, int timeout
		, int (* before_wait)(hp_poll * po), void * arg);
//@param mode: 0==run forever, 1==run once
int hp_poll_add(hp_poll * po, hp_sock_t fd, int events
		, hp_poll_cb_t  fn, hp_poll_loop_t on_loop, void * arg);
int hp_poll_rm(hp_poll * po, hp_sock_t fd);
int hp_poll_run(hp_poll * po, int mode);
void hp_poll_uninit(hp_poll * po);

void * hp_poll_find(hp_poll * po, void * key, hp_cmp_fn_t on_cmp);
/////////////////////////////////////////////////////////////////////////////////////////
#define hp_poll_size(po) (po)->nfds
char * hp_poll_e2str(int events, char * buf, int len);

/////////////////////////////////////////////////////////////////////////////////////////
#ifndef NDEBUG
int test_hp_poll_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif

#endif  /* HAVE_POLL_H */
#endif /* LIBHP_POLL_H__ */
