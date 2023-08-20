/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2021/4/9
 *
 * networking I/O, using epoll on *nix and IOCP on Win32, else poll()
 * */
/////////////////////////////////////////////////////////////////////////////////////////

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "hp/hp_io_t.h"
#include "redis/src/adlist.h" /* list */
#include "hp/sdsinc.h"		/* sds */
#include "hp/hp_err.h"
#include <assert.h> /* assert */
#include <errno.h> /*  */
#include <limits.h> /* INT_MAX */
#include <stdio.h>
#include <string.h>
#include "hp/hp_log.h"
#include "hp/str_dump.h" 	/*dumpstr*/
#include "hp/string_util.h" /*strncasecmp*/

#ifndef _MSC_VER
#include <sys/fcntl.h>  /* fcntl */
#else
#include <WS2tcpip.h>	//socklen_t
#endif /* _MSC_VER */
/////////////////////////////////////////////////////////////////////////////////////////

#ifndef _MSC_VER
#define safe_call(fn, args...) do{ if(fn) { fn(args); } } while(0)
#else
#define safe_call(fn, ...) do{ if(fn) { fn(__VA_ARGS__); } } while(0)
#endif //_MSC_VER

/////////////////////////////////////////////////////////////////////////////////////////
// on data

static int hp_io_t_on_data(hp_io_t * io, char * buf, size_t * len)
{
	assert(io && buf && len);
	int rc;
	if (!(io->iohdl.on_parse)) {
		*len = 0;
	}

	if (!(*len > 0))
		return 0;

	for (; *len > 0;) {
		void * iohdr = 0, * body= 0;
		int n = io->iohdl.on_parse(io, buf, len, &iohdr, &body);
		if (n < 0) {
			*len = 0;
			return n;
		}
		else if (n > 0) {
			if (io->iohdl.on_dispatch){
				return io->iohdl.on_dispatch(io, iohdr, body);
			}
		}
	}
	return 0;
}

// on data for Win32/IOCP
#if defined(_MSC_VER)
static int hp_io_t_internal_on_data(hp_iocp * iocpctx, void * arg, char * buf, size_t * len){
	assert(iocpctx && buf && len);
	hp_io_t * io = (hp_io_t *)arg;
	assert(io);

// on data for epoll/poll
#elif defined(HAVE_SYS_EPOLL_H) || defined HAVE_POLL_H

static int hp_io_t_internal_on_data(char * buf, size_t* len, void * arg){
	hp_io_t * io = (hp_io_t *)arg;
	assert(io);
// on data for select
#else
#endif //defined(HAVE_SYS_EPOLL_H) || defined HAVE_POLL_H

	return  hp_io_t_on_data(io, buf, len);
}


/////////////////////////////////////////////////////////////////////////////////////////
//on error

static void hp_io_t_on_error(hp_io_t * io, int err, char const * errstr)
{
	assert(io && io->ioctx);
#if defined(HAVE_SYS_EPOLL_H)
	hp_epoll_rm(&io->ioctx->epo, io->fd);
	hp_close(io->fd);
#elif defined(HAVE_POLL_H)
	hp_poll_rm(&io->ioctx->po, io->fd);
	hp_close(io->fd);
#endif //defined(HAVE_SYS_EPOLL_H)

#if defined(HAVE_SYS_UIO_H)
	hp_rd_uninit(&io->rd);
	hp_wr_uninit(&io->wr);
#endif //#if defined(HAVE_SYS_UIO_H)

	safe_call(io->iohdl.on_delete, io, err, errstr);
}

//on error for Win32/IOCP
#if defined(_MSC_VER)
static int hp_io_t_internal_on_error(hp_iocp * iocp, void * arg, int err, char const * errstr) {
	assert(iocp && arg);
	hp_io_t * io = (hp_io_t *)arg;
	hp_io_t_on_error(io, err, errstr);
	return 0;
}
#endif //defined(_MSC_VER)

//on error for epoll/poll
#if defined(HAVE_SYS_EPOLL_H) || defined HAVE_POLL_H
static void hp_io_t_internal_on_werror(struct hp_wr * eto, int err, void * arg)
{
	return hp_io_t_on_error((hp_io_t * )arg, err, strerror(err));
}

static void hp_io_t_internal_on_rerror(struct hp_rd * eti, int err, void * arg)
{
	return hp_io_t_on_error((hp_io_t * )arg, err, (err == 0? "EOF" : strerror(err)));
}
#endif //defined(HAVE_SYS_EPOLL_H) || defined HAVE_POLL_H

#if defined(HAVE_SYS_EPOLL_H)
static int hp_io_t_internal_io_cb(epoll_event * ev, void * arg)
{
	int rc;
	assert(ev && arg);
	hp_io_t * io = (hp_io_t *)arg;
	assert(io);

	if((ev->events & EPOLLERR)){
		hp_io_t_on_error(io, EPOLLERR, "EPOLLERR");
		return EPOLLERR;
	}

	if((ev->events & EPOLLIN)){
		hp_rd_read(&io->rd, hp_io_fd(io), io);
		rc = io->rd.err;
	}
	if(rc != 0) return rc;

	if((ev->events & EPOLLOUT)){
		hp_wr_write(&io->wr, hp_io_fd(io), io);
	}

	return 0;
}

#elif defined(HAVE_POLL_H)
static int hp_io_t_internal_io_cb(pollfd * pfd, void * arg)
{
	assert(pfd && arg);
	hp_io_t * io = (hp_io_t *)arg;

	if((pfd->revents & POLLERR)){
		hp_io_t_on_error(io, POLLERR, "POLLERR");
		pfd->revents = 0;
		return 0;
	}
	if((pfd->revents & POLLIN)){
		hp_rd_read(&io->rd, hp_io_fd(io), io);
	}
	if((pfd->revents & POLLOUT)){
		hp_wr_write(&io->wr, hp_io_fd(io), io);
	}
	pfd->revents = 0;
	return 0;
}

#else
	//select
#endif //defined(HAVE_SYS_EPOLL_H)

/////////////////////////////////////////////////////////////////////////////////////
// on accept

#if defined(HAVE_SYS_EPOLL_H)
static int hp_io_t_internal_on_accept(epoll_event * ev, void * arg) {
	assert(ev);
#elif defined(_MSC_VER)
static int hp_io_t_internal_on_accept(hp_iocp * iocpctx, void * arg)  {
	assert(iocpctx);
#elif defined(HAVE_POLL_H)
static int hp_io_t_internal_on_accept(pollfd * pfd, void * arg)  {
	assert(pfd);
	pfd->revents = 0;
#else // select
#endif //defined(HAVE_SYS_EPOLL_H)
	hp_io_t * io = (hp_io_t * )arg;
	assert(io);

	hp_sock_t fd = hp_io_fd(io);
	assert(hp_sock_is_valid(fd));
	int rc;

	for(;;){
		socklen_t len = (int)sizeof(io->addr);
		hp_sock_t confd = accept(fd, (struct sockaddr *)&io->addr, &len);
#ifndef _MSC_VER
		if(!hp_sock_is_valid(confd)){
			if (errno == EINTR || errno == EAGAIN) { return 0; }
			hp_log(stderr, "%s: accept failed, errno=%d, error='%s'\n", __FUNCTION__, errno, strerror(errno));
#else
		if(!hp_sock_is_valid(confd)){
			int err = WSAGetLastError();
			if(WSAEWOULDBLOCK == err) { return 0; }
			hp_err_t errstr = "accept: %s";
			hp_log(stderr, "%s: accept failed, errno=%d, error='%s'\n", __FUNCTION__, err, hp_err(err, errstr));
#endif /* _MSC_VER */
			return -1;
		}
		if(hp_tcp_nodelay(confd) < 0)
			{ hp_close(confd); continue; }

		hp_io_t * nio = io->iohdl.on_new? io->iohdl.on_new(io, confd) : 0;
		if(!nio){
			hp_close(confd);
			continue;
		}
	}
	
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////
// on_loop

static int hp_io_t_internal_on_loop(void * arg)
{
	assert(arg);
	auto io = (hp_io_t *)arg;

	if (io->iohdl.on_loop) {
		return io->iohdl.on_loop(io);
	}
	return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
//APIs

static int iolist_match(void *ptr, void *key)
{
	assert(ptr);
	if(!key) { return 0; }
	hp_io_t * io = (hp_io_t *)ptr, * k = (hp_io_t *)key;
	return (k->id > 0 && io->id == k->id) ||
			(hp_sock_is_valid(hp_io_fd(k)) && hp_io_fd(io) == hp_io_fd(k));
}

int hp_io_init(hp_io_ctx * ioctx, hp_ioopt opt)
{
	if(!(ioctx))
		return -1;
	int rc = 0;
	memset(ioctx, 0, sizeof(hp_io_ctx));

#if defined(HAVE_SYS_EPOLL_H)
	/* init epoll */
	rc = hp_epoll_init(&ioctx->epo, opt.maxfd, opt.timeout, 0, 0);
#elif defined(_MSC_VER)
	rc = hp_iocp_init(&(ioctx->iocp), opt.maxfd, opt.hwnd, opt.nthreads, WM_USER + opt.wm_user, opt.timeout, 0/*ioctx->iolist*/);
	assert(rc == 0);
#elif defined HAVE_POLL_H
	rc = hp_poll_init(&ioctx->po, opt.maxfd, opt.timeout, 0, 0);
#else
	//select
	rc = -1;
#endif //defined(HAVE_SYS_EPOLL_H)

	return rc;
}

int hp_io_write(hp_io_t * io, void * buf, size_t len, hp_free_t free, void * ptr)
{
	int rc;
#if defined(HAVE_SYS_UIO_H)
	rc = hp_wr_add(&io->wr, buf, len,  free, ptr);
#elif defined(_MSC_VER)
	rc = hp_iocp_write(&io->ioctx->iocp, io->id, buf, len, free, ptr);
#endif //defined(HAVE_SYS_EPOLL_H) || defined(HAVE_POLL_H)
	return rc;
}

int hp_io_run(hp_io_ctx * ioctx, int interval, int mode)
{
	int rc = 0;
#if defined(HAVE_SYS_EPOLL_H)
	rc = hp_epoll_run(&ioctx->epo, mode);
#elif defined(_MSC_VER)
	rc = hp_iocp_run(&ioctx->iocp, mode);
#elif defined(HAVE_POLL_H)
	rc = hp_poll_run(&ioctx->po, mode);
#else
#endif //#if defined(HAVE_SYS_EPOLL_H)

	return rc;
}

int hp_io_uninit(hp_io_ctx * ioctx)
{
	if(!ioctx)
		return -1;
#if defined(HAVE_SYS_EPOLL_H)
	hp_epoll_uninit(&ioctx->epo);
#elif defined(_MSC_VER)
	hp_iocp_uninit(&ioctx->iocp);
#elif defined(HAVE_POLL_H)
	hp_poll_uninit(&ioctx->po);
#else
	//select
#endif //#if defined(HAVE_SYS_EPOLL_H)

	return 0;
}

hp_sock_t hp_io_fd(hp_io_t * io)
{
	if(!(io && io->ioctx))
		return hp_sock_invalid;
	return io->fd;
}

int hp_io_size(hp_io_ctx * ioctx)
{
	if(!(ioctx))
		return -1;
#if defined(_MSC_VER)
	return hp_iocp_size(&ioctx->iocp);
#elif defined(HAVE_SYS_EPOLL_H)
	return hp_epoll_size(&ioctx->epo);
#elif defined(HAVE_POLL_H)
	return hp_poll_size(&ioctx->po);
#else
	return -1;
#endif /* _MSC_VER */

//	return ioctx? listLength(ioctx->iolist) : 0;
}

int hp_io_add(hp_io_ctx * ioctx, hp_io_t * io, hp_sock_t fd, hp_iohdl iohdl)
{
	int rc = 0;
	if(!(ioctx && io && (iohdl.on_new || iohdl.on_parse)))
		return -1;
	if(!hp_sock_is_valid(fd))
		return -2;

	io->ioctx = ioctx;
	io->iohdl = iohdl;
	io->fd = fd;
#ifndef _MSC_VER
	io->id = fd;
#endif //#ifndef _MSC_VER

	if(io->iohdl.on_new){
#if defined(HAVE_SYS_EPOLL_H)
		rc = hp_epoll_add(&ioctx->epo, fd, EPOLLIN, hp_io_t_internal_on_accept
				, (iohdl.on_loop? hp_io_t_internal_on_loop : 0), io);
#elif defined HAVE_POLL_H
		rc = hp_poll_add(&ioctx->po, fd, POLLIN, hp_io_t_internal_on_accept,
				(iohdl.on_loop? hp_io_t_internal_on_loop : 0), io);
#elif defined _MSC_VER
		io->id = hp_iocp_add(&ioctx->iocp, 0/*nibuf*/, fd,
							hp_io_t_internal_on_accept, 0/*on_data*/, 0/*on_error*/,
							(iohdl.on_loop? hp_io_t_internal_on_loop : 0), io);
		rc = (io->id >= 0? 0 : io->id);
#else
		// select
		rc = -1;
#endif //defined(HAVE_SYS_EPOLL_H)
	}
	else if(io->iohdl.on_parse){
#if defined(HAVE_SYS_UIO_H)
		rc = hp_rd_init(&io->rd, 1024 * 8, hp_io_t_internal_on_data, hp_io_t_internal_on_rerror);
		rc = rc == 0? hp_wr_init(&io->wr, 8, hp_io_t_internal_on_werror) : rc;
#endif //#if defined(HAVE_SYS_UIO_H)
#if defined(HAVE_SYS_EPOLL_H)
		/* try to add to epoll event system */
		rc = rc == 0? hp_epoll_add(&ioctx->epo, fd,  EPOLLIN | EPOLLOUT |  EPOLLET
				, hp_io_t_internal_io_cb, (iohdl.on_loop? hp_io_t_internal_on_loop : 0), io) : rc;
#elif defined HAVE_POLL_H
		rc = hp_poll_add(&ioctx->po, fd, POLLIN | POLLOUT, hp_io_t_internal_io_cb,
				(iohdl.on_loop? hp_io_t_internal_on_loop : 0), io);
#elif defined _MSC_VER
		io->id = hp_iocp_add(&ioctx->iocp, 0/*nibuf*/, fd,
							0/*on_accept*/, hp_io_t_internal_on_data, hp_io_t_internal_on_error,
							(iohdl.on_loop? hp_io_t_internal_on_loop : 0), io);
		rc = (io->id >= 0? 0 : io->id);
#else
		// select
		rc = -1;
#endif //HAVE_SYS_EPOLL_H
	}

	return rc;
}

int hp_io_rm(hp_io_ctx * ioctx, int id)
{
	int rc;
#if defined(HAVE_SYS_EPOLL_H)
		rc = hp_epoll_rm(&ioctx->epo, id);
#elif defined HAVE_POLL_H
		rc = hp_poll_rm(&ioctx->po, id);
#elif defined _MSC_VER
		rc = hp_iocp_rm(&ioctx->iocp, id);
#else
		// select
		rc = -1;
#endif //defined(HAVE_SYS_EPOLL_H)
		return rc;
}

hp_io_t * hp_io_find(hp_io_ctx * ioctx, void * key, int (* on_cmp)(const void *key, const void *ptr))
{
	if(!(ioctx))
		return 0;

#if defined(HAVE_SYS_EPOLL_H)
	return (hp_io_t *)hp_epoll_find(&ioctx->epo, key, on_cmp);
#elif defined HAVE_POLL_H
	return (hp_io_t *)hp_poll_find(&ioctx->po, key, on_cmp);

#elif defined _MSC_VER
	return (hp_io_t *)hp_iocp_find(&ioctx->iocp, key, on_cmp);
#else
		// select
#endif //defined(HAVE_SYS_EPOLL_H)
	return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// tests

#if !defined(NDEBUG)
#include <time.h>   /*difftime*/
#if defined(LIBHP_WITH_CJSON) && defined(LIBHP_WITH_HTTP)
#include <cjson/cJSON.h>
#include <http_parser.h>
#endif //#if defined(LIBHP_WITH_CJSON) && defined(LIBHP_WITH_HTTP)
#include "hp/hp_net.h" /* hp_tcp_connect */
#include "redis/src/adlist.h"  /* list */
#include "hp/sdsinc.h" /* sds */
#include "hp/hp_config.h"
#include "hp/hp_cjson.h"
#include "hp/string_util.h"
#include "hp/hp_assert.h"
#define cfg hp_config_test
#define cfgi(key) atoi(hp_config_test(key))
/////////////////////////////////////////////////////////////////////////////////////

#if !defined(_MSC_VER)  && defined(LIBHP_WITH_CJSON) && defined(LIBHP_WITH_HTTP)
/**
 * server:
 * simple HTTP request and response
 */

typedef struct http_parse{
	char content_type[64];
	union {
		char url[512];
		int status_code;
	} u;
} http_parse;

static inline char const * status_code_cstr(int code)
{
	switch(code){
		case 200 : return "OK";
		case 404 : return "Not Found";
		case 500 : return "Internal Server Error";
		default: return "Unknown";
	}
}
/////////////////////////////////////////////////////////////////////////////////////

#define HTTP_REQ(out, parse, content_length) do {                \
	out = sdscatprintf(sdsempty(),                               \
	"GET %s HTTP/1.1\r\n"                                        \
	"Host: %s:%d\r\n"                                            \
	"Content-Type: application/%s\r\n"                           \
	"Content-Length: %d\r\n\r\n"                                 \
	, parse.u.url, cfg("ip")                   \
	, cfgi("tcp.port")                             \
	, (parse.content_type)      \
	, (int)(content_length)); } while(0)

#define HTTP_RESP(out, parse, content_length) do {               \
	out = sdscatprintf(sdsempty(),                               \
	"HTTP/1.1 %d %s\r\n"                                         \
	"Host: %s:%d\r\n"                                            \
	"Content-Type: application/%s\r\n"                           \
	"Content-Length: %d\r\n\r\n"                                 \
	, parse.u.status_code                                        \
	, status_code_cstr(parse.u.status_code)                      \
	, cfg("ip")                                \
	, cfgi("tcp.port")                             \
	, (parse.content_type)      \
	, (int)(content_length)); } while(0)

#ifdef _MSC_VER

#define JSONRPC(out, T, parse, fmt, ...) do {                    \
	sds json = sdscatprintf(sdsempty(), fmt, __VA_ARGS__);       \
	sds http;                                  					 \
	HTTP_##T(http, parse, sdslen(json));                         \
	out = sdscatprintf(sdsempty(), "%s%s\r\n", http, json);      \
	sdsfree(json); sdsfree(http); } while(0)

#else

#define JSONRPC(out, T, parse, fmt, args...) do {                \
	sds json = sdscatprintf(sdsempty(), fmt, ##args);            \
	sds http;                                  					 \
	HTTP_##T(http, parse, sdslen(json));                         \
	out = sdscatprintf(sdsempty(), "%s%s\r\n", http, json);      \
	sdsfree(json); sdsfree(http); } while(0)

#endif //

/**
 * rc: >0 OK, ==0 need more data, <0 failed
 * */
#define JSONRPC_PARSE(in, parse, json, rc) do {                   \
	rc = -1; json = 0;                                            \
	char ip[64] = "(null)", scstr[62] = "";                       \
	char * jsonstr = (char *)malloc(1024 * 1024 * 4);             \
	jsonstr[0] = '\0';                                            \
	int port = 0, content_length = 0;                             \
	rc = parse.u.status_code? 5 <= sscanf(in,                     \
	"GET %s HTTP/1.1\r\n"                                         \
	"Host: %[0-9.]:%d\r\n"                                        \
	"Content-Type: application/%s\r\n"                            \
	"Content-Length: %d\r\n\r\n%[^\r\n]\r\n"                      \
	, parse.u.url, ip, &port, parse.content_type                  \
	, &content_length, jsonstr                                    \
	) :  6 <= sscanf(in,                                          \
	"HTTP/1.1 %d %[^\r\n]\r\n"                                    \
	"Host: %[0-9.]:%d\r\n"                                        \
	"Content-Type: application/%s\r\n"                            \
	"Content-Length: %d\r\n\r\n%[^\r\n]\r\n"                      \
	, &parse.u.status_code, scstr, ip, &port                      \
	, parse.content_type, &content_length, jsonstr                \
	);                                                            \
	if(rc == 0) { /*need more data?*/}                            \
	else if(parse.u.url[0] && \
			strcmp(ip, cfg("ip")) == 0 &&       \
			cfgi("tcp.port") == port) {             \
		rc = 1;                                                   \
		if(strncasecmp(parse.content_type, "json", 4) == 0 &&     \
				jsonstr[0] && content_length >= 2) {              \
			json = cJSON_Parse(jsonstr);                          \
			if(!json) rc = -2;                                    \
		}                                                         \
	} else { rc = -1;  } free(jsonstr); }while(0)

/////////////////////////////////////////////////////////////////////////////////////
typedef struct httpclient {
	hp_io_t io;
	sds in;
	http_parse parse;
	cJSON * json;
	int flags;
	int rpc_id;
} httpclient;
typedef struct httprequest {
	hp_io_t io;	/*MUST be the first member*/
	sds in;
	http_parse parse;
	cJSON * json;
} httprequest;
typedef struct httpserver {
	hp_io_t listenio;
	list clients;
} httpserver;

static int client_init(httpclient * c)
{
	memset(c, 0, sizeof(httpclient));
	c->in = sdsempty();
	strcpy(c->parse.content_type, "html");
	return 0;
}
static void client_uninit(httpclient * c)
{
	sdsfree(c->in);
//	sdsfree(out);
}
static int request_init(httprequest * req)
{
	memset(req, 0, sizeof(httprequest));
	req->in = sdsempty();
	strcpy(req->parse.content_type, "html");
//	req->out = sdsempty();

	return 0;
}
static void request_uninit(httprequest * req)
{
	sdsfree(req->in);
//	sdsfree(req->out);
}
static int server_init(httpserver * s)
{
//	memset(s, 0, sizeof(httpserver));
	return 0;
}
static void server_uninit(httpserver * s)
{

}

/////////////////////////////////////////////////////////////////////////////////////

static hp_io_t * test_http_server_on_new(hp_io_t * cio, hp_sock_t fd)
{
	assert(cio && cio->ioctx);

	httprequest * req = (httprequest *)malloc(sizeof(httprequest));
	int rc = request_init(req); assert(rc == 0);

	hp_iohdl niohdl = cio->iohdl;
	niohdl.on_new = 0;
	rc = hp_io_add(cio->ioctx, (hp_io_t *)req, fd, niohdl);
	if (rc != 0) {
		request_uninit(req);
		free(req);
		return 0;
	}

	req->io.addr = cio->addr;

	char buf[64] = "";
	hp_log(std::cout, "%s: new HTTP connection from '%s:%d', IO total=%d\n",
			__FUNCTION__, hp_addr4name(&cio->addr, ":", buf, sizeof(buf)), 0,
			hp_io_size(cio->ioctx));
	return (hp_io_t *)req;
}

static int test_http_server_on_parse(hp_io_t * io, char * buf, size_t * len
		, void ** hdrp, void ** bodyp)
{
	assert(io && buf && len && hdrp && bodyp);

	int rc;
	httprequest *req = (httprequest *)io;
	req->in = sdscatlen(req->in, buf, *len);

	req->parse.u.status_code = 1;
	JSONRPC_PARSE(req->in, req->parse, req->json, rc);
	if(rc <= 0) return rc;

	*len = 0;
	sdsclear(req->in);
	*hdrp = 0;
	*bodyp = 0;

	return rc;
}

static int reply_file(httprequest *req, char const * filestr, int status_code)
{
	int rc;
	sds file = sdscatprintf(sdsempty(), "%s/%s", cfg("web_root"), filestr);
	sds html = hp_fread(file);

	sds httphdr = 0;
	req->parse.u.status_code = status_code;
	HTTP_RESP(httphdr, req->parse, sdslen(html) ); assert(httphdr);

	rc = hp_io_write(&req->io, httphdr, sdslen(httphdr), (hp_free_t)sdsfree, 0);
	rc = (rc == 0)? hp_io_write(&req->io, html, sdslen(html), (hp_free_t)sdsfree, 0) : -1;

	static char buf[] = "\r\n\r\n";
	rc = (rc == 0)? hp_io_write(&req->io, buf, 4, 0, 0) : -2;

	sdsfree(file);
	return rc;
}

static int reply_jsonrpc(httprequest *req, cJSON * cjson)
{
	assert(req && cjson);
	int rc = -1;
	char method[64] = "";
	char const * m = cjson_sval(cjson, "method", method);
	if(strncmp("substract", m, strlen(m)) == 0){

		int result = cjson_ival(cjson, "params[0]", 0) - cjson_ival(cjson, "params[1]", 0);

		sds out = 0;
		JSONRPC(out, RESP, req->parse, "{ \"jsonrpc\": \"2.0\",\"result\": %d, \"id\": %d }"
				, result, cjson_ival(cjson, "id", -1));  assert(out);
		rc = hp_io_write(&req->io, out, sdslen(out), (hp_free_t)sdsfree, 0);
	}
	else{
		sds out = 0;
		JSONRPC(out, RESP, req->parse, "{ \"jsonrpc\": \"2.0\",\"result\": %d, \"id\": %d }"
				, -1, cjson_ival(cjson, "id", -1)); assert(out);
		rc = hp_io_write(&req->io, out, sdslen(out), (hp_free_t)sdsfree, 0);
	}

	return rc;
}

static int test_http_server_on_dispatch(hp_io_t * io, void * hdr, void * body)
{
	assert(io);
	httprequest *req = (httprequest *)io;

	int rc = -1;
	char const * url = cfg("test_hp_io_t_main.url");
	if(strncmp(req->parse.u.url, url, strlen(url)) == 0){
		rc = reply_file(req, (url[0] == '/'? url + 1 : url), 200);
	}
	else if(strncmp(req->parse.u.url, "/jsonrpc", strlen("/jsonrpc")) == 0){
		if(!req->json) return -2;

		strcpy(req->parse.content_type, "json");
		req->parse.u.status_code = 200;
		rc = reply_jsonrpc(req, req->json);
	}
	else{
		rc = reply_file(req, "404.html", 404);
	}
	return rc;
}
static void test_http_server_on_delete(hp_io_t * io, int err, char const * errstr)
{
	httprequest * req = (httprequest *)io;
	assert(io && io->ioctx);

	char buf[64] = "";
	hp_log(std::cout, "%s: delete HTTP connection '%s:%d', IO total=%d\n", __FUNCTION__
			, hp_addr4name(&io->addr, ":", buf, sizeof(buf)), 0, hp_io_size(io->ioctx));

	request_uninit(req);
	free(req);
}

static int test_http_cli_on_parse(hp_io_t * io, char * buf, size_t * len
		, void ** hdrp, void ** bodyp)
{
	assert(io && buf && len && hdrp && bodyp);

	int rc;
	httpclient *c = (httpclient *)io;
	c->in = sdscatlen(c->in, buf, *len);

	c->parse.u.status_code = 0;
	JSONRPC_PARSE(c->in, c->parse, c->json, rc);
	if(rc <= 0) return rc;

	*len = 0;
	sdsclear(c->in);
	*hdrp = 0;
	*bodyp = 0;

	return rc;
}
static int test_http_cli_on_dispatch(hp_io_t * io, void * hdr, void * body)
{
	assert(io);
	httpclient *c = (httpclient *)io;
	int rc;

	if(c->parse.u.status_code != 200){
		hp_log(stderr, "%s: HTTP request failed: status_code=%d\n", __FUNCTION__, c->parse.u.status_code);

		if(c->flags == 3)
			c->flags = 4;

		//HTTP client close actively
		rc = -1;
	}
	else if(c->flags == 0) {
		//send json-rpc request
		sds out;
		http_parse parse = { .content_type = "json", .u = "/jsonrpc"};
		JSONRPC(out, REQ, parse,
				"{\"jsonrpc\": \"2.0\",\"method\": \"substract\",\"params\": [%d,%d],\"id\": %d}"
				, 42, 23, ++c->rpc_id);
		hp_log(std::cout, "%s: >> '%s'\n", __FUNCTION__, out);
		rc = hp_io_write(&c->io, out, sdslen(out), (hp_free_t)sdsfree, 0);

		c->flags = 1;
	}
	else if(c->flags == 1){

		assert(c->json);
		hp_log(std::cout, "%s: << '%s'\n", __FUNCTION__, cjson_cstr(c->json));

		//send another json-rpc request
		sds out;
		http_parse parse = { .content_type = "json", .u = "/jsonrpc"};
		JSONRPC(out, REQ, parse,
				"{\"jsonrpc\": \"2.0\",\"method\": \"this_jsonapi_not_exist\",\"params\": [%d,%d],\"id\": %d}"
				, 42, 23, ++c->rpc_id);
		hp_log(std::cout, "%s: >> '%s'\n", __FUNCTION__, out);
		rc = hp_io_write(&c->io, out, sdslen(out), (hp_free_t)sdsfree, 0);

		cJSON_Delete(c->json);
		c->flags = 2;
	}
	else if(c->flags == 2){
		assert(c->json);
		hp_log(std::cout, "%s: << '%s'\n", __FUNCTION__, cjson_cstr(c->json));

		sds out;
		http_parse parse = { .content_type = "html", .u = "/this_file_not_exist.html" };
		HTTP_REQ(out, parse, 0);
		rc = hp_io_write(&c->io, out, sdslen(out), (hp_free_t)sdsfree, 0);

		cJSON_Delete(c->json);
		c->flags = 3;
	}

	return rc;
}
static void test_http_cli_on_delete(hp_io_t * io)
{
	assert(io);
	httpclient *c = (httpclient *)io;
	client_uninit(c);
	free(c);
}

#endif//#if defined(LIBHP_WITH_CJSON) && defined(LIBHP_WITH_HTTP)

/////////////////////////////////////////////////////////////////////////////////////
// tests: close socket

typedef struct server {
	hp_io_t listenio;
	int test;
} server;

// for simplicity, we use the same "client" struct for both server and client side code
typedef struct client {
	hp_io_t io;
	sds in;
	server * s;
	char addr[64];
	int test;
} client;

hp_io_t * s_on_new(hp_io_t * cio, hp_sock_t fd)
{
	int rc;
	assert(cio && cio->ioctx && cio->user);
	server * s = (server *)cio->user;

	client * req = (client *)calloc(1, sizeof(client));
	req->s = s;
	req->in = sdsempty();
	req->io.addr = cio->addr;
	req->test = s->test;

	hp_iohdl niohdl = cio->iohdl;
	niohdl.on_new = 0;
	rc = hp_io_add(cio->ioctx, (hp_io_t *)req, fd, niohdl); assert(rc == 0);

	hp_log(std::cout, "%s: new TCP connection from '%s', IO total=%d\n", __FUNCTION__
			, hp_addr4name(&req->io.addr, ":", req->addr, sizeof(req->addr)), hp_io_size(cio->ioctx));

	if(req->test == 3){
		static char world[] = "hello";
		rc = hp_io_write(&req->io, world, strlen(world), 0, 0);
		assert(rc == 0);
	}

	return (hp_io_t *)req;
}

static int s_on_parse(hp_io_t * io, char * buf, size_t * len
		, void ** hdrp, void ** bodyp)
{
	assert(io && buf && len && hdrp && bodyp);

	int rc = 0;
	client *req = (client *)io;
	assert(req->s);
	*hdrp = 0;
	*bodyp = 0;

	if(req->test == 1){
		req->in = sdscatlen(req->in, buf, *len);
		if(strncmp(req->in, "hello", strlen("hello")) == 0) {
			rc = 1;
		} else rc = 0;
	}
	else if(req->test == 3){
		req->in = sdscatlen(req->in, buf, *len);
		if(strncmp(req->in, "world", strlen("world")) == 0) {
			rc = 1;
		} else rc = 0;
	}

	*len = 0;

	return rc;
}

static int s_on_dispatch(hp_io_t * io, void * hdr, void * body)
{
	int rc = 0;
	assert(io);
	client *req = (client *)io;
	assert(req->s);

	if(req->test == 1){
		if(strncmp(req->in, "hello", strlen("hello")) == 0) {

			static char world[] = "world";
			rc = hp_io_write(&req->io, world, strlen(world), 0, 0);
			assert(rc == 0);
			req->test = 0;
		}
	}
	else if(req->test == 3){
		if(strncmp(req->in, "world", strlen("world")) == 0) {

			hp_log(std::cout, "%s: server %d test done!\n", __FUNCTION__, io->id);
			req->test = 0;
			hp_shutdown(io->fd, 2);
		}
	}
	return rc;
}

static int s_on_loop(hp_io_t * io)
{
	int rc = 0;
	assert(io && io->ioctx);
	//stkip listenfd
	if(io->iohdl.on_new)
		return 0;

	struct client * req = (struct client *)io;
	if(req->test == 2){
		static int n = 100;
		--n;
		if(n <= 0){
			req->test = 0;
			hp_shutdown(io->fd, 2);
			rc = -1;
		}
	}

	return rc;
}

static void s_on_delete(hp_io_t * io, int err, char const * errstr)
{
	client * req = (client *)io;
	assert(io && io->ioctx);
	assert(req->s);
	server * s = req->s;

	hp_log(std::cout, "%s: delete TCP connection '%s', %d/'%s', IO total=%d\n", __FUNCTION__
			, req->addr, err, errstr, hp_io_size(io->ioctx));

	free(req);
}

static int c_on_parse(hp_io_t * io, char * buf, size_t * len
		, void ** hdrp, void ** bodyp)
{
	assert(io && buf && len && hdrp && bodyp);

	int rc = 0;
	client *c = (client *)io;
	assert(!c->s);
	*hdrp = 0;
	*bodyp = 0;

	assert(c->in);
	if(c->test == 1){
		c->in = sdscatlen(c->in, buf, *len);
		if(strncmp(c->in, "world", strlen("world")) == 0) {
			rc = 1;
		} else rc = 0;
	}
	else if(c->test == 3){
		c->in = sdscatlen(c->in, buf, *len);
		if(strncmp(c->in, "hello", strlen("hello")) == 0) {
			rc = 1;
		} else rc = 0;
	}
	*len = 0;

	return rc;
}
static int c_on_dispatch(hp_io_t * io, void * hdr, void * body)
{
	int rc = 0;
	assert(io);
	client *c = (client *)io;
	assert(!c->s);

	if(c->test == 1){
		if(strncmp(c->in, "world", strlen("world")) == 0) {
			hp_log(std::cout, "%s: client %d test done!\n", __FUNCTION__, io->id);
			// client done
			c->test = 0;
			hp_shutdown(io->fd, 2);
			rc = -1;
		}
	}
	else if(c->test == 3){
		if(strncmp(c->in, "hello", strlen("hello")) == 0) {
			rc = hp_io_write(&c->io, sdsnew("world"), strlen("world"), (hp_free_t)sdsfree, 0);
			assert(rc == 0);
			c->test = 0;
		}
	}

	return rc;
}
static void c_on_delete(hp_io_t * io, int err, char const * errstr)
{
	assert(io);
	client *c = (client *)io;
	assert(!c->s);
	hp_close(io->fd);

	hp_log(std::cout, "%s: %d disconnected err=%d/'%s'\n", __FUNCTION__, io->id, err, errstr);
}

/////////////////////////////////////////////////////////////////////////////////////

static int checkiftestrunning(const void *key, const void *ptr)
{
	assert(ptr);
	auto io = (hp_io_t *)ptr;
	//ignore listen fd
	return (!io->iohdl.on_new && ((client *)io)->test != 0);
}

static int client_server_echo_test(int test, int n)
{
	int rc, i;

	server sobj = { 0 }, * s = &sobj;
	hp_io_ctx ioctxobj, * ioctx = &ioctxobj;

	s->listenio.user = s;
	s->test = test;

	client * c = (client *)calloc(70000, sizeof(client));
	assert(c);

	hp_sock_t listen_fd = hp_tcp_listen(cfgi("tcp.port"));
	assert(hp_sock_is_valid(listen_fd));

#ifndef _MSC_VER
	hp_iohdl hdl = {
			.on_new = s_on_new,
			.on_parse = s_on_parse,
			.on_dispatch = s_on_dispatch,
			.on_loop = (test == 2 ? s_on_loop : 0),
			.on_delete = s_on_delete
	};

	hp_iohdl hdlc = {
			.on_new = 0/*NOt listen socket*/,
			.on_parse = c_on_parse,
			.on_dispatch = c_on_dispatch,
			.on_loop = 0,
			.on_delete = c_on_delete,
	};

	hp_ioopt opt = {
		.maxfd = 2 * n + 1,
		.timeout = 200, /* poll timeout */
#ifdef _MSC_VER
		.wm_user = 900, /* WM_USER + N */
		.hwnd = 0		/* hwnd */
		.nthreads = 0,
#endif /* _MSC_VER */
	};
#else
	hp_iohdl hdl = { s_on_new, s_on_parse, s_on_dispatch, (test == 2 ? s_on_loop : 0), s_on_delete };
	hp_iohdl hdlc = { 0/*NOt listen socket*/, c_on_parse, c_on_dispatch, 0, c_on_delete, };
	hp_ioopt opt = { 2 * n + 1, 200, /* poll timeout */ 900, /* WM_USER + N */ 0,		/* hwnd */ 0 };

#endif //#ifndef _MSC_VER

	rc = hp_io_init(ioctx, opt);
	assert(rc == 0);

	/* add listen socket */
	rc = hp_io_add(ioctx, &s->listenio, listen_fd, hdl);
	assert(rc == 0);

	/* add connect socket */
	for(i = 0; i < n; ++i){

		c[i].in = sdsempty();
		c[i].test = test;

		hp_sock_t fd = hp_tcp_connect("127.0.0.1", cfgi("tcp.port"));
		assert(hp_sock_is_valid(fd));
		rc = hp_io_add(ioctx, &c[i].io, fd, hdlc);
		assert(rc == 0);

		if(test == 1){
			rc = hp_io_write(&c[i].io, sdsnew("hello"), strlen("hello"), (hp_free_t)sdsfree, 0);
			assert(rc == 0);
		}
	}
	hp_log(std::cout, "%s: listening on TCP port=%d, waiting for connection ...\n", __FUNCTION__, cfgi("tcp.port"));
	/* run event loop, 1 for listenio  */
	int s_tdone = 0;
	for (;; ) {
		hp_io_run(ioctx, 200, 1);

		if(s_tdone && hp_io_size(ioctx) <= 1) break;
		if(s_tdone == 0){

			auto fc = (client *)hp_io_find(ioctx, 0, checkiftestrunning);
			if(!fc)
				s_tdone = 1;
		}
	}

	hp_io_uninit(ioctx);

	/*clear*/
	for(i = 0; c[i].in; ++i){
		sdsfree(c[i].in);
	}
	hp_close(listen_fd);
	free(c);

	return rc;
}

static void add_remove_test(int n)
{
	int rc, i;
	hp_io_t io[1024];
	hp_io_ctx ioctxbj = { 0 }, * ioctx = &ioctxbj;

#ifndef _MSC_VER
	hp_ioopt opt = {
		.maxfd = n,
		.timeout = 200, /* poll timeout */
#ifdef _MSC_VER
		.wm_user = 900, /* WM_USER + N */
		.hwnd = 0		/* hwnd */
		.nthreads = 0,
#endif /* _MSC_VER */
	};

	hp_iohdl hdl = {
			.on_new = s_on_new,
			.on_parse = s_on_parse,
			.on_dispatch = s_on_dispatch,
			.on_loop = 0,
			.on_delete = s_on_delete
	};

	hp_iohdl hdlc = {
			.on_new = 0/*NOt listen socket*/,
			.on_parse = c_on_parse,
			.on_dispatch = c_on_dispatch,
			.on_loop = 0,
			.on_delete = c_on_delete,
	};
#else
	hp_ioopt opt = { n, 200, /* poll timeout */ 900, /* WM_USER + N */ /* hwnd */ 0, };
	hp_iohdl hdl = { s_on_new, s_on_parse, s_on_dispatch, 0/*on_loop*/, s_on_delete };
	hp_iohdl hdlc = { 0/*NOt listen socket*/, c_on_parse, c_on_dispatch, 0,  c_on_delete, };
#endif //#ifndef _MSC_VER
	rc = hp_io_init(ioctx, opt); assert(rc == 0);
	assert(hp_io_size(ioctx) == 0);

	for(i = 0; i < n; ++i){
		hp_sock_t confd = hp_tcp_connect("127.0.0.1", cfgi("tcp.port"));
		assert(hp_sock_is_valid(confd));
		io[i].fd = confd;

		rc = hp_io_add(ioctx, io + i, io[i].fd, hdlc);
		assert(rc == 0);

		//add same fd
		rc = hp_io_add(ioctx, io + i, io[i].fd, hdl);
		assert(rc == 0);

		assert(hp_io_size(ioctx) == i + 1);
	}
	{
		hp_sock_t confd = hp_tcp_connect("127.0.0.1", cfgi("tcp.port"));
		assert(hp_sock_is_valid(confd));
		rc = hp_io_add(ioctx, io + i, n + 1, hdl);
		assert(rc < 0);
		hp_close(confd);
	}

	for(i = 0; i < n; ++i){
		hp_close(io[i].fd);
		rc = hp_io_rm(ioctx, io[i].id);
		assert(rc == 0);
	}

	assert(hp_io_size(ioctx) == 0);
	hp_io_uninit(ioctx);
}

/////////////////////////////////////////////////////////////////////////////////////

static int search_test_on_cmp(const void *key, const void *ptr)
{
	assert(key && ptr);
	auto fd = *(hp_sock_t *)key;
	auto io = (hp_io_t *)ptr;
	return io->fd == fd;
}

static void search_test(int n)
{
	int rc, i;
	hp_io_t io[1024];
	hp_io_ctx ioctxbj = { 0 }, * ioctx = &ioctxbj;

#ifndef _MSC_VER
	hp_ioopt opt = {
		.maxfd = n,
		.timeout = 200, /* poll timeout */
#ifdef _MSC_VER
		.wm_user = 900, /* WM_USER + N */
		.hwnd = 0		/* hwnd */
		.nthreads = 0,
#endif /* _MSC_VER */
	};

	hp_iohdl hdl = {
			.on_new = s_on_new,
			.on_parse = s_on_parse,
			.on_dispatch = s_on_dispatch,
			.on_loop = 0/*on_loop*/,
			.on_delete = s_on_delete
	};

	hp_iohdl hdlc = {
			.on_new = 0/*NOt listen socket*/,
			.on_parse = c_on_parse,
			.on_dispatch = c_on_dispatch,
			.on_loop = 0,
			.on_delete = c_on_delete,
	};
#else
	hp_ioopt opt = { n, 200, /* poll timeout */ 900, /* WM_USER + N */ 0,		/* hwnd */ 0, };
	hp_iohdl hdl = { s_on_new, s_on_parse, s_on_dispatch, 0/*on_loop*/, s_on_delete };
	hp_iohdl hdlc = { 0/*NOt listen socket*/, c_on_parse, c_on_dispatch, 0, c_on_delete, };
#endif //#ifndef _MSC_VER
	rc = hp_io_init(ioctx, opt); assert(rc == 0);
	assert(hp_io_size(ioctx) == 0);

	for(i = 0; i < n; ++i){
		hp_sock_t confd = hp_tcp_connect("127.0.0.1", 1);
		hp_assert(hp_sock_is_valid(confd), "i=%i", i);
		io[i].fd = confd;

		assert(!hp_io_find(ioctx, &io[i].fd, search_test_on_cmp));
		rc = hp_io_add(ioctx, io + i, io[i].fd, hdlc);
		assert(rc == 0);

		assert(hp_io_find(ioctx, &io[i].fd, search_test_on_cmp));
		//add same fd
		rc = hp_io_add(ioctx, io + i, io[i].fd, hdl);
		assert(rc == 0);

		assert(hp_io_size(ioctx) == i + 1);

		rc = hp_io_rm(ioctx, io[i].id);
		assert(rc == 0);

		assert(!hp_io_find(ioctx, &io[i].fd, search_test_on_cmp));

		rc = hp_io_add(ioctx, io + i, io[i].fd, hdl);
		assert(rc == 0);
	}

	{
		hp_sock_t confd = hp_tcp_connect("127.0.0.1", 1);
		hp_assert(hp_sock_is_valid(confd), "i=%i", i);

		rc = hp_io_add(ioctx, io + i, confd, hdl);
		assert(rc < 0);

		hp_close(confd);
	}

	assert(hp_io_find(ioctx, &io[0].fd, search_test_on_cmp));
	assert(hp_io_find(ioctx, &io[n/2].fd, search_test_on_cmp));
	assert(hp_io_find(ioctx, &io[n-1].fd, search_test_on_cmp));
	assert(!hp_io_find(ioctx, (void *)search_test_on_cmp, search_test_on_cmp));

	for(i = 0; i < n; ++i){
		rc = hp_io_rm(ioctx, io[i].id);
		assert(rc == 0);
		hp_close(io[i].fd);
	}

	assert(hp_io_size(ioctx) == 0);
	hp_io_uninit(ioctx);
}

/////////////////////////////////////////////////////////////////////////////////////

int test_hp_io_t_main(int argc, char ** argv)
{
	int rc, i;
	//add,remove test
	{
		add_remove_test(1);
		add_remove_test(2);
		add_remove_test(3);
	}
	// search test
	{
		search_test(1);
		search_test(2);
		search_test(3);
		search_test(500);
	}
	/*
	 * simple echo server, client sent "hello", server reply with "world"
	 *  */
	{
		rc = client_server_echo_test(1, 1); assert(rc == 0);
		rc = client_server_echo_test(1, 2); assert(rc == 0);
		rc = client_server_echo_test(1, 3); assert(rc == 0);
		rc = client_server_echo_test(1, 500); assert(rc == 0);
	}
	/* on_loop test:
	 *
	 * 1.client connect to server OK;
	 * 2.server::on_loop return -1 later after client connected;
	 * 3.client should detect read/write error later;
	 *  */
	{
		rc = client_server_echo_test(2, 1); assert(rc == 0);
		rc = client_server_echo_test(2, 2); assert(rc == 0);
		rc = client_server_echo_test(2, 3); assert(rc == 0);
		rc = client_server_echo_test(2, 500); assert(rc == 0);
	}
	/* server client on_read,write error, server close this client
	 *
	 * 1.client connect to server OK;
	 * 2.server close this client;
	 * 3.client should detect read/write error later;
	 *  */
	{
		rc = client_server_echo_test(3, 1); assert(rc == 0);
		rc = client_server_echo_test(3, 2); assert(rc == 0);
		rc = client_server_echo_test(3, 3); assert(rc == 0);
		rc = client_server_echo_test(3, 500); assert(rc == 0);
	}

	/* server server on_read_error/on_write_error:
	 *
	 * 1.client connect to server OK;
	 * 2.client close the socket after connect;
	 * 2.server should detect read/write error later for this client;
	 *  */
	{
//		rc = client_server_echo_test(4, 1); assert(rc == 0);
//		rc = client_server_echo_test(4, 2); assert(rc == 0);
//		rc = client_server_echo_test(4, 3); assert(rc == 0);
//		rc = client_server_echo_test(4, 500); assert(rc == 0);
	}
	{
		char buf[1024] = "";
		int n = sscanf("\r\n{\"jsonrpc\": \"2.0\",\"method\": \"this_jsonapi_not_exist\",\"params\": [%d,%d],\"id\": %d}\r\n",
				"\r\n%[^\r\n]\r\n", buf);
		n = 0;
	}
	/* simple json-rpc server: HTTP client and HTTP server */
#if !defined(_MSC_VER) && defined(LIBHP_WITH_CJSON) && defined(LIBHP_WITH_HTTP)
	{
		{
			sds file = sdscatprintf(sdsempty(), "%s/%s", hp_config_test("web_root"), "index.html");
			hp_assert_path(file, REG);
			sdsfree(file);
		}
		{
			sds file = sdscatprintf(sdsempty(), "%s/%s", hp_config_test("web_root"), "404.html");
			hp_assert_path(file, REG);
			sdsfree(file);
		}
		hp_io_ctx ioctxobj, *ioctx = &ioctxobj;
		httpclient cobj, *c = &cobj;
		httpserver sobj, *s = &sobj;

		hp_ioopt opt = {
			.maxfd = 0,
			.timeout = 200,
#ifdef _MSC_VER
			.wm_user = 900, /* WM_USER + N */
			.hwnd = 0		/* hwnd */
			.nthreads = 0,
#endif /* _MSC_VER */
		};
		rc = hp_io_init(ioctx, opt); assert(rc == 0);
		rc = client_init(c); assert(rc == 0);
		rc = server_init(s); assert(rc == 0);

		hp_sock_t listen_fd = hp_tcp_listen(cfgi("tcp.port")); assert(listen_fd > 0);
		hp_sock_t confd = hp_tcp_connect(cfg("ip"),
					cfgi("tcp.port")); assert(confd > 0);

		hp_iohdl hdl = {
				.on_new = test_http_server_on_new,
				.on_parse = test_http_server_on_parse,
				.on_dispatch = test_http_server_on_dispatch,
				.on_loop = 0,
				.on_delete = test_http_server_on_delete
		};

		hp_iohdl hdlc = {
				.on_new = 0/*test_http_cli_on_new*/,
				.on_parse = test_http_cli_on_parse,
				.on_dispatch = test_http_cli_on_dispatch,
				.on_loop = 0,
				.on_delete = 0/*test_http_cli_on_delete*/,
		};

		/* add HTTP server listen socket */
		rc = hp_io_add(ioctx, &s->listenio, listen_fd, hdl);
		assert(rc == 0);
		/* add HTTP connect socket */
		rc = hp_io_add(ioctx, &c->io, confd, hdlc);
		assert(rc == 0);

		//HTTP client
		//send s impel HTTP request first
		sds out;
		http_parse parse = { .content_type = "html"};
		strncpy(parse.u.url, cfg("test_hp_io_t_main.url"), sizeof(parse.u.url));

		HTTP_REQ(out, parse, 0);
		rc = hp_io_write(&c->io, out, sdslen(out), (hp_free_t)sdsfree, 0);
		assert(rc == 0);
		hp_log(std::cout, "%s: HTPP request sent:\n%s", __FUNCTION__, out);

		hp_log(std::cout, "%s: listening on TCP port=%d, waiting for connection ...\n", __FUNCTION__, cfgi("tcp.port"));
		/* run event loop */
		int quit = 3;
		for (; quit > 0;) {
			hp_io_run(ioctx, 200, 0);

			if(hp_io_size(ioctx) == 1)
				--quit;
		}
		/*clear*/
		hp_close(confd);
		hp_close(listen_fd);
		server_uninit(s);
		client_uninit(c);
		hp_io_uninit(ioctx);
	}
#endif //#if defined(LIBHP_WITH_CJSON) && defined(LIBHP_WITH_HTTP)

	return 0;
}

#endif /* NDEBUG */
