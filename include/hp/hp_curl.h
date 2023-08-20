/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/8/26
 *
 * libcurl using hp_epoll
 *
 */

#ifndef LIBHP_CURL_H
#define LIBHP_CURL_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifndef _MSC_VER
#ifdef LIBHP_WITH_CURL

#include "hp_epoll.h"    /* hp_epoll */
#include "hp_timerfd.h"  /* hp_timerfd */
#include <curl/curl.h>   /* libcurl */
#include "sdsinc.h"     /* sds */
/////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hp_curlm hp_curlm;
typedef struct hp_curlitem hp_curlitem;

struct hp_curlm {
	CURLM *          curl_handle;        /* the libcurl multi handle */
	hp_epoll *       efds;
	hp_timerfd       timer;
	int              n;                 /* number of easy */

	int              max_n;
};
/*
 * @param on_done: callback when finished
 */
int hp_curlm_add(hp_curlm * curlm, const char * url
		, struct curl_slist * hdrs, const char * body
		, int (* on_done)(hp_curlm * curlm, char const * url, sds str, void * arg)
		, void * arg);

int hp_curlm_init(hp_curlm * curlm, hp_epoll * efds, int max_n);
void hp_curlm_uninit(hp_curlm * curlm);
/*
 * use libcurl in easy mode
 * @return:  body received, NOTE free after used
 * */
sds hp_curl_easy_perform(char const * url, struct curl_slist * hdrs, void * data, int f);

/*
 * sample: a=b&c=d
 * */
int hp_curl_append_header_from_qeury_str(struct curl_slist ** hdr, char * querystr);

#ifndef NDEBUG
int test_hp_curl_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif

#endif /* LIBHP_WITH_CURL */
#endif /* _MSC_VER */
#endif /* LIBHP_CURL_H */
