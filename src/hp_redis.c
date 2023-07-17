 /*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2020/7/2
 *
 * Redis util
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_WITH_REDIS

#include "hp/sdsinc.h"
#include <unistd.h> /* sleep */
#include <time.h>
#include "hp/hp_redis.h"
#include "unistd.h"
#include "hp/hp_libc.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <hiredis/async.h>
#ifndef LIBHP_WITH_WIN32_INTERROP
#include <hiredis/adapters/libuv.h>
#else
#include <hiredis/adapters/ae.h>
#endif /* _MSC_VER */

#include "hp/hp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////////

static void auth_cb(redisAsyncContext *c, void *r, void *privdata)
{
    redisReply * reply = (redisReply *)r;
	if (!reply) {
		hp_log(stderr, "redis AUTH '%s' faield\n", ("redis.password"));
	}
}

static void hp_redis_connectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        hp_log(stdout, "%s: connect Redis failed: '%s'\n", __FUNCTION__, c->errstr);
        return;
    }
}

static void hp_redis_disconnectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
		hp_log(stderr, "%s: Error: %s\n", __FUNCTION__, c->errstr);
        return;
    }
}

int hp_redis_init(redisAsyncContext ** redisc, hp_redis_ev_t * s_ev, char const * addr, char const *passwd
	, void ( * on_connect)(const redisAsyncContext *c, int status))
{
	if(!(redisc && s_ev && addr))
		return -1;

	redisAsyncContext * c = 0;

	char host[64] = "";
	int port = 0;

	int n = sscanf(addr, "%[^:]:%d", host, &port);
	if(n != 2)
		return -2;

	c = redisAsyncConnect(host, port);
	if (!(c && c->err == REDIS_OK)) {
		hp_log(stderr, "%s: redisAsyncConnect '%s:%d' failed, err=%d/'%s'\n", __FUNCTION__
		, host, port, c->err, c->errstr);
		return -2;
	}
	c->dataCleanup = 0;

#ifndef LIBHP_WITH_WIN32_INTERROP
	redisLibuvAttach(c, s_ev);
#else
	redisAeAttach(s_ev, c);
#endif /* _MSC_VER */

	redisAsyncSetConnectCallback(c, on_connect? on_connect : hp_redis_connectCallback);
	redisAsyncSetDisconnectCallback(c, hp_redis_disconnectCallback);

	if(passwd && strlen(passwd) > 0)
		redisAsyncCommand(c, auth_cb, /* privdata */0, "AUTH %s", passwd);

	if(hp_log_level > 0){
		hp_log(stdout, "%s: connecting to Redis, host='%s:%d', password='%s' ...\n", __FUNCTION__
			, host, port, (strlen(passwd) > 0? "***" : ""));
	}

	*redisc = c;

	return 0;
}

void hp_redis_uninit(redisAsyncContext *redisc)
{
	if(redisc)
		redisAsyncDisconnect(redisc);

	if(hp_log_level > 0)
		hp_log(stdout, "%s: disconneted Redis\n", __FUNCTION__);
}

/////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
#include "hp/hp_config.h"
static hp_redis_ev_t s_evobj, *s_ev = &s_evobj;

static int done = 0;
static int s_conn_flag = 0;

static void test_hp_redis_cb_1(redisAsyncContext *c, void *r, void *privdata)
{
	redisReply * reply = (redisReply *)r;
	if(reply && reply->type != REDIS_REPLY_ERROR)
		done = 1;
}

static void test_hp_redis_cb_2(redisAsyncContext *c, void *r, void *privdata)
{
	redisReply * reply = (redisReply *)r;
	if(reply && reply->type != REDIS_REPLY_ERROR)
		done = 1;
}

static void test_hp_redis_on_connect_1(const redisAsyncContext *c, int status) {
	s_conn_flag = (status != REDIS_OK)? -1 : (hp_max(s_conn_flag, 0) + 1);
	if (status != REDIS_OK) {
        hp_log(stdout, "%s: connect Redis failed: '%s'\n", __FUNCTION__, c->errstr);
        return;
    }
}
static void test_hp_redis_on_connect_2(const redisAsyncContext *c, int status) {
	s_conn_flag = (status != REDIS_OK) ? -1 : (hp_max(s_conn_flag, 0) + 1);
	if (status != REDIS_OK) {
		hp_log(stdout, "%s: connect Redis failed: '%s'\n", __FUNCTION__, c->errstr);
		return;
	}
}

int test_hp_redis_main(int argc, char ** argv)
{
	assert(hp_config_test);
	hp_config_t cfg = hp_config_test;

	int r, rc;
	/* test if connect OK */
	{
		redisAsyncContext * c = 0;
		rev_init(s_ev);
		r = hp_redis_init(&c, s_ev, cfg("redis"), cfg("redis.password"), test_hp_redis_on_connect_1);

		assert(r == 0 && c);
		for (;;) {
			rev_run(s_ev);
			if (s_conn_flag < 0) {
				fprintf(stdout, "%s: connect to Redis failed, skip this test\n", __FUNCTION__);
				return 0;
			}
			else if(s_conn_flag > 0) { break; }
		};
		hp_redis_uninit(c);
		rev_close(s_ev);
		done = 0;
	}
	/* basic test */
	{
		redisAsyncContext * c = 0;
		rev_init(s_ev);
		r = hp_redis_init(&c, s_ev, cfg("redis"), cfg("redis.password"), 0);
		assert(r == 0 && c);

		int ping = 0;
		for (; !done;) {
			if (!ping) {
				rc = redisAsyncCommand(c, test_hp_redis_cb_1, 0/* privdata */, "ping");
				assert(rc == 0);
				ping = 1;
			}
			rev_run(s_ev);
		}
		hp_redis_uninit(c);
		rev_close(s_ev);
		done = 0;
	}
	/* reconnect test */
	{
		static redisAsyncContext * c = 0;
		rev_init(s_ev);
		r = hp_redis_init(&c, s_ev, cfg("redis"), cfg("redis.password"), test_hp_redis_on_connect_2);
		assert(r == 0 && c);

		for (; ;) {
			rc = redisAsyncCommand(c, test_hp_redis_cb_2, 0/* privdata */, "ping");
			assert(rc == 0);
			rev_run(s_ev);
			if(done) { break; }
		}
		hp_redis_uninit(c);
		rev_close(s_ev);
		done = 0;
	}

	if(s_conn_flag < 0)
		fprintf(stdout, "%s: connect to Redis failed, skip this test\n", __FUNCTION__);
	return 0;
}

#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif

#endif /* LIBHP_WITH_REDIS */
