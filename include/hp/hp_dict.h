/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2019/6/9
 *
 * dict with sds as key and void * as value
 * */

#ifndef LIBHP_DICT_H__
#define LIBHP_DICT_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_DEPRECADTED

#include "sdsinc.h"        /* sds */
#include "redis/src/dict.h" /* dict */
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif
/////////////////////////////////////////////////////////////////////////////////////////
int hp_dict_init(dict **d, void (* f)(void * ptr));
int hp_dict_set(dict * d, char const * k, void * v);
void * hp_dict_find(dict * d, char const * k);
void * hp_dict_findif(dict * d, char const * k, void * def);
int hp_dict_percent(dict * ht, int (* fn)(void * ptr), int * left, int * total);
int hp_dict_del(dict * ht, const char *key);
/* uninit */
#define hp_dict_uninit(d) \
do { \
	dictRelease(d); \
}while(0)

/////////////////////////////////////////////////////////////////////////////////////////
#ifndef NDEBUG
int test_hp_dict_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif

#endif /* LIBHP_DICT_H__ */
#endif //LIBHP_DEPRECADTED
