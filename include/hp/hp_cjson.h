/*!
 *  This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2019/11/14
 *
 * cjson utils
 *
 * */
#ifndef LIBHP_CJSON_H
#define LIBHP_CJSON_H

#ifdef LIBHP_WITH_CJSON

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <string.h>
#include <assert.h>
#include "cjson/cJSON.h"	/* cJSON */
#include "sdsinc.h"        /* sds */
#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////////
typedef struct  {
char * (* str)(cJSON * cjson);
int (* size)(cJSON const * cjson);
}hp_cjson;
extern hp_cjson const HP_CJSON_PKG;
/*
 * @brif: printf cJSON @param cjson to string with format @param fmt
 * @return: sds string, empty if cjson NULL
 * */
/*
#define cjson_fmt(fmt, cjson, key) \
(cJSON_GetObjectItem((cjson),(key))? \
	((cJSON_IsString(cJSON_GetObjectItem((cjson),(key))))? \
		((cJSON_GetObjectItem((cjson),(key))->valuestring && strlen(cJSON_GetObjectItem((cjson),(key))->valuestring) > 0)? \
			sdscatprintf(sdsempty(), fmt, cJSON_GetObjectItem((cjson),(key))->valuestring) \
		: sdsempty()) \
	: (sdscatprintf(sdsempty(), fmt, cJSON_GetObjectItem((cjson),(key))->valueint))) \
:sdsempty())
*/

sds cjson_fmt(char const * fmt, cJSON const * cjson, char const * key);

cJSON * cjson_(cJSON const * cjson, char const * key);
char const * cjson_cstr(cJSON const * cjson);
int cjson_ival(cJSON const * cjson, char const * key, int def);
double cjson_dval(cJSON const * cjson, char const * key, double def);
/*
 * sample: @pram key:
 * data/imeis[0]  : NOT implement yet! treat data/imeis as an array and get frist one
 * data/imei      : get sub node data/imei
 * data/imei[,13] : substring(data/imei, 0, 13)
 * */
char const * cjson_sval(cJSON const * cjson, char const * key, char * def);
int cjson_is(cJSON * cjson, char const * key
	, cJSON_bool (* fn)(cJSON const * item));
/////////////////////////////////////////////////////////////////////////////////////////

/* @brief: apply check function @param fn to json with key @param key
 *
 * @param fn:	the check function, return NULL for check failed, @see check_XXX
 * @return:		!0 for true
 * */
int cjson_check(cJSON * cjson, char const * key,
		cJSON * (* fn)(cJSON const * val));

/* @brief: check if all cJSON keys exist,
 * APPEND 0 AT END! e.g.:
 * 	cjson_exists(json, "a/b", "c", 0)
 *
 * @param keys: e.g. 'age', 'name', 'a/b'
 * @return:		!0 for true
 * */
int cjson_exists(cJSON const * cjson, ...);

/*
 * @brief: take out all values of 1 coloumn from a cJSON key, e.g.
 * json = '{"objs":[{"id":"2384", "name":"jack"},{"id":"2385", "name":"tom"}]}'
 * cjson_col(json, "objs/id") => '["2384","2385"]'
 */
cJSON * cjson_col(cJSON const * cjson, char const * key);
cJSON * cjson_parse(char const * fmt, ...);
cJSON * cjson_parselen(char const * str, int len);

sds cjson_in(cJSON * array);
sds cjson_cmdline(char const * cmd, cJSON * args);
/////////////////////////////////////////////////////////////////////////////////////////
#ifndef NDEBUG
int test_hp_cjson_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif
#endif /* LIBHP_CJSON_H */
#endif //LIBHP_WITH_CJSON
