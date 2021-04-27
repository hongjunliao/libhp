 /*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2020/7/2
 *
 * Redis util
 * */
#ifndef HP_REDIS_H__
#define HP_REDIS_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_WITH_REDIS

#ifndef LIBHP_WITH_WIN32_INTERROP
#include <uv.h>
typedef uv_loop_t hp_redis_ev_t;
#define rev_init(rev) do { if(uv_loop_init(rev) != 0) { rev = 0; } } while(0)
#define rev_run(rev) do { uv_run(rev, UV_RUN_NOWAIT); } while(0)
#define rev_close(rev) do { uv_loop_close(rev); } while(0)
#else
#include "redis/src/ae.h" /* aeEventLoop */
typedef aeEventLoop hp_redis_ev_t;
#define rev_init(rev) do { rev = aeCreateEventLoop(1024 * 10);assert(rev); } while(0)
#define rev_run(rev) do { aeProcessEvents((rev), AE_ALL_EVENTS); } while(0)
#define rev_close(rev) do { aeDeleteEventLoop(rev); } while(0)
#endif /* _MSC_VER */
#include <hiredis/async.h>

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////////
int hp_redis_init(redisAsyncContext ** redisc, hp_redis_ev_t * rev, char const * addr, char const *passwd
	, void ( * on_connect)(const redisAsyncContext *c, int status));
void hp_redis_uninit(redisAsyncContext *redisc);
/////////////////////////////////////////////////////////////////////////////////////////
#ifndef NDEBUG
int test_hp_redis_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifndef NDEBUG

#endif

#ifdef __cplusplus
}
#endif

#endif /* LIBHP_WITH_REDIS */
#endif /* HP_REDIS_H__ */
