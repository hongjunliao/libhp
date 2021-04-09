/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2019/6/9
 *
 * */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_DEPRECADTED

#include "sdsinc.h"        /* sds */
#include "redis/src/dict.h" /* dict */
#include "hp_dict.h"    /*  */
#include <string.h>     /* strcmp */
#include <assert.h>
/////////////////////////////////////////////////////////////////////////////////////////

/*
 * dict with sds as key and void * as value
 * */
static unsigned int hp_dict_hashFunction(const void *key)
{
	return dictGenHashFunction((unsigned char*)(sds)key, sdslen((sds)key));
}

static int hp_dict_keyCompare(void *privdata, const void *key1, const void *key2)
{
	DICT_NOTUSED(privdata);
	return sdscmp((sds)key1, (sds)key2) == 0;
}

static void * hp_dict_keyDup(void *privdata, const void *key)
{
	return sdsdup((sds)key);
}

static void hp_dict_keyDestructor(void *privdata, void *key)
{
	sdsfree((sds)key);
}

static void hp_dict_valDestructor(void *privdata, void *obj)
{
	if(privdata){
		void (* f)(void * ptr) = privdata;
		f(obj);
	}
}

/* init */
int hp_dict_init(dict **d, void (* f)(void * ptr))
{
	if(!d)
		return -1;
	static dictType hp_dict_init_dicttype = {
		hp_dict_hashFunction, /* hash function */
		hp_dict_keyDup,       /* key dup */
		NULL,                 /* val dup */
		hp_dict_keyCompare,   /* key compare */
		hp_dict_keyDestructor,/* key destructor */
		hp_dict_valDestructor /* val destructor */
	};
	(*d) = dictCreate(&hp_dict_init_dicttype, f);

	return 0;
}

/* get */
#define hp_dict_get(d, k, T, ptr) \
do { \
	sds hp_dict_get_key = sdsnew(k); \
	dictEntry * hp_dict_get_ent = dictFind((d), hp_dict_get_key); \
	if(hp_dict_get_ent) \
		(*ptr) = *(T **)(hp_dict_get_ent->v.val); \
    sdsfree(hp_dict_get_key); \
} while (0);


/* put */
#define  hp_dict_put(d, k, ptr) \
do { \
	sds hp_dict_put_key = sdsnew(k); \
	dictReplace((d), hp_dict_put_key, &ptr); \
    sdsfree(hp_dict_put_key); \
} while (0);


void * hp_dict_find(dict * d, char const * k)
{
	if(!(d && k))
		return 0;

	void * val = 0;
	sds hp_dict_get_key = sdsnew(k);
	dictEntry * hp_dict_get_ent = dictFind((d), hp_dict_get_key);
	if(hp_dict_get_ent)
		val = (hp_dict_get_ent->v.val);
    sdsfree(hp_dict_get_key);
    return val;
}

void * hp_dict_findif(dict * d, char const * k, void * def)
{
	void * val = hp_dict_find(d, k);
	return val? val : def;
}

int hp_dict_percent(dict * ht, int (* fn)(void * ptr), int * left, int * total)
{
	if(!(ht && fn))
		return -1;

	int n = 0, m = 0;

	dictIterator * iter = dictGetIterator(ht);
	dictEntry * ent;
	for(ent = 0; (ent = dictNext(iter));){
		++m;
		if(fn(ent->v.val))
			++n;
	}
	dictReleaseIterator(iter);

	if(left) *left = n;
	if(total) *total = m;

	return 0;
}

int hp_dict_del(dict * ht, const char *key)
{
	int rc;
	sds htkey = sdsnew(key);
	rc = dictDelete(ht, htkey);

	sdsfree(htkey);
	return rc;
}

int hp_dict_set(dict * d, char const * k, void * v)
{
	if(!(d && k))
		return 0;

	sds hp_dict_put_key = sdsnew(k);
	int rc = dictReplace((d), hp_dict_put_key, v);
    sdsfree(hp_dict_put_key);

    return rc;
}

#ifndef NDEBUG
int test_hp_dict_main(int argc, char ** argv)
{
	int rc;
	/* tests for hp_dict */
	{
		dict * d = 0;
		hp_dict_init(&d, 0);
		assert(d);

		char * val = "world", * ptr = 0;
		hp_dict_get(d, "hello", char, &ptr);
		assert(!ptr);

		hp_dict_put(d, "hello", val);
		hp_dict_get(d, "hello", char, &ptr);
		assert(ptr && val == ptr);

		hp_dict_uninit(d);
	}
	{
		dict * d = 0;
		hp_dict_init(&d, 0);
		assert(d);

		char * val = "world", * ptr = 0;
		hp_dict_find(d, "hello");
		assert(!ptr);

		int rc = hp_dict_set(d, "hello", val);
		assert(rc);

		ptr = hp_dict_find(d, "hello");
		assert(ptr && strcmp(ptr, val) == 0);

		hp_dict_uninit(d);
	}
	{
		dict * d = 0;
		hp_dict_init(&d, 0);
		assert(d);

		char * val = "world", * ptr = 0;
		hp_dict_find(d, "hello");
		assert(!ptr);

		rc = hp_dict_set(d, "hello", val);
		assert(rc);

		rc = hp_dict_set(d, "world", val);
		assert(rc);

		ptr = hp_dict_find(d, "hello");
		assert(ptr && strcmp(ptr, val) == 0);

		rc = hp_dict_del(d, "hello");
		assert(rc == DICT_OK);

		ptr = hp_dict_find(d, "hello");
		assert(!ptr);

		ptr = hp_dict_find(d, "world");
		assert(ptr && strcmp(ptr, val) == 0);

		hp_dict_uninit(d);
	}
	{
		dict * d = 0;
#ifdef __GCC__
		void f(void * ptr) { if(ptr) sdsfree(ptr); }
		hp_dict_init(&d, f);
#else
		hp_dict_init(&d, sdsfree);
#endif
		assert(d);

		hp_dict_set(d, "hello", sdsnew("jack"));
		hp_dict_set(d, "hello", sdsnew("jack"));

		hp_dict_uninit(d);
	}
	return 0;
}

#endif /* NDEBUG */

#endif //LIBHP_DEPRECADTED

