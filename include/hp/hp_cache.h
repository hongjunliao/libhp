/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/4/8
 *
 * a simple cache system for file
 * */

#ifndef LIBHP_CACHE_H__
#define LIBHP_CACHE_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdlib.h>

/////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

#define HPCACHE_KEYMAX                            2048

/*
 * cache for file
 * */
struct hp_cache_entry
{
	char *       buf;
	size_t       size;
	time_t       chksum;
};

struct hp_cache_entry * hp_cache_entry_alloc(size_t bufsize);
void hp_cache_entry_free(struct hp_cache_entry * entp);
/*!
 * @param key: file path
 * @return: 0 on success and @param entpp NOT NULL; else <0
 */
int hp_cache_get(char const * key, char const * dir, struct hp_cache_entry ** entpp);

#ifndef NDEBUG
int test_hp_cache_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif

#endif /* LIBHP_CACHE_H__ */
