/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2021/4/9
 *
 * networking I/O, using epoll on *nix and IOCP on Win32
 * */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "Win32_Interop.h"
#include "redis/src/adlist.h" /* list */
#include "sdsinc.h"		/* sds */
#include "hp_io_t.h"
#include "hp_err.h"
#include <assert.h> /* assert */
#include <errno.h> /*  */
#include <limits.h> /* INT_MAX */
#include <stdio.h>
#include <string.h>
#include "hp_log.h"
#include "str_dump.h" /*dumpstr*/

#if !defined(_WIN32) && !defined(_MSC_VER)
#include <poll.h>  /* poll */
#include <sys/fcntl.h>  /* fcntl */
#include <arpa/inet.h>	/* inet_ntop */
#endif /* _MSC_VER */

#define safe_call(fn, args...) do{ if(fn) { fn(args); } } while(0)
/////////////////////////////////////////////////////////////////////////////////////////
static int hp_io_t_internal_on_data(hp_io_t * io, char * buf, size_t * len)
{
	assert(io && buf && len);
	int rc;
	if (!(io->iohdl.on_parse)) {
		*len = 0;
	}

	if (!(*len > 0))
		return 0;

	for (; *len > 0;) {
		hp_iohdr_t * iohdr = 0;
		char * body = 0;
		int n = io->iohdl.on_parse(io, buf, len, &iohdr, &body);
		if (n < 0) {
			*len = 0;
			return n;
		}
		else if (n == 0)
			return 0;
		if (io->iohdl.on_dispatch){
			rc = io->iohdl.on_dispatch(io, iohdr, body);
			if(rc != 0)
				return -1;
		}
	}
	return 0;
}

//static int hp_io_internal_on_error(hp_io_t * io, int err, char const * errstr)
//{
//	assert(io && io->iohdl);
////	list * li = 0;
////#if !defined(__linux__) && !defined(_MSC_VER)
////#elif !defined(_MSC_VER)
////	assert(io->ioctx);
////	li = (list *)hp_epoll_arg();
////#else
////	assert(io->ioctx->iocp && io->ioctx->iocp->user);
////	li = (list *)io->ioctx->iocp->user;
////#endif /* _MSC_VER */
//
//	hp_log((err != 0 ? stderr : stdout), "%s: close session, fd=%d, err=%d/'%s', total=%d\n"
//		, __FUNCTION__, io->ed.fd
//		, err, errstr, 0/*listLength(li)*/);
//
//	if(io->iohdl.on_delete)
//		io->iohdl.on_delete(io);
//	return 0;
//}

#if !defined(__linux__) && !defined(_MSC_VER)
static int hp_io_t_internal_on_accept(void * arg, struct pollfd * pfd)
#elif !defined(_MSC_VER)
static int hp_io_t_internal_on_accept(struct epoll_event * ev)
#else
static hp_sock_t hp_io_t_internal_on_accept(hp_iocp * iocpctx, int index)
#endif /* _MSC_VER */
{
#if !defined(__linux__) && !defined(_MSC_VER)
	assert(pfd);
	hp_io_ctx * listenio = 0;
	hp_sock_t fd = pfd->fd;
#elif !defined(_MSC_VER)
	hp_io_t * io = (hp_io_t * )hp_epoll_arg(ev);
	hp_sock_t fd = hp_epoll_fd(ev);
#else
	hp_io_ctx * listenio = (hp_io_ctx *)hp_iocp_arg(iocpctx, index);
	hp_sock_t fd = listenio->fd;
#endif /* _MSC_VER */
	assert(io);
	int rc;

	for(;;){
		struct sockaddr_in clientaddr = { 0 };
		socklen_t len = sizeof(clientaddr);
		hp_sock_t confd = accept(fd, (struct sockaddr *)&clientaddr, &len);
#ifndef _MSC_VER
		if(!hp_sock_is_valid(confd)){
			if (errno == EINTR || errno == EAGAIN) { return 0; }
			hp_log(stderr, "%s: accept failed, errno=%d, error='%s'\n", __FUNCTION__, errno, strerror(errno));
#else
		if(!hp_sock_is_valid(confd)){
			int err = WSAGetLastError();
			if(WSAEWOULDBLOCK == err) { return 0; }
			hp_err_t errstr = "accept: %s";
			hp_log(stderr, "%s: accept failed, errno=%d, error='%s'\n", __FUNCTION__, errno, hp_err(errno, errstr));
#endif /* _MSC_VER */
			return -1;
		}

#if (!defined(_WIN32) && !defined(_MSC_VER)) || (defined LIBHP_WITH_WIN32_INTERROP)
		if (fcntl(confd, F_SETFL, O_NONBLOCK) < 0)
#else
		u_long sockopt = 1;
		if (ioctlsocket(confd, FIONBIO, &sockopt) < 0)
#endif /* LIBHP_WITH_WIN32_INTERROP */
		{ hp_sock_close(confd); continue; }

		char is_c = 0;
		hp_io_t * nio = io->iohdl.on_new? io->iohdl.on_new(io, confd) : 0;
		if(nio){
			/* nio is NOT a listen io */
			hp_iohdl niohdl = io->iohdl;
			niohdl.on_new = 0;

			rc = hp_io_add(io->ioctx, nio, confd, niohdl);
			if (rc != 0) {
				safe_call(niohdl.on_delete, nio);
				is_c = 1;
			}
		}
		else { is_c = 1; }

		if (is_c) {
			hp_sock_close(confd);
			continue;
		}
	}
	
	return 0;
}

static int iolist_match(void *ptr, void *key)
{
	assert(ptr);
	if(!key) { return 0; }
	hp_io_t * io = (hp_io_t *)ptr, * k = (hp_io_t *)key;
	return (k->id > 0 && io->id == k->id) ||
			(hp_sock_is_valid(hp_io_fd(k)) && hp_io_fd(io) == hp_io_fd(k));
}

int hp_io_init(hp_io_ctx * ioctx)
{
	if(!(ioctx))
		return -1;
	int rc = 0;
	memset(ioctx, 0, sizeof(hp_io_ctx));
	ioctx->iolist = listCreate();
	listSetMatchMethod(ioctx->iolist, iolist_match);

#if defined(__linux__)
	/* init epoll */
	if (hp_epoll_init(&ioctx->efds, 65535) != 0)
		return -5;
	ioctx->efds.arg = ioctx->iolist;

#elif defined(_MSC_VER)
	ioctx->fd = opt->listen_fd;

	rc = hp_iocp_init(&(ioctx->iocp), 0, WM_USER + opt->wm_user, 200, ioctx->iolist);
	assert(rc == 0);

	int tid = (int)GetCurrentThreadId();
	rc = hp_iocp_run(&ioctx->iocp, tid, opt->hwnd);
	assert(rc == 0);

	if(hp_sock_is_valid(opt->listen_fd)){
		int index = hp_iocp_add(&ioctx->iocp, 0, 0, opt->listen_fd, hp_io_t_internal_on_accept, 0, 0, ioctx);
		rc = (index >= 0 ? 0 : index);
	}
#elif !defined(_WIN32)
	if(hp_poll_init(&ioctx->fds, 65535) != 0)
		return -5;
	if (opt->listen_fd >= 0) {
		rc = hp_poll_add(&ioctx->fds, opt->listen_fd, POLLIN, hp_io_t_internal_on_accept, ioctx); assert(rc == 0);
	}
#else
	//select
#endif /* _MSC_VER */

	return rc;
}

int hp_io_write(hp_io_t * io, void * buf, size_t len, hp_io_free_t free, void * ptr)
{
	int rc;
#if (!defined(_MSC_VER) && !defined(_WIN32))
	rc = hp_eto_add(&io->eto, buf, len,  free, ptr);
#elif defined(_MSC_VER)
	rc = hp_iocp_write(listenio->ioctx->iocp, listenio->index, buf, len, free, ptr);
#endif /* _MSC_VER */
	return rc;
}

#if !defined(__linux__) && !defined(_MSC_VER)
#elif !defined(_MSC_VER)
static int hp_io_epoll_before_wait(struct hp_epoll * efds)
{
	assert(efds && efds->arg);

	listIter * iter = 0;
	listNode * node;

	/* delete node in loop is fine, but only the iter node */
	iter = listGetIterator((list *)efds->arg, 0);
	for(node = 0; (node = listNext(iter));){
		hp_io_t * io = (hp_io_t *)listNodeValue(node);
		assert(io);
		hp_eti_try_read(&io->eti, &io->ed);
	}
	listReleaseIterator(iter);

	iter = listGetIterator((list *)efds->arg, 0);
	for(node = 0; (node = listNext(iter));){
		hp_io_t * io = (hp_io_t *)listNodeValue(node);
		assert(io);
		hp_eto_try_write(&io->eto, &io->ed);
	}
	listReleaseIterator(iter);

	iter = listGetIterator((list *)efds->arg, 0);
	for(node = 0; (node = listNext(iter));){
		hp_io_t * io = (hp_io_t *)listNodeValue(node);
		assert(io);
		if (io->iohdl.on_loop) {
			io->iohdl.on_loop(io);
		}
	}
	listReleaseIterator(iter);

	return 0;
}
#endif /* _MSC_VER */

int hp_io_run(hp_io_ctx * ioctx, int interval, int mode)
{
	int rc = 0;
#if defined(__linux__)
	rc = hp_epoll_run(&ioctx->efds, interval, (mode != 0? hp_io_epoll_before_wait : (void *)-1));
	if(mode == 0) { hp_io_epoll_before_wait(&ioctx->efds); }
#elif defined(_MSC_VER)
	for (;;) {
		MSG msgobj = { 0 }, *msg = &msgobj;
		for (; PeekMessage((LPMSG)msg, (HWND)-1
				, (UINT)HP_IOCP_WM_FIRST(&ioctx->iocp), (UINT)HP_IOCP_WM_LAST(&ioctx->iocp)
				, PM_REMOVE | PM_NOYIELD); ) {

			rc = hp_iocp_handle_msg(&ioctx->iocp, msg->message, msg->wParam, msg->lParam);
		}

		/* user loop */
		listIter * iter = 0;
		listNode * node;

		iter = listGetIterator(ioctx->iolist, 0);
		for (node = 0; (node = listNext(iter));) {
			hp_io_t * listenio = (hp_io_t *)listNodeValue(node);
			assert(listenio);
			hp_iocp_try_write(&ioctx->iocp, listenio->index);
		}
		listReleaseIterator(iter);

		iter = listGetIterator(ioctx->iolist, 0);
		for (node = 0; (node = listNext(iter));) {
			hp_io_t * listenio = (hp_io_t *)listNodeValue(node);
			assert(listenio);

			if (listenio->iohdl && listenio->iohdl.on_loop) {
				listenio->iohdl.on_loop(listenio);
			}
		}
		listReleaseIterator(iter);

		if(mode == 0)
			break;
	}
#elif !defined(_WIN32) && !defined(_MSC_VER)
	rc = hp_poll_run(&ioctx->fds, interval, (mode != 0? /*hp_io_epoll_before_wait*/0 : (void *)-1));
//	if(mode == 0) { hp_io_epoll_before_wait(&ioctx->efds); }
#else
#endif /* _MSC_VER */

	return rc;
}

int hp_io_uninit(hp_io_ctx * ioctx)
{
	if(!ioctx)
		return -1;
#if defined(__linux__)
//	hp_epoll_del(&ioctx->efds, ioctx->epolld.fd, EPOLLIN, &ioctx->epolld);
	hp_epoll_uninit(&ioctx->efds);
//	hp_sock_close(ioctx->epolld.fd);
#elif defined(_MSC_VER)
	hp_iocp_uninit(&ioctx->iocp);
#elif !defined(_WIN32) && !defined(_MSC_VER)
	hp_poll_uninit(&ioctx->fds);
#else
#endif /* _MSC_VER */
	listRelease(ioctx->iolist);

	return 0;
}

hp_sock_t hp_io_fd(hp_io_t * io)
{
	if(!io) return hp_sock_invalid;
#if defined(_MSC_VER)
#elif defined(__linux__)
	return io->ed.fd;
#elif !defined(_WIN32)
#else
#endif /* _MSC_VER */
}

int hp_io_count(hp_io_ctx * ioctx)
{
	return ioctx? listLength(ioctx->iolist) : 0;
}
/////////////////////////////////////////////////////////////////////////////////////

#if !defined(__linux__) && !defined(_MSC_VER)
#elif !defined(_MSC_VER)

//FIXME: io->on_error
static void hp_io_t_internal_on_error(int err, char const * errstr, void * arg)
{
	struct epoll_event * ev = (struct epoll_event *)arg;
	assert(ev);
	ev->events = 0;

	hp_io_t * io = (hp_io_t *)hp_epoll_arg(ev);
	assert(io && io->ioctx);


	list * li = (list *)io->ioctx->iolist;
	hp_io_t key = { 0 };
	key.id = io->id;

	listNode * node = listSearchKey(li, &key);
	if(node){ listDelNode(li, node); }

	hp_epoll_del(&io->ioctx->efds, io->ed.fd, EPOLLIN | EPOLLOUT | EPOLLET, &io->ed);
	hp_sock_close(io->ed.fd);

	hp_eti_uninit(&io->eti);
	hp_eto_uninit(&io->eto);

	safe_call(io->iohdl.on_delete, io);
//	io->iohdl.on_error(io, err, errstr);
}

static int hp_io_t_internal_io_cb(struct epoll_event * ev)
{
	assert(ev);

	if((ev->events & EPOLLERR)){
		hp_io_t * io = (hp_io_t *)hp_epoll_arg(ev);
		assert(io);

		hp_io_t_internal_on_error(EPOLLERR, "EPOLLERR", ev);
		return 0;
	}

	if((ev->events & EPOLLIN)){
		hp_io_t * io = (hp_io_t *)hp_epoll_arg(ev);
		assert(io);

		hp_eti_read(&io->eti, hp_io_fd(io), ev);
	}

	if((ev->events & EPOLLOUT)){
		hp_io_t * io = (hp_io_t *)hp_epoll_arg(ev);
		assert(io);

		hp_eto_write(&io->eto, hp_io_fd(io), ev);
	}

	return 0;
}

static int hp_io_t_internal_on_pack(char* buf, size_t* len, void * arg)
{
	struct epoll_event * ev = (struct epoll_event *)arg;
	assert(ev);
	hp_io_t * io = (hp_io_t *)hp_epoll_arg(ev);
	assert(io);

	if(!(hp_io_t_internal_on_data(io, buf, len) >= 0)){
		 shutdown(io->ed.fd, SHUT_RDWR);
		 return EBADF;
	}else return EAGAIN;
}

static void hp_io_t_internal_on_werror(struct hp_eto * eto, int err, void * arg)
{
	return hp_io_t_internal_on_error(err, strerror(err), arg);
}

static void hp_io_t_internal_on_rerror(struct hp_eti * eti, int err, void * arg)
{
	return hp_io_t_internal_on_error(err, (err == 0? "EOF" : strerror(err)), arg);
}

#else
static int hp_io_t_internal_on_error(hp_iocp * iocpctx, int index, int err, char const * errstr)
{
	assert(iocpctx);
	hp_io_t * listenio = (hp_io_t *)hp_iocp_arg(iocpctx, index);
	assert(listenio);
	assert(listenio->ioctx->iocp && listenio->ioctx->iocp->user);
	list * li = (list *)listenio->ioctx->iocp->user;
      
	hp_io_t key = { 0 };
	key.id = listenio->id;

	listNode * node = listSearchKey(li, &key);
	if (node) { listDelNode(li, node); }

//	return io->on_error(io, err, errstr);
	return 0;
}

int hp_io_t_internal_on_data(hp_iocp * iocpctx, int index, char * buf, size_t * len)
{
	assert(iocpctx && buf && len);
	hp_io_t * listenio = (hp_io_t *)hp_iocp_arg(iocpctx, index);
	assert(listenio);

	return listenio->on_data(listenio, buf, len);
}
#endif /* _MSC_VER */

/////////////////////////////////////////////////////////////////////////////////////

int hp_io_add(hp_io_ctx * ioctx, hp_io_t * io, hp_sock_t fd, hp_iohdl iohdl)
{
	int rc = 0;
	if(!(ioctx && io && (iohdl.on_new || iohdl.on_parse)))
		return -1;
	memset(io, 0, sizeof(hp_io_t));

	io->id = ++ioctx->ioid;
	io->ioctx = ioctx;
	io->iohdl = iohdl;

	if(ioctx->ioid == INT_MAX - 1)
		ioctx->ioid = 0;
#if !defined(__linux__) && !defined(_MSC_VER)
#elif !defined(_MSC_VER)

	if(io->iohdl.on_new){
		hp_epolld_set(&io->ed, fd, hp_io_t_internal_on_accept, io);
		rc = hp_epoll_add(&ioctx->efds, fd, EPOLLIN, &io->ed);
	}
	else if(io->iohdl.on_parse){
		hp_eti_init(&io->eti, 1024 * 128);
		hp_eto_init(&io->eto, 8);

		struct hp_eto * eto = &io->eto;
		eto->write_error = hp_io_t_internal_on_werror;

		struct hp_eti * eti = &io->eti;
		eti->read_error = hp_io_t_internal_on_rerror;
		eti->pack = hp_io_t_internal_on_pack;

		hp_epolld_set(&io->ed, fd, hp_io_t_internal_io_cb, io);
		/* try to add to epoll event system */
		rc = hp_epoll_add(&ioctx->efds, fd,  EPOLLIN | EPOLLOUT |  EPOLLET, &io->ed);
	}
#else
	listenio->index = hp_iocp_add(&ioctx->iocp, 0, 0, listenio->fd, 0, hp_io_t_internal_on_data, hp_io_t_internal_on_error, listenio);
	rc = (listenio->index >= 0? 0 : listenio->index);
#endif /* _MSC_VER */

	listAddNodeTail(ioctx->iolist, io);

	return rc;
}

/////////////////////////////////////////////////////////////////////////////////////
#ifndef NDEBUG
#include <time.h>   /*difftime*/
#include "hp_net.h" /* hp_net_connect */
#include "klist.h"  /* list_head */
#include "sdsinc.h" /* sds */
#include "hp_config.h"
#include "hp_cjson.h"
#include "string_util.h"
#include "hp_assert.h"
#define cfg hp_config_test
#define cfgi(key) atoi(hp_config_test(key))
/////////////////////////////////////////////////////////////////////////////////////

typedef struct http_parse{
	union {
		char url[512];
		int status_code;
	} u;
	char content_type[64];
} http_parse;

static inline char * status_code_cstr(int code)
{
	switch(code){
		case 200 : return "OK";
		case 404 : return "Not Found";
		case 500 : return "Internal Server Error";
		default: return "Unknown";
	}
}
/////////////////////////////////////////////////////////////////////////////////////

/**
 * test:
 * simple HTTP request and response
 */
#define HTTP_REQ(out, parse, content_length) do {                \
	out = sdscatprintf(sdsempty(),                               \
	"GET %s HTTP/1.1\r\n"                                        \
	"Host: %s:%d\r\n"                                            \
	"Content-Type: application/%s\r\n"                           \
	"Content-Length: %d\r\n\r\n"                                 \
	, parse.u.url, cfg("test_hp_io_t_main.ip")                   \
	, cfgi("test_hp_io_t_main.port")                             \
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
	, cfg("test_hp_io_t_main.ip")                                \
	, cfgi("test_hp_io_t_main.port")                             \
	, (parse.content_type)      \
	, (int)(content_length)); } while(0)

#define JSONRPC(out, T, parse, fmt, args...) do {                \
	sds json = sdscatprintf(sdsempty(), fmt, ##args);            \
	sds http;                                  					 \
	HTTP_##T(http, parse, sdslen(json));                         \
	out = sdscatprintf(sdsempty(), "%s%s\r\n", http, json);      \
	sdsfree(json); sdsfree(http); } while(0)

/**
 * rc: >0 OK, ==0 need more data, <0 failed
 * */
#define JSONRPC_PARSE(in, parse, json, rc) do {                   \
	rc = -1; json = 0;                                            \
	char ip[64] = "(null)", scstr[62] = "";                       \
	char * jsonstr = malloc(1024 * 1024 * 4);                     \
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
			strcmp(ip, cfg("test_hp_io_t_main.ip")) == 0 &&       \
			cfgi("test_hp_io_t_main.port") == port) {             \
		rc = 1;                                                   \
		if(strncasecmp(parse.content_type, "json", 4) == 0 &&     \
				jsonstr[0] && content_length >= 2) {              \
			json = cJSON_Parse(jsonstr);                          \
			if(!json) rc = -2;                                    \
		}                                                         \
	} else { rc = -1;  } free(jsonstr); }while(0)

/////////////////////////////////////////////////////////////////////////////////////

//
//#define JSONRPC_SUBSTRACT_RESP(out, a, b) do {                    \
//	sds JSONRPC_SUBSTRACT_http;                                  \
//	sds JSONRPC_SUBSTRACT_json = sdscatprintf(sdsempty(),        \
//	"{"                                                          \
//		"\"jsonrpc\": \"2.0\","                                  \
//		"\"result\": \"substract\","                             \
//		"\"params\": [%d,%d]"                                    \
//	"}", (a), (b));                                              \
//	HTTP_REQ(JSONRPC_SUBSTRACT_http, "/substract", (int)sdslen(JSONRPC_SUBSTRACT_json)); \
//	out = sdscatprintf(sdsempty(), "%s%s\r\n\r\n",               \
//			JSONRPC_SUBSTRACT_http, JSONRPC_SUBSTRACT_json);     \
//	sdsfree(JSONRPC_SUBSTRACT_json); sdsfree(JSONRPC_SUBSTRACT_http); } while(0)

//#define JSONRPC_SUBSTRACT_REQ(out, a, b) do{                     \
//	out = sdscatprintf(sdsempty(),                               \
//	"GET /substract HTTP/1.1\r\n"                                \
//	"Host: %s:%d\r\n"                                            \
//	"Content-Type: application/json\r\n"                         \
//	"Content-Length: %d\r\n"                                     \
//	"%s\r\n"                                                     \
//	"\r\n"                                                       \
//	, cfg("test_hp_io_t_main.ip")                     \
//	, atoi(cfg("test_hp_io_t_main.port"))             \
//	, (int)sdslen(JSONRPC_SUBSTRACT_json), JSONRPC_SUBSTRACT_json\
//	); sdsfree(JSONRPC_SUBSTRACT_json);}while(0)
//
//#define JSONRPC_SUBSTRACT_RESP(out, a, b) do{                    \
//	sds JSONRPC_SUBSTRACT_json = sdscatprintf(sdsempty(),        \
//	"{"                                                          \
//		"\"jsonrpc\": \"2.0\","                                  \
//		"\"method\": \"substract\","                             \
//		"\"params\": [%d,%d]"                                    \
//	"}", (a), (b));                                              \
//	out = sdscatprintf(sdsempty(),                               \
//	"GET /substract HTTP/1.1\r\n"                                \
//	"Host: %s:%d\r\n"                                            \
//	"Content-Type: application/json\r\n"                         \
//	"Content-Length: %d\r\n"                                     \
//	"%s\r\n"                                                     \
//	"\r\n"                                                       \
//	, cfg("test_hp_io_t_main.ip")                     \
//	, atoi(cfg("test_hp_io_t_main.port"))             \
//	, (int)sdslen(JSONRPC_SUBSTRACT_json), JSONRPC_SUBSTRACT_json\
//	); sdsfree(JSONRPC_SUBSTRACT_json);}while(0)


/////////////////////////////////////////////////////////////////////////////////////
typedef struct httpclient {
	hp_io_t io;
	sds in;
	http_parse parse;
	cJSON * json;
	int flags;
	int rpc_id;
	struct list_head li;
} httpclient;
typedef struct httprequest {
	hp_io_t io;	/*MUST be the first member*/
	sds in;
	http_parse parse;
	cJSON * json;
	struct list_head li;
} httprequest;
typedef struct httpserver {
	hp_io_t listenio;
	struct list_head clients;
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
	INIT_LIST_HEAD(&s->clients);
	return 0;
}
static void server_uninit(httpserver * s)
{

	struct list_head * pos, *next;;
	list_for_each_safe(pos, next, &s->clients) {
		httpclient* node = (httpclient *)list_entry(pos, httpclient, li);
		assert(node);
		list_del(&node->li);
	}
}

/////////////////////////////////////////////////////////////////////////////////////

hp_io_t * test_http_server_on_new(hp_io_t * cio, hp_sock_t fd)
{
	assert(cio && cio->ioctx);
	httprequest * req = (httprequest *)malloc(sizeof(httprequest));
	int rc = request_init(req); assert(rc == 0);

	char buf[64] = "";
	hp_log(stdout, "%s: new HTTP connection from '%s', IO total=%d\n", __FUNCTION__, hp_get_ipport_cstr(fd, buf),
			hp_io_count(cio->ioctx));
	return (hp_io_t *)req;
}

int test_http_server_on_parse(hp_io_t * io, char * buf, size_t * len
		, hp_iohdr_t ** hdrp, char ** bodyp)
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

	rc = hp_io_write(&req->io, httphdr, sdslen(httphdr), (hp_io_free_t)sdsfree, 0);
	rc = (rc == 0)? hp_io_write(&req->io, html, sdslen(html), (hp_io_free_t)sdsfree, 0) : -1;
	rc = (rc == 0)? hp_io_write(&req->io, "\r\n\r\n", 4, 0, 0) : -2;

	sdsfree(file);
	return rc;
}

static int reply_jsonrpc(httprequest *req, cJSON * cjson)
{
	assert(req && cjson);
	int rc = -1;
	char const * m = cjson_sval(cjson, "method", "");
	if(strncmp("substract", m, strlen(m)) == 0){

		int result = cjson_ival(cjson, "params[0]", 0) - cjson_ival(cjson, "params[1]", 0);

		sds out = 0;
		JSONRPC(out, RESP, req->parse, "{ \"jsonrpc\": \"2.0\",\"result\": %d, \"id\": %d }"
				, result, cjson_ival(cjson, "id", -1));  assert(out);
		rc = hp_io_write(&req->io, out, sdslen(out), (hp_io_free_t)sdsfree, 0);
	}
	else{
		sds out = 0;
		JSONRPC(out, RESP, req->parse, "{ \"jsonrpc\": \"2.0\",\"result\": %d, \"id\": %d }"
				, -1, cjson_ival(cjson, "id", -1)); assert(out);
		rc = hp_io_write(&req->io, out, sdslen(out), (hp_io_free_t)sdsfree, 0);
	}

	return rc;
}

int test_http_server_on_dispatch(hp_io_t * io, hp_iohdr_t * imhdr, char * body)
{
	assert(io);
	httprequest *req = (httprequest *)io;

	int rc = -1;
	if(strncmp(req->parse.u.url, "/index.html", strlen("/index.html")) == 0){
		rc = reply_file(req, "index.html", 200);
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
void test_http_server_on_delete(hp_io_t * io)
{
	httprequest * req = (httprequest *)io;
	assert(io && io->ioctx);

	char buf[64] = "";
	hp_log(stdout, "%s: delete HTTP connection '%s', IO total=%d\n", __FUNCTION__
			, hp_get_ipport_cstr(hp_io_fd(io), buf), hp_io_count(io->ioctx));

	request_uninit(req);
	free(req);
}

int test_http_cli_on_parse(hp_io_t * io, char * buf, size_t * len
		, hp_iohdr_t ** hdrp, char ** bodyp)
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
int test_http_cli_on_dispatch(hp_io_t * io, hp_iohdr_t * imhdr, char * body)
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
		http_parse parse = { .content_type = "json", .u.url = "/jsonrpc"};
		JSONRPC(out, REQ, parse,
				"{\"jsonrpc\": \"2.0\",\"method\": \"substract\",\"params\": [%d,%d],\"id\": %d}"
				, 42, 23, ++c->rpc_id);
		hp_log(stdout, "%s: >> '%s'\n", __FUNCTION__, out);
		rc = hp_io_write(&c->io, out, sdslen(out), (hp_io_free_t)sdsfree, 0);

		c->flags = 1;
	}
	else if(c->flags == 1){

		assert(c->json);
		hp_log(stdout, "%s: << '%s'\n", __FUNCTION__, cjson_cstr(c->json));

		//send another json-rpc request
		sds out;
		http_parse parse = { .content_type = "json", .u.url = "/jsonrpc"};
		JSONRPC(out, REQ, parse,
				"{\"jsonrpc\": \"2.0\",\"method\": \"this_jsonapi_not_exist\",\"params\": [%d,%d],\"id\": %d}"
				, 42, 23, ++c->rpc_id);
		hp_log(stdout, "%s: >> '%s'\n", __FUNCTION__, out);
		rc = hp_io_write(&c->io, out, sdslen(out), (hp_io_free_t)sdsfree, 0);

		cJSON_Delete(c->json);
		c->flags = 2;
	}
	else if(c->flags == 2){
		assert(c->json);
		hp_log(stdout, "%s: << '%s'\n", __FUNCTION__, cjson_cstr(c->json));

		sds out;
		http_parse parse = { .content_type = "html", .u.url = "/this_file_not_exist.html"};
		HTTP_REQ(out, parse, 0);
		rc = hp_io_write(&c->io, out, sdslen(out), (hp_io_free_t)sdsfree, 0);

		cJSON_Delete(c->json);
		c->flags = 3;
	}

	return rc;
}
void test_http_cli_on_delete(hp_io_t * io)
{
	assert(io);
	httpclient *c = (httpclient *)io;
	client_uninit(c);
	free(c);
}

/////////////////////////////////////////////////////////////////////////////////////

/**
 * callback in loop,
 * this function will be hi-frequently called
 * DO NOT block TOO LONG
 * @return: return <0 to ask to close the client
*/
static hp_iohdl s_http_server_hdl = {
		.on_new = test_http_server_on_new,
		.on_parse = test_http_server_on_parse,
		.on_dispatch = test_http_server_on_dispatch,
		.on_loop = 0,
		.on_delete = test_http_server_on_delete,
#ifdef _MSC_VER
		.wm_user = 0 	/* WM_USER + N */
		.hwnd = 0    /* hwnd */
#endif /* _MSC_VER */
};

static hp_iohdl s_http_cli_hdl = {
		.on_new = 0/*test_http_cli_on_new*/,
		.on_parse = test_http_cli_on_parse,
		.on_dispatch = test_http_cli_on_dispatch,
		.on_loop = 0,
		.on_delete = 0/*test_http_cli_on_delete*/,
#ifdef _MSC_VER
		.wm_user = 0 	/* WM_USER + N */
		.hwnd = 0    /* hwnd */
#endif /* _MSC_VER */
};

/////////////////////////////////////////////////////////////////////////////////////////
/* simple echo test */
//
//static int test__on_data(hp_io_t * io, char * buf, size_t * len)
//{
//	assert(io && buf && len);
//
////	struct client * node = (struct client *)list_entry(&s->clients, struct client, li);
////	assert(node);
//
//	s_test->in = sdscatlen(s_test->in, buf, *len);
//
//	assert(strncmp(s_test->in, s_test->out, strlen(s_test->in)) == 0);
//
//	*len = 0;
//
//	if(sdslen(s_test->in) == sdslen(s_test->in))
//		s_test->quit = 1;
//
//	return 0;
//}
//
//static int test__on_error(hp_io_t * io, int err, char const * errstr)
//{
//	hp_log((err != 0 ? stderr : stdout), "%s: close session, fd=%d, err=%d/'%s'\n"
//		, __FUNCTION__, io->fd
//		, err, errstr);
//	return 0;
//}
//
//static int test__server_on_data(hp_io_t * io, char * buf, size_t * len)
//{
//	assert(io && buf && len);
//	int rc;
//
//	hp_log(stdout, "%s: %s\n", __FUNCTION__, dumpstr(buf, *len, 128));
//
//	rc = hp_io_write(io, buf, *len, (void *)-1, 0);
//	assert(rc == 0);
//
//	*len = 0;
//
//	return 0;
//}
//
//static int test__server_on_error(hp_io_t * io, int err, char const * errstr)
//{
//	hp_log((err != 0 ? stderr : stdout), "%s: close session, fd=%d, err=%d/'%s', total=%d\n"
//		, __FUNCTION__, io->fd
//		, err, errstr, -1);
//	return 0;
//}
//
//static int test__on_new(hp_sock_t fd)
//{
//	int rc;
//
//	int tcp_keepalive = cfgi("tcp-keepalive");
//	if(hp_net_set_alive(fd, tcp_keepalive) != 0)
//		hp_log(stderr, "%s: WARNING: socket_set_alive failed, interval=%d s\n", __FUNCTION__, tcp_keepalive);
//
//	struct client * c = calloc(1, sizeof(struct client));
//
//	list_add_tail(&c->li, &s->clients);
//
//	rc = hp_io_add(ioctx, &c->io, fd, test__server_on_data, test__server_on_error);
//	assert(rc == 0);
//
//	return 0;
//}

/////////////////////////////////////////////////////////////////////////////////////////
/* echo test using handle */
//
//static hp_io_t *  test_hp_io_hdl_on_new(hp_io_ctx * ioctx, hp_sock_t fd)
//{
//	struct client * c = calloc(1, sizeof(struct client));
//	return (hp_io_t *)c;
//}
//static int test_hp_io_hdl_on_parse(hp_io_t * io, char * buf, size_t * len, int flags
//	, hp_iohdr_t ** hdrp, char ** bodyp)
//{
//	assert(io && buf && len && hdrp && bodyp);
//
//	*hdrp = (hp_iohdr_t *)io;
//	*bodyp = sdsnewlen(buf, *len);
//	*len = 0;
//	return 1;
//}
//
//static int test_hp_io_hdl_on_dispatch(hp_io_t * io, hp_iohdr_t * imhdr, char * body)
//{
//	/* body == io is set in on_parse */
//	assert(io && (void *)imhdr == (void *)io && body);
//	int rc;
//
//	rc = hp_io_write(io, body, sdslen((sds)body), (hp_io_free_t)sdsfree, 0);
//	assert(rc == 0);
//
//	return 0;
//}
//
//static void test_hp_io_hdl_on_delete(hp_io_t * io)
//{
//	assert(io);
//	struct client * c = (struct client * )io;
//	free(c);
//}
//
///////////////////////////////////////////////////////////////////////////////////////
///* test client read eror */
//
//static int test__on_read_error_data(hp_io_t * io, char * buf, size_t * len)
//{
//	assert(io && buf && len);
//	return 0;
//}
//
//static int test__on_read_error_error(hp_io_t * io, int err, char const * errstr)
//{
//	s_test->quit = 1;
//
//	hp_log((err != 0 ? stderr : stdout), "%s: close session, fd=%d, err=%d/'%s'\n"
//		, __FUNCTION__, io->fd
//		, err, errstr);
//	return 0;
//}
///////////////////////////////////////////////////////////////////////////////////////
///* test client read error, server close the client */
//
//static int test__on_data_2(hp_io_t * io, char * buf, size_t * len)
//{
//	assert(io && buf && len);
//	return 0;
//}
//
//static int test__on_error_2(hp_io_t * io, int err, char const * errstr)
//{
//	s_test->quit = 1;
//
//	hp_log((err != 0 ? stderr : stdout), "%s: close session, fd=%d, err=%d/'%s'\n"
//		, __FUNCTION__, io->fd
//		, err, errstr);
//	s_test->quit = 1;
//	return 0;
//}
//
//static int test__on_accept_2(hp_sock_t fd)
//{
//	return -1; /* tell that close the client */
//}
///////////////////////////////////////////////////////////////////////////////////////
///* test server read/write eror */
//#define test__server_on__server_error_data test__server_on_data
//
//static int test__server_on__server_error_error(hp_io_t * io, int err, char const * errstr)
//{
//	hp_log((err != 0 ? stderr : stdout), "%s: close session, fd=%d, err=%d/'%s', total=%d\n"
//		, __FUNCTION__, io->fd
//		, err, errstr, -1);
//	s_test->quit = 1;
//	return 0;
//}
//
//static int test__on_server_error_accept(hp_sock_t fd)
//{
//	int rc;
//
//	int tcp_keepalive = cfgi("tcp-keepalive");
//	if(hp_net_set_alive(fd, tcp_keepalive) != 0)
//		hp_log(stderr, "%s: WARNING: socket_set_alive failed, interval=%d s\n", __FUNCTION__, tcp_keepalive);
//
//	struct client * c = calloc(1, sizeof(struct client));
//
//	list_add_tail(&c->li, &s->clients);
//
//	rc = hp_io_add(ioctx, &c->io, fd, test__server_on__server_error_data, test__server_on__server_error_error);
//	assert(rc == 0);
//
//	return 0;
//}
//
//static int test__on_server_read_error_data(hp_io_t * io, char * buf, size_t * len)
//{
//	assert(io && buf && len);
//	return 0;
//}
//
//static int test__on_server_read_error_error(hp_io_t * io, int err, char const * errstr)
//{
//	hp_log((err != 0 ? stderr : stdout), "%s: close session, fd=%d, err=%d/'%s'\n"
//		, __FUNCTION__, io->fd
//		, err, errstr);
//	return 0;
//}

/////////////////////////////////////////////////////////////////////////////////////


int test_hp_io_t_main(int argc, char ** argv)
{
	int rc;
	{
		char buf[1024] = "";
		int n = sscanf("\r\n{\"jsonrpc\": \"2.0\",\"method\": \"this_jsonapi_not_exist\",\"params\": [%d,%d],\"id\": %d}\r\n",
				"\r\n%[^\r\n]\r\n", buf);
		n = 0;
	}
	/* simple json-rpc test: HTTP client and HTTP server */
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

		rc = hp_io_init(ioctx); assert(rc == 0);
		rc = client_init(c); assert(rc == 0);
		rc = server_init(s); assert(rc == 0);

		hp_sock_t listen_fd = hp_net_listen(7006); assert(listen_fd > 0);
		hp_sock_t confd = hp_net_connect(cfg("test_hp_io_t_main.ip"),
					cfgi("test_hp_io_t_main.port")); assert(confd > 0);


		/* add HTTP server listen socket */
		rc = hp_io_add(ioctx, &s->listenio, listen_fd, s_http_server_hdl);
		assert(rc == 0);
		/* add HTTP connect socket */
		rc = hp_io_add(ioctx, &c->io, confd, s_http_cli_hdl);
		assert(rc == 0);

		//HTTP client
		//send s impel HTTP request first
		sds out;
		http_parse parse = { .content_type = "html", .u.url = "/index.html"};
		HTTP_REQ(out, parse, 0);
		rc = hp_io_write(&c->io, out, sdslen(out), (hp_io_free_t)sdsfree, 0);
		assert(rc == 0);
		hp_log(stdout, "%s: HTPP request sent:\n%s", __FUNCTION__, out);

		/* run event loop */
		int quit = 3;
		for (; quit > 0;) {
			hp_io_run(ioctx, 200, 0);

			if(hp_io_count(ioctx) == 1)
				--quit;
		}


		/*clear*/
		hp_sock_close(confd);
		hp_sock_close(listen_fd);
		server_uninit(s);
		client_uninit(c);
		hp_io_uninit(ioctx);
	}
//	/* echo test with handle:
//	 *
//	 * 1.simple echo test
//	 * 2.using hp_iohdl
//	 *  */
//	{
//		INIT_LIST_HEAD(&s->clients);
//
//		s_test->in = sdsempty();
//		s_test->out = sdscatprintf(sdsempty(), "GET %s HTTP/1.1\r\nHost: %s:%d\r\n\r\n"
//			, s_url, server_ip, server_port);
//
//		struct client cobj, *c = &cobj;
//
//		hp_sock_t listen_fd = hp_net_listen(7006);
//		hp_sock_t fd = hp_net_connect("127.0.0.1", 7006);
//
//		/* init with listen socket */
//		hp_iohdl hdl = {
//			.on_new = test_hp_io_hdl_on_new,
//			.on_parse = test_hp_io_hdl_on_parse,
//			.on_dispatch = test_hp_io_hdl_on_dispatch,
//			.on_delete = test_hp_io_hdl_on_delete,
//		};
//
//		hp_ioopt ioopt = { listen_fd, 0, hdl
//#ifdef _MSC_VER
//		, 200  /* poll timeout */
//		, 0    /* hwnd */
//#endif /* _MSC_VER */
//		};
//
//		rc = hp_io_init(ioctx, &ioopt);
//		assert(rc == 0);
//
//		/* add connect socket */
//		rc = hp_io_add(ioctx, &c->io, fd, test__on_data, test__on_error);
//		assert(rc == 0);
//
//		rc = hp_io_write(&c->io, sdsdup(s_test->out), sdslen(s_test->out), (hp_io_free_t)sdsfree, 0);
//
//		/* run event loop */
//		for (; !s_test->quit;) {
//			hp_io_run(ioctx, 200, 0);
//		}
//
//		hp_io_uninit(ioctx);
//
//		/*clear*/
//		sdsfree(s_test->in);
//		sdsfree(s_test->out);
//
//		struct list_head * pos, *next;;
//		list_for_each_safe(pos, next, &s->clients) {
//			struct client * node = (struct client *)list_entry(pos, struct client, li);
//			assert(node);
//			list_del(&node->li);
//			free(node);
//		}
//		s_test->quit = 0;
//	}
//	/* test client on_read_error:
//	 *
//	 * 1.client connect to server OK;
//	 * 2.client close the socket after connect;
//	 * 3.client should detect read error later;
//	 *  */
//	{
//		INIT_LIST_HEAD(&s->clients);
//
//		time_t now = time(0);
//		struct client cobj, *c = &cobj;
//
//		hp_sock_t listen_fd = hp_net_listen(7006);
//		hp_sock_t fd = hp_net_connect("127.0.0.1", 7006);
//
//		/* init with listen socket */
//		hp_ioopt ioopt = { listen_fd, test__on_new, { 0 }
//#ifdef _MSC_VER
//		, 200  /* poll timeout */
//		, 0    /* hwnd */
//#endif /* _MSC_VER */
//		};
//
//		rc = hp_io_init(ioctx, &ioopt);
//		assert(rc == 0);
//
//		/* add connect socket */
//		rc = hp_io_add(ioctx, &c->io, fd, test__on_read_error_data, test__on_read_error_error);
//		assert(rc == 0);
//
//		/* run event loop */
//		for (; !s_test->quit;) {
//			hp_io_run(ioctx, 200, 0);
//
//			if(fd != hp_sock_invalid && difftime(time(0), now) > 2){
//				hp_sock_close(fd);
//				fd = hp_sock_invalid;
//			}
//		}
//
//		hp_io_uninit(ioctx);
//
//		struct list_head * pos, *next;;
//		list_for_each_safe(pos, next, &s->clients) {
//			struct client * node = (struct client *)list_entry(pos, struct client, li);
//			assert(node);
//			list_del(&node->li);
//			free(node);
//		}
//		s_test->quit = 0;
//	}
//	/* test client on_read,write error, server close this client
//	 *
//	 * 1.client connect to server OK;
//	 * 2.server close this client;
//	 * 3.client should detect read/write error later;
//	 *  */
//	{
//		INIT_LIST_HEAD(&s->clients);
//
//		struct client cobj, *c = &cobj;
//
//		hp_sock_t listen_fd = hp_net_listen(7006);
//		hp_sock_t fd = hp_net_connect("127.0.0.1", 7006);
//
//		/* init with listen socket */
//		hp_ioopt ioopt = { listen_fd, test__on_accept_2, { 0 }
//#ifdef _MSC_VER
//		, 200  /* poll timeout */
//		, 0    /* hwnd */
//#endif /* _MSC_VER */
//		};
//
//		rc = hp_io_init(ioctx, &ioopt);
//		assert(rc == 0);
//
//		/* add connect socket */
//		rc = hp_io_add(ioctx, &c->io, fd, test__on_data_2, test__on_error_2);
//		assert(rc == 0);
//
//		/* run event loop */
//		for (; !s_test->quit;) {
//			hp_io_run(ioctx, 200, 0);
//		}
//
//		hp_io_uninit(ioctx);
//
//		struct list_head * pos, *next;;
//		list_for_each_safe(pos, next, &s->clients) {
//			struct client * node = (struct client *)list_entry(pos, struct client, li);
//			assert(node);
//			list_del(&node->li);
//			free(node);
//		}
//		s_test->quit = 0;
//	}
//
//	/* test server on_read_error/on_write_error:
//	 *
//	 * 1.client connect to server OK;
//	 * 2.client close the socket after connect;
//	 * 2.server should detect read/write error later for this client;
//	 *  */
//	{
//		INIT_LIST_HEAD(&s->clients);
//
//		time_t now = time(0);
//		struct client cobj, *c = &cobj;
//
//		hp_sock_t listen_fd = hp_net_listen(7006);
//		hp_sock_t fd = hp_net_connect("127.0.0.1", 7006);
//
//		/* init with listen socket */
//		hp_ioopt ioopt = { listen_fd, test__on_server_error_accept, { 0 }
//#ifdef _MSC_VER
//		, 200  /* poll timeout */
//		, 0    /* hwnd */
//#endif /* _MSC_VER */
//		};
//
//		rc = hp_io_init(ioctx, &ioopt);
//		assert(rc == 0);
//
//		/* add connect socket */
//		rc = hp_io_add(ioctx, &c->io, fd, test__on_server_read_error_data, test__on_server_read_error_error);
//		assert(rc == 0);
//
//		/* run event loop */
//		for (; !s_test->quit;) {
//			hp_io_run(ioctx, 200, 0);
//
//			if(fd != hp_sock_invalid && difftime(time(0), now) > 2){
//				hp_sock_close(fd);
//				fd = hp_sock_invalid;
//			}
//		}
//
//		hp_io_uninit(ioctx);
//
//		struct list_head * pos, *next;;
//		list_for_each_safe(pos, next, &s->clients) {
//			struct client * node = (struct client *)list_entry(pos, struct client, li);
//			assert(node);
//			list_del(&node->li);
//			free(node);
//		}
//		s_test->quit = 0;
//	}

	return 0;
}

#endif /* NDEBUG */
