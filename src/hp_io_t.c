/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2021/4/9
 *
 * networking I/O, using epoll on *nix and IOCP on Win32
 * */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <unistd.h> /* close */
#include <assert.h> /* assert */
#include <errno.h> /*  */
#include <stdio.h>
#include <string.h>
#include "hp_io_t.h"
#include "hp_log.h"
#include <uv.h>   /* uv_ip4_name */

#ifndef _MSC_VER
#include <sys/ioctl.h>  /* ioctl */
#include <arpa/inet.h>	/* inet_ntop */
#else
#endif /* _MSC_VER */

extern int gloglevel;
/////////////////////////////////////////////////////////////////////////////////////////

static int hp_io_t__on_accept(struct epoll_event * ev)
{
	hp_io_ctx * ioctx = (hp_io_ctx * )hp_epoll_arg(ev);
	assert(ioctx);

	for(;;){
		struct sockaddr_in clientaddr = { 0 };
		socklen_t len = sizeof(clientaddr);
		int confd = accept(hp_epoll_fd(ev), (struct sockaddr *)&clientaddr, &len);
		if(confd < 0 ){
			if(errno == EINTR || errno == EAGAIN)
				return 0;

			hp_log(stderr, "%s: accept failed, errno=%d, error='%s'\n", __FUNCTION__
					, errno, strerror(errno));
			return -1;
		}

		if(ioctx->on_accept(confd) < 0){
			close(confd);
			continue;
		}

		unsigned long sockopt = 1;
		ioctl(confd, FIONBIO, &sockopt);

		++ioctx->n_accept;

		char cliaddstr[64];
		uv_ip4_name(&clientaddr, cliaddstr, sizeof(cliaddstr));

		if(gloglevel > 2){
			hp_log(stdout, "%s: new connect from '%s', fd=%d, total=%d\n"
				, __FUNCTION__, cliaddstr, confd, ioctx->n_accept);
		}
	}
	return 0;
}

int hp_io_init(hp_io_ctx * ioctx, hp_sock_t fd, hp_io_on_accept on_accept)
{
	if(!ioctx)
		return -1;
	int rc;
	memset(ioctx, 0, sizeof(hp_io_ctx));

#ifndef _MSC_VER
	/* init epoll */
	if (hp_epoll_init(&ioctx->efds, 65535) != 0)
		return -5;

	ioctx->on_accept = on_accept;
	hp_epolld_set(&ioctx->epolld, fd, hp_io_t__on_accept, ioctx);
	rc = hp_epoll_add(&ioctx->efds, fd, EPOLLIN, &ioctx->epolld); assert(rc == 0);
#else
	rc = hp_iocp_init(&(ioctx->iocpctx), 2, WM_USER + 100, 200, 0);
#endif /* _MSC_VER */

	return rc;
}

int hp_io_write(hp_io_t * io, void * buf, size_t len, hp_io_free_t free, void * ptr)
{
	int rc;
#ifndef _MSC_VER
	rc = hp_eto_add(&io->eto, buf, len,  free, ptr);
#else
	rc = hp_iocp_write(io->io, io->index, buf, len, free, ptr);
#endif /* _MSC_VER */
	return rc;
}

int hp_io_run(hp_io_ctx * ioctx, int interval)
{
	int rc;
#ifndef _MSC_VER
	rc = hp_epoll_run(&ioctx->efds, interval, (interval > 0? 0 : (void *)-1));
#else
	MSG msgobj = { 0 }, *msg = &msgobj;
	if (PeekMessage((LPMSG)msg, (HWND)0, (UINT)0, (UINT)0, PM_REMOVE)) {
		rc = hp_iocp_handle_msg(&ioctx->iocpctx, msg->message, msg->wParam, msg->lParam);
	}
#endif /* _MSC_VER */

	return rc;
}

int hp_io_uninit(hp_io_ctx * ioctx)
{
	if(!ioctx)
		return -1;

	return -1;
}

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

	return 0;
}

int hp_io_t__on_data(hp_iocp * iocpctx, int index, char * buf, size_t len)
{
}
#endif /* _MSC_VER */


int hp_io_add(hp_io_ctx * ioctx, hp_io_t * io, hp_sock_t fd
	, hp_io_on_data on_data, hp_io_on_error on_error)
{
	if(!(ioctx && io))
		return -1;
	memset(io, 0, sizeof(hp_io_t));

//	io->c = ioctx;

	io->fd = fd;
	io->on_data = on_data;
	io->on_error = on_error;

	int rc;
#ifndef _MSC_VER

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
	io->io = ioctx;
	io->index = hp_iocp_add(&ioctx->iocpctx, 0, 0, fd, 0, hp_io_t__on_data, hp_io_t__on_error, 0);
#endif /* _MSC_VER */
	return rc;
}

/////////////////////////////////////////////////////////////////////////////////////
#ifndef NDEBUG
#include "hp_net.h" /* hp_net_connect */
#include "klist.h"  /* list_head */
#include "sdsinc.h"
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
};

static struct test s_testobj, * s_test = &s_testobj;
static struct server s_sreverobj, * s_srever = &s_sreverobj;

int on_data(hp_io_t * io, char * buf, size_t * len)
{
	assert(io && buf && len);

//	struct client * node = (struct client *)list_entry(&s_srever->clients, struct client, li);
//	assert(node);

	s_test->in = sdscatlen(s_test->in, buf, *len);

	assert(strncmp(s_test->in, s_test->out, strlen(s_test->in)) == 0);

	return 0;
}

int server_on_data(hp_io_t * io, char * buf, size_t * len)
{
	assert(io && buf);
	int rc;

	rc = hp_io_write(io, buf, *len, (void *)-1, 0);
	assert(rc == 0);

	*len = 0;

	return 0;
}

static int server_on_error(hp_io_t * io, int err, char const * errstr)
{
	return 0;
}

static int on_error(hp_io_t * io, int err, char const * errstr)
{
	return 0;
}

static int on_accept(hp_sock_t fd)
{
	int rc;

	int tcp_keepalive = cfgi("tcp-keepalive");
	if(hp_net_set_alive(fd, tcp_keepalive) != 0)
		hp_log(stderr, "%s: WARNING: socket_set_alive failed, interval=%ds\n", __FUNCTION__, tcp_keepalive);

	struct client * c = calloc(1, sizeof(struct client));
	list_add_tail(&c->li, &s_srever->clients);

	rc = hp_io_add(&s_srever->ioctx, &c->io, fd, server_on_data, server_on_error);
	assert(rc == 0);

	return 0;
}

int test_hp_io_t_main(int argc, char ** argv)
{
	int rc;

	INIT_LIST_HEAD(&s_srever->clients);

	s_test->in = sdsempty();
	s_test->out = sdscatprintf(sdsempty(), "GET %s HTTP/1.1\r\nHost: %s:%d\r\n\r\n"
				, s_url, server_ip, server_port);

	struct client cobj, *c = &cobj;

	hp_sock_t listen_fd = hp_net_listen(7006);
	hp_sock_t fd = hp_net_connect("127.0.0.1", 7006);

	/* init with listen socket */
	rc = hp_io_init(&s_srever->ioctx, listen_fd, on_accept);
	assert(rc == 0);

	/* add connect socket */
	rc = hp_io_add(&s_srever->ioctx, &c->io, fd, on_data, on_error);
	assert(rc == 0);

	rc = hp_io_write(&c->io, sdsdup(s_test->out), sdslen(s_test->out), (hp_io_free_t)sdsfree, 0);

	/* run event loop */
	hp_io_run(&s_srever->ioctx, 200);

	hp_io_uninit(&s_srever->ioctx);

	/**/
	sdsfree(s_test->out);

	return 0;
}

#endif /* NDEBUG */
