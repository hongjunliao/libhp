/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/11/2
 *
 * my libc
 * */

#include "hp_libc.h"
#include <string.h>     /* strcmp */
#include <assert.h>
#include "sdsinc.h"		/* sds */
/////////////////////////////////////////////////////////////////////////////////////////

//char *  hp_nmalloc(size_t nmemb, size_t size)
//{
//	size_t * addr = malloc(sizeof(size_t) + nmemb * size);
//	if (!addr) return 0;
//
//	((size_t *)(addr))[0] = nmemb;
//	return (void *)(&addr[1]);
//}

static int hp_map_find_by_key(const void * a, const void * b)
{
	hp_ivpair * ent1 = (hp_ivpair *)a, * ent2 = (hp_ivpair *)b;
	assert(ent1 && ent2);
	return !(ent1->key == ent2->key);
}

void * hp_iv_lfind(hp_ivpair * m, size_t nm, int key)
{
	if(!(m && nm > 0 && key >= 0))
		return (void *)0;

	hp_ivpair k = { .key = key }, * p =
			lfind(&k, m, &nm, sizeof(hp_ivpair), hp_map_find_by_key);
	return p? m[p->key - p->offset].val : (void *)0;
}

/////////////////////////////////////////////////////////////////////////////////////////

int hp_is_equal(const void * a, const void * b)
{
	return !(a == b);
}

int is_int_equal (const void * a, const void * b)
{
	int * ent1 = (int *)a, * ent2 = (int *)b;
	assert(ent1 && ent2);
	return !(*ent1 == *ent2);
}

int hp_is_strcmp_equal(const void * a, const void * b)
{
	assert(a && b);
	char * ent1 = *(char **)a, * ent2 = *(char **)b;
	return strcmp(ent1, ent2) != 0;
}

#ifndef WITHOUT_SDS
int hp_is_sds_equal(const void * a, const void * b)
{
	sds * ent1 = (sds *)a, *ent2 = (sds *)b;
	assert(ent1 && ent2);
	return !(sdscmp(*ent1, *ent2) == 0);
}
#endif /* WITH_SDS */

/////////////////////////////////////////////////////////////////////////////////////////

/**
 * reserve N unused available in
 * array [BASE,BASE+NMEMB*SIZE)
 * @see libc: search.h/lsearch
 *
 * @param base :       in_out, base addr
 * @param nmemb:       in_out, number of elements
 * @param size:        bytes per element
 * @param n:           number of elements used
 * @param nreserve:    number of elements unused to reserve
 * @param flag:        HP_LRESERVE_XXX
 */
void hp_lreserve(void ** base, size_t * nmemb, size_t size, size_t n, size_t nreserve, int flags)
{
	if(!(base && nmemb))
		return;

	if(!(size > 0 && nreserve > 0)) return;
	if(!(*base)){
		if(*nmemb < nreserve) *nmemb += nreserve;

		if(flags & HP_LRESERVE_CLR)
			*base = calloc(*nmemb, size);
		else *base = malloc(*nmemb * size);

		return;
	}

	if(!(*nmemb > 0 && *nmemb >= n)) return;
	else if(*nmemb - n < nreserve){
		*nmemb += nreserve;
		*base = realloc(*base, (*nmemb) * size);
		if(flags & HP_LRESERVE_CLR)
			memset((char *)*base + n * size, 0, (*nmemb - n) * size);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////
/*
 * code from https://github.com/tuner/intarray
 * set operations
 */

/////////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
#include <stdio.h>
#include <string.h>        /* strcmp */
#include <assert.h>        /* assert */
#include <stdint.h>	       /* SIZE_MAX */

typedef union tuple_union tuple_union;
typedef struct tuple_t tuple_t;
typedef struct hp_tuple_t hp_tuple_t;

struct hp_tuple_t {
	char t;
	union {
		char b[8];
	} v;
};

union tuple_union {
	int	   i;
	double d;
	void * v;
	char * c;
};

struct tuple_t {
	char        t;
	tuple_union v;
};

tuple_t * make_ctuple(size_t n)
{
	tuple_t * rc = calloc(n, sizeof(tuple_t));
	return rc;
}


/////////////////////////////////////////////////////////////////////////////////////////
struct person {
	int age;
};
struct student {
	struct person p;
	int cls;
};

int test_hp_libc_main(int argc, char ** argv)
{
	int rc;
	{
		struct student sobj = { 0 }, * s = &sobj;
		assert(s == &s->p);

		struct person * p = &(s->p);

		struct person * p2 = (struct person *)s;
		p2->age = 10;
		assert(s->p.age == 10);

		assert(p == p2);

		int a = 10;
		int b = a;
		assert(a == b);
	}
	{
		void * d = calloc(1, 0);
		free(d);
		free(0);
	}
	{
		tuple_union tu[] = { { 0 } };
		tu[0].v = &argc;
		HP_UNUSED(tu);
	}
	{
		tuple_union tu[] = { { .i = 3 },{ .d = 3.1 }, { .c = "hello" } };
		size_t i;
		for (i = 0; i < sizeof(tu) / sizeof(tu[0]); ++i) {
			tuple_union * ptu = tu + i;
			HP_UNUSED(ptu);
		}
	}
	{
		tuple_t tu[] = { { .t = 0 , .v.i = 3},{ .t = 1 ,.v.d = 3.1 },{ .t = 3 ,.v.c = "hello" } };
		size_t i;
		for (i = 0; i < sizeof(tu) / sizeof(tu[0]); ++i) {
			tuple_t * ptu = tu + i;
			HP_UNUSED(ptu);
		}
	}
	{
		tuple_t * tu = make_ctuple(2);
		free(tu);
	}
	{
		fprintf(stdout, "%s: ", __FUNCTION__);
		hp_tuple_t tu[] = { {.t = 0,.v = { 1 } },{.t = 1,.v = { 3.1 } }, {.t = 3,.v = { "hello" } } };
		size_t i;
		for (i = 0; i < sizeof(tu) / sizeof(tu[0]); ++i) {
			hp_tuple_t * ptu = tu + i;
			if (ptu->t == 0) fprintf(stdout, "int=%d, ", *(int *)ptu->v.b);
			else if (ptu->t == 1) fprintf(stdout, "double=%f, ", *(double *)ptu->v.b);
			else if (ptu->t == 3) fprintf(stdout, "char *=%s, ", ptu->v.b);
		}
		fprintf(stdout, "\n");
	}
	/////////////////////////////////////////////////////////////////////////////////////////
	/* tests for tupleN_t */
	{
		typedef int (* fprintf_t)(FILE * , char const * fmt, ...);
		tuple2_t(int, fprintf_t) tu = { ._1 = 4, ._2 = fprintf };
		assert(tu._1 == 4);
		assert(tu._2 == fprintf);
		tu._2(stdout, "%s: fprintf called by tuple2_t(stack alloc), int=%d\n", __FUNCTION__, tu._1);
	}
	{
		typedef int(*fprintf_t)(FILE *, char const * fmt, ...);
		typedef tuple2_t(int, fprintf_t) tu_t;
		tu_t *tu = calloc(1, sizeof(tu_t));
		tu->_1 = 4;
		tu->_2 = fprintf;

		assert(tu->_1 == 4);
		assert(tu->_2 == fprintf);
		tu->_2(stdout, "%s: fprintf called by tuple2_t(heap alloc), int=%d\n", __FUNCTION__, tu->_1);

		free(tu);
	}
	{
		sds s = sdsnew("hello");
		typedef int(*fprintf_t)(FILE *, char const * fmt, ...);
		typedef tuple3_t(int, fprintf_t, sds) tu_t;
		tu_t *tu = calloc(1, sizeof(tu_t));
		tu->_1 = 4;
		tu->_2 = fprintf;
		tu->_3 = s;

		assert(tu->_1 == 4);
		assert(tu->_2 == fprintf);
		assert(tu->_3 == s);

		tu->_2(stdout, "%s: fprintf called by tuple3_t(heap alloc), int=%d, sds='%s'\n", __FUNCTION__, tu->_1, tu->_3);

		free(tu);
		sdsfree(s);
	}
	{
		sds s = sdsnew("hello");
		typedef sds * sds_ptr_t;
		typedef int(*fprintf_t)(FILE *, char const * fmt, ...);
		typedef tuple4_t(int, fprintf_t, sds, sds_ptr_t) tu_t;
		tu_t *tu = calloc(1, sizeof(tu_t));
		tu->_1 = 4;
		tu->_2 = fprintf;
		tu->_3 = s;
		tu->_4 = &s;

		assert(tu->_1 == 4);
		assert(tu->_2 == fprintf);
		assert(tu->_3 == s);
		assert(tu->_4 == &s);

		tu->_2(stdout, "%s: fprintf called by tuple4_t(heap alloc), int=%d, sds='%s', ptr=%p\n", __FUNCTION__, tu->_1, tu->_3, tu->_4);

		free(tu);
		sdsfree(s);
	}
	/////////////////////////////////////////////////////////////////////////////////////////

	//{
	//	char ** v = 0;
	//	size_t i;
	//	for (i = 0; i < 3024; ++i) {
	//		char * p = hp_nmalloc(749, 157);
	//		memcpy(p, "hello", 6);

	//		cvector_push_back(v, p);
	//	}
	//	for(i = 0; i < cvector_size(v); ++i)
	//		hp_nfree(v[i]);
	//	cvector_free(v);
	//}
	//{ char * p = hp_nmalloc(0, 0); assert(p); hp_nfree(p); }
	//{ char * p = hp_nmalloc(0, 1); assert(p); hp_nfree(p); }
	//{ char * p = hp_nmalloc(1, 0); assert(p); hp_nfree(p);}
	//{
	//	char * p = hp_nmalloc(1, 1);
	//	assert(p && hp_nsize(p) == 1);
	//	hp_nfree(p);
	//}
	//{
	//	char * p = hp_nmalloc(6, sizeof(char));
	//	assert(p && hp_nsize(p) == 6);
	//	strcpy(p, "hello");
	//	assert(memcmp(p, "hello", 6) == 0);
	//	hp_nfree(p);
	//}
	//{
	//	char * p = hp_nmalloc(6, sizeof(char));
	//	assert(p && hp_nsize(p) == 6);
	//	strcpy(p, "hello");
	//	assert(memcmp(p, "hello", 6) == 0);
	//	hp_nfree(p);
	//}
	//{
	//	char * p = hp_nmalloc(6, sizeof(char));
	//	assert(p && hp_nsize(p) == 6);
	//	strcpy(p, "hello");
	//	assert(memcmp(p, "hello", 6) == 0);
	//	hp_nfree(p);
	//}
	//{
	//	char * p = hp_nmalloc(6, sizeof(char));
	//	assert(p && hp_nsize(p) == 6);
	//	strcpy(p, "hello");
	//	assert(memcmp(p, "hello", 6) == 0);
	//	hp_nfree(p);
	//}
	{
		int vals[] = { 1, 8, 5 }, * val = 0;
		hp_ivpair m[] = { { 1, vals + 0, 1}, { 8, vals + 1, 7}, { 5, vals + 2, 3} };

		val = (int *)hp_iv_lfind(m, sizeof(m) / sizeof(m[0]), 1);
		assert(val && *val == 1);

		val = (int *)hp_iv_lfind(m, sizeof(m) / sizeof(m[0]), 8);
		assert(val && *val == 8);

		val = (int *)hp_iv_lfind(m, sizeof(m) / sizeof(m[0]), 5);
		assert(val && *val == 5);

		val = (int *)hp_iv_lfind(m, sizeof(m) / sizeof(m[0]), 9);
		assert(!val);
	}
	{
		size_t N = sizeof(size_t) * 8;
		fprintf(stdout, "%s: 1 << [0-%lu]: ", __FUNCTION__, N);
		size_t i;
		for(i = 0; i <= N; ++i){
			fprintf(stdout, "%lu, ", ((size_t)1 << i));
		}
		fprintf(stdout, "\n");
	}
	{
		int * a1 = 0, a2[100] = { 0 };;
		size_t na1 = 100;
		hp_lreserve((void **)&a1, &na1, sizeof(int), 0, 10, 0);
		assert(a1);
		assert(na1 == 100);
		hp_lreserve((void **)&a1, &na1, sizeof(int), 4, 10, 0);
		assert(na1 == 100);
		hp_lreserve((void **)&a1, &na1, sizeof(int), 90, 10, 0);
		assert(na1 == 100);

		memcpy(a2, a1, sizeof(a2));
		hp_lreserve((void **)&a1, &na1, sizeof(int), 91, 10, 0);
		assert(na1 == 110);
		assert(memcmp(a1, a2, sizeof(a2)) == 0);

		free(a1);
	}
	{
		int * a1 = 0, a2[100] = { 0 }, a3[100] = { 0 };
		size_t na1 = 100;
		hp_lreserve((void **)&a1, &na1, sizeof(int), 0, 10, 0);
		assert(a1);
		assert(na1 == 100);
		hp_lreserve((void **)&a1, &na1, sizeof(int), 4, 10, 0);
		assert(na1 == 100);
		hp_lreserve((void **)&a1, &na1, sizeof(int), 90, 10, 0);
		assert(na1 == 100);

		memcpy(a1, a2, sizeof(a2));
		hp_lreserve((void **)&a1, &na1, sizeof(int), 91, 10, HP_LRESERVE_CLR);
		assert(na1 == 110);
		assert(memcmp(a1, a2, sizeof(a2)) == 0);
		assert(memcmp(a1 + 100, a3, sizeof(int) * 10) == 0);

		free(a1);
	}
	{
		int * a1 = 0, a2[100] = { 0 };
		size_t na1 = 100;
		hp_lreserve((void **)&a1, &na1, sizeof(int), 0, 10, HP_LRESERVE_CLR);
		assert(a1);
		assert(na1 == 100);
		assert(memcmp(a1, a2, sizeof(a2)) == 0);
		free(a1);
	}
	{
//		char ** v = 0;
//		size_t i;
//		for (i = 0; i < 3024; ++i) {
//			char * p = hp_nmalloc(749, 157);
//			memcpy(p, "hello", 6);
//
//			cvector_push_back(v, p);
//		}
//		for(i = 0; i < cvector_size(v); ++i)
//			hp_nfree(v[i]);
//		cvector_free(v);
	}
#ifndef _MSC_VER
	int test_fsutil_main(int argc, char ** argv);
	rc = test_fsutil_main(argc, argv); assert(rc == 0);
#endif /* _MSC_VER */

	return rc;
}

#endif /* NDEBUG */

