/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2019/6/9
 *
 * 2024/4/21 updated:
 * simple c++ wrapper for redis/dict
 * */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "hp/hp_dict.h"    /*  */
#include "hp/sdsinc.h"        /* sds */
#include <string.h>     /* strcmp */
#include <assert.h>
/////////////////////////////////////////////////////////////////////////////////////////
#ifndef DICT_NOTUSED
/* Unused arguments generate annoying warnings... */
#define DICT_NOTUSED(V) ((void) V)
#endif

static int si_keyCompare(dict *d, const void *key1, const void *key2)
{
    int l1,l2;
    DICT_NOTUSED(d);

    l1 = sdslen((sds)key1);
    l2 = sdslen((sds)key2);
    if (l1 != l2) return 0;
    return memcmp(key1, key2, l1) == 0;
}

static void si_keyDestructor(dict *d, void *key)
{
    DICT_NOTUSED(d);
    sdsfree((sds)key);
}

static uint64_t si_hashFunction(const void *key)
{
    return dictGenHashFunction((unsigned char*)key, sdslen((char*)key));
}

static size_t si_dictEntryMetadataBytes(dict *d) { return sizeof(int) ; }

dictType hp_dict_si::sidt = {
	.hashFunction = si_hashFunction,        /* hashFunction */
    .keyDup = NULL,                   /* keyDup */
	.valDup = NULL,                   /* valDup */
	.keyCompare = si_keyCompare,      	/* keyCompare */
    .keyDestructor = si_keyDestructor,      /* keyDestructor */
	.valDestructor = 0,				       /* valDestructor */
	//yes, no value(因测试时发现，如果设置为1，程序运行时报“corrupted size vs. prev_size”，故关闭)
	.no_value = 0,
	.dictEntryMetadataBytes = si_dictEntryMetadataBytes,
};

/////////////////////////////////////////////////////////////////////////////////////////

int& hp_dict_si::operator [](char const * k_)
{
	sds k = sdsnew(k_);
    dictEntry *e, *existing;
    e = dictAddRaw(dict_,k,&existing);
    if (!e) {
    	e = existing;
    	sdsfree(k);
    }
	auto& v = *(int *)dictEntryMetadata(e);
	return v;
}

hp_dict_si::hp_dict_si()
{
	dict_ = dictCreate(&hp_dict_si::sidt);
}

hp_dict_si::~hp_dict_si()
{
	dictRelease(dict_);
}

/////////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
int test_hp_dict_main(int argc, char ** argv)
{
	int rc = 0;
	{
		hp_dict_si sidict;
		assert(sidict["hello"] == 0);
		++sidict["hello"];
		assert(sidict["hello"] == 1);
	}
	{
		hp_dict_si sidict;
		sidict[("hello")] = 1;
		sidict[("world")] = 2;
		assert(sidict[("hello")] == 1);
	}
	{
		hp_dict_si sidict;
		sidict[("hello")] = 1;
		++sidict[("hello")];
		assert(sidict[("hello")] == 2);
	}
	{
		hp_dict_si sidict;
		sidict[("hello")] = 1;
		++sidict[("hello")];
		++sidict[("hello")];
		assert(sidict[("hello")] == 3);
	}
	{
		hp_dict_si sidict;
		sidict[("hello")] = 1;
		sidict[("hello")] += 2;
		assert(sidict[("hello")] == 3);
	}
	return rc;
}
#endif /* NDEBUG */
