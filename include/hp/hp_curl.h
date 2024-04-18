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

#ifdef LIBHP_WITH_CURL

#include <curl/curl.h>   /* libcurl */
#include "sdsinc.h"     /* sds */

#if (defined HAVE_SYS_TIMERFD_H) && (defined HAVE_SYS_EPOLL_H)
#include "hp_epoll.h"    /* hp_epoll */
#include "hp_timerfd.h"  /* hp_timerfd */
typedef hp_epoll hp_curl_loop_t;
#else  //use libuv
#include <stdint.h>      /* size_t */
#include "uv.h"         /* libuv */
typedef uv_loop_t hp_curl_loop_t;
#endif
/////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hp_curl hp_curl;

/* callback
 *	 bytes:   bytes received
 *   content_length:   total bytes of body if has "Content-Length"
 *   resp:    @see hp_uv_curladd
 *   arg:     @see hp_uv_curladd
 */
typedef int (* hp_curl_proress_cb_t)(int bytes, int content_length, sds resp, void * arg);
typedef int (* hp_curl_done_cb_t)(hp_curl * hcurl, CURL *easy_handle, char const * url, sds str, void * arg);

struct hp_curl {
	CURLM *          curl_handle;        /* the libcurl multi handle */
#if (defined HAVE_SYS_TIMERFD_H) && (defined HAVE_SYS_EPOLL_H)
	hp_epoll *       loop;
	hp_timerfd       timer;
#else
	uv_loop_t * loop;
	uv_timer_t  timer;
#endif
};

int hp_curlinit(hp_curl * hcurl, hp_curl_loop_t * loop);

/*
 * see https://curl.haxx.se/libcurl/c/curl_mime_init.html
 *
 * do a HTTP/1 POST/GET request async
 * @param url:			 URL, HTTP/1 only
 * @param hdrs, form:    HTTP headers(and form if POST)
 * @param resp:          response, NULL for ignore, empty then to buffer, else to file
 * @param on_proress:    callback for user, progress
 * @param on_done:       callback for user, when done
 * @arg:                 user data
 *
 * @return:				0 on OK
 * */
int hp_curladd(hp_curl * hcurl, CURL * handle, const char * url
		, struct curl_slist * hdrs
		, void * form
		, char const * resp
		, hp_curl_proress_cb_t on_proress
		, hp_curl_done_cb_t on_done
		, void * arg, int flags);

void hp_curluninit(hp_curl * hcurl);
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

/////////////////////////////////////////////////////////////////////////////////////

#endif /* LIBHP_WITH_CURL */
#endif /* LIBHP_CURL_H */
