 /*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2020/7/2
 *
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_WITH_REDIS

#include "Win32_Interop.h"
#ifndef _MSC_VER
#include <sys/time.h> /*gettimeofday*/
#else
#include <WinSock2.h>
#endif /* _MSC_VER */

#include "hp_pub.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "sdsinc.h"
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

static void hp_pub_pub_cb(redisAsyncContext *c, void *r, void *privdata)
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
	if(len <= 0) { len = strlen(msg); }

	char uuidstr[64] = "";
	struct timeval tv;
	gettimeofday(&tv, NULL);
	snprintf(uuidstr, sizeof(uuidstr), "%.0f", tv.tv_sec * 1000.0 + tv.tv_usec / 1000);

#ifndef _MSC_VER
	rc = redisAsyncCommand(c, 0, 0/* privdata */, "ZADD %s %s %b", topic, uuidstr, msg, len);
#else
	const char * argv[] = { "ZADD", topic, uuidstr, msg};
	size_t argvlen[] = {4, strlen(topic), strlen(uuidstr), len};
	int argc = sizeof(argv) / sizeof(argv[0]);
	rc = redisAsyncCommandArgv(c, 0, 0/* privdata */, argc, argv, argvlen);
#endif /* _MSC_VER */

	if(rc != 0){
		hp_log(stderr, "%s: Redis 'ZADD %s %s %s', failed: %d/'%s'\n"
			, __FUNCTION__, topic, uuidstr, msg, c->err, c->errstr);
	}

	rc = redisAsyncCommand(c, (done? done : hp_pub_pub_cb), done/* privdata */, "publish %s %s", topic, uuidstr);
	if(rc != 0){
		hp_log(stderr, "%s: Redis 'publish %s %s' failed: %d/'%s'\n"
			, __FUNCTION__, topic, uuidstr, c->err, c->errstr);
	}

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

static void hp_sub_all_cb(redisAsyncContext *c, void *r, void *privdata)
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

	rc = redisAsyncCommand(ps->c, hp_sub_all_cb, async/* privdata */, "evalsha %s 0 %s", ps->shasub, ps->sid);

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
	const char ** argv = 0;
	cvector_init(argv, n_topic + 8);
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
	cvector_free(argv);

	return rc;
}

static void hp_sub_session_cb(redisAsyncContext *c, void *r, void *privdata)
{
	assert(privdata);
	hp_sub_async_t * as = (hp_sub_async_t *)privdata;
	redisReply *reply = (redisReply *)r;

	int i, rc;

    int n_topics = (reply && reply->elements > 0? reply->elements : 0);
	sds * topics = 0;
	cvector_init(topics, n_topics);

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
	cvector_free(topics);

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

	const char ** argv = 0;
	cvector_init(argv, n_topic + 8);
	argv[0] = "evalsha";
	argv[1] = shasup;
	argv[2] = nk;

	for(i = 0; i < n_topic; ++i)
		argv[i + 3] = topic[i];
	argv[i + 3] = sid;

	int argc = n_topic + 4;
	rc = redisAsyncCommandArgv(c, sup_cb, 0/* privdata */, argc, argv, 0);
	sdsfree(nk);
	cvector_free(argv);

	return rc;
}

static void hp_subc_free(void * ptr)
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
	subc->dataCleanup = hp_subc_free;

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
		, void const * arg)
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
		rc = hp_sub_sup(done->c, done->shasup, done->sid, n_topic, topic);
		/* subscribe */
		rc = hp_sub_dosub(subc, n_topic, topic);
	}
	else{ /* sub topic from session */
		hp_sub_async_t * async = calloc(1, sizeof(hp_sub_async_t));
		async->s = done;
		list_add_tail(&async->node, &done->async_list);

		rc = redisAsyncCommand(done->c, hp_sub_session_cb, async/* privdata */, "HKEYS %s", done->sid);
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
#ifndef _MSC_VER
#include <uuid/uuid.h>
#endif /* _MSC_VER */
#include "hp_config.h"	/* hp_config_t  */
extern hp_config_t g_conf;

static int done = 0, dones[64] = { 0 };
static int s_conn_flag = 0;

char const * sids[] = { "rmqtt:s:865452044887154", "rmqtt:s:868783048901857" };
static char * topics[] = { "rmqtt:1:865452044887154", "rmqtt:apps:chat.tox.antox", "rmqtt:dept:1006"};
static char const * fmts[] = { "{\"uuid0\":\"%s\"}", "{\"uuid1\":\"%s\"}", "{\"uuid2\":\"%s\"}" };
static sds msgs[128] = { 0 };

static char const * fmt_msg(char const * fmt)
{
	char uuidstr[64] = "";
#ifndef _MSC_VER
	uuid_t uuid;
	uuid_generate_time(uuid);
	uuid_unparse(uuid, uuidstr);
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	snprintf(uuidstr, sizeof(uuidstr), "%.0f", tv.tv_sec * 1000.0 + tv.tv_usec / 1000);
#endif /* _MSC_VER */


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
			rc = redisAsyncCommand(s->c, test_cb_1_cb, 0/* privdata */, "hset %s %s %s", sids[0], topic, id);
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
			rc = redisAsyncCommand(s->c, test_cb_2_cb, 0/* privdata */, "hset %s %s %s", sids[0], topic, id);
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
			rc = redisAsyncCommand(s->c, test_cb_2_2_cb, 0/* privdata */, "hset %s %s %s", sids[0], topic, id);
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
			rc = redisAsyncCommand(s->c, test_cb_2_3_cb, s->arg._1/* privdata */, "hset %s %s %s", (char *)s->arg._1, topic, id);
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
				rc = redisAsyncCommand(s->c, test_cb_sub_from_session_1, 0/* privdata */, "hset %s %s %s", sids[0], topic, id);
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

static void hp_pub_on_connect_1(const redisAsyncContext *c, int status) {
	s_conn_flag = (status != REDIS_OK) ? -1 : (hp_max(s_conn_flag, 0) + 1);
	if (status != REDIS_OK) {
        hp_log(stdout, "%s: connect Redis failed: '%s'\n", __FUNCTION__, c->errstr);
        return;
    }
}

void is_done_1(redisAsyncContext *c, void *r, void *privdata) {
	redisReply * reply = (redisReply *)r;
	assert(reply && reply->type != REDIS_REPLY_ERROR);
	done = 1;
}

void is_done_2(redisAsyncContext *c, void *r, void *privdata) {
	redisReply * reply = (redisReply *)r;
	assert(reply && reply->type != REDIS_REPLY_ERROR);
	++done;
}

int test_hp_pub_main(int argc, char ** argv)
{
	assert(g_conf);
	hp_config_t cfg = g_conf;

	int i, r;
	int n_topic = sizeof(topics) / sizeof(topics[0]);
	int arg = 10;

	{
		redisAsyncContext * pubc = 0;
		hp_redis_ev_t s_evobj, *rev = &s_evobj;
		rev_init(rev); assert(rev);
		/* test if connect OK */
		r = hp_redis_init(&pubc, rev, cfg("redis"), cfg("redis.password"), hp_pub_on_connect_1);
		assert(r == 0 && pubc);
		for (;;) {
			rev_run(rev);
			if (s_conn_flag < 0) {
				fprintf(stdout, "%s: connect to Redis failed, skip this test\n", __FUNCTION__);
				return 0;
			}
			else if (s_conn_flag > 0) { break; }
		};
		hp_redis_uninit(pubc);
	}


	/* test for command: zrevrange rmqtt:1:868783048901857 0 0 withscores
	 * OK */
	{
		redisAsyncContext * pubc = 0;
		redisAsyncContext * subcs[64] = { 0 };
		hp_redis_ev_t s_evobj, *rev = &s_evobj;
		rev_init(rev); assert(rev);
		r = hp_redis_init(&pubc, rev, cfg("redis"), cfg("redis.password"), 0);
		assert(r == 0 && pubc);
		for (i = 0; i < 1; ++i) {
			r = hp_redis_init(&subcs[i], rev, cfg("redis"), cfg("redis.password"), 0);
			assert(r == 0 && subcs[i]);
		}
		r = redisAsyncCommand(pubc, zrevrange_cb, 0/* privdata */, "zrevrange %s 0 0 withscores", "rmqtt:1:868783048901857");
		assert(r == 0);
		for(; !done;) rev_run(rev);
		hp_redis_uninit(pubc);
		for (i = 0; subcs[i]; ++i) { hp_redis_uninit(subcs[i]); }
		rev_close(rev);
		done = 0;
	}
	/* test for command: zrevrange rmqtt:1:868783048901857 0 0 withscores
	 * zet NOT exist */
	{
		redisAsyncContext * pubc = 0;
		redisAsyncContext * subcs[64] = { 0 };
		hp_redis_ev_t s_evobj, *rev = &s_evobj;
		rev_init(rev); assert(rev);
		r = hp_redis_init(&pubc, rev, cfg("redis"), cfg("redis.password"), 0);
		assert(r == 0 && pubc);
		r = redisAsyncCommand(pubc, zrevrange_cb_2, 0/* privdata */, "zrevrange %s 0 0 withscores", "rmqtt:1:not_exist");
		assert(r == 0);
		for(; !done;) rev_run(rev);
		hp_redis_uninit(pubc);
		for (i = 0; subcs[i]; ++i) { hp_redis_uninit(subcs[i]); }
		rev_close(rev);
		done = 0;
	}
	/* failed: invalid arg */
	{
		redisAsyncContext * pubc = 0;
		redisAsyncContext * subcs[64] = { 0 };
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
		redisAsyncContext * pubc = 0;
		redisAsyncContext * subcs[64] = { 0 };
		hp_redis_ev_t s_evobj, *rev = &s_evobj;
		rev_init(rev); assert(rev);
		r = hp_redis_init(&pubc, rev, cfg("redis"), cfg("redis.password"), 0);
		assert(r == 0 && pubc);
		for (i = 0; i < 1; ++i) {
			r = hp_redis_init(&subcs[i], rev, cfg("redis"), cfg("redis.password"), 0);
			assert(r == 0 && subcs[i]);
		}

		int unsub = 0;
		subcs[0] = hp_subc(pubc, subcs[0], cfg("redis.shasub"), cfg("redis.shasup"), sids[0], test_cb_sub_only_from_session, &arg);
		r = hp_sub(subcs[0], 0, 0);
		assert(r == 0);

		for(i = 0;;) {
			rev_run(rev);

			if(!unsub && done >= 1){
				r = hp_unsub(subcs[0]);
				assert(r == 0);

				unsub = 1;
				continue;
			}
			if(done >= 1 && unsub)
				break;
		}
		hp_redis_uninit(pubc);
		for (i = 0; subcs[i]; ++i) { hp_redis_uninit(subcs[i]); }
		rev_close(rev);

		done = 0;
		for(i = 0; msgs[i]; ++i) sdsfree(msgs[i]);
		memset(msgs, 0, sizeof(msgs));
	}
	/* _1 OK: pub only, pub 1 */
	{
		redisAsyncContext * pubc = 0;
		hp_redis_ev_t s_evobj, *rev = &s_evobj;
		rev_init(rev); assert(rev);
		r = hp_redis_init(&pubc, rev, cfg("redis"), cfg("redis.password"), 0);
		assert(r == 0 && pubc);

		r = hp_pub(pubc, topics[0], fmt_msg(fmts[0]), -1, is_done_1);
		assert(r == 0);

		for(; !done;) rev_run(rev);
		hp_redis_uninit(pubc);
		rev_close(rev);

		done = 0;
	}
	/* _2 OK: pub only, pub 2 */
	{
		redisAsyncContext * pubc = 0;
		hp_redis_ev_t s_evobj, *rev = &s_evobj;
		rev_init(rev); assert(rev);
		r = hp_redis_init(&pubc, rev, cfg("redis"), cfg("redis.password"), 0);
		assert(r == 0 && pubc);

		r = hp_pub(pubc, topics[0], fmt_msg(fmts[0]), -1, is_done_2);
		assert(r == 0);

		r = hp_pub(pubc, topics[0], fmt_msg(fmts[0]), -1, is_done_2);
		assert(r == 0);

		for(; done < 2;) rev_run(rev);

		hp_redis_uninit(pubc);
		rev_close(rev);
		done = 0;
	}
	/* OK: resub */
	{
		redisAsyncContext * pubc = 0;
		redisAsyncContext * subcs[64] = { 0 };
		hp_redis_ev_t s_evobj, *rev = &s_evobj;
		rev_init(rev); assert(rev);
		r = hp_redis_init(&pubc, rev, cfg("redis"), cfg("redis.password"), 0);
		assert(r == 0 && pubc);
		for (i = 0; i < 1; ++i) {
			r = hp_redis_init(&subcs[i], rev, cfg("redis"), cfg("redis.password"), 0);
			assert(r == 0 && subcs[i]);
		}

		int unsub = 0, resub = 0;
		redisAsyncContext * c = hp_subc(pubc, subcs[0], cfg("redis.shasub"), cfg("redis.shasup"), sids[0], test_cb_resub, &arg);
		r = hp_sub(c, 0, 0);
		assert(r == 0);

		for(i = 0;;) {
			rev_run(rev);

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
		hp_redis_uninit(pubc);
		for (i = 0; subcs[i]; ++i) { hp_redis_uninit(subcs[i]); }
		rev_close(rev);

		done = 0;
		for(i = 0; msgs[i]; ++i) sdsfree(msgs[i]);
		memset(msgs, 0, sizeof(msgs));
	}

	/* OK: pub 1 */
	{
		redisAsyncContext * pubc = 0;
		redisAsyncContext * subcs[64] = { 0 };
		hp_redis_ev_t s_evobj, *rev = &s_evobj;
		rev_init(rev); assert(rev);
		r = hp_redis_init(&pubc, rev, cfg("redis"), cfg("redis.password"), 0);
		assert(r == 0 && pubc);
		for (i = 0; i < 1; ++i) {
			r = hp_redis_init(&subcs[i], rev, cfg("redis"), cfg("redis.password"), 0);
			assert(r == 0 && subcs[i]);
		}

		int unsub = 0;
		redisAsyncContext * c = hp_subc(pubc, subcs[0], cfg("redis.shasub"), cfg("redis.shasup"), sids[0], test_cb_1, &arg);
		r = hp_sub(c, n_topic, topics);
		assert(r == 0);

		for(i = 0;;) {
			rev_run(rev);

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
		hp_redis_uninit(pubc);
		for (i = 0; subcs[i]; ++i) { hp_redis_uninit(subcs[i]); }
		rev_close(rev);

		done = 0;
		for(i = 0; msgs[i]; ++i) sdsfree(msgs[i]);
		memset(msgs, 0, sizeof(msgs));
	}

	/* OK: pub 1 and exit without unsub */
	{
		redisAsyncContext * pubc = 0;
		redisAsyncContext * subcs[64] = { 0 };
		hp_redis_ev_t s_evobj, *rev = &s_evobj;
		rev_init(rev); assert(rev);
		r = hp_redis_init(&pubc, rev, cfg("redis"), cfg("redis.password"), 0);
		assert(r == 0 && pubc);

		static redisAsyncContext * c = 0;
		r = hp_redis_init(&c, rev, cfg("redis"), cfg("redis.password"), 0);
		assert(r == 0 && c);

		c = hp_subc(pubc, c, cfg("redis.shasub"), cfg("redis.shasup"), sids[0], test_cb_1, &arg);
		r = hp_sub(c, n_topic, topics);
		assert(r == 0);

		for(i = 0;;) {
			rev_run(rev);

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
		hp_redis_uninit(pubc);
		for (i = 0; subcs[i]; ++i) { hp_redis_uninit(subcs[i]); }
		rev_close(rev);

		done = 0;
		for(i = 0; msgs[i]; ++i) sdsfree(msgs[i]);
		memset(msgs, 0, sizeof(msgs));
	}

	/* OK: pub 2 */
	{
		redisAsyncContext * pubc = 0;
		redisAsyncContext * subcs[64] = { 0 };
		hp_redis_ev_t s_evobj, *rev = &s_evobj;
		rev_init(rev); assert(rev);
		r = hp_redis_init(&pubc, rev, cfg("redis"), cfg("redis.password"), 0);
		assert(r == 0 && pubc);
		for (i = 0; i < 1; ++i) {
			r = hp_redis_init(&subcs[i], rev, cfg("redis"), cfg("redis.password"), 0);
			assert(r == 0 && subcs[i]);
		}

		int unsub = 0;
		redisAsyncContext * c = hp_subc(pubc, subcs[0], cfg("redis.shasub"), cfg("redis.shasup"), sids[0], test_cb_2, &arg);
		r = hp_sub(c, n_topic, topics);
		assert(r == 0);

		for(i = 0;;) {
			rev_run(rev);

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
		hp_redis_uninit(pubc);
		for (i = 0; subcs[i]; ++i) { hp_redis_uninit(subcs[i]); }
		rev_close(rev);

		done = 0;
		for(i = 0; msgs[i]; ++i) sdsfree(msgs[i]);
		memset(msgs, 0, sizeof(msgs));
	}

	/* OK: pub 2, with the same topic */
	{
		redisAsyncContext * pubc = 0;
		redisAsyncContext * subcs[64] = { 0 };
		hp_redis_ev_t s_evobj, *rev = &s_evobj;
		rev_init(rev); assert(rev);
		r = hp_redis_init(&pubc, rev, cfg("redis"), cfg("redis.password"), 0);
		assert(r == 0 && pubc);
		for (i = 0; i < 1; ++i) {
			r = hp_redis_init(&subcs[i], rev, cfg("redis"), cfg("redis.password"), 0);
			assert(r == 0 && subcs[i]);
		}

		int unsub = 0;
		redisAsyncContext * c = hp_subc(pubc, subcs[0], cfg("redis.shasub"), cfg("redis.shasup"), sids[0], test_cb_2_2, &arg);
		r = hp_sub(c, n_topic, topics);
		assert(r == 0);

		for(i = 0;;) {
			rev_run(rev);

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
		hp_redis_uninit(pubc);
		for (i = 0; subcs[i]; ++i) { hp_redis_uninit(subcs[i]); }
		rev_close(rev);

		done = 0;
		for(i = 0; msgs[i]; ++i) sdsfree(msgs[i]);
		memset(msgs, 0, sizeof(msgs));
	}

	/* OK: one message 2 subscribers */
	{
		redisAsyncContext * pubc = 0;
		redisAsyncContext * subcs[64] = { 0 };
		hp_redis_ev_t s_evobj, *rev = &s_evobj;
		rev_init(rev); assert(rev);
		r = hp_redis_init(&pubc, rev, cfg("redis"), cfg("redis.password"), 0);
		assert(r == 0 && pubc);
		for (i = 0; i < 2; ++i) {
			r = hp_redis_init(&subcs[i], rev, cfg("redis"), cfg("redis.password"), 0);
			assert(r == 0 && subcs[i]);
		}

		int unsub = 0;
		subcs[0] = hp_subc(pubc, subcs[0], cfg("redis.shasub"), cfg("redis.shasup"), sids[0], test_cb_2_3, sids[0]);
		r = hp_sub(subcs[0], 1, topics + 1);
		assert(r == 0);

		subcs[1] = hp_subc(pubc, subcs[1], cfg("redis.shasub"), cfg("redis.shasup"), sids[1], test_cb_2_3, sids[1]);
		r = hp_sub(subcs[1], 1, topics + 1);
		assert(r == 0);

		for(i = 0;;) {
			rev_run(rev);

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
		hp_redis_uninit(pubc);
		for (i = 0; subcs[i]; ++i) { hp_redis_uninit(subcs[i]); }
		rev_close(rev);

		done= 0;
		dones[0] = dones[1] = 0;
		for(i = 0; msgs[i]; ++i) sdsfree(msgs[i]);
		memset(msgs, 0, sizeof(msgs));
	}

	/* OK: pub 1, sub from session */
	{
		redisAsyncContext * pubc = 0;
		redisAsyncContext * subcs[64] = { 0 };
		hp_redis_ev_t s_evobj, *rev = &s_evobj;
		rev_init(rev); assert(rev);
		r = hp_redis_init(&pubc, rev, cfg("redis"), cfg("redis.password"), 0);
		assert(r == 0 && pubc);
		for (i = 0; i < 1; ++i) {
			r = hp_redis_init(&subcs[i], rev, cfg("redis"), cfg("redis.password"), 0);
			assert(r == 0 && subcs[i]);
		}

		int unsub = 0;
		subcs[0] = hp_subc(pubc, subcs[0], cfg("redis.shasub"), cfg("redis.shasup"), sids[0], test_cb_sub_from_session, &arg);
		r = hp_sub(subcs[0], 0, 0);
		assert(r == 0);

		for(i = 0;;) {
			rev_run(rev);

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
		hp_redis_uninit(pubc);
		for (i = 0; subcs[i]; ++i) { hp_redis_uninit(subcs[i]); }
		rev_close(rev);

		done = 0;
		for(i = 0; msgs[i]; ++i) sdsfree(msgs[i]);
		memset(msgs, 0, sizeof(msgs));
	}
	return 0;
}

#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif

#endif /* LIBHP_WITH_REDIS  */
