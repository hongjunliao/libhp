/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2023/8/4
 *
 * */

/////////////////////////////////////////////////////////////////////////////////////

#ifndef LIBHP_SEARCH_H__
#define LIBHP_SEARCH_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef HAVE_SEARCH_H
#include <search.h> //lfind
#include <stddef.h> //size_t

/////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

/**
 * wrapper for the dangerous lfind function
 * */
#ifdef _MSC_VER
#define lfind _lfind
static void * hp_lfind(const void * key, const void * base,
		unsigned int nmemb, unsigned int size, int (* cb)(const void * k, const void * e))
#else
static void * hp_lfind(const void * key, const void * base,
	    size_t nmemb, size_t size, int (* cb)(const void * k, const void * e))
#endif //#ifdef _MSC_VER
{
	return lfind(key, base, &nmemb, size, cb);
}
#ifdef __cplusplus
}
#endif

#endif //#ifdef HAVE_SEARCH_H

#endif /* LIBHP_SEARCH_H__ */
