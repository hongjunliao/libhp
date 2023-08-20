
/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2020/1/6
 *
 *
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_DEPRECADTED

#if !defined(__linux__) && !defined(_MSC_VER)
#elif !defined(_MSC_VER)


#include <unistd.h>
#include <sys/time.h>   /* gettimeofday */
#include <sys/ioctl.h>  /* ioctl */
#include <string.h> 	/* strlen */
#include <arpa/inet.h>	/* inet_ntop */
#include <stdio.h>
#include <string.h>     /* memset, ... */
#include <errno.h>      /* errno */
#include <assert.h>     /* define NDEBUG to disable assertion */
#include <time.h>
#include <stdlib.h>
#include "hp/hp_epoll.h"    /* hp_epoll */
#include "hp/hp_io.h"       /* hp_rd */
#include "hp/hp_timerfd.h"  /* hp_timerfd */
#include "hp/sdsinc.h"     /* sds */
#include "hp/hp_io.h"
#include "hp/hp_log.h"     /* hp_log */
#include "hp/hp_libc.h"    /* hp_min */
#include "hp/hp_net.h"     /*  */
#include "hp/str_dump.h"   /* dumpstr */
#include "hp/string_util.h"/* sdslen_null */
#include "hp/klist.h"
#include "c-vector/cvector.h"
#include "hp/hp_tcpio.h"
#include "hp/hp_libc.h"
/////////////////////////////////////////////////////////////////////////////////////
typedef struct tcpio_cli tcpio_cli;

struct tcpio_cli {
	struct hp_rd    eti;        /*  */
	struct hp_wr    eto;        /* */
	struct hp_epolld epolld;
};

static int accept_cb(struct epoll_event * ev);
static int io_cb(struct epoll_event * ev);

int hp_tcpio_init(hp_tcpio * ctx, hp_epoll * efds, int fd
		, void * conn, void * pack)
{
	if(!ctx)
		return -1;

	int rc;
	ctx->efds = efds;
	ctx->conn = conn;
	ctx->pack = pack;

	hp_epolld_set(&ctx->ed, fd, accept_cb, ctx);
	rc = hp_epoll_add(efds, fd, EPOLLIN, &ctx->ed);
	assert(rc == 0);

	return 0;
}

void hp_tcpio_uninit(hp_tcpio * ctx)
{
	if(!ctx)
		return;

}
/////////////////////////////////////////////////////////////////////////////////////////


static int io_cb(struct epoll_event * ev)
{
#ifndef NDEBUG
		char buf[64];
		hp_log(stdout, "%s: fd=%d, events='%s'\n", __FUNCTION__
				, hp_epoll_fd(ev), hp_epoll_e2str(ev->events, buf, sizeof(buf)));
#endif /* NDEBUG */

	void * arg = hp_epoll_arg(ev);
	HP_UNUSED(arg);
	return 0;
}

static int pack_cb(char* buf, size_t* len, void* arg)
{
	*len = 0;
	return EAGAIN;
}

static void write_error_cb(struct hp_wr * eto, int err, void * arg)
{
	struct epoll_event * ev = (struct epoll_event *)arg;
	assert(ev);

	struct xhmdm_dev * client = (struct xhmdm_dev *)hp_epoll_arg(ev);
	assert(client);

}

static void read_error_cb(struct hp_rd * eti, int err, void * arg)
{
	struct epoll_event * ev = (struct epoll_event *)arg;
	assert(ev);

	struct xhmdm_dev * client = (struct xhmdm_dev *)hp_epoll_arg(ev);
	assert(client);

}

static int accept_cb(struct epoll_event * ev)
{
	hp_tcpio * ctx = (hp_tcpio * )hp_epoll_arg(ev);
	assert(ctx);
	assert(ctx->efds);

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
		unsigned long sockopt = 1;
		ioctl(confd, FIONBIO, &sockopt);

		char cliaddstr[64];
		hp_addr4name(&clientaddr, ":", cliaddstr, sizeof(cliaddstr));
		hp_log(stdout, "%s: new connect from '%s', fd=%d\n"
				, __FUNCTION__, cliaddstr, confd);

		void * arg = 0;
		int rc = ctx->conn(ctx, confd, &arg);
		if(rc == 0){
			tcpio_cli * client = calloc(1, sizeof(tcpio_cli));

			hp_epolld_set(&client->epolld, confd, io_cb, arg);

			hp_rd_init(&client->eti, 1024 * 2, pack_cb, read_error_cb);
			hp_wr_init(&client->eto, 2, write_error_cb);

			/* try to add to epoll event system */
			rc = hp_epoll_add(ctx->efds, confd,  EPOLLIN | EPOLLOUT |  EPOLLET, &client->epolld);
			assert(rc == 0);
		}
		else close(confd);
	}
	return 0;
}


/////////////////////////////////////////////////////////////////////////////////////////
#ifndef NDEBUG
#include <assert.h>        /* assert */

struct my_cli{
	int fd;
};

static int test_conn_cb(hp_tcpio * ctx, int confd, void **arg)
{
	assert(ctx && arg);

	struct my_cli * cli = calloc(1, sizeof(struct my_cli));
	cli->fd = confd;
	*arg = cli;

	return 0;
}

static int test_pack_cb(char* buf, size_t* len, void* arg)
{
	*len = 0;
	return EAGAIN;
}


int test_hp_tcpio_main(int argc, char ** argv)
{
	return 0;

	int rc;

	hp_epoll efdsobj, * efds = &efdsobj;
	rc = hp_epoll_init(efds, 5000);
	assert(rc == 0);

	int fd = hp_tcp_listen(7006);
	assert(fd > 0);

	hp_tcpio ctxobj, * ctx = &ctxobj;
	rc = hp_tcpio_init(ctx, efds, fd, test_conn_cb, test_pack_cb);
	assert(rc == 0);

	hp_log(stdout, "%s: listening on port=%d, waiting for dev connection ...\n", __FUNCTION__, 7006);

	hp_epoll_run(efds, 200, 0, 0);

	hp_tcpio_uninit(ctx);
	close(fd);
	hp_epoll_uninit(efds);

	return 0;
}

#endif /* NDEBUG */
#endif /* _MSC_VER */
#endif //#ifdef LIBHP_DEPRECADTED
