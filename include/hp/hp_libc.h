/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/11/1
 *
 * my libc
 * */

#ifndef LIBHP_LIBC_H__
#define LIBHP_LIBC_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdlib.h>
#include <search.h>     /* lfind */

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////////
/* Unused arguments generate annoying warnings... */
#define HP_UNUSED(V) ((void) V)

#define hp_min(a, b)                              ((a) < (b)? (a):(b))
#define hp_max(a, b)                              ((a) > (b)? (a):(b))
#define hp_new(T) ((T *)calloc(1, sizeof(T)))
/////////////////////////////////////////////////////////////////////////////////////////

/* char * with allloc length(size_t width) in header */
//char *  hp_nmalloc(size_t nmemb, size_t size);
//#define hp_nsize(p)   (((size_t *)(p))[-1])
//#define hp_nfree(p)   do { if(p) free(&((size_t *)(p))[-1]); } while(0)

/* for array, realloc */
#define HP_LRESERVE_CLR    (1 << 1)
void hp_lreserve(void ** base, size_t * nmemb, size_t size, size_t n, size_t nreserve, int flags);

int hp_is_equal(const void * a, const void * b);
int is_int_equal (const void * a, const void * b);
int hp_is_strcmp_equal(const void * a, const void * b);

int hp_is_sds_equal(const void * a, const void * b);
/////////////////////////////////////////////////////////////////////////////////////////

typedef struct hp_ivpair hp_ivpair;
/*
 * pair: int->void *
 * */
struct hp_ivpair {
	size_t key;
	void * val;
	size_t offset;
};

/*
 * a simple version of std::map<int, void *> in c
 * note: use lfind
 * */
/*
 * @param flags: HPMF_XXX
 * */
void * hp_iv_lfind(hp_ivpair * m, size_t nm, int key);

/////////////////////////////////////////////////////////////////////////////////////////
/* a simple tuple type */
typedef void * void_ptr_t;
typedef char * char_ptr_t;
typedef int *  int_ptr_t;

#define tuple2_t(T1, T2) \
struct tuple_struct_##T1##T2 { \
	T1 _1; \
	T2 _2; \
}

#define tuple3_t(T1, T2, T3) \
struct tuple_struct_##T1##T2##T3 { \
	T1 _1; \
	T2 _2; \
	T3 _3; \
}

#define tuple4_t(T1, T2, T3, T4) \
struct tuple_struct_4_##T1##T2##T3##T4 { \
	T1 _1; \
	T2 _2; \
	T3 _3; \
	T4 _4; \
}

#define hp_tuple2_t(T, T1, T2) \
typedef struct hp_tuple_##T { \
	T1 _1; \
	T2 _2; \
} T

#define hp_tuple2_init(T, T1, T2) do {T->_1 = T1; T->_2 = T2;}while(0)

#define hp_tuple3_t(T, T1, T2, T3) \
typedef struct hp_tuple_##T { \
	T1 _1; \
	T2 _2; \
	T3 _3; \
} T

#define hp_tuple4_t(T, T1, T2, T3, T4) \
typedef struct hp_tuple_##T { \
	T1 _1; \
	T2 _2; \
	T3 _3; \
	T4 _4; \
} T

#define hp_tuple5_t(T, T1, T2, T3, T4, T5) \
typedef struct hp_tuple_##T { \
	T1 _1; \
	T2 _2; \
	T3 _3; \
	T4 _4; \
	T5 _5; \
} T

#define hp_tuple6_t(T, T1, T2, T3, T4, T5, T6) \
typedef struct hp_tuple_##T { \
	T1 _1; \
	T2 _2; \
	T3 _3; \
	T4 _4; \
	T5 _5; \
	T6 _6; \
} T

#define hp_tuple7_t(T, T1, T2, T3, T4, T5, T6, T7) \
typedef struct hp_tuple_##T { \
	T1 _1; \
	T2 _2; \
	T3 _3; \
	T4 _4; \
	T5 _5; \
	T6 _6; \
	T7 _7; \
} T

/////////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
int test_hp_libc_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif

#endif /* LIBHP_LIBC_H__ */
