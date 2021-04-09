/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/4/8
 *
 * the HTTP module
 * */

#ifndef LIBHP_HTTP_H__
#define LIBHP_HTTP_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "sds/sds.h"     /* sds */
#include "http_parser.h"
#include "hp_epoll.h"    /* hp_epoll */
#include "hp_io.h"       /* hp_eti */
#include "klist.h"        /* list_head */
/////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hp_http hp_http;
typedef struct hp_httpreq hp_httpreq;

/*
 * HTTP request
 * */
struct hp_httpreq{
	int                  fd;
	int                  flags;
	char                 ip[16];

	sds                  url;           /* e.g. "/refresh_dstmap?a=1&b=2" */
	sds                  url_path;      /* e.g. "/refresh_dstmap" */
	sds                  url_query;     /* e.g. "/a=1&b=2" */

	struct http_parser * parser;

	struct hp_epolld     epolld;
	struct hp_eti        ibuf;
	struct hp_eto        obuf;

	struct list_head     list;
	struct hp_http *     http;          /* ref to the http module */
};

struct hp_httpresp{
	int                  flags;
	int                  type;          /* HTML/JSON? */
	int                  status_code;   /* 200/404/500 */
	char *               html;
	size_t               nhtml;
	char *               error_html;
};

struct hp_http{
	hp_epoll *           efds;
	hp_epolld            ed;
	struct list_head     req_list;
	int                  nreq;
	int (* init)(hp_http * http, int fd, hp_epoll * efds);
	void (* uninit)(hp_http * http);
	int (* size)(hp_http * http);

	/* callbacks, set by user */
	int (* process)(struct hp_http * http, struct hp_httpreq * req, struct hp_httpresp * resp);
	int tcp_keepalive;         /* TCP option, keepalive */
};

int hp_http_init(hp_http * http, int fd, hp_epoll * efds);
void hp_http_uninit(hp_http * http);

#ifndef NDEBUG
int test_hp_http_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif

#endif /* LIBHP_HTTP_H__ */
