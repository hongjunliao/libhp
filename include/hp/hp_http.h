/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/4/8
 *
 * the HTTP server module, for HTTP client, please use hp_curlm or hp_uv_curlm
 *
 * 2023/7/7:reconstructed using hp_io_t for cross platform usage
 * */

#ifndef LIBHP_HTTP_H__
#define LIBHP_HTTP_H__

//#include "Win32_Interop.h"
#include "sdsinc.h"    	/* sds */
#include "hp_sock_t.h"   /* hp_sock_t */
#include "hp_io_t.h"   	/* hp_io_ctx */
#include "redis/src/adlist.h" /* list */

/////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hp_httpreq hp_httpreq;
typedef struct hp_httpresp hp_httpresp;
typedef struct hp_http hp_http;
/* callbacks, set by user */
typedef int (* hp_http_request_cb_t)(struct hp_http * http, struct hp_httpreq * req, struct hp_httpresp * resp);


/* for HTTP request */
struct hp_httpreq {
	hp_io_t 	base;
	hp_http * 	http;
	int flags;
	sds url;           /* e.g. "/query?a=1&b=2" */
	sds url_path;      /* e.g. "/query" */
	sds url_query;     /* e.g. "/a=1&b=2" */
	struct http_parser * parser;
};

struct hp_httpresp{
	int                  flags;
	int                  type;          /* HTML/JSON? */
	int                  status_code;   /* 200/404/500 */
	sds                  html;
	char *               error_html;
};

/*for hp_io_t, comment it is OK if you don't use it's members */
//union hp_iohdr { char unused; };

struct hp_http {
	hp_io_ctx * ioctx;
	hp_io_t 	listenio;
	/* callbacks, set by user */
	hp_http_request_cb_t on_request_cb;
	list * reqlist;
};

/////////////////////////////////////////////////////////////////////////////////////////

int hp_http_init(hp_http * http	, hp_io_ctx * ioctx, hp_sock_t listenfd, int tcp_keepalive
		, hp_http_request_cb_t on_request_cb);
void hp_http_uninit(hp_http * http);

#define hp_http_count(http) (listLength((http)->reqlist))
/////////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
int test_hp_http_main(int argc, char ** argv);
#endif //NDEBUG

/////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif
#endif /* LIBHP_HTTP_H__ */
