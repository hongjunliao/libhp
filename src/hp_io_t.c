/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2021/4/9
 *
 * networking I/O, using epoll on *nix and IOCP on Win32
 * */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

/* for sds */
#ifdef _MSC_VER
#include "redis/src/Win32_Interop/Win32_Portability.h"
#include "redis/src/Win32_Interop/win32_types.h"
#endif

#include "hp_io_t.h"
#include <unistd.h> /* close */
#include <assert.h> /* assert */
#include <errno.h> /*  */
#include <stdio.h>
#include <string.h>
#include "hp_log.h"
#include "str_dump.h" /*dumpstr*/
#include <uv.h>   /* uv_ip4_name */
#include "redis/src/adlist.h" /* list */

#ifndef _MSC_VER
#include <sys/ioctl.h>  /* ioctl */
#include <arpa/inet.h>	/* inet_ntop */
#else
#define ioctl ioctlsocket
#endif /* _MSC_VER */

extern int gloglevel;

/////////////////////////////////////////////////////////////////////////////////////////

static int hp_io_internal_on_data(hp_io_t * io, char * buf, size_t * len)
{
	assert(io && io->iohdl && buf && len);
	if (!(io->iohdl && io->iohdl->on_parse)) {
		*len = 0;
	}

	if (!(*len > 0))
		return 0;

	for (; *len > 0;) {
		hp_iohdr_t * iohdr = 0;
		char * body = 0;
		int n = io->iohdl->on_parse(io, buf, len, io->iohdl->flags, &iohdr, &body);
		if (n < 0) {
			*len = 0;
			return n;
		}
		else if (n == 0)
			return 0;
		if (io->iohdl->on_dispatch)
			io->iohdl->on_dispatch(io, iohdr, body);
	}
	return 0;
}

static int hp_io_internal_on_error(hp_io_t * io, int err, char const * errstr)
{
	assert(io && io->iohdl);
	list * li = 0;
#ifndef _MSC_VER
	assert(io->efds && io->efds->arg);
	li = (list *)io->efds->arg;
#else
	assert(io->iocp && io->iocp->user);
	li = (list *)io->iocp->user;
#endif /* _MSC_VER */
	hp_log((err != 0 ? stderr : stdout), "%s: close session, fd=%d, err=%d/'%s', total=%d\n"
		, __FUNCTION__, io->fd
		, err, errstr, listLength(li));

	if(io->iohdl->on_delete)
		io->iohdl->on_delete(io);
	return 0;
}

#ifndef _MSC_VER
static int hp_io_internal_on_accept(struct epoll_event * ev)
#else
static hp_sock_t hp_io_internal_on_accept(hp_iocp * iocpctx, int index)
#endif /* _MSC_VER */
{
#ifndef _MSC_VER
	hp_io_ctx * ioctx = (hp_io_ctx * )hp_epoll_arg(ev);
	hp_sock_t fd = hp_epoll_fd(ev);
#else
	hp_io_ctx * ioctx = (hp_io_ctx *)hp_iocp_arg(iocpctx, index);
	hp_sock_t fd = ioctx->fd;
#endif /* _MSC_VER */
	assert(ioctx);
	int rc;

	for(;;){
		struct sockaddr_in clientaddr = { 0 };
		socklen_t len = sizeof(clientaddr);
		int confd = accept(fd, (struct sockaddr *)&clientaddr, &len);
		if(confd < 0 ){
			if(errno == EINTR || errno == EAGAIN)
				return 0;

			hp_log(stderr, "%s: accept failed, errno=%d, error='%s'\n", __FUNCTION__
					, errno, strerror(errno));
			return -1;
		}

		int is_c = !(ioctx->on_accept || ioctx->iohdl.on_new);
		if(!is_c && ioctx->on_accept && ioctx->on_accept(confd) < 0){ is_c = 1; }
		if (!is_c && ioctx->iohdl.on_new) {
			hp_io_t * nio = ioctx->iohdl.on_new(ioctx, confd);
			if(nio){
				rc = hp_io_add(ioctx, nio, confd, hp_io_internal_on_data, hp_io_internal_on_error);
				if (rc != 0) { is_c = 1; }

				nio->iohdl = &ioctx->iohdl;
			} else { is_c = 1; }
		}
		if (is_c) {
			close(confd);
			continue;
		}

		unsigned long sockopt = 1;
#ifdef LIBHP_WITH_WIN32_INTERROP
		ioctl(FDAPI_get_ossocket(fd), FIONBIO, &sockopt);
#else
		ioctl(confd, FIONBIO, &sockopt);
#endif /* LIBHP_WITH_WIN32_INTERROP */

		char cliaddstr[64];
		uv_ip4_name(&clientaddr, cliaddstr, sizeof(cliaddstr));

		if(gloglevel > 2){
			hp_log(stdout, "%s: new connect from '%s', fd=%d, total=%d\n"
				, __FUNCTION__, cliaddstr, confd, listLength(ioctx->iolist));
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
			(hp_sock_is_valid(k->fd) && io->fd == k->fd);
}

int hp_io_init(hp_io_ctx * ioctx, hp_ioopt * opt)
{
	if(!(ioctx && opt))
		return -1;
	int rc;
	memset(ioctx, 0, sizeof(hp_io_ctx));
	ioctx->on_accept = opt->on_accept;
	ioctx->iohdl = opt->iohdl;
	ioctx->iolist = listCreate();
	listSetMatchMethod(ioctx->iolist, iolist_match);

#ifndef _MSC_VER
	/* init epoll */
	if (hp_epoll_init(&ioctx->efds, 65535) != 0)
		return -5;
	ioctx->efds.arg = ioctx->iolist;

	if (opt->listen_fd >= 0) {
		hp_epolld_set(&ioctx->epolld, opt->listen_fd, hp_io_internal_on_accept, ioctx);
		rc = hp_epoll_add(&ioctx->efds, opt->listen_fd, EPOLLIN, &ioctx->epolld); assert(rc == 0);
	}
#else
	ioctx->fd = opt->listen_fd;

	rc = hp_iocp_init(&(ioctx->iocp), 0, WM_USER + opt->wm_user, 200, ioctx->iolist);
	assert(rc == 0);

	int tid = (int)GetCurrentThreadId();
	rc = hp_iocp_run(&ioctx->iocp, tid, opt->hwnd);
	assert(rc == 0);

	if(hp_sock_is_valid(opt->listen_fd)){
		int index = hp_iocp_add(&ioctx->iocp, 0, 0, opt->listen_fd, hp_io_internal_on_accept, 0, 0, ioctx);
		rc = (index >= 0 ? 0 : index);
	}

#endif /* _MSC_VER */

	return rc;
}

int hp_io_write(hp_io_t * io, void * buf, size_t len, hp_io_free_t free, void * ptr)
{
	int rc;
#ifndef _MSC_VER
	rc = hp_eto_add(&io->eto, buf, len,  free, ptr);
#else
	rc = hp_iocp_write(io->iocp, io->index, buf, len, free, ptr);
#endif /* _MSC_VER */
	return rc;
}

#ifndef _MSC_VER
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
		if (io->iohdl && io->iohdl->on_loop) {
			io->iohdl->on_loop(io);
		}
	}
	listReleaseIterator(iter);

	return 0;
}
#endif /* _MSC_VER */

int hp_io_run(hp_io_ctx * ioctx, int interval, int mode)
{
	int rc = 0;
#ifndef _MSC_VER
	rc = hp_epoll_run(&ioctx->efds, interval, (mode != 0? hp_io_epoll_before_wait : (void *)-1));
	if(mode == 0) { hp_io_epoll_before_wait(&ioctx->efds); }
#else
	for (;;) {
		MSG msgobj = { 0 }, *msg = &msgobj;
		if (PeekMessage((LPMSG)msg, (HWND)-1
				, (UINT)HP_IOCP_WM_FIRST(&ioctx->iocp), (UINT)HP_IOCP_WM_LAST(&ioctx->iocp)
				, PM_REMOVE | PM_NOYIELD)) {

			rc = hp_iocp_handle_msg(&ioctx->iocp, msg->message, msg->wParam, msg->lParam);
		}

		/* user loop */
		listIter * iter = 0;
		listNode * node;
		iter = listGetIterator(ioctx->iolist, 0);
		for (node = 0; (node = listNext(iter));) {
			hp_io_t * io = (hp_io_t *)listNodeValue(node);
			assert(io);

			if (io->iohdl && io->iohdl->on_loop) {
				io->iohdl->on_loop(io);
			}
		}
		listReleaseIterator(iter);

		iter = listGetIterator(ioctx->iolist, 0);
		for (node = 0; (node = listNext(iter));) {
			hp_io_t * io = (hp_io_t *)listNodeValue(node);
			assert(io);
			hp_iocp_try_write(&ioctx->iocp, io->index);
		}
		listReleaseIterator(iter);

		if(mode == 0)
			break;
	}
#endif /* _MSC_VER */

	return rc;
}

int hp_io_close(hp_io_t * io)
{
	if(!(io)) { return -1; }
	int rc;
#ifndef _MSC_VER
	rc = close(io->ed.fd);
#else
	rc = hp_iocp_close(io->iocp, io->index);
#endif /* _MSC_VER */
	return rc;
}

int hp_io_close_sock(hp_io_ctx * ioctx, hp_sock_t fd)
{
	if(!(ioctx && hp_sock_is_valid(fd))) { return -1; }

	hp_io_t key = { 0 };
	key.fd = fd;

	listNode * node = listSearchKey(ioctx->iolist, &key);
	if (!node) { return -1; }

	hp_io_t * io = (hp_io_t *)listNodeValue(node);
	return hp_io_close(io);
}

int hp_io_uninit(hp_io_ctx * ioctx)
{
	if(!ioctx)
		return -1;
#ifndef _MSC_VER
	hp_epoll_del(&ioctx->efds, ioctx->epolld.fd, EPOLLIN, &ioctx->epolld);
	hp_epoll_uninit(&ioctx->efds);
	close(ioctx->epolld.fd);
#else
	hp_iocp_uninit(&ioctx->iocp);
#endif /* _MSC_VER */
	listRelease(ioctx->iolist);

	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////

#ifndef _MSC_VER

static int hp_io_t_io_cb(struct epoll_event * ev)
{
	assert(ev);

	if((ev->events & EPOLLERR)){
		hp_io_t * io = (hp_io_t *)hp_epoll_arg(ev);
		assert(io);

		ev->events = 0;
		io->on_error(io, EPOLLERR, "EPOLLERR");

		return 0;
	}

	if((ev->events & EPOLLIN)){
		hp_io_t * io = (hp_io_t *)hp_epoll_arg(ev);
		assert(io);

		hp_eti_read(&io->eti, io->fd, ev);
	}

	if((ev->events & EPOLLOUT)){
		hp_io_t * io = (hp_io_t *)hp_epoll_arg(ev);
		assert(io);

		hp_eto_write(&io->eto, io->fd, ev);
	}

	return 0;
}

static int hp_io_t__on_data(char* buf, size_t* len, void * arg)
{
	struct epoll_event * ev = (struct epoll_event *)arg;
	assert(ev);
	hp_io_t * io = (hp_io_t *)hp_epoll_arg(ev);
	assert(io);

	if (!(*len > 0))
		return EAGAIN;

	int rc = io->on_data(io, buf, len);
	return (rc >= 0? EAGAIN : rc);
}

static void hp_io_t__on_error(int err, char const * errstr, void * arg)
{
	struct epoll_event * ev = (struct epoll_event *)arg;
	assert(ev);
	ev->events = 0;

	hp_io_t * io = (hp_io_t *)hp_epoll_arg(ev);
	assert(io);
	assert(io->efds && io->efds->arg);

	list * li = (list *)io->efds->arg;
	hp_io_t key = { 0 };
	key.id = io->id;

	listNode * node = listSearchKey(li, &key);
	if(node){ listDelNode(li, node); }

	hp_epoll_del(io->efds, io->ed.fd, EPOLLIN | EPOLLOUT | EPOLLET, &io->ed);
	close(io->ed.fd);

	hp_eti_uninit(&io->eti);
	hp_eto_uninit(&io->eto);

	io->on_error(io, err, errstr);
}

static void hp_io_t__on_werror(struct hp_eto * eto, int err, void * arg)
{
	return hp_io_t__on_error(err, strerror(err), arg);
}

static void hp_io_t__on_rerror(struct hp_eti * eti, int err, void * arg)
{
	return hp_io_t__on_error(err, (err == 0? "EOF" : strerror(err)), arg);
}

#else
static int hp_io_t__on_error(hp_iocp * iocpctx, int index, int err, char const * errstr)
{
	assert(iocpctx);
	hp_io_t * io = (hp_io_t *)hp_iocp_arg(iocpctx, index);
	assert(io);
	assert(io->iocp && io->iocp->user);
	list * li = (list *)io->iocp->user;
      
	hp_io_t key = { 0 };
	key.id = io->id;

	listNode * node = listSearchKey(li, &key);
	if (node) { listDelNode(li, node); }

	return io->on_error(io, err, errstr);
}

int hp_io_t__on_data(hp_iocp * iocpctx, int index, char * buf, size_t * len)
{
	assert(iocpctx && buf && len);
	hp_io_t * io = (hp_io_t *)hp_iocp_arg(iocpctx, index);
	assert(io);

	return io->on_data(io, buf, len);
}
#endif /* _MSC_VER */

/////////////////////////////////////////////////////////////////////////////////////

int hp_io_add(hp_io_ctx * ioctx, hp_io_t * io, hp_sock_t fd
	, hp_io_on_data on_data, hp_io_on_error on_error)
{
	int rc;
	if(!(ioctx && io))
		return -1;
	memset(io, 0, sizeof(hp_io_t));

	io->fd = fd;
	io->on_data = on_data;
	io->on_error = on_error;
	io->id = ++ioctx->ioid;

	if(ioctx->ioid == INT_MAX - 1)
		ioctx->ioid = 0;
#ifndef _MSC_VER
	io->efds = &ioctx->efds;

	hp_eti_init(&io->eti, 1024 * 128);
	hp_eto_init(&io->eto, 8);

	struct hp_eto * eto = &io->eto;
	eto->write_error = hp_io_t__on_werror;

	struct hp_eti * eti = &io->eti;
	eti->read_error = hp_io_t__on_rerror;
	eti->pack = hp_io_t__on_data;

	hp_epolld_set(&io->ed, fd, hp_io_t_io_cb, io);
	/* try to add to epoll event system */
	rc = hp_epoll_add(&ioctx->efds, fd,  EPOLLIN | EPOLLOUT |  EPOLLET, &io->ed);
	assert(rc == 0);

#else
	io->iocp = &ioctx->iocp;

	io->index = hp_iocp_add(&ioctx->iocp, 0, 0, io->fd, 0, hp_io_t__on_data, hp_io_t__on_error, io);
	rc = (io->index >= 0? 0 : io->index);
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

static char server_ip[128] = "127.0.0.1";
static char s_url[1024] = "/";
static int server_port = 7006;
extern hp_config_t g_conf;

#define cfgi(k) atoi(g_conf(k))

struct client {
	hp_io_t io;
	struct list_head li;
};
struct server {
	struct list_head clients;
	hp_io_ctx ioctx;
};
struct test {
	sds in, out;
	int quit;
};

static struct test s_testobj, * s_test = &s_testobj;
static struct server s_sreverobj, * s_srever = &s_sreverobj;

/////////////////////////////////////////////////////////////////////////////////////
/* simple echo test */

static int test__on_data(hp_io_t * io, char * buf, size_t * len)
{
	assert(io && buf && len);

//	struct client * node = (struct client *)list_entry(&s_srever->clients, struct client, li);
//	assert(node);

	s_test->in = sdscatlen(s_test->in, buf, *len);

	assert(strncmp(s_test->in, s_test->out, strlen(s_test->in)) == 0);

	*len = 0;

	if(sdslen(s_test->in) == sdslen(s_test->in))
		s_test->quit = 1;

	return 0;
}

static int test__on_error(hp_io_t * io, int err, char const * errstr)
{
	hp_log((err != 0 ? stderr : stdout), "%s: close session, fd=%d, err=%d/'%s'\n"
		, __FUNCTION__, io->fd
		, err, errstr);
	return 0;
}

static int test__server_on_data(hp_io_t * io, char * buf, size_t * len)
{
	assert(io && buf && len);
	int rc;

	hp_log(stdout, "%s: %s\n", __FUNCTION__, dumpstr(buf, *len, 128));

	rc = hp_io_write(io, buf, *len, (void *)-1, 0);
	assert(rc == 0);

	*len = 0;

	return 0;
}

static int test__server_on_error(hp_io_t * io, int err, char const * errstr)
{
	hp_log((err != 0 ? stderr : stdout), "%s: close session, fd=%d, err=%d/'%s', total=%d\n"
		, __FUNCTION__, io->fd
		, err, errstr, -1);
	return 0;
}

static int test__on_accept(hp_sock_t fd)
{
	int rc;

	int tcp_keepalive = cfgi("tcp-keepalive");
	if(hp_net_set_alive(fd, tcp_keepalive) != 0)
		hp_log(stderr, "%s: WARNING: socket_set_alive failed, interval=%d s\n", __FUNCTION__, tcp_keepalive);

	struct client * c = calloc(1, sizeof(struct client));

	list_add_tail(&c->li, &s_srever->clients);

	rc = hp_io_add(&s_srever->ioctx, &c->io, fd, test__server_on_data, test__server_on_error);
	assert(rc == 0);

	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////
/* echo test using handle */

static hp_io_t *  test_hp_io_hdl_on_new(hp_io_ctx * ioctx, hp_sock_t fd) 
{
	struct client * c = calloc(1, sizeof(struct client));
	return (hp_io_t *)c;
}
static int test_hp_io_hdl_on_parse(hp_io_t * io, char * buf, size_t * len, int flags
	, hp_iohdr_t ** hdrp, char ** bodyp)
{
	assert(io && buf && len && hdrp && bodyp);

	*hdrp = (hp_iohdr_t *)io;
	*bodyp = sdsnewlen(buf, *len);
	*len = 0;
	return 1;
}

static int test_hp_io_hdl_on_dispatch(hp_io_t * io, hp_iohdr_t * imhdr, char * body)
{
	/* body == io is set in on_parse */
	assert(io && (void *)imhdr == (void *)io && body);
	int rc;

	rc = hp_io_write(io, body, sdslen((sds)body), (hp_io_free_t)sdsfree, 0);
	assert(rc == 0);

	return 0;
}

static void test_hp_io_hdl_on_delete(hp_io_t * io) 
{
	assert(io);
	struct client * c = (struct client * )io;
	free(c);
}

/////////////////////////////////////////////////////////////////////////////////////
/* test client read eror */

static int test__on_read_error_data(hp_io_t * io, char * buf, size_t * len)
{
	assert(io && buf && len);
	return 0;
}

static int test__on_read_error_error(hp_io_t * io, int err, char const * errstr)
{
	s_test->quit = 1;

	hp_log((err != 0 ? stderr : stdout), "%s: close session, fd=%d, err=%d/'%s'\n"
		, __FUNCTION__, io->fd
		, err, errstr);
	return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
/* test client read error, server close the client */

static int test__on_data_2(hp_io_t * io, char * buf, size_t * len)
{
	assert(io && buf && len);
	return 0;
}

static int test__on_error_2(hp_io_t * io, int err, char const * errstr)
{
	s_test->quit = 1;

	hp_log((err != 0 ? stderr : stdout), "%s: close session, fd=%d, err=%d/'%s'\n"
		, __FUNCTION__, io->fd
		, err, errstr);
	s_test->quit = 1;
	return 0;
}

static int test__on_accept_2(hp_sock_t fd)
{
	return -1; /* tell that close the client */
}
/////////////////////////////////////////////////////////////////////////////////////
/* test server read/write eror */
#define test__server_on__server_error_data test__server_on_data

static int test__server_on__server_error_error(hp_io_t * io, int err, char const * errstr)
{
	hp_log((err != 0 ? stderr : stdout), "%s: close session, fd=%d, err=%d/'%s', total=%d\n"
		, __FUNCTION__, io->fd
		, err, errstr, -1);
	s_test->quit = 1;
	return 0;
}

static int test__on_server_error_accept(hp_sock_t fd)
{
	int rc;

	int tcp_keepalive = cfgi("tcp-keepalive");
	if(hp_net_set_alive(fd, tcp_keepalive) != 0)
		hp_log(stderr, "%s: WARNING: socket_set_alive failed, interval=%d s\n", __FUNCTION__, tcp_keepalive);

	struct client * c = calloc(1, sizeof(struct client));

	list_add_tail(&c->li, &s_srever->clients);

	rc = hp_io_add(&s_srever->ioctx, &c->io, fd, test__server_on__server_error_data, test__server_on__server_error_error);
	assert(rc == 0);

	return 0;
}

static int test__on_server_read_error_data(hp_io_t * io, char * buf, size_t * len)
{
	assert(io && buf && len);
	return 0;
}

static int test__on_server_read_error_error(hp_io_t * io, int err, char const * errstr)
{
	hp_log((err != 0 ? stderr : stdout), "%s: close session, fd=%d, err=%d/'%s'\n"
		, __FUNCTION__, io->fd
		, err, errstr);
	return 0;
}
/////////////////////////////////////////////////////////////////////////////////////////

int test_hp_io_t_main(int argc, char ** argv)
{
	int rc;
	/* simple echo test */
	{
		INIT_LIST_HEAD(&s_srever->clients);

		s_test->in = sdsempty();
		s_test->out = sdscatprintf(sdsempty(), "GET %s HTTP/1.1\r\nHost: %s:%d\r\n\r\n"
			, s_url, server_ip, server_port);

		struct client cobj, *c = &cobj;

		hp_sock_t listen_fd = hp_net_listen(7006); assert(listen_fd > 0);
		hp_sock_t fd = hp_net_connect("127.0.0.1", 7006); assert(fd > 0);

		hp_ioopt ioopt = { listen_fd, test__on_accept, { 0 }
#ifdef _MSC_VER
		, 200  /* poll timeout */
		, 0    /* hwnd */
#endif /* _MSC_VER */
		};

		/* init with listen socket */
		rc = hp_io_init(&s_srever->ioctx, &ioopt);
		assert(rc == 0);

		/* add connect socket */
		rc = hp_io_add(&s_srever->ioctx, &c->io, fd, test__on_data, test__on_error);
		assert(rc == 0);

		rc = hp_io_write(&c->io, sdsdup(s_test->out), sdslen(s_test->out), (hp_io_free_t)sdsfree, 0);

		/* run event loop */
		for (; !s_test->quit;) {
			hp_io_run(&s_srever->ioctx, 200, 0);
		}

		hp_io_uninit(&s_srever->ioctx);

		/*clear*/
		sdsfree(s_test->in);
		sdsfree(s_test->out);

		struct list_head * pos, *next;;
		list_for_each_safe(pos, next, &s_srever->clients) {
			struct client * node = (struct client *)list_entry(pos, struct client, li);
			assert(node);
			list_del(&node->li);
			free(node);
		}
		s_test->quit = 0;
	}
	/* echo test with handle:
	 *
	 * 1.simple echo test
	 * 2.using hp_iohdl
	 *  */
	{
		INIT_LIST_HEAD(&s_srever->clients);

		s_test->in = sdsempty();
		s_test->out = sdscatprintf(sdsempty(), "GET %s HTTP/1.1\r\nHost: %s:%d\r\n\r\n"
			, s_url, server_ip, server_port);

		struct client cobj, *c = &cobj;

		hp_sock_t listen_fd = hp_net_listen(7006);
		hp_sock_t fd = hp_net_connect("127.0.0.1", 7006);

		/* init with listen socket */
		hp_iohdl hdl = {
			.on_new = test_hp_io_hdl_on_new,
			.on_parse = test_hp_io_hdl_on_parse,
			.on_dispatch = test_hp_io_hdl_on_dispatch,
			.on_delete = test_hp_io_hdl_on_delete,
		};

		hp_ioopt ioopt = { listen_fd, 0, hdl
#ifdef _MSC_VER
		, 200  /* poll timeout */
		, 0    /* hwnd */
#endif /* _MSC_VER */
		};

		rc = hp_io_init(&s_srever->ioctx, &ioopt);
		assert(rc == 0);

		/* add connect socket */
		rc = hp_io_add(&s_srever->ioctx, &c->io, fd, test__on_data, test__on_error);
		assert(rc == 0);

		rc = hp_io_write(&c->io, sdsdup(s_test->out), sdslen(s_test->out), (hp_io_free_t)sdsfree, 0);

		/* run event loop */
		for (; !s_test->quit;) {
			hp_io_run(&s_srever->ioctx, 200, 0);
		}

		hp_io_uninit(&s_srever->ioctx);

		/*clear*/
		sdsfree(s_test->in);
		sdsfree(s_test->out);

		struct list_head * pos, *next;;
		list_for_each_safe(pos, next, &s_srever->clients) {
			struct client * node = (struct client *)list_entry(pos, struct client, li);
			assert(node);
			list_del(&node->li);
			free(node);
		}
		s_test->quit = 0;
	}
	/* test client on_read_error:
	 *
	 * 1.client connect to server OK;
	 * 2.client close the socket after connect;
	 * 3.client should detect read error later;
	 *  */
	{
		INIT_LIST_HEAD(&s_srever->clients);

		time_t now = time(0);
		struct client cobj, *c = &cobj;

		hp_sock_t listen_fd = hp_net_listen(7006);
		hp_sock_t fd = hp_net_connect("127.0.0.1", 7006);

		/* init with listen socket */
		hp_ioopt ioopt = { listen_fd, test__on_accept, { 0 }
#ifdef _MSC_VER
		, 200  /* poll timeout */
		, 0    /* hwnd */
#endif /* _MSC_VER */
		};

		rc = hp_io_init(&s_srever->ioctx, &ioopt);
		assert(rc == 0);

		/* add connect socket */
		rc = hp_io_add(&s_srever->ioctx, &c->io, fd, test__on_read_error_data, test__on_read_error_error);
		assert(rc == 0);

		/* run event loop */
		for (; !s_test->quit;) {
			hp_io_run(&s_srever->ioctx, 200, 0);

			if(fd != hp_sock_invalid && difftime(time(0), now) > 2){
				hp_sock_close(fd);
				fd = hp_sock_invalid;
			}
		}

		hp_io_uninit(&s_srever->ioctx);

		struct list_head * pos, *next;;
		list_for_each_safe(pos, next, &s_srever->clients) {
			struct client * node = (struct client *)list_entry(pos, struct client, li);
			assert(node);
			list_del(&node->li);
			free(node);
		}
		s_test->quit = 0;
	}
	/* test client on_read,write error, server close this client
	 *
	 * 1.client connect to server OK;
	 * 2.server close this client;
	 * 3.client should detect read/write error later;
	 *  */
	{
		INIT_LIST_HEAD(&s_srever->clients);

		struct client cobj, *c = &cobj;

		hp_sock_t listen_fd = hp_net_listen(7006);
		hp_sock_t fd = hp_net_connect("127.0.0.1", 7006);

		/* init with listen socket */
		hp_ioopt ioopt = { listen_fd, test__on_accept_2, { 0 }
#ifdef _MSC_VER
		, 200  /* poll timeout */
		, 0    /* hwnd */
#endif /* _MSC_VER */
		};

		rc = hp_io_init(&s_srever->ioctx, &ioopt);
		assert(rc == 0);

		/* add connect socket */
		rc = hp_io_add(&s_srever->ioctx, &c->io, fd, test__on_data_2, test__on_error_2);
		assert(rc == 0);

		/* run event loop */
		for (; !s_test->quit;) {
			hp_io_run(&s_srever->ioctx, 200, 0);
		}

		hp_io_uninit(&s_srever->ioctx);

		struct list_head * pos, *next;;
		list_for_each_safe(pos, next, &s_srever->clients) {
			struct client * node = (struct client *)list_entry(pos, struct client, li);
			assert(node);
			list_del(&node->li);
			free(node);
		}
		s_test->quit = 0;
	}

	/* test server on_read_error/on_write_error:
	 *
	 * 1.client connect to server OK;
	 * 2.client close the socket after connect;
	 * 2.server should detect read/write error later for this client;
	 *  */
	{
		INIT_LIST_HEAD(&s_srever->clients);

		time_t now = time(0);
		struct client cobj, *c = &cobj;

		hp_sock_t listen_fd = hp_net_listen(7006);
		hp_sock_t fd = hp_net_connect("127.0.0.1", 7006);

		/* init with listen socket */
		hp_ioopt ioopt = { listen_fd, test__on_server_error_accept, { 0 }
#ifdef _MSC_VER
		, 200  /* poll timeout */
		, 0    /* hwnd */
#endif /* _MSC_VER */
		};

		rc = hp_io_init(&s_srever->ioctx, &ioopt);
		assert(rc == 0);

		/* add connect socket */
		rc = hp_io_add(&s_srever->ioctx, &c->io, fd, test__on_server_read_error_data, test__on_server_read_error_error);
		assert(rc == 0);

		/* run event loop */
		for (; !s_test->quit;) {
			hp_io_run(&s_srever->ioctx, 200, 0);

			if(fd != hp_sock_invalid && difftime(time(0), now) > 2){
				hp_sock_close(fd);
				fd = hp_sock_invalid;
			}
		}

		hp_io_uninit(&s_srever->ioctx);

		struct list_head * pos, *next;;
		list_for_each_safe(pos, next, &s_srever->clients) {
			struct client * node = (struct client *)list_entry(pos, struct client, li);
			assert(node);
			list_del(&node->li);
			free(node);
		}
		s_test->quit = 0;
	}

	return 0;
}

#endif /* NDEBUG */
