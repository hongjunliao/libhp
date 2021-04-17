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

#ifndef _MSC_VER
#include <uv.h>
typedef uv_loop_t hp_redis_ev_t;
#else
#include "redis/src/ae.h" /* aeEventLoop */
typedef aeEventLoop hp_redis_ev_t;
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
