 /*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2020/7/2
 *
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_WITH_REDIS

#include "hp_pub.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "sds/sds.h"
#include "c-vector/cvector.h"
#include "hp_log.h"
#include "hp_libc.h"
#include "string_util.h"
#include "klist.h"        /* list_head */

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////////
/**
 * pub
 * */

static void pub_cb(redisAsyncContext *c, void *r, void *privdata)
{
	redisReply * reply = (redisReply *)r;

    if (!(reply && reply->type != REDIS_REPLY_ERROR)){
		hp_log(stderr, "%s: redis PUBLISH failed, err/errstr=%d/'%s'/'%s'\n"
				, __FUNCTION__, c->err, (reply? reply->str : ""), c->errstr);
    }
}

int hp_pub(redisAsyncContext * c, char const * topic, char const * msg, int len
	, redisCallbackFn done)
{
	if(!(c && topic && msg))
		return -1;

	int rc;
	if(len <= 0)
		len = strlen(msg);

	char uuidstr[64] = "";

	struct timeval tv;
	gettimeofday(&tv, NULL);
	snprintf(uuidstr, sizeof(uuidstr), "%ld", tv.tv_sec * 1000 + tv.tv_usec / 1000);

	rc = redisAsyncCommand(c, 0, 0/* privdata */, "ZADD %s %s %b", topic, uuidstr, msg, len);
	if(rc != 0)
		hp_log(stderr, "%s: Redis 'ZADD %s %s %s', failed: %d/'%s'\n"
			, __FUNCTION__, topic, uuidstr, msg, c->err, c->errstr);

	rc = redisAsyncCommand(c, (done? done : pub_cb), done/* privdata */, "publish %s %s", topic, uuidstr);
	if(rc != 0)
		hp_log(stderr, "%s: Redis 'publish %s %s' failed: %d/'%s'\n"
			, __FUNCTION__, topic, uuidstr, c->err, c->errstr);

	return rc;
}

/////////////////////////////////////////////////////////////////////////////////////////
/**
 * sub
 * */
typedef struct hp_sub_async_t hp_sub_async_t;
struct hp_sub_async_t {
	hp_sub_t * s;
	struct list_head node;
};

hp_tuple3_t(sort_ctx, sds /*t*/, sds /*msg*/, char const * /*topic*/);

static int comp(const void * a, const void * b)
{
	sort_ctx * ent1 = (sort_ctx *)a, * ent2 = (sort_ctx *)b;
	assert(ent1 && ent2);
	return sdscmp(ent1->_1, ent2->_1);
}

static void all_cb(redisAsyncContext *c, void *r, void *privdata)
{
	assert(privdata);
	hp_sub_async_t * as = (hp_sub_async_t *)privdata;
	redisReply * reply = (redisReply *)r;
	size_t i, j;

    sort_ctx * sortc = 0;
    cvector_init(sortc, 0);

	if(!as->s){
		goto ret;
	}

    if (!(reply && reply->element && reply->type != REDIS_REPLY_ERROR)){
		hp_log(stderr, "%s: redis evalsha '%s' failed, err/errstr=%d/'%s'/'%s'\n"
				, __FUNCTION__, "", c->err, (reply? reply->str : ""), c->errstr);

		/* query failed not means subscribe failed */
		goto ret;
    }

    for(j  = 1; j < reply->elements; j += 2){
    	char const *  t = reply->element[j - 1]->str;
    	redisReply * d = reply->element[j];
        for(i  = 1; i < d->elements; i += 2){
        	sds msg = sdsnewlen(d->element[i - 1]->str, d->element[i - 1]->len);
    		sds mid = sdsnewlen(d->element[i]->str, d->element[i]->len);

    		sort_ctx item = { ._1 = mid, ._2 = msg, ._3 = t };
    		cvector_push_back(sortc, item);
        }
    }
    cvector_qsort(sortc, qsort, comp);

    for(i = 0; i < cvector_size(sortc); ++i){
    	if(as->s->cb)
    		as->s->cb(as->s, sortc[i]._3, sortc[i]._1, sortc[i]._2);

    	sdsfree(sortc[i]._1);
    	sdsfree(sortc[i]._2);
    }
ret:
    cvector_free(sortc);
	if(as->s)
		list_del(&as->node);
	free(as);
}

static int hp_sub_all(hp_sub_t * ps)
{
	assert(ps);

	int rc;

	hp_sub_async_t * async = calloc(1, sizeof(hp_sub_async_t));
	async->s = ps;
	list_add_tail(&async->node, &ps->async_list);

	rc = redisAsyncCommand(ps->c, all_cb, async/* privdata */, "evalsha %s 0 %s", ps->shasub, ps->sid);

	return rc;
}

static void msg_cb(redisAsyncContext *c, void *r, void *privdata)
{
	hp_sub_t * ps = (hp_sub_t *)c->data;
	assert(ps);
	redisReply * reply = (redisReply *)r;

	int rc;
	HP_UNUSED(rc);

    if (!(reply && reply->type != REDIS_REPLY_ERROR)){

  		hp_log(stderr, "%s: redis command failed, err/errstr=%d/'%s'/'%s'\n"
				, __FUNCTION__, c->err, (reply? reply->str : ""), c->errstr);
				
		return;
    }

	if(strcmp( reply->element[0]->str,"subscribe") == 0){
		
		/* query 'history' messages first time :) */
		if(reply->element[2]->integer == 1){

			rc = hp_sub_all(ps);
		}

		goto ret;
	}

	if(strcmp( reply->element[0]->str,"unsubscribe") == 0){

		/* callback to tell that unsub is done */
		if(ps->cb && reply->element[2]->integer == 0){

			sds err = sdsempty();
			sds mid = sdsnew("");
			ps->cb(ps, "", mid, err);
			sdsfree(mid);
			sdsfree(err);
		}

		goto ret;
	}

	rc = hp_sub_all(ps);

ret:
	return;
}

static int hp_sub_dosub(redisAsyncContext * c
		, int n_topic, char * const* topic)
{
	hp_sub_t * ps = (hp_sub_t *)c->data;
	assert(ps);
	assert(c && n_topic > 0 && topic);
	redisReply *reply = 0;

	int i, rc;
	const char *argv[n_topic + 8];
	argv[0] = "subscribe";

	sds msg = sdsempty();

	for(i = 0; i < n_topic; ++i){
		argv[i + 1] = topic[i];
		msg = sdscatfmt(msg, "\t%s\n", topic[i]);
	}

	int argc = n_topic + 1;
	rc = redisAsyncCommandArgv(c, msg_cb, 0/* privdata */, argc, argv, 0);

	hp_log(stdout, "%s: rc=%d, subscribing topics total=%d:\n%s", __FUNCTION__, rc, n_topic, msg);

	if(rc != 0){
		if(ps->cb){
			sds err = sdscatprintf(sdsempty(), "%s: redis subscribe failed, err/errstr=%d/'%s'/'%s'"
				, __FUNCTION__, c->err, (reply? reply->str : ""), c->errstr);
			sds mid = sdsnew("subscribe");
			
			ps->cb(ps, 0, mid, err);
			
			sdsfree(mid);
			sdsfree(err);	
		}
		else hp_log(stderr, "%s: redis subscribe failed, err/errstr=%d/'%s'/'%s'"
				, __FUNCTION__, c->err, (reply? reply->str : ""), c->errstr);
	}

	sdsfree(msg);

	return rc;
}

static void session_cb(redisAsyncContext *c, void *r, void *privdata)
{
	assert(privdata);
	hp_sub_async_t * as = (hp_sub_async_t *)privdata;
	redisReply *reply = (redisReply *)r;

	int i, rc;

    int n_topics = (reply && reply->elements > 0? reply->elements : 0);
	sds topics[n_topics];

	if(!as->s){
		goto ret;
	}

    if (!(reply && reply->type != REDIS_REPLY_ERROR)){

		if(as->s->cb){
			sds err = sdscatprintf(sdsempty(), "%s: redis HKEYS failed, err/errstr=%d/'%s'/'%s'"
				, __FUNCTION__, c->err, (reply? reply->str : ""), c->errstr);
			sds mid = sdsnew("HKEYS");
			
			as->s->cb(as->s, 0, mid, err);
			
			sdsfree(mid);
			sdsfree(err);	
		}
		else hp_log(stderr, "%s: redis HKEYS failed, err/errstr=%d/'%s'/'%s'"
				, __FUNCTION__, c->err, (reply? reply->str : ""), c->errstr);

		/* subscribe failed, end */
		goto ret;
    }

    if(n_topics == 0){
		hp_log(stderr, "%s: session empty, sid='%s'\n"
				, __FUNCTION__, as->s->sid);

		goto ret;
    }

    for(i = 0; i < reply->elements; ++i){

    	sds topic = sdsnewlen(reply->element[i]->str, reply->element[i]->len);
    	topics[i] = topic;
    }
	hp_sub_dosub(as->s->subc, n_topics, topics);

	for(i = 0; i < n_topics; ++i)
		sdsfree(topics[i]);
ret:
	if(as->s)
		list_del(&as->node);
	free(as);

    return;
}

static void sup_cb(redisAsyncContext *c, void *r, void *privdata)
{
	redisReply *reply = (redisReply *)r;

    if (!(reply && reply->type != REDIS_REPLY_ERROR)){
		hp_log(stderr, "%s: redis command failed, err/errstr=%d/'%s'\n"
				, __FUNCTION__, c->err, (reply? reply->str : c->errstr));
		return;
    }
}

static int hp_sub_sup(redisAsyncContext * c
		, char const * shasup
		, char const * sid
		, int n_topic, char * const* topic)
{
	assert(c && shasup && n_topic > 0 && topic);

	int i, rc;

	sds nk = sdsfromlonglong(n_topic);

	const char *argv[n_topic + 8];
	argv[0] = "evalsha";
	argv[1] = shasup;
	argv[2] = nk;

	for(i = 0; i < n_topic; ++i)
		argv[i + 3] = topic[i];
	argv[i + 3] = sid;

	int argc = n_topic + 4;
	rc = redisAsyncCommandArgv(c, sup_cb, 0/* privdata */, argc, argv, 0);
	sdsfree(nk);

	return rc;
}

static void my_free(void * ptr)
{
	printf("%s: free redisAsyncContext::data ptr=%p\n", __FUNCTION__, ptr);

	hp_sub_t * done = (hp_sub_t *)ptr;

	struct list_head * pos, * next;;
	list_for_each_safe(pos, next, &done->async_list){
		hp_sub_async_t * node = (hp_sub_async_t *)list_entry(pos, hp_sub_async_t, node);
		assert(node);

		list_del(&node->node);
		node->s = 0;
	}
	free(ptr);
}

static void disconnectCallback(const redisAsyncContext *subc, int status)
{
	if(!(subc && subc->data))
		return;

	hp_sub_t * done = (hp_sub_t *)subc->data;
	if(done->cb){

		sds mid = sdsnew("");
		sds errstr = sdsnew(subc->errstr);

		done->cb(done, 0, mid, errstr);

		sdsfree(mid);
		sdsfree(errstr);
	}
}

redisAsyncContext * hp_subc_arg(redisAsyncContext * c, redisAsyncContext * subc
		, char const * shasub, char const * shasup
		, char const * sid
		, hp_sub_cb_t cb
		, hp_sub_arg_t arg)
{
	if(!(c && subc && shasub && shasup && sid))
		return 0;

	int rc;
	/* the context */
	if(!subc->data){
		subc->data = calloc(1, sizeof(hp_sub_t));
		INIT_LIST_HEAD(&((hp_sub_t *)subc->data)->async_list);
	}
#ifndef NDEBUG
	subc->dataCleanup = my_free;
#else 
	subc->dataCleanup = free;
#endif		

	hp_sub_t * done = (hp_sub_t *)subc->data;
	done->cb = cb;
	done->arg = arg;
	done->c = c;
	done->subc = subc;
	strncpy(done->shasub, shasub, sizeof(done->shasub) - 1);
	strncpy(done->sid, sid, sizeof(done->sid) - 1);

	if(shasup)
		strncpy(done->shasup, shasup, sizeof(done->shasup) - 1);

	redisAsyncSetDisconnectCallback(subc, disconnectCallback);

	return subc;
}

redisAsyncContext * hp_subc(redisAsyncContext * c, redisAsyncContext * subc
		, char const * shasub, char const * shasup
		, char const * sid
		, hp_sub_cb_t cb
		, void * arg)
{
	hp_sub_arg_t a = { arg, 0 };
	return hp_subc_arg(c, subc, shasub, shasup, sid, cb, a);
}

int hp_sub(redisAsyncContext * subc, int n_topic, char * const* topic)
{
	if(!(subc && subc->data))
		return -1;

	int rc;
	hp_sub_t * done = (hp_sub_t *)subc->data;

	/* sub topic from params */
	if(topic && n_topic > 0){

		/* update session */
		if(strlen(done->shasup) > 0){
			rc = hp_sub_sup(done->c, done->shasup, done->sid, n_topic, topic);
		}
		/* subscribe */
		rc = hp_sub_dosub(subc, n_topic, topic);
	}
	else{ /* sub topic from session */
		hp_sub_async_t * async = calloc(1, sizeof(hp_sub_async_t));
		async->s = done;
		list_add_tail(&async->node, &done->async_list);

		rc= redisAsyncCommand(done->c, session_cb, async/* privdata */, "HKEYS %s", done->sid);
	}

	return rc;
}

int hp_unsub(redisAsyncContext * subc)
{
	if(!(subc))
		return -1;

	int rc = 0;
	redisReply *reply = 0;

	rc = redisAsyncCommand(subc, 0, 0/* privdata */, "UNSUBSCRIBE");
	if(rc != 0){
		hp_log(stderr, "%s: redis UNSUBSCRIBE failed, err/errstr=%d/'%s'/'%s'\n"
				, __FUNCTION__, subc->err, (reply? reply->str : ""), subc->errstr);
	}

	return rc;
}

static void ping_cb(redisAsyncContext *c, void *r, void *privdata)
{
	redisReply * reply = (redisReply *)r;

    if (!(reply && reply->type != REDIS_REPLY_ERROR)){
		hp_log(stderr, "%s: redis PING failed, err/errstr=%d/'%s'/'%s'\n"
				, __FUNCTION__, c->err, (reply? reply->str : ""), c->errstr);
    }
}

int hp_sub_ping(redisAsyncContext * subc)
{
	if(!(subc))
		return -1;

	int rc = redisAsyncCommand(subc, /*ping_cb*/0, 0/* privdata */, "ping");

	if(rc != 0){
		hp_log(stderr, "%s: Redis 'PING' failed: %d/'%s'\n"
			, __FUNCTION__, subc->err, subc->errstr);
	}

	return rc;
}
/////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
#include "hp_redis.h"
#include <uuid/uuid.h>
#include "hp_config.h"	/* hp_config_t  */

extern hp_config_t g_conf;

static int done = 0, dones[64] = { 0 };
static redisAsyncContext * pubc = 0;
static int s_conn_failed = 0;

char const * sids[] = { "xhmdm_test:s:865452044887154", "xhmdm_test:s:868783048901857" };
static char * topics[] = { "xhmdm_test:1:865452044887154", "xhmdm_test:apps:chat.tox.antox", "xhmdm_test:dept:1006"};
static char const * fmts[] = { "{\"uuid0\":\"%s\"}", "{\"uuid1\":\"%s\"}", "{\"uuid2\":\"%s\"}" };
static sds msgs[128] = { 0 };

static char const * fmt_msg(char const * fmt)
{
	char uuidstr[64] = "";
	uuid_t uuid;
	uuid_generate_time(uuid);
	uuid_unparse(uuid, uuidstr);

	static char msg[1024] = "";
	msg[0] = '\0';
	sprintf(msg, fmt, uuidstr);

	return msg;
}

static void test_cb_resub(hp_sub_t * s, char const * topic, sds id, sds msg)
{
	assert(s && s->arg._1 && id && msg);
	assert(*(int *)s->arg._1 == 10);
	++done;
}

static void test_cb_1_cb(redisAsyncContext *c, void *r, void *privdata)
{
	redisReply * reply = (redisReply *)r; assert(reply && reply->type != REDIS_REPLY_ERROR);
	done = 1;
}
static void test_cb_1(hp_sub_t * s, char const * topic, sds id, sds msg)
{
	assert(s && s->arg._1 && id && msg);
	assert(*(int *)s->arg._1 == 10);

	if(!topic){
		fprintf(stderr, "%s: erorr %s\n", __FUNCTION__, msg);
		return;
	}
	else if(topic[0] == '\0') { 
		fprintf(stdout, "%s: unsubscribed\n", __FUNCTION__);
		return;
	}

	int rc;
	if(msgs[0]){
		if(strcmp(msg, msgs[0]) == 0){
			assert(strcmp(topic, topics[0]) == 0);
			rc = redisAsyncCommand(pubc, test_cb_1_cb, 0/* privdata */, "hset %s %s %s", sids[0], topic, id);
			assert(rc == 0);
		}
	}
}

static void test_cb_2_cb(redisAsyncContext *c, void *r, void *privdata)
{
	redisReply * reply = (redisReply *)r; assert(reply && reply->type != REDIS_REPLY_ERROR);
	++done;
}
static void test_cb_2(hp_sub_t * s, char const * topic, sds id, sds msg)
{
	assert(s && s->arg._1 && id && msg);
	assert(*(int *)s->arg._1 == 10);

	if(!topic){
		fprintf(stderr, "%s: erorr %s\n", __FUNCTION__, msg);
		return;
	}
	else if(topic[0] == '\0') { 
		fprintf(stdout, "%s: unsubscribed\n", __FUNCTION__);
		return;
	}

	int i, rc;
	for(i = 0; i < 2; ++i){
		if(msgs[i] && strcmp(msg, msgs[i]) == 0){
			assert(strcmp(topic, topics[i]) == 0);
			rc = redisAsyncCommand(pubc, test_cb_2_cb, 0/* privdata */, "hset %s %s %s", sids[0], topic, id);
			assert(rc == 0);
			break;
		}
	}
}

static void test_cb_2_2_cb(redisAsyncContext *c, void *r, void *privdata)
{
	redisReply *reply = (redisReply *)r; assert(reply && reply->type != REDIS_REPLY_ERROR);
	++done;
}
static void test_cb_2_2(hp_sub_t * s, char const * topic, sds id, sds msg)
{
	assert(s && s->arg._1 && id && msg);
	assert(*(int *)s->arg._1 == 10);

	if(!topic){
		fprintf(stderr, "%s: erorr %s\n", __FUNCTION__, msg);
		return;
	}
	else if(topic[0] == '\0') { 
		fprintf(stdout, "%s: unsubscribed\n", __FUNCTION__);
		return;
	}
	
	int i, rc;
	for(i = 0; i < 2; ++i){
		if(msgs[i] && strcmp(msg, msgs[i]) == 0){
			assert(strcmp(topic, topics[0]) == 0);
			rc = redisAsyncCommand(pubc, test_cb_2_2_cb, 0/* privdata */, "hset %s %s %s", sids[0], topic, id);
			assert(rc == 0);
			break;
		}
	}
}

static void test_cb_2_3_cb(redisAsyncContext *c, void *r, void *privdata)
{
	assert(privdata);
	char * sid = (char *)privdata;
	redisReply * reply = (redisReply *)r; assert(reply && reply->type != REDIS_REPLY_ERROR);

	int i;

	for(i = 0; i  < 2; ++i){
		if(strcmp((char *)sid, sids[i]) == 0){
			dones[i] = 1;
			break;
		}
	}
}
static void test_cb_2_3(hp_sub_t * s, char const * topic, sds id, sds msg)
{
	assert(s && s->arg._1 && id && msg);
	char * sid = (char *)s->arg._1;

	if(!topic){
		fprintf(stderr, "%s: erorr %s\n", __FUNCTION__, msg);
		return;
	}
	else if(topic[0] == '\0') { 
		fprintf(stdout, "%s: unsubscribed\n", __FUNCTION__);
		return;
	}

	int i, rc;
	assert(strcmp((char *)sid, sids[0]) == 0 || strcmp((char *)sid, sids[1]) == 0);
	for(i = 0; i < 2; ++i){
		if(msgs[i] && strcmp(msg, msgs[i]) == 0){
			assert(strcmp(topic, topics[1]) == 0);
			rc = redisAsyncCommand(pubc, test_cb_2_3_cb, s->arg._1/* privdata */, "hset %s %s %s", (char *)s->arg._1, topic, id);
			assert(rc == 0);
			break;
		}
	}
}

static void test_cb_sub_from_session_1(redisAsyncContext *c, void *r, void *privdata)
{
	redisReply * reply = (redisReply *)r; assert(reply && reply->type != REDIS_REPLY_ERROR);
	done = 1;
}

static void test_cb_sub_from_session(hp_sub_t * s, char const * topic, sds id, sds msg)
{
	if(!topic){
		fprintf(stderr, "%s: erorr %s\n", __FUNCTION__, msg);
		return;
	}
	else if(topic[0] == '\0') { 
		fprintf(stdout, "%s: unsubscribed\n", __FUNCTION__);
		return;
	}

	assert(s && s->arg._1 && id && msg);
	assert(*(int *)s->arg._1 == 10);
	int rc;
	if(msgs[0]){
		if(strcmp(msg, msgs[0]) == 0){
			if(strcmp(topic, topics[1]) == 0){
				rc = redisAsyncCommand(pubc, test_cb_sub_from_session_1, 0/* privdata */, "hset %s %s %s", sids[0], topic, id);
				assert(rc == 0);
			}
		}
	}
}

static void test_cb_sub_only_from_session(hp_sub_t * s, char const * topic, sds id, sds msg)
{
	if(!topic){
		fprintf(stderr, "%s: erorr %s\n", __FUNCTION__, msg);
		return;
	}
	else if(topic[0] == '\0') {
		fprintf(stdout, "%s: unsubscribed\n", __FUNCTION__);
		return;
	}

	assert(s && s->arg._1 && id && msg);
	assert(*(int *)s->arg._1 == 10);

	++done;
}


static void zrevrange_cb(redisAsyncContext *c, void *r, void *privdata)
{
	redisReply * reply = (redisReply *)r;

	if (!reply) {
		hp_log(stderr, "%s: redis command  failed, err/errstr=%d/'%s'\n"
				, __FUNCTION__, c->err, c->errstr);
		goto ret;
	}
	done = 1;
ret:
	return;
}

static void zrevrange_cb_2(redisAsyncContext *c, void *r, void *privdata)
{
	redisReply * reply = (redisReply *)r;

	if (!reply) {
		hp_log(stderr, "%s: redis command  failed, err/errstr=%d/'%s'\n"
				, __FUNCTION__, c->err, c->errstr);
		goto ret;
	}
	assert(reply->elements == 0);
	done = 1;
ret:
	return;
}

static void on_connect(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        hp_log(stdout, "%s: connect Redis failed: '%s'\n", __FUNCTION__, c->errstr);
        s_conn_failed = 1;
        return;
    }
}

int test_hp_pub_main(int argc, char ** argv)
{
	assert(g_conf);
	hp_config_t cfg = g_conf;

	int i, r;
	int n_topic = sizeof(topics) / sizeof(topics[0]);
	int arg = 10;

	static redisAsyncContext * subcs[64] = { 0 };
	uv_loop_t uvloopobj = { 0 }, * uvloop = &uvloopobj;

	r = uv_loop_init(uvloop);
	assert(r == 0);

	/* test if connect OK */
	r = hp_redis_init(&pubc, uvloop, cfg("redis"), cfg("redis.password"), on_connect);
	assert(r == 0 && pubc);
	for(;;) {
		uv_run(uvloop, UV_RUN_NOWAIT);
		if(s_conn_failed){
			fprintf(stdout, "%s: connect to Redis failed, skip this test\n", __FUNCTION__);
			return 0;
		}
	};
	hp_redis_uninit(pubc);

	r = hp_redis_init(&pubc, uvloop, cfg("redis"), cfg("redis.password"), 0);
	assert(r == 0 && pubc);
	r = hp_redis_init(&subcs[0], uvloop, cfg("redis"), cfg("redis.password"), 0);
	assert(r == 0 && subcs[0]);

	/* test for command: zrevrange xhmdm_test:1:868783048901857 0 0 withscores
	 * OK */
	{
		r = redisAsyncCommand(pubc, zrevrange_cb, 0/* privdata */, "zrevrange %s 0 0 withscores", "xhmdm_test:1:868783048901857");
		assert(r == 0);
		for(; !done;) uv_run(uvloop, UV_RUN_NOWAIT);
		done = 0;
	}
	/* test for command: zrevrange xhmdm_test:1:868783048901857 0 0 withscores
	 * zet NOT exist */
	{
		r = redisAsyncCommand(pubc, zrevrange_cb_2, 0/* privdata */, "zrevrange %s 0 0 withscores", "xhmdm_test:1:not_exist");
		assert(r == 0);
		for(; !done;) uv_run(uvloop, UV_RUN_NOWAIT);
		done = 0;
	}
	/* failed: invalid arg */
	{
		assert(hp_pub(0, 0, 0, 0, 0) != 0);
		assert(hp_pub(pubc, 0, 0, 0, 0) != 0);
		assert(hp_pub(pubc, topics[0], 0, 0, 0) != 0);

		assert(!hp_subc(0, 0, 0, 0, 0, 0, 0));
		assert(!hp_subc(pubc, 0, 0, 0, 0, 0, 0));
		assert(!hp_subc(pubc, subcs[0], 0, 0, 0, 0, 0));
		assert(!hp_subc(pubc, subcs[0], cfg("redis.shasub"), 0, 0, 0, 0));
		assert(!hp_subc(pubc, subcs[0], cfg("redis.shasub"), cfg("redis.shasup"), 0, 0, 0));
//		assert(hp_subc(pubc, subcs[0], cfg("redis.shasub"), cfg("redis.shasup"), sids[0], 0, 0));

		assert(hp_sub(0, 0, 0) != 0);
		assert(hp_sub(pubc, 0, 0) != 0);
	}
	/* OK: sub only, sub from session */
	{
		int unsub = 0;
		subcs[0] = hp_subc(pubc, subcs[0], cfg("redis.shasub"), cfg("redis.shasup"), sids[1], test_cb_sub_only_from_session, &arg);
		r = hp_sub(subcs[0], 0, 0);
		assert(r == 0);

		for(i = 0;;) {
			uv_run(uvloop, UV_RUN_NOWAIT);

			if(!unsub && done >= 1){
				r = hp_unsub(subcs[0]);
				assert(r == 0);

				unsub = 1;
				continue;
			}
			if(done >= 1 && unsub)
				break;
		}

		done = 0;
		for(i = 0; msgs[i]; ++i) sdsfree(msgs[i]);
		memset(msgs, 0, sizeof(msgs));
	}

	/* OK: pub only, pub 1 */
	{
		void is_done(redisAsyncContext *c, void *r, void *privdata) { 
			redisReply * reply = (redisReply *)r;
			assert(reply && reply->type != REDIS_REPLY_ERROR);
			done = 1; 
		}
		r = hp_pub(pubc, topics[0], fmt_msg(fmts[0]), -1, is_done);
		assert(r == 0);

		for(; !done;) uv_run(uvloop, UV_RUN_NOWAIT);

		done = 0;
	}
	/* OK: pub only, pub 2 */
	{
		void is_done(redisAsyncContext *c, void *r, void *privdata) { 
			redisReply * reply = (redisReply *)r;
			assert(reply && reply->type != REDIS_REPLY_ERROR);
			++done; 
		}
		r = hp_pub(pubc, topics[0], fmt_msg(fmts[0]), -1, is_done);
		assert(r == 0);

		r = hp_pub(pubc, topics[0], fmt_msg(fmts[0]), -1, is_done);
		assert(r == 0);

		for(; done < 2;) uv_run(uvloop, UV_RUN_NOWAIT);

		done = 0;
	}
	/* OK: resub */
	{
		int unsub = 0, resub = 0;
		redisAsyncContext * c = hp_subc(pubc, subcs[0], cfg("redis.shasub"), cfg("redis.shasup"), sids[0], test_cb_resub, &arg);
		r = hp_sub(c, 0, 0);
		assert(r == 0);

		for(i = 0;;) {
			uv_run(uvloop, UV_RUN_NOWAIT);

			if(!resub){
				r = hp_sub(c, n_topic, topics);
				assert(r == 0);

				resub = 1;
				continue;
			}

			if(!unsub && resub){
				r = hp_unsub(subcs[0]);
				assert(r == 0);

				unsub = 1;
				continue;
			}
			if(done >= 2)
				break;
		}

		done = 0;
		for(i = 0; msgs[i]; ++i) sdsfree(msgs[i]);
		memset(msgs, 0, sizeof(msgs));
	}

	/* OK: pub 1 */
	{
		int unsub = 0;
		redisAsyncContext * c = hp_subc(pubc, subcs[0], cfg("redis.shasub"), cfg("redis.shasup"), sids[0], test_cb_1, &arg);
		r = hp_sub(c, n_topic, topics);
		assert(r == 0);

		for(i = 0;;) {
			uv_run(uvloop, UV_RUN_NOWAIT);

			if(i < 1){
				msgs[i] = sdsnew(fmt_msg(fmts[i]));
				r = hp_pub(pubc, topics[i], msgs[i], -1, 0);
				assert(r == 0);
				++i;
				continue;
			}

			if(!unsub && done >= 1){
				r = hp_unsub(subcs[0]);
				assert(r == 0);

				unsub = 1;
				continue;
			}
			if(done && unsub)
				break;
		}

		done = 0;
		for(i = 0; msgs[i]; ++i) sdsfree(msgs[i]);
		memset(msgs, 0, sizeof(msgs));
	}

	/* OK: pub 1 and exit without unsub */
	{
		static redisAsyncContext * c = 0;
		r = hp_redis_init(&c, uvloop, cfg("redis"), cfg("redis.password"), 0);
		assert(r == 0 && c);

		c = hp_subc(pubc, c, cfg("redis.shasub"), cfg("redis.shasup"), sids[0], test_cb_1, &arg);
		r = hp_sub(c, n_topic, topics);
		assert(r == 0);

		for(i = 0;;) {
			uv_run(uvloop, UV_RUN_NOWAIT);

			if(i < 1){
				msgs[i] = sdsnew(fmt_msg(fmts[i]));
				r = hp_pub(pubc, topics[i], msgs[i], -1, 0);
				assert(r == 0);
				++i;
				continue;
			}

			if(done >= 1){
				hp_redis_uninit(c);

				break;
			}
		}

		done = 0;
		for(i = 0; msgs[i]; ++i) sdsfree(msgs[i]);
		memset(msgs, 0, sizeof(msgs));
	}

	/* OK: pub 2 */
	{
		int unsub = 0; 
		redisAsyncContext * c = hp_subc(pubc, subcs[0], cfg("redis.shasub"), cfg("redis.shasup"), sids[0], test_cb_2, &arg);
		r = hp_sub(c, n_topic, topics);
		assert(r == 0);

		for(i = 0;;) {
			uv_run(uvloop, UV_RUN_NOWAIT);

			if(i < 2){
				msgs[i] = sdsnew(fmt_msg(fmts[i]));
				r = hp_pub(pubc, topics[i], msgs[i], -1, 0);
				assert(r == 0);
				++i;
				continue;
			}

			if(!unsub && done >= 2){
				r = hp_unsub(subcs[0]);
				assert(r == 0);

				unsub = 1;
				continue;
			}
			if(done >= 2 && unsub)
				break;
		}

		done = 0;
		for(i = 0; msgs[i]; ++i) sdsfree(msgs[i]);
		memset(msgs, 0, sizeof(msgs));
	}

	/* OK: pub 2, with the same topic */
	{
		int unsub = 0; 
		redisAsyncContext * c = hp_subc(pubc, subcs[0], cfg("redis.shasub"), cfg("redis.shasup"), sids[0], test_cb_2_2, &arg);
		r = hp_sub(c, n_topic, topics);
		assert(r == 0);

		for(i = 0;;) {
			uv_run(uvloop, UV_RUN_NOWAIT);

			if(i < 2){
				msgs[i] = sdsnew(fmt_msg(fmts[i]));
				r = hp_pub(pubc, topics[0], msgs[i], -1, 0);
				assert(r == 0);
				++i;
				continue;
			}

			if(!unsub && done >= 2){
				r = hp_unsub(subcs[0]);
				assert(r == 0);

				unsub = 1;
				continue;
			}
			if(done >= 2 && unsub)
				break;
		}

		done = 0;
		for(i = 0; msgs[i]; ++i) sdsfree(msgs[i]);
		memset(msgs, 0, sizeof(msgs));
	}

	/* OK: one message 2 subscribers */
	{
		r = hp_redis_init(&subcs[1], uvloop, cfg("redis"), cfg("redis.password"), 0);
		assert(r == 0 && subcs[1]);

		int unsub = 0;
		subcs[0] = hp_subc(pubc, subcs[0], cfg("redis.shasub"), cfg("redis.shasup"), sids[0], test_cb_2_3, sids[0]);
		r = hp_sub(subcs[0], 1, topics + 1);
		assert(r == 0);

		subcs[1] = hp_subc(pubc, subcs[1], cfg("redis.shasub"), cfg("redis.shasup"), sids[1], test_cb_2_3, sids[1]);
		r = hp_sub(subcs[1], 1, topics + 1);
		assert(r == 0);

		for(i = 0;;) {
			uv_run(uvloop, UV_RUN_NOWAIT);

			if(i < 1){
				msgs[i] = sdsnew(fmt_msg(fmts[i]));
				r = hp_pub(pubc, topics[1], msgs[i], -1, 0);
				assert(r == 0);
				++i;
				continue;
			}

			if(!unsub && dones[0] && dones[1]){
				r = hp_unsub(subcs[0]);
				assert(r == 0);

				r = hp_unsub(subcs[1]);
				assert(r == 0);

				unsub = 1;
				continue;
			}
			if(dones[0] && dones[1] && unsub)
				break;
		}

		done= 0;
		dones[0] = dones[1] = 0;
		for(i = 0; msgs[i]; ++i) sdsfree(msgs[i]);
		memset(msgs, 0, sizeof(msgs));
	}

	/* OK: pub 1, sub from session */
	{
		int unsub = 0; 
		subcs[0] = hp_subc(pubc, subcs[0], cfg("redis.shasub"), cfg("redis.shasup"), sids[0], test_cb_sub_from_session, &arg);
		r = hp_sub(subcs[0], 0, 0);
		assert(r == 0);

		for(i = 0;;) {
			uv_run(uvloop, UV_RUN_NOWAIT);

			if(i < 1){
				msgs[i] = sdsnew(fmt_msg(fmts[i]));
				r = hp_pub(pubc, topics[1], msgs[i], -1, 0);
				assert(r == 0);
				++i;
				continue;
			}

			if(!unsub && done >= 1){
				r = hp_unsub(subcs[0]);
				assert(r == 0);

				unsub = 1;
				continue;
			}
			if(done >= 1 && unsub)
				break;
		}

		done = 0;
		for(i = 0; msgs[i]; ++i) sdsfree(msgs[i]);
		memset(msgs, 0, sizeof(msgs));
	}

	hp_redis_uninit(pubc);
	for(i = 0; subcs[i]; ++i)
		hp_redis_uninit(subcs[i]);
	uv_loop_close(uvloop);

	return 0;
}

#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif

#endif /* LIBHP_WITH_REDIS  */
