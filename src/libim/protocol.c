
/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2020/1/6
 *
 *
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */
#ifndef _MSC_VER

#include <unistd.h>
#include <limits.h>		/* INT_MAX */
#include <sys/time.h>   /* gettimeofday */
#include <sys/ioctl.h>  /* ioctl */
#include <string.h> 	/* strlen */
#include <arpa/inet.h>	/* inet_ntop */
#include <stdio.h>
#include <stddef.h>		/* size_t */
#include <string.h>     /* memset, ... */
#include <errno.h>      /* errno */
#include <assert.h>     /* define NDEBUG to disable assertion */
#include <time.h>
#include <stdlib.h>
#include "cJSON/cJSON.h"	/* cJSON */
#include "hp/hp_epoll.h"    /* hp_epoll */
#include "hp/hp_io.h"       /* hp_eti */
#include "hp/hp_timerfd.h"  /* hp_timerfd */
#include "klist.h"        /* list_head */
#include "sds/sds.h"     /* sds */
#include "hp/hp_log.h"     /* hp_log */
#include "hp/hp_libc.h"    /* hp_min */
#include "hp/hp_net.h"     /*  */
#include "hp/str_dump.h"   /* dumpstr */
#include "hp/string_util.h"/* sdslen_null */
#include "hp_libim.h"
#include "c-vector/cvector.h"
#ifndef __ANDROID__
#include "uuid/uuid.h"
#else
#endif /* __ANDROID__ */

extern int gloglevel;
/////////////////////////////////////////////////////////////////////////////////////

static int libim__accept_cb(struct epoll_event * ev);
static int libim__io_cb(struct epoll_event * ev);
static int libim__data_cb(char* buf, size_t* len, void* arg);
static void libim__write_error_cb(struct hp_eto * eto, int err, void * arg);
static void libim__read_error_cb(struct hp_eti * eti, int err, void * arg);

int libim_cli_init(libim_cli * client
		, int fd, libim_ctx * ctx)
{
	if(!(client && ctx))
		return -1;

	memset(client, 0, sizeof(libim_cli));

	client->fd = fd;
	client->ctx = ctx;
	client->id = ctx->cid;

#ifndef __ANDROID__
	uuid_t uuid;
	uuid_generate_time(uuid);
	char uuidstr[64] = "";
	uuid_unparse(uuid, uuidstr);

	client->sid = sdsnew(uuidstr);
#else
	client->sid = sdsnew("");
#endif /* __ANDROID__ */


	hp_eti_init(&client->eti, 1024 * 128);
	hp_eto_init(&client->eto, HP_ETIO_VEC);

	struct hp_eto * eto = &client->eto;
	eto->write_error = libim__write_error_cb;

	struct hp_eti * eti = &client->eti;
	eti->read_error = libim__read_error_cb;
	eti->pack = libim__data_cb;

	hp_epolld_set(&client->epolld, client->fd, libim__io_cb, client);

//	uv_timer_init(loop, &client->timer_info);
//	client->timer_info.data = client;

	return 0;
}

void libim_cli_uninit(libim_cli * client)
{
	if(!client)
		return ;

	sdsfree(client->sid);

	hp_eti_uninit(&client->eti);
	hp_eto_uninit(&client->eto);
}
/////////////////////////////////////////////////////////////////////////////////////////

static void libim__close(libim_cli *client, int err, char const * errstr)
{
	assert(client && client->ctx);

	hp_epoll_del(client->ctx->efds, client->fd,  EPOLLIN | EPOLLOUT |  EPOLLET, &client->epolld);

	list_del(&client->list);
	--client->ctx->n_cli;

	if(gloglevel > 3)
		hp_log((err != 0? stderr : stdout), "%s: close session, fd=%d, err=%d/'%s', total=%d\n"
			, __FUNCTION__, client->fd
			, err, errstr, client->ctx->n_cli);

//	uv_timer_stop(&client->timer_info);
	close(client->fd);

	if(client->ctx->proto.delete)
		client->ctx->proto.delete(client);
}

static int libim__data_cb(char* buf, size_t* len, void * arg)
{
	assert(buf && len && arg);

	int rc;
	struct epoll_event* ev = (struct epoll_event*) arg;
	assert(ev);
	libim_cli * client = (libim_cli *) hp_epoll_arg(ev);
	assert(client);
	libim_ctx * ctx = client->ctx;
	assert(ctx);

	if(!ctx->proto.unpack){
		*len = 0;
	}

	if (!(*len > 0))
		return EAGAIN;

	for (; *len > 0 ;) {
		libimhdr * imhdr = 0;
		char * body = 0;
		int n = ctx->proto.unpack(ctx->magic, buf, len, ctx->proto.flags, &imhdr, &body);
		if (n < 0) {
			*len = 0;
			return EAGAIN;
		} else if (n == 0)
			return EAGAIN;

		if(ctx->proto.dispatch)
			ctx->proto.dispatch(client, imhdr, body);
	}
	return EAGAIN;
}

static void libim__write_error_cb(struct hp_eto * eto, int err, void * arg)
{
	struct epoll_event * ev = (struct epoll_event *)arg;
	assert(ev);
	ev->events = 0;

	struct libim_cli * client = (struct libim_cli *)hp_epoll_arg(ev);
	assert(client);

	libim__close(client, err, strerror(err));
}

static void libim__read_error_cb(struct hp_eti * eti, int err, void * arg)
{
	struct epoll_event * ev = (struct epoll_event *)arg;
	assert(ev);
	ev->events = 0;

	struct libim_cli * client = (struct libim_cli *)hp_epoll_arg(ev);
	assert(client);

	libim__close(client, err, err == 0? "EOF" : strerror(err));
}

static int libim__io_cb(struct epoll_event * ev)
{
	assert(ev);

	if((ev->events & EPOLLERR)){
		struct libim_cli * client = (struct libim_cli *)hp_epoll_arg(ev);
		assert(client);

		ev->events = 0;
		libim__close(client, EPOLLERR, "EPOLLERR");

		return 0;
	}

	if((ev->events & EPOLLIN)){
		struct libim_cli * client = (struct libim_cli *)hp_epoll_arg(ev);
		assert(client);

		hp_eti_read(&client->eti, client->fd, ev);
	}

	if((ev->events & EPOLLOUT)){
		struct libim_cli * client = (struct libim_cli *)hp_epoll_arg(ev);
		assert(client);

		hp_eto_write(&client->eto, client->fd, ev);
	}

	return 0;
}

static int libim__accept_cb(struct epoll_event * ev)
{
	libim_ctx * ctx = (libim_ctx * )hp_epoll_arg(ev);
	assert(ctx);
	assert(ctx->efds && ctx->tcp_keepalive);

	int rc;
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

		char cliaddstr[64];
		get_ipport_cstr2(&clientaddr, ":", cliaddstr, sizeof(cliaddstr));

		if(gloglevel > 2)
			hp_log(stdout, "%s: new connect from '%s', fd=%d, total=%d\n"
				, __FUNCTION__, cliaddstr, confd, ctx->n_cli);

		unsigned long sockopt = 1;
		ioctl(confd, FIONBIO, &sockopt);

		if(hp_net_set_alive(confd, ctx->tcp_keepalive) != 0)
			hp_log(stderr, "%s: WARNING: socket_set_alive failed, interval=%ds\n", __FUNCTION__, ctx->tcp_keepalive);

		if(!ctx->proto.new){
			close(confd);
			continue;
		}

		++ctx->cid;
		libim_cli * client = ctx->proto.new(confd, ctx);
		if(!client){
			close(confd);
			continue;
		}

		list_add_tail(&client->list, &ctx->cli);
		++ctx->n_cli;

		if(ctx->cid + 1 == INT_MAX)
			ctx->cid = 0;

		/* try to add to epoll event system */
		rc = hp_epoll_add(ctx->efds, confd,  EPOLLIN | EPOLLOUT |  EPOLLET, &client->epolld);
		assert(rc == 0);
	}
	return 0;
}

libim_cli * xhmdm_libim_find_if(libim_ctx * ctx, void * key,
		int (*cmp)(libim_cli * node, void * key))
{
	if(!(ctx && cmp))
		return 0;


	struct list_head * pos, * next;;
	list_for_each_safe(pos, next, &ctx->cli){
		libim_cli* node = (libim_cli *)list_entry(pos, libim_cli, list);
		assert(node);

		if(cmp(node, key))
			return node;
	}

	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////

int xhmdm_libim_init(libim_ctx * ctx, hp_epoll * efds
		, int fd, int tcp_keepalive
		, libim_proto proto
		, unsigned int magic)
{
	if(!(ctx && efds))
		return -1;

	memset(ctx, 0, sizeof(libim_ctx));
	ctx->efds = efds;
	ctx->magic = magic;

	hp_epolld_set(&ctx->ed, fd, libim__accept_cb, ctx);
	ctx->tcp_keepalive = tcp_keepalive;

	INIT_LIST_HEAD(&ctx->cli);
	ctx->n_cli = 0;

	if(!proto.new)
		proto = libim_default_proto_get();
	ctx->proto = proto;

	return 0;
}

void xhmdm_libim_uninit(libim_ctx * ctx)
{
	if(!ctx)
		return;

	struct list_head * pos, * next;;
	list_for_each_safe(pos, next, &ctx->cli){
		libim_cli* client = (libim_cli *)list_entry(pos, libim_cli, list);
		assert(client);

		list_del(&client->list);

		libim_cli_uninit(client);
		free(client);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
#include <assert.h>        /* assert */

int test_xhmdm_tcpcli_main(int argc, char ** argv)
{
	int rc;

	int libim_port = 7006, tcp_keepalive = 4;
	hp_epoll efdsobj, * efds = &efdsobj;
	rc = hp_epoll_init(efds, 5000);
	assert(rc == 0);

	int fd = hp_net_listen(libim_port);
	assert(fd > 0);

	libim_ctx ctxobj = { .efds = efds, .tcp_keepalive = tcp_keepalive }, * ctx = &ctxobj;
	hp_epolld_set(&ctx->ed, fd, libim__accept_cb, ctx);
	rc = hp_epoll_add(efds, fd, EPOLLIN, &ctx->ed);
	assert(rc == 0);

	hp_log(stdout, "%s: listening on port=%d, waiting for connection ...\n", __FUNCTION__, libim_port);

	hp_epoll_run(efds, 200, (void *)-1);

	close(fd);
	hp_epoll_uninit(efds);

	return 0;
}

#endif /* NDEBUG */
#endif /* _MSC_VER */
