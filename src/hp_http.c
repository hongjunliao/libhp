/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/4/8
 *
 * the HTTP module
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_WITH_HTTP

#include "hp_http.h"
#include "hp_log.h"     /* hp_log */
#include "hp_epoll.h"   /*  */
#include "hp_net.h"     /*  */
#include "hp_url.h"     /* hp_urldecode */
#include "hp_cache.h"   /* hp_cache_get */
#include "str_dump.h"   /* dumpstr */
#include <unistd.h>
#include <dlfcn.h>      /* dlsym */
#include <sys/ioctl.h>  /* ioctl */
#include <sys/stat.h>	/*fstat*/
#include <string.h> 	/* strlen */
#include <arpa/inet.h>	/* inet_ntop */
#include <sys/socket.h>	/* accept */
#include <netinet/in.h>	/* sockaddr_in */
#include <string.h> 	/* strlen */
#include <stdio.h>
#include <stdlib.h> 	/* calloc */
#include <string.h>     /* memset, ... */
#include <errno.h>      /* errno */
#include <assert.h>     /* define NDEBUG to disable assertion */
#include <time.h>
#include "sdsinc.h"    /* sds */
#include "klist.h"        /* list_head */
#include "http_parser.h"

extern int gloglevel;
/////////////////////////////////////////////////////////////////////////////////////

static int request_url_cb (http_parser *p, const char *buf, size_t len);
static int on_message_complete (http_parser * parser);

static struct http_parser_settings ghpsettings = {
		.on_url = request_url_cb,
		.on_message_complete = on_message_complete
};

/////////////////////////////////////////////////////////////////////////////////////

static int hp_http_size(struct hp_http * http)
{
	return http? http->nreq : 0;
}

static hp_httpreq * hp_httpcli_new(struct hp_http * http, int fd)
{
	if(!http)
		return 0;

	struct hp_httpreq * req = (struct hp_httpreq *)calloc(1, sizeof(struct hp_httpreq));

	req->flags = 0;
	req->fd = fd;
	req->url = sdsempty();
	req->url_path = sdsempty();
	req->url_query = sdsempty();

	req->parser = (struct http_parser *)malloc(sizeof(struct http_parser));
	http_parser_init(req->parser, HTTP_REQUEST);

	req->parser->data = req;

	hp_eti_init(&req->ibuf, 0);
	hp_eto_init(&req->obuf, HP_ETIO_VEC);

	req->http = http;

	return req;
}

static void hp_httpcli_delete(struct hp_http * http, struct hp_httpreq * req)
{
	if(!req)
		return;


	hp_eto_uninit(&req->obuf);
	hp_eti_uninit(&req->ibuf);

	free(req->parser);

	sdsfree(req->url_query);
	sdsfree(req->url_path);
	sdsfree(req->url);

	if(req->list.next && req->list.prev)
		list_del(&req->list);

	free(req);
	--http->nreq;
}

static int hp_httpcli_insert(struct hp_http * http, struct hp_httpreq * cli)
{
	if(!http) return -1;

	list_add_tail(&cli->list, &http->req_list);
	++http->nreq;

	return 0;
}

static int request_url_cb (http_parser * parser, const char *buf, size_t len)
{
	struct hp_httpreq * cli = (struct hp_httpreq *)parser->data;
	assert(cli);

	cli->url = sdscatlen(cli->url, buf, len);

#ifndef NDEBUG
	if(gloglevel > 8){
		int len = sdslen(cli->url);
		hp_log(stdout, "%s: fd=%d, buf='%s'/%zu, request_url='%s', len=%d\n"
			, __FUNCTION__, cli->fd
			, dumpstr(buf, len, 64), len
			, dumpstr(cli->url, len, 64), len);
	}
#endif /* NDEBUG */

	return 0;
}

static int on_message_complete (http_parser * parser)
{
	struct hp_httpreq * req = (struct hp_httpreq *)parser->data;
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
		if(gloglevel > 5){
			hp_log(stdout, "%s: fd=%d, request_url=%d/'%s', queryd=%d/'%s'\n"
				, __FUNCTION__, req->fd
				, sdslen(url), dumpstr(url, sdslen(url), 64)
				, sdslen(req->url_query), dumpstr(req->url_query, sdslen(req->url_query), 64)
				);
		}
#endif /* NDEBUG */
	}

	return 0;
}

static int hp_http_accept(struct hp_http * http, int listenfd, struct hp_httpreq ** reqp)
{
	if(!(http && reqp)) return -1;

	struct sockaddr_in clientaddr = { 0 };
	socklen_t len = sizeof(clientaddr);
	int confd = accept(listenfd, (struct sockaddr *)&clientaddr, &len);
	if(confd < 0 ){
		if(errno == EINTR || errno == EAGAIN)
			return 0;

		hp_log(stderr, "%s: accept failed, errno=%d, error='%s'\n", __FUNCTION__, errno, strerror(errno));
		return -1;
	}

	unsigned long sockopt = 1;
	ioctl(confd, FIONBIO, &sockopt);

	if(hp_net_set_alive(confd, http->tcp_keepalive) != 0)
		hp_log(stderr, "%s: WARNING: socket_set_alive failed, interval=%ds\n", __FUNCTION__, http->tcp_keepalive);

	struct hp_httpreq * cli = hp_httpcli_new(http, confd);

	inet_ntop(AF_INET, &clientaddr.sin_addr, cli->ip, sizeof(cli->ip));

	if(hp_httpcli_insert(http, cli) != 0){
		hp_log(stderr, "%s: hp_httpcli_insert failed, close, fd=%d\n", __FUNCTION__, cli->fd);
		hp_httpcli_delete(http, cli);
		return 0;
	}

	*reqp = cli;
	return 1;
}

ssize_t hp_http_parse(struct hp_httpreq * req, char const * buf, size_t len)
{
	if(!req)
		return -1;

	size_t nparsed = http_parser_execute(req->parser, &ghpsettings, buf, len);
	if (req->parser->upgrade) { /* handle new protocol */
		return nparsed;
	}
	else if (nparsed != len) {
		/* Handle error. Usually just close the connection. */
		if(gloglevel > 8)
			hp_log(stderr, "%s: http_parser_execute failed, fd=%d, parsed/buf=%zu/%d, buf='%s'\n"
				, __FUNCTION__, req->fd
				, nparsed, len
				, dumpstr(buf, len, 128));

		return -1;
	}
	return nparsed;
}

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

static int http_response(struct hp_httpreq * req, struct hp_httpresp * resp)
{
	int r;
	char * buf = (char * )malloc(512);
	int n = snprintf(buf, 512, "HTTP/1.1 %d %s \r\n"
				"Content-Type:%s;charset=utf-8\r\n"
				"Access-Control-Allow-Origin:*\r\n"
				"Access-Control-Allow-Methods:*\r\n"
				"Access-Control-Allow-Credentials:true\r\n"
				"Access-Control-Allow-Headers:x-requested-with,content-type\r\n"
			    "Content-Length:%zu\r\n"
				"\r\n",
			resp->status_code, http_status_code_reason_cstr(resp->status_code)
			, http_content_type_cstr(resp->type)
			, resp->nhtml);

	r = hp_eto_add(&req->obuf, buf, n, free, 0);
	if(r != 0) return -1;

	r = hp_eto_add(&req->obuf, resp->html, resp->nhtml, (resp->flags == 1? free : 0), 0);
	if(r != 0) return -1;

	r = hp_eto_add(&req->obuf, "\r\n\r\n", 4, 0, 0);
	if(r != 0) return -1;

	struct epoll_event evobj, * ev = &evobj;
	ev->events = EPOLLOUT;
	ev->data.ptr = &req->epolld;
	hp_eto_write(&req->obuf, req->fd, ev);

	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////

static void  http_epoll_request_close(struct hp_httpreq *  req, int err, char const * errstr)
{
	hp_epoll_del(req->http->efds, req->fd, EPOLLIN | EPOLLOUT | EPOLLET, &req->epolld);
	if(gloglevel > 6)
		hp_log(stdout, "%s: delete connection from '%s', fd=%d, err='%s'/%d, left=%d, i/o=%zu/%zu\n", __FUNCTION__
				, req->ip, req->fd
				, errstr, err
				, req->http->size(req->http) - 1
				, req->ibuf.i_bytes, req->obuf.o_bytes);
	close(req->fd);
	req->fd = 0;

	hp_httpcli_delete(req->http, req);
}

static void http_epoll_request_write_done(struct hp_eto * eto, int err, void * arg)
{
	struct epoll_event * ev = (struct epoll_event *)arg;
	assert(ev);
	struct hp_httpreq *  req = (struct hp_httpreq *)hp_epoll_arg(ev);
	assert(req);

	ev->events = 0;

	http_epoll_request_close(req, err, (err > 0? strerror(err) : ""));
}

/*
 * see hp_http_epoll_parse
 * */
static void http_epoll_request_read_error(struct hp_eti * eti, int err, void * arg)
{
	struct epoll_event * ev = (struct epoll_event *)arg;
	assert(ev);
	struct hp_httpreq *  req = (struct hp_httpreq *)hp_epoll_arg(ev);
	assert(req);

	ev->events = 0;

	char const * errstr = err > 0? strerror(err) :
	                      (err == 0? "EOF" :
	                      (err == -2? "parse error" :
	                      (err == -3? "hp_http_process failed" : "")));
	http_epoll_request_close(req, err, errstr);
}

static int hp_http_epoll_parse(char * buf, size_t * len, void * arg)
{
	assert(buf && len && arg);

	struct epoll_event * ev = (struct epoll_event *)arg;
	assert(ev);
	struct hp_httpreq *  req = (struct hp_httpreq *)hp_epoll_arg(ev);
	assert(req);

	size_t buflen = *len;

	if(!(buflen > 0))
		return EAGAIN;

	int rc = 0;

	ssize_t n = hp_http_parse(req, buf, buflen);
	if(n <= 0){
		rc = -2; /* parse error*/
		goto failed;
	}

	memmove(buf, buf + n, sizeof(char) * (buflen - n));
	*len -= n;

	if(req->flags == 1){ /* parsed as HTTP request */
		if(!req->http->process){
			rc = -4;  /* handle NOT set */
			goto failed;
		}
		struct hp_httpresp  respobj = { 0, 1, 200 }, * resp = &respobj;
		int r = req->http->process(req->http, req, resp);
		if(r != 0){
			rc = -3; /* HTTP process error, return */
			goto failed;
		}
		else{
			assert(resp->html);

			struct hp_eto * eto = &req->obuf;
			eto->write_done = eto->write_error= http_epoll_request_write_done;

			hp_epoll_add(req->http->efds, req->fd, EPOLLOUT | EPOLLET, &req->epolld);

			http_response(req, resp);
			/* HTTP request parse done, stop reading, time to write response */
			return 0;
		}
	}
failed:
	ev->events = 0;
	http_epoll_request_close(req, rc, "handle failed");
	return rc;
}

static int http_epoll_handle_clients_io(struct epoll_event * ev)
{
	if(!(ev && hp_epoll_arg(ev)))
		return -1;
	struct hp_httpreq *  req = (struct hp_httpreq *)hp_epoll_arg(ev);
	assert(req);

#ifndef NDEBUG
	if(gloglevel > 9){
		char buf[128];
		hp_log(stdout, "%s: fd=%d, events='%s'\n", __FUNCTION__
				, hp_epoll_fd(ev), hp_epoll_e2str(ev->events, buf, sizeof(buf)));
	}
#endif /* NDEBUG */

	if((ev->events & EPOLLERR)){
		http_epoll_request_close(req, -1, "EPOLLERR");
		return 0;
	}

	if((ev->events & EPOLLIN)){
		hp_eti_read(&req->ibuf, req->fd, ev);
	}

	if((ev->events & EPOLLOUT)){
		hp_eto_write(&req->obuf, req->fd, ev);
	}

	return 0;
}

static int http_epoll_handle_accepts(struct epoll_event * ev)
{
	hp_http * http = (hp_http * )hp_epoll_arg(ev);
	assert(http);

	for(;;){
		struct hp_httpreq * req = 0;
		int r = hp_http_accept(http, hp_epoll_fd(ev), &req);
		if(r < 0){
			return -1;
		}
		else if(r == 0)
			return 0;

		assert(req);

		if(gloglevel > 6)
			hp_log(stdout, "%s: HTTP connection from '%s', fd=%d, count=%d\n"
					, __FUNCTION__, req->ip, req->fd, http->size(http));

		struct hp_eti * eti = &req->ibuf;
		eti->pack = hp_http_epoll_parse;
		eti->read_error = http_epoll_request_read_error;

		hp_epolld_set(&req->epolld, req->fd, http_epoll_handle_clients_io, req);
		hp_epoll_add(http->efds, req->fd, EPOLLIN | EPOLLET, &req->epolld);
	}
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////
/* HTTP module */

int hp_http_init(hp_http * http, int fd, hp_epoll * efds)
{
	if(!(http && efds)) return -1;

	memset(http, 0, sizeof(hp_http));

	INIT_LIST_HEAD(&http->req_list);

	http->init = hp_http_init;
	http->uninit = hp_http_uninit;
	http->size = hp_http_size;

	http->efds = efds;
	hp_epolld_set(&http->ed, fd, http_epoll_handle_accepts, http);

	return 0;
}

void hp_http_uninit(hp_http * http)
{
	if(!http)
		return;

	struct list_head * pos, * next;
	list_for_each_safe(pos, next, &http->req_list){
    	hp_httpreq * req = (hp_httpreq *)list_entry(pos, hp_httpreq, list);
    	assert(req);
    	hp_httpcli_delete(http, req);
	}
}

int hp_http_dl_reload(void * addr, void * hdl)
{
	if(!(addr && hdl))
		return -1;

	hp_http * http = (hp_http * )addr;

	http->init = dlsym(hdl, "hp_http_init");
	http->uninit = dlsym(hdl, "hp_http_uninit");
	assert(http->init);
	assert(http->uninit);

	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////
/* tests */
#ifndef NDEBUG
#ifdef LIBHP_WITH_CURL

#include "hp_net.h"      /* errno */
#include "hp_curl.h"     /* errno */
#include <sys/stat.h>	 /*fstat*/

#define MULTI_REQ_HTTP "http://127.0.0.1:18541/index.html"
#define MULTI_REQ_FILE "webroot/index.html"

static int test_curl_multi_on_done(hp_curlm * curlm, char const * url, sds str, void * arg)
{
	assert(curlm);
	assert(strcmp(url, MULTI_REQ_HTTP) == 0);
	assert(arg == curlm);

	struct stat fsobj, * fs = &fsobj;
	int rc = stat(MULTI_REQ_FILE, fs);
	assert(rc == 0);

	FILE * f = fopen(MULTI_REQ_FILE, "r");
	assert(f);

	char * buf = malloc(fs->st_size);
	size_t size = fread(buf, sizeof(char), fs->st_size, f);
	assert(size == fs->st_size);
	fclose(f);

	assert(strncmp(str, buf, size) == 0);
	free(buf);

	sdsfree(str);

	if(curlm->n == 0)
		curlm->efds->stop = 1;

	return 0;
}

static int test_http_process(struct hp_http * http, struct hp_httpreq * req, struct hp_httpresp * resp)
{
	struct stat fsobj, * fs = &fsobj;

	sds file = sdscatprintf(sdsempty(), "webroot/%s", req->url_path);

	int rc = stat(file, fs);
	assert(rc == 0);

	FILE * f = fopen(file, "r");
	assert(f);

	char * buf = malloc(fs->st_size);
	size_t size = fread(buf, sizeof(char), fs->st_size, f);
	assert(size == fs->st_size);
	fclose(f);

	resp->status_code = 200;
	resp->flags = 1;
	resp->html = buf;
	resp->nhtml = size;

	sdsfree(file);

	return 0;
}

int test_hp_http_main(int argc, char ** argv)
{
	{
		int rc;
		hp_epoll efdsobj, * efds = &efdsobj;
		rc = hp_epoll_init(efds, 200);
		assert(rc == 0);

		int fd = hp_net_listen(18541);
		assert(fd > 0);

		hp_http http_obj, * http = &http_obj;
		rc = hp_http_init(http, fd, efds);
		assert(rc == 0);

		http->process = test_http_process;

		hp_curlm hp_curl_multiobj, * curlm = &hp_curl_multiobj;
		rc = hp_curlm_init(curlm, efds, 0);
		assert(rc == 0);

		assert(hp_curlm_add);

		rc = hp_curlm_add(curlm, MULTI_REQ_HTTP, 0, 0, test_curl_multi_on_done, curlm);
		assert(rc == 0);

		rc = hp_curlm_add(curlm, MULTI_REQ_HTTP, 0, 0, test_curl_multi_on_done, curlm);
		assert(rc == 0);

		rc = hp_curlm_add(curlm, MULTI_REQ_HTTP, 0, 0, test_curl_multi_on_done, curlm);
		assert(rc == 0);

		assert(curlm->n == 3);

		rc = hp_epoll_add(efds, fd, EPOLLIN | EPOLLET, &http->ed);
		assert(rc == 0);

		hp_epoll_run(efds, 200, 0);

		assert(curlm->n == 0);
		assert(http->nreq == 0);

		hp_curlm_uninit(curlm);
		http->uninit(http);
		hp_epoll_uninit(efds);
		close(fd);
	}
	return 0;
}
#endif
#endif /* NDEBUG */
#endif
