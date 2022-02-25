/*!
 *  This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2020/1/3
 *
 *
 * */
#ifndef HP_TCPIO_H
#define HP_TCPIO_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */
#if !defined(__linux__) && !defined(_MSC_VER)
#elif !defined(_MSC_VER)
#include "hp_epoll.h"    /* hp_epoll */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hp_tcpio hp_tcpio;
struct hp_tcpio {
	int 		tcp_keepalive;
	hp_epoll * 	efds;
	hp_epolld   ed;

	int (* conn)(hp_tcpio * ctx, int connfd, void ** arg);
	int (* pack)(char* buf, size_t* len, void* arg);
	void (* write_error)(struct hp_eto * eto, int err, void * arg);
	void (* read_error)(struct hp_eti * eti, int err, void * arg);
};

int hp_tcpio_init(hp_tcpio * ctx, hp_epoll * efds, int fd
		, void * conn, void * pack);
void hp_tcpio_uninit(hp_tcpio * ctx);

#ifdef __cplusplus
}
#endif
#endif /* _MSC_VER */
#endif /* HP_TCPIO_H */
