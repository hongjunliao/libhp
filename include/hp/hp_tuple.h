/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/11/1
 *
* a simple tuple type
 * */

#ifndef LIBHP_LIBC_H__
#define LIBHP_LIBC_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////////

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

#ifdef __cplusplus
}
#endif

#endif /* LIBHP_LIBC_H__ */
