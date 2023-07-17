/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/4/8
 *
 * the HTTP server module, for HTTP client, please use hp_curlm or hp_uv_curlm
 *
 * 2023/7/7:reconstructed using hp_io_t for cross platform usage
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_WITH_HTTP
//#include "Win32_Interop.h"
#ifndef _MSC_VER
#endif /* _MSC_VER */
#include "hp/hp_http.h"
//#include <unistd.h>
#include <stddef.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>     /* assert */
#include "hp/hp_log.h"
#include "http_parser.h"
#include "hp/str_dump.h"
#include "hp/hp_url.h" //hp_urldecode
#include "hp/hp_net.h"
/////////////////////////////////////////////////////////////////////////////////////

static int request_url_cb (http_parser * parser, const char *buf, size_t len)
{
	hp_httpreq * cli = (hp_httpreq *)parser->data;
	assert(cli);

	cli->url = sdscatlen(cli->url, buf, len);

#ifndef NDEBUG
	if(hp_log_level > 9){
		int len = sdslen(cli->url);
		hp_log(stdout, "%s: fd=%d, buf='%s'/%zu, request_url='%s', len=%d\n"
			, __FUNCTION__, -1
			, dumpstr(buf, len, 64), len
			, dumpstr(cli->url, len, 64), len);
	}
#endif /* NDEBUG */

	return 0;
}

static int on_message_complete (http_parser * parser)
{
	hp_httpreq * req = (hp_httpreq *)parser->data;
	assert(req);

	sds url = req->url;

	struct http_parser_url uobj, * u = &uobj;
	if(http_parser_parse_url(url, sdslen(url), 0, u) != 0)
		return -1;

	req->flags = 1;

	if ((u->field_set & (1 << UF_PATH))) {
		int len = (u)->field_data[(UF_PATH)].len;
		if(len > 0){
			req->url_path = sdsMakeRoomFor(req->url_path, len);
			memcpy(req->url_path, url + (u)->field_data[(UF_PATH)].off, len);
			sdsIncrLen(req->url_path, len);
		}

		if ((u->field_set & (1 << UF_QUERY))) {
			len = (u)->field_data[(UF_QUERY)].len;
			if(len > 0){
				char * queryd = malloc(len * 2);
				queryd[0] = '\0';
				int qdlen = 0;
				hp_urldecode(url + (u)->field_data[(UF_QUERY)].off, len, queryd, &qdlen);

				req->url_query = sdsMakeRoomFor(req->url_query, qdlen);
				memcpy(req->url_query, queryd, qdlen);
				sdsIncrLen(req->url_query, qdlen);

				free(queryd);
			}
		}
#ifndef NDEBUG
		if(hp_log_level > 9){
			hp_log(stdout, "%s: fd=%d, request_url=%d/'%s', queryd=%d/'%s'\n"
				, __FUNCTION__, -1/*req->base.fd*/
				, sdslen(url), dumpstr(url, sdslen(url), 64)
				, sdslen(req->url_query), dumpstr(req->url_query, sdslen(req->url_query), 64)
				);
		}
#endif /* NDEBUG */
	}

	return 0;
}

static struct http_parser_settings ghpsettings = {
		.on_url = request_url_cb,
		.on_message_complete = on_message_complete
};

/////////////////////////////////////////////////////////////////////////////////////////

static inline char * http_status_code_reason_cstr(int code)
{
	switch(code){
		case 200 : return "OK";
		case 404 : return "Not Found";
		case 500 : return "Internal Server Error";
		default: return "Unknown";
	}
}

static inline char * http_content_type_cstr(int type)
{
	if(type == 1) return "text/html";
	else if(type == 2) return "text/json";
	else if(type == 3) return "image/x-icon";
	else if(type == 4) return "application/octet-stream";
	else return "unknown/unknown";
}

static int http_response(hp_httpreq * req, struct hp_httpresp * resp)
{
	hp_io_t * io = (hp_io_t *)req;
	if(!(resp && req)) return -1;

	int rc;
	sds buf = sdscatfmt(sdsempty(), "HTTP/1.1 %u %s \r\n"
				"Content-Type:%s;charset=utf-8\r\n"
				"Access-Control-Allow-Origin:*\r\n"
				"Access-Control-Allow-Methods:*\r\n"
				"Access-Control-Allow-Credentials:true\r\n"
				"Access-Control-Allow-Headers:x-requested-with,content-type\r\n"
			    "Content-Length:%u\r\n"
				"\r\n",
			resp->status_code, http_status_code_reason_cstr(resp->status_code)
			, http_content_type_cstr(resp->type)
			, sdslen(resp->html));

	rc = hp_io_write(io, buf, sdslen(buf), (hp_io_free_t)sdsfree, 0);
	if(rc != 0) return -1;

	rc = hp_io_write(io, resp->html, sdslen(resp->html), (resp->flags == 1? (hp_io_free_t)sdsfree : 0), 0);
	if(rc != 0) return -1;

	rc = hp_io_write(io, "\r\n\r\n", 4, 0, 0);
	if(rc != 0) return -1;

	return rc;
}

/////////////////////////////////////////////////////////////////////////////////////////

static int hp_httpreq_init(hp_httpreq * req, hp_http * http)
{
	if(!(req && http))
		return -1;

	memset(req, 0, sizeof(hp_httpreq));

	req->http = http;
	req->flags = 0;
	req->url = sdsempty();
	req->url_path = sdsempty();
	req->url_query = sdsempty();

	req->parser = (struct http_parser *)malloc(sizeof(struct http_parser));
	http_parser_init(req->parser, HTTP_REQUEST);

	req->parser->data = req;

	return 0;
}

static void hp_httpreq_uninit(hp_httpreq * req)
{
	if(!req)
		return;

//	http_parser_uninit(req->parser);
	free(req->parser);

	sdsfree(req->url_query);
	sdsfree(req->url_path);
	sdsfree(req->url);
}

static int hp_httpreq_loop(hp_http * req)
{
	return 0;
}

static hp_io_t * hp_httpreq_on_new(hp_io_t * cio, hp_sock_t fd)
{
	assert(cio && cio->ioctx && cio->user);
	hp_httpreq * req = (hp_httpreq *)calloc(1, sizeof(hp_httpreq));
	int rc = hp_httpreq_init(req, (hp_http *)cio->user);
	assert(rc == 0);

#ifndef NDEBUG
	if(hp_log_level > 0){
		char buf[64] = "";
		hp_log(stdout, "%s: new HTTP connection from '%s', IO total=%d\n", __FUNCTION__, hp_get_ipport_cstr(fd, buf),
				hp_io_count(cio->ioctx));
	}
#endif
	return (hp_io_t *)req;
}

static ssize_t hp_httpreq_parse(hp_httpreq * req, char const * buf, size_t len)
{
	if(!req)
		return -1;

	size_t nparsed = http_parser_execute(req->parser, &ghpsettings, buf, len);
	if (req->parser->upgrade) { /* handle new protocol */
		return nparsed;
	}
	else if (nparsed != len) {
		/* Handle error. Usually just close the connection. */
		if(hp_log_level > 8)
			hp_log(stderr, "%s: http_parser_execute failed, fd=%d, parsed/buf=%zu/%d, buf='%s'\n"
				, __FUNCTION__, -1/*req->base.fd*/
				, nparsed, len
				, dumpstr(buf, len, 128));

		return -1;
	}
	return nparsed;
}

static int hp_httpreq_on_parse(hp_io_t * io, char * buf, size_t * len,
		hp_iohdr_t ** hdrp, char ** bodyp)
{
	hp_httpreq * req = (hp_httpreq *)io;
	if(!(req && buf && len && hdrp && bodyp)) return -1;


	ssize_t n = hp_httpreq_parse(req, buf, *len);
	if(n <= 0) return -1; /* parse error*/
	else if(req->flags == 1){ /* parsed as HTTP request */

		//	HTTP protocol is text-based, there is NO 'message header'
		*hdrp = 0;
		*bodyp = 0;

		memmove(buf, buf + n, sizeof(char) * (*len - n));
		*len -= n;

		return n;
	}
	else return 0; // need more data
}

static int hp_httpreq_on_dispatch(hp_io_t * io, hp_iohdr_t * imhdr, char * body)
{
	hp_httpreq * req = (hp_httpreq *)io;
	if(!(req && req->http)) return -1;

	int rc = 0;
	if(!req->http->on_request_cb){
		rc = -4;  /* handle NOT set */
		goto failed;
	}
	struct hp_httpresp  respobj = { 0, 1, 200 }, * resp = &respobj;
	int r = req->http->on_request_cb(req->http, req, resp);
	if(r != 0){
		rc = -3; /* HTTP process error, return */
		goto failed;
	}
	else{
		/* HTTP request parse done, stop reading, time to write response */
		return http_response(req, resp);;
	}
failed:
	return rc;
}

static int hp_httpreq_on_loop(hp_io_t * io)
{
	return hp_httpreq_loop((hp_http *)io);
}

static void hp_httpreq_on_delete(hp_io_t * io)
{
	hp_httpreq * req = (hp_httpreq *)io;
	if(!(req && req->http)) { return; }

#ifndef NDEBUG
	if(hp_log_level > 0){
		char buf[64] = "";
		hp_log(stdout, "%s: delete HTTP connection '%s', IO total=%d\n", __FUNCTION__
				, hp_get_ipport_cstr(hp_io_fd(io), buf), hp_io_count(io->ioctx));
	}
#endif

	hp_httpreq_uninit((hp_httpreq *)io);
	free(io);
}

/* callbacks for rmqtt clients */
static hp_iohdl s_httphdl = {
	.on_new = hp_httpreq_on_new,
	.on_parse = hp_httpreq_on_parse,
	.on_dispatch = hp_httpreq_on_dispatch,
	.on_loop = hp_httpreq_on_loop,
	.on_delete = hp_httpreq_on_delete,
#ifdef _MSC_VER
	.wm_user = 0 	/* WM_USER + N */
	.hwnd = 0    /* hwnd */
#endif /* _MSC_VER */
};

/////////////////////////////////////////////////////////////////////////////////////////

int hp_http_init(hp_http * http	, hp_io_ctx * ioctx, hp_sock_t listenfd, int tcp_keepalive
		, hp_http_request_cb_t on_request_cb)
{
	int rc;
	if (!(http && ioctx)) { return -1; }

	memset(http, 0, sizeof(hp_httpreq));
	http->ioctx = ioctx;
	http->on_request_cb = on_request_cb;

	rc = hp_io_add(http->ioctx, &http->listenio, listenfd, s_httphdl);
	if (rc != 0) { return -4; }

	http->listenio.user = http;
	return rc;
}

void hp_http_uninit(hp_http * http)
{
	return;
}

/////////////////////////////////////////////////////////////////////////////////////////
/* tests */
#ifndef NDEBUG
#ifdef LIBHP_WITH_CURL

#include "hp/hp_net.h"      /* errno */
#include "hp/hp_curl.h"     /* errno */
#include "hp/hp_test.h"
#include "hp/hp_assert.h"	//hp_assert
#include "hp/hp_config.h"
#include "hp/string_util.h"
/////////////////////////////////////////////////////////////////////////////////////////
#define TEST_URL "http://127.0.0.1:18541/index.html"

static int test_curl_multi_on_done(hp_curlm * curlm, char const * url, sds str, void * arg)
{
	sds file = sdscatprintf(sdsempty(), "%s/%s", hp_config_test("web_root"), "index.html");
	sds fc = hp_fread(file);
	assert(strncmp(str, fc, sdslen(fc)) == 0);
	sdsfree(fc);
	sdsfree(file);

	return 0;
}

static int test_http_process(struct hp_http * http, hp_httpreq * req, struct hp_httpresp * resp)
{
	struct stat fsobj, * fs = &fsobj;

	sds file = sdscatprintf(sdsempty(), "%s/%s", hp_config_test("web_root"), req->url_path);
	hp_assert_path(file, REG);

	resp->status_code = 200;
	resp->flags = 1;
	resp->html = hp_fread(file);

	sdsfree(file);

	return 0;
}

int test_hp_http_main(int argc, char ** argv)
{
	int rc = 0;
#ifdef __linux__
	{
		{
			sds file = sdscatprintf(sdsempty(), "%s/%s", hp_config_test("web_root"), "index.html");
			hp_assert_path(file, REG);
			sdsfree(file);
		}

		int listenfd = hp_net_listen(18541);
		hp_assert(listenfd > 0, "%s: unable to create listen socket at %d", __FUNCTION__, 18541);

		hp_io_ctx ioctxobj, *ioctx = &ioctxobj;
		hp_http http_obj, * http = &http_obj;

		rc = hp_io_init(ioctx); assert(rc == 0);

		rc = hp_http_init(http, ioctx, listenfd, 0, test_http_process);
		assert(rc == 0);

		hp_curlm hp_curl_multiobj, * curlm = &hp_curl_multiobj;
		rc = hp_curlm_init(curlm, &ioctx->efds, 0);
		assert(rc == 0);

		rc = hp_curlm_add(curlm, TEST_URL, 0, 0, test_curl_multi_on_done, curlm);
		assert(rc == 0);

		rc = hp_curlm_add(curlm, TEST_URL, 0, 0, test_curl_multi_on_done, curlm);
		assert(rc == 0);

		rc = hp_curlm_add(curlm, TEST_URL, 0, 0, test_curl_multi_on_done, curlm);
		assert(rc == 0);

		assert(curlm->n == 3);

		int quit = 3;
		for(; quit > 0;){
			hp_io_run(ioctx, 200, 0);
			if(hp_io_count(ioctx) == 1)
				--quit;
		}

		assert(curlm->n == 0);

		hp_curlm_uninit(curlm);
		hp_http_uninit(http);
		close(listenfd);
	}
#endif //__linux__

	return rc;
}
#endif //LIBHP_WITH_CURL
#endif /* NDEBUG */
#endif //LIBHP_WITH_HTTP
