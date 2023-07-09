 /*!
 * This file is PART of libxhhp project
 * @author hongjun.liao <docici@126.com>, @date 2020/7/9
 *
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_DEPRECADTED
#ifdef LIBHP_WITH_TIMERFD

#include "hp_expire.h"
#include <sys/time.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "sdsinc.h"        /* sds */
#include "redis/src/dict.h" /* dict */
#include "hp_epoll.h"
#include "hp_dict.h"

/////////////////////////////////////////////////////////////////////////////////////////
static void def_free(void * p) {}

typedef struct expire_s {
	hp_expire * e;
	time_t t;
	void * p;
}expire_s;

static int handle_timer(hp_timerfd * timerfd)
{
	assert(timerfd && timerfd->arg);
	hp_expire * c = (hp_expire * )timerfd->arg;

	struct timeval tv;
	gettimeofday(&tv, NULL);
	time_t now = tv.tv_sec * 1000 + tv.tv_usec / 1000;

	dictIterator * iter = dictGetIterator(c->dic);
	dictEntry * ent;
	for(ent = 0; (ent = dictNext(iter));){
		expire_s * cli = ent->v.val;
		assert(cli && cli->e);

		if(cli->p && (now - cli->t) >= cli->e->ms){
			cli->e->free(cli->p);
			cli->p = 0;
		}
	}
	dictReleaseIterator(iter);

	return 0;
}

static void expire_s_free(void * ptr)
{
	assert(ptr);
	expire_s * p = (expire_s *)ptr;
	assert(p->e);
	p->e->free(p->p);

	free(p);
}

int hp_expire_init(hp_expire * e, hp_epoll * efds, int timeout_ms, void (* free)(void *))
{
	if(!(e && ((efds && timeout_ms > 0) || timeout_ms <= 0)))
		return -1;
	int r;

	r = 0;
	memset(e, 0, sizeof(hp_expire));

	hp_dict_init(&e->dic, expire_s_free);
	e->ms = timeout_ms;
	e->free = (free? free : def_free);

	if(efds && timeout_ms > 0){
		/**
		 * the timer is half of timeout, to make sure that
		 * expire time always in (0, timeout_ms]
		 * */
		r = hp_timerfd_init(&e->tfd, efds, handle_timer, timeout_ms / 2, e);
	}

	return r;
}

int hp_expire_init2(hp_expire * e, hp_epoll * efds, int tfd, int timeout_ms, void (* free)(void *))
{
	if(!(e))
		return -1;
	int r;

	r = 0;
	memset(e, 0, sizeof(hp_expire));

	hp_dict_init(&e->dic, expire_s_free);
	e->ms = timeout_ms;
	e->free = (free? free : def_free);

	if(timeout_ms > 0){
		/* @see hp_expire_init */
		r = hp_timerfd_init2(&e->tfd, efds, tfd, handle_timer, timeout_ms / 2, e);
	}

	return r;

}

void * hp_expire_get(hp_expire * e, char const * id, void * ( *fn)(char const * id))
{
	if(!(e && id))
		return 0;

	int rc;
	expire_s * p = 0;

	p = (expire_s *)hp_dict_find(e->dic, id);

	if(!(p && p->p)) {
		if(!p){
			p = calloc(1, sizeof(expire_s));

			p->e = e;

			rc = hp_dict_set(e->dic, id, p);
			assert(rc);
		}
		if(!p->p && fn)
			p->p = fn(id);
	}

	struct timeval tv;
	gettimeofday(&tv, NULL);
	time_t now = tv.tv_sec * 1000 + tv.tv_usec / 1000;

	p->t = now;

	return p->p;
}

void hp_expire_uninit(hp_expire * e)
{
	if(!e)
		return;

	hp_dict_uninit(e->dic);

	if(e->ms > 0)
		hp_timerfd_uninit(&e->tfd);
}

/////////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
#include <unistd.h>
#include "hp_redis.h"
#include "hp_config.h"

static hp_epoll efds_obj, *efds = &efds_obj;

int test_hp_expire_main(int argc, char ** argv)
{
	assert(hp_config_test);

	int i, rc;

	{
		sds empty1 = sdsempty(), empty2 = sdsempty();
		assert(sdscmp(empty1, empty2) == 0);
		sdsfree(empty1);
		sdsfree(empty2);
	}
	/* OK: expire for redisAsyncContext, with "default"(empty) ID */
	{
		int inc = 0;

		int n_rediscs = 0;
		redisAsyncContext * rediscs[128] = { 0 };

		uv_loop_t uvloopobj = { 0 }, * uvloop = &uvloopobj;
		rc = uv_loop_init(uvloop);
		assert(rc == 0);

		void f(void * p)
		{
			--inc;

			rediscs[n_rediscs++] = p;
		}
		void * n(char const * id)
		{
			++inc;

			redisAsyncContext * c = 0;
			rc = hp_redis_init(&c, uvloop, hp_config_test("redis"), hp_config_test("redis.password"), 0);
			return (rc == 0? c : 0);
		}

		hp_expire eobj, * e = &eobj;
		rc = hp_epoll_init(efds, 1000);
		assert(rc == 0);

		rc = hp_expire_init(e, efds, 5000, f);
		assert(rc == 0);

		void * expire = hp_expire_get(e, "", n);
		assert(expire);

		time_t now =  time(0);
		for(;;){
			uv_run(uvloop, UV_RUN_NOWAIT);
			hp_epoll_run(efds, 200, (void *)-1);

			void * p = hp_expire_get(e, "hello", n);
			assert(p == hp_expire_get(e, "hello", n));
			assert(p != hp_expire_get(e, "world", n));

			if(time(0) - now > 5){
				void * expire2 = hp_expire_get(e, "", n);
				assert(expire2 != expire);

				break;
			}
		}

		hp_expire_uninit(e);

		assert(inc == 0);

		for(i = 0; i < n_rediscs; ++i)
			hp_redis_uninit(rediscs[i]);

		uv_loop_close(uvloop);
	}
	/* OK: expire for redisAsyncContext */
	{
		int inc = 0;

		int n_rediscs = 0;
		redisAsyncContext * rediscs[128] = { 0 };

		uv_loop_t uvloopobj = { 0 }, * uvloop = &uvloopobj;
		rc = uv_loop_init(uvloop);
		assert(rc == 0);

		void f(void * p)
		{
			--inc;

			rediscs[n_rediscs++] = p;
		}
		void * n(char const * id)
		{
			++inc;

			redisAsyncContext * c = 0;
			rc = hp_redis_init(&c, uvloop, hp_config_test("redis"), hp_config_test("redis.password"), 0);
			return (rc == 0? c : 0);
		}

		hp_expire eobj, * e = &eobj;
		rc = hp_epoll_init(efds, 1000);
		assert(rc == 0);

		rc = hp_expire_init(e, efds, 5000, f);
		assert(rc == 0);

		void * expire = hp_expire_get(e, "expire", n);
		assert(expire);

		time_t now =  time(0);
		for(;;){
			uv_run(uvloop, UV_RUN_NOWAIT);
			hp_epoll_run(efds, 200, (void *)-1);

			void * p = hp_expire_get(e, "hello", n);
			assert(p == hp_expire_get(e, "hello", n));
			assert(p != hp_expire_get(e, "world", n));

			if(time(0) - now > 5){
				void * expire2 = hp_expire_get(e, "expire", n);
				assert(expire2 != expire);

				break;
			}
		}

		hp_expire_uninit(e);

		assert(inc == 0);

		for(i = 0; i < n_rediscs; ++i)
			hp_redis_uninit(rediscs[i]);

		uv_loop_close(uvloop);
	}
	return 0;
}

#endif
#endif /* LIBHP_WITH_TIMERFD */
#endif //LIBHP_DEPRECADTED
