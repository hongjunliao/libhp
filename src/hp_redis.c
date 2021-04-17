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

#include "sdsinc.h"
#include <unistd.h> /* sleep */
#include <time.h>
#include "hp_redis.h"
#include "unistd.h"
#include "hp_libc.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <hiredis/async.h>
#ifndef _MSC_VER
#include <hiredis/adapters/libuv.h>
#else
#include <hiredis/adapters/ae.h>
#endif /* _MSC_VER */

#include "hp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int gloglevel;
/////////////////////////////////////////////////////////////////////////////////////////

static void auth_cb(redisAsyncContext *c, void *r, void *privdata)
{
    redisReply * reply = (redisReply *)r;
	if (!reply) {
		hp_log(stderr, "redis AUTH '%s' faield\n", ("redis.password"));
	}
}

static void connectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        hp_log(stdout, "%s: connect Redis failed: '%s'\n", __FUNCTION__, c->errstr);
        return;
    }
}

static void disconnectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        printf("Error: %s\n", c->errstr);
        return;
    }
}

int hp_redis_init(redisAsyncContext ** redisc, hp_redis_ev_t * rev, char const * addr, char const *passwd
	, void ( * on_connect)(const redisAsyncContext *c, int status))
{
	if(!(redisc && addr))
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
#ifndef _MSC_VER
	redisLibuvAttach(c, rev);
#else
	redisAeAttach(rev, c);
#endif /* _MSC_VER */

	redisAsyncSetConnectCallback(c, on_connect? on_connect : connectCallback);
	redisAsyncSetDisconnectCallback(c, disconnectCallback);

	if(passwd && strlen(passwd) > 0)
		redisAsyncCommand(c, auth_cb, /* privdata */0, "AUTH %s", passwd);

	if(gloglevel > 0)
		hp_log(stdout, "%s: connecting to Redis, host='%s:%d', password='%s' ...\n", __FUNCTION__
			, host, port, (strlen(passwd) > 0? "***" : ""));

	*redisc = c;

	return 0;
}

void hp_redis_uninit(redisAsyncContext *redisc)
{
	if(redisc)
		redisAsyncDisconnect(redisc);

	if(gloglevel > 0)
		hp_log(stdout, "%s: disconneted Redis\n", __FUNCTION__);
}

/////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
#include "hp_config.h"

extern hp_config_t g_conf;

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
	assert(g_conf);
	hp_config_t cfg = g_conf;

	int r, rc;
#ifndef _MSC_VER
	/* test if connect OK */
	{
		redisAsyncContext * c = 0;
		hp_redis_ev_t revobj = { 0 }, * rev = &revobj;

		r = uv_loop_init(rev);
		assert(r == 0);
		r = hp_redis_init(&c, rev, cfg("redis"), cfg("redis.password"), test_hp_redis_on_connect_1);

		assert(r == 0 && c);
		for(;;) {
			uv_run(rev, UV_RUN_NOWAIT);
			if(s_conn_flag){
				fprintf(stdout, "%s: connect to Redis failed, skip this test\n", __FUNCTION__);
				return 0;
			}
		};
		hp_redis_uninit(c);
	}

	/* basic test */
	{
		redisAsyncContext * c = 0;
		hp_redis_ev_t revobj = { 0 }, * rev = &revobj;

		r = uv_loop_init(rev);
		assert(r == 0);
		r = hp_redis_init(&c, rev, cfg("redis"), cfg("redis.password"), 0);

		assert(r == 0 && c);

		int ping = 0;
		for(;!done ;) {
			if(!ping){
				rc = redisAsyncCommand(c, test_hp_redis_cb_1, 0/* privdata */, "ping");
				assert(rc == 0);
				ping  = 1;
			}
			uv_run(rev, UV_RUN_NOWAIT);
		}
	}
	/* reconnect test */	
	{
		static redisAsyncContext * c = 0;
		hp_redis_ev_t revobj = { 0 }, * rev = &revobj;

		r = uv_loop_init(rev);
		assert(r == 0);
		r = hp_redis_init(&c, rev, cfg("redis"), cfg("redis.password"), test_hp_redis_on_connect_2);

		assert(r == 0 && c);

		for(; ;) {
			rc = redisAsyncCommand(c, test_hp_redis_cb_2, 0/* privdata */, "ping");
			if(rc != 0)
				printf("%s: Error: %d\n", __FUNCTION__, c->err);
			uv_run(rev, UV_RUN_NOWAIT);

			sleep(2);
		}
	}	
#else
	/* test if connect OK */
	{
		redisAsyncContext * c = 0;
		hp_redis_ev_t *rev = aeCreateEventLoop(65535); assert(rev);
		r = hp_redis_init(&c, rev, cfg("redis"), cfg("redis.password"), test_hp_redis_on_connect_1);

		assert(r == 0 && c);
		for (;;) {
			aeProcessEvents(rev, AE_ALL_EVENTS);
			if (s_conn_flag < 0) {
				fprintf(stdout, "%s: connect to Redis failed, skip this test\n", __FUNCTION__);
				return 0;
			}
			else if(s_conn_flag > 0) { break; }
		};
		hp_redis_uninit(c);
	}
	/* basic test */
	{
		redisAsyncContext * c = 0;
		hp_redis_ev_t *rev = aeCreateEventLoop(65535); assert(rev);
		r = hp_redis_init(&c, rev, cfg("redis"), cfg("redis.password"), 0);
		assert(r == 0 && c);

		int ping = 0;
		for (; !done;) {
			if (!ping) {
				rc = redisAsyncCommand(c, test_hp_redis_cb_1, 0/* privdata */, "ping");
				assert(rc == 0);
				ping = 1;
			}
			aeProcessEvents(rev, AE_ALL_EVENTS);
		}
		hp_redis_uninit(c);
	}
	/* reconnect test */
	{
		static redisAsyncContext * c = 0;
		hp_redis_ev_t *rev = aeCreateEventLoop(65535); assert(rev);
		r = hp_redis_init(&c, rev, cfg("redis"), cfg("redis.password"), test_hp_redis_on_connect_2);
		assert(r == 0 && c);

		for (; ;) {
			rc = redisAsyncCommand(c, test_hp_redis_cb_2, 0/* privdata */, "ping");
			assert(rc == 0);
			aeProcessEvents(rev, AE_ALL_EVENTS);
			if(done) { break; }
		}
		hp_redis_uninit(c);
	}
#endif /* _MSC_VER */

	if(s_conn_flag < 0)
		fprintf(stdout, "%s: connect to Redis failed, skip this test\n", __FUNCTION__);
	return 0;
}

#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif

#endif /* LIBHP_WITH_REDIS */
