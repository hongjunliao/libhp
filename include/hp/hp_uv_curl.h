/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2019/8/8
 *
 * curl using libuv
 * */

#ifndef LIBHP_UV_URL_H
#define LIBHP_UV_URL_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_WITH_CURL

#include "sdsinc.h"        /* sds */
#include <stdint.h>      /* size_t */
#include <curl/curl.h>   /* libcurl */
#include "uv.h"         /* libuv */
/////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hp_uv_curlm hp_uv_curlm;

/* callback
 *	 bytes:   bytes received
 *   content_length:   total bytes of body if has "Content-Length"
 *   resp:    @see hp_uv_curlm_add
 *   arg:     @see hp_uv_curlm_add
 */
typedef int (* hp_uv_curl_proress_cb_t)(int bytes, int content_length, sds resp, void * arg);
typedef int (* hp_uv_curl_done_cb_t)(hp_uv_curlm * curlm, char const * url, sds str, void * arg);


struct hp_uv_curlm {
	CURLM *     curl_handle;        /* the libcurl multi handle */
	uv_timer_t  timeout;
	uv_loop_t * loop;
};

/*
 * see https://curl.haxx.se/libcurl/c/curl_mime_init.html
 * */
/*
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
int hp_uv_curlm_add(hp_uv_curlm * curlm, CURL * handle, const char * url
		, struct curl_slist * hdrs
		, void * form
		, char const * resp
		, hp_uv_curl_proress_cb_t on_proress
		, hp_uv_curl_done_cb_t on_done
		, void * arg, int flags);

int hp_uv_curlm_init(hp_uv_curlm * curlm, uv_loop_t * loop);
void hp_uv_curlm_uninit(hp_uv_curlm * curlm);

#ifndef NDEBUG
int test_hp_uv_curl_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif

#endif /*  LIBHP_WITH_CURL */
#endif /* LIBHP_UV_URL_H */
