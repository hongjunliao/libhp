 /*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2020/7/2
 *
 * message pub/sub using Redis, with async mode
 * 1. using Redis pub/sub
 * 2. NO Qos support, messages need remove manually
 * */

/*
 * You are required to use two connections for cli and sub ...
 * @see
 * https://gist.github.com/pietern/348262
 * https://blog.csdn.net/a1234H/article/details/85073135
 * */

#ifndef LIBHP_PUB_H__
#define LIBHP_PUB_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_WITH_REDIS

#include <hiredis/async.h>
#include "sdsinc.h"
#include "hp_libc.h" /*hp_tupleN_t*/
#include "klist.h"        /* list_head */

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////////

hp_tuple2_t(hp_sub_arg_t, void * /*arg*/, int /*argi*/);
typedef struct hp_sub_t hp_sub_t;
/**
 * @param topic: topic name on which @param msg recieved;
 * 		"" for unsubscribe done;
 * 		NULL for error;
*/
typedef void (* hp_sub_cb_t)(hp_sub_t * s, char const * topic, sds id, sds msg);

struct hp_sub_t {
	redisAsyncContext * c, * subc;
	char shasub[64], shasup[64];
	char sid[128];
	hp_sub_cb_t cb;
	hp_sub_arg_t arg;
	/* internal use */
	struct list_head async_list;
};

/**
 * @param len: if <= 0, then use strlen for @param msg
 * @param done: to check if operation is done
 */
int hp_pub(redisAsyncContext * c, char const * topic, char const * msg, int len
	, redisCallbackFn done);

/**
 * @brief: set redisAsyncContext
 * @param c, @param subc: independent redisAsyncContext for Redis subscribe command is required
 * @param shasub, @param shasup: see scripts/sub.lua, sup.lua, e.g:
 * 	46f4d5d9de9a8d524a8ee43e8b0fafcd6d66e340
 * @param sid: hash key for session, e.g. xhmdm_test/s/865452044887154
 * @param cb, @param arg: callback
 *
 * @return: @param subc
 */
redisAsyncContext * hp_subc(redisAsyncContext * c, redisAsyncContext * subc
		, char const * shasub, char const * shasup
		, char const * sid
		, hp_sub_cb_t cb
		, void const * arg);
redisAsyncContext * hp_subc_arg(redisAsyncContext * c, redisAsyncContext * subc
		, char const * shasub, char const * shasup
		, char const * sid
		, hp_sub_cb_t cb
		, hp_sub_arg_t arg);

/**
 * @brief: subscribe topics in
 * 1.form @param topic with count @param n_topic
 * 2.from @param sid
 *
 * @param subc: returned by hp_subc
 * @param n_topic, topic:
 * 1.if NOT NULL, update session and subscribe
 * 2.NULL, first find session by @param sid, then subscribe
 *
 * @return: 0 on OK
 */
int hp_sub(redisAsyncContext * subc, int n_topic, char * const* topic);

/**
 * @return: 0 on OK
 * */
int hp_unsub(redisAsyncContext * subc);

int hp_sub_ping(redisAsyncContext * subc);

/////////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
int test_hp_pub_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif

#endif /* LIBHP_WITH_REDIS  */

#endif /* LIBHP_PUB_H__ */
