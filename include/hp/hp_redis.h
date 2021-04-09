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

#include "sds/sds.h"
/* use our modified sds instead of default
 * please include sds/sds.h first
 *  */
#include <hiredis/async.h>
#include <uv.h>

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////////
int hp_redis_init(redisAsyncContext ** redisc, uv_loop_t * uvloop, char const * addr, char const *passwd
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
