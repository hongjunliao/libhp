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

#include <unistd.h>
#include "hp_redis.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "sds/sds.h"
#include <hiredis/async.h>
#include <hiredis/adapters/libuv.h>
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

int hp_redis_init(redisAsyncContext ** redisc, uv_loop_t * uvloop, char const * addr, char const *passwd
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
	if(!(c && c->err == REDIS_OK))
		return -2;

	redisLibuvAttach(c, uvloop);
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
static int s_conn_failed = 0;

static void cb(redisAsyncContext *c, void *r, void *privdata)
{
	redisReply * reply = (redisReply *)r;
	if(reply && reply->type != REDIS_REPLY_ERROR)
		done = 1;
}

static void cb2(redisAsyncContext *c, void *r, void *privdata)
{
	redisReply * reply = (redisReply *)r;
	if(reply && reply->type != REDIS_REPLY_ERROR)
		done = 0;
}

static void on_connect(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        hp_log(stdout, "%s: connect Redis failed: '%s'\n", __FUNCTION__, c->errstr);
        s_conn_failed = 1;
        return;
    }
}

int test_hp_redis_main(int argc, char ** argv)
{
	assert(g_conf);
	hp_config_t cfg = g_conf;

	int r, rc;
	/* test if connect OK */
	{
		redisAsyncContext * c = 0;
		uv_loop_t uvloopobj = { 0 }, * uvloop = &uvloopobj;

		r = uv_loop_init(uvloop);
		assert(r == 0);
		r = hp_redis_init(&c, uvloop, cfg("redis"), cfg("redis.password"), on_connect);

		assert(r == 0 && c);
		for(;;) {
			uv_run(uvloop, UV_RUN_NOWAIT);
			if(s_conn_failed){
				fprintf(stdout, "%s: connect to Redis failed, skip this test\n", __FUNCTION__);
				return 0;
			}
		};
		hp_redis_uninit(c);
	}

	/* basic test */
	{
		redisAsyncContext * c = 0;
		uv_loop_t uvloopobj = { 0 }, * uvloop = &uvloopobj;

		r = uv_loop_init(uvloop);
		assert(r == 0);
		r = hp_redis_init(&c, uvloop, cfg("redis"), cfg("redis.password"), 0);

		assert(r == 0 && c);

		int ping = 0;
		for(;!done ;) {
			if(!ping){
				rc = redisAsyncCommand(c, cb, 0/* privdata */, "ping");
				assert(rc == 0);
				ping  = 1;
			}
			uv_run(uvloop, UV_RUN_NOWAIT);
		}
	}
	/* reconnect test */	
	{
		static redisAsyncContext * c = 0;
		uv_loop_t uvloopobj = { 0 }, * uvloop = &uvloopobj;

		r = uv_loop_init(uvloop);
		assert(r == 0);
		r = hp_redis_init(&c, uvloop, cfg("redis"), cfg("redis.password"), on_connect);

		assert(r == 0 && c);

		for(; ;) {
			rc = redisAsyncCommand(c, cb2, 0/* privdata */, "ping");
			if(rc != 0)
				printf("%s: Error: %d\n", __FUNCTION__, c->err);
			uv_run(uvloop, UV_RUN_NOWAIT);

			sleep(2);
		}
	}	

	if(s_conn_failed)
		fprintf(stdout, "%s: connect to Redis failed, skip this test\n", __FUNCTION__);
	return 0;
}

#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif

#endif /* LIBHP_WITH_REDIS */
