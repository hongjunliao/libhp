/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/4/12
 *
 * URL encode/decode
 * from https://github.com/happyfish100/libfastcommon
 * */

#ifndef HP_URLENCODE_H_
#define HP_URLENCODE_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_WITH_HTTP

#include "libyuarel/yuarel.h"   /* yuarel_param */

/////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

/** url decode, terminated with \0
 *  parameters:
 *  	src: the source string to decode
 *  	src_len: source string length
 *  	dest: store dest string
 *  	dest_len: store the dest string length
 *  return: error no, 0 success, != 0 fail
*/
char * hp_urldecode(const char *src, const int src_len, char *dest, int *dest_len);

/*
 * NOTE: will modify @param qeury
 * */
int hp_url_query(char * query, struct yuarel_param * params, int np, struct yuarel_param * q, int nq, char sep);

#ifndef NDEBUG
int test_hp_url_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif

#endif /* LIBHP_WITH_HTTP */
#endif /* HP_URLENCODE_H_ */

