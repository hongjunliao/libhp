/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/5/18
 *
 * the epoll event system
 * 2023/8/5 updated: add hp_epoll::ed, hp_epoll_add(arg)
 * */
/////////////////////////////////////////////////////////////////////////////////////

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef HAVE_SYS_EPOLL_H

#include "hp/hp_epoll.h"   /* hp_epoll */
#include "hp/hp_log.h"     /* hp_log */
#include <sys/epoll.h>  /* epoll_event */
#include <unistd.h>
#include <string.h> 	/* strlen */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>     /* memset, ... */
#include <errno.h>      /* errno */
#include <assert.h>     /* define NDEBUG to disable assertion */
#include <sys/epoll.h>  /* epoll_event */
#include "hp/hp_search.h" /* hp_lfind */
#include "hp/hp_libc.h" /* hp_tuple2_t */
/////////////////////////////////////////////////////////////////////////////////////

struct hp_epolld {
	int                  fd;
	hp_epoll_cb_t        fn;
	void *               arg;
	int (* on_loop)(void * arg);
};
#define hp_epolld_fd(ev) (((hp_epolld *)((ev)->data.ptr))->fd)
#define return_(code) { rc = code; goto ret; }

/////////////////////////////////////////////////////////////////////////////////////

int hp_epoll_run(hp_epoll * epo, int mode)
{
	int rc, i, n;

	int runed = 0;
	for(;!runed;){
		if(mode){
			if(epo->before_wait){
				int rc = epo->before_wait(epo);
				if(rc != 0) return rc;
			}
			runed = 1;
		}

		//try read/write: for epoll in EPOLLET mode, we need drive
		//it manually
		for(i = 0; i < epo->ev_len; ++i){
			assert(epo->ed[i]);
			if(epo->ed[i] && epo->ed[i]->fn){
				epoll_event e = { .events = EPOLLIN | EPOLLOUT, .data = epo->ed[i] };
				epo->ed[i]->fn(&e, epo->ed[i]->arg);
			}
		}

		n = epoll_wait(epo->fd, epo->ev, epo->ev_len, epo->timeout);
		if(n < 0){
			if(errno == EINTR || errno == EAGAIN)
				continue;

			hp_log(stderr, "%s: epoll_wait failed, errno=%d, error='%s'\n"
					, __FUNCTION__, errno, strerror(errno));
			return -7;
		}

		for (i = 0; i < n; ++i) {
			epoll_event * ev = epo->ev + i;
			hp_epolld * ed = (hp_epolld  *)ev->data.ptr;
			if(ed && ed->fn){
				rc = ed->fn(ev, ed->arg);
			}
		}

		for(i = 0; i < epo->ev_len; ++i){
			assert(epo->ed[i]);
			if(epo->ed[i]->on_loop)
				epo->ed[i]->on_loop(epo->ed[i]->arg);
		}
	}

	return 0;
}

int hp_epoll_init(hp_epoll * epo, int max_ev_len, int timeout
		, int (* before_wait)(hp_epoll * epo), void * arg)
{
	if(!epo) return -1;
	if(max_ev_len <= 0)
		max_ev_len = 65535;

	memset(epo, 0, sizeof(hp_epoll));

	int epollfd = epoll_create1(0);;
	if (epollfd == -1) {
		hp_log(stderr, "%s: epoll_create1 failed, errno=%d, error='%s'\n"
				, __FUNCTION__, errno, strerror(errno));
		return -2;
	}
	epo->fd = epollfd;

	epo->ev = malloc(sizeof(epoll_event) * max_ev_len);
	epo->ed = malloc(sizeof(hp_epolld *) * max_ev_len);
	epo->max_ev_len = max_ev_len;
	epo->before_wait = before_wait;
	epo->arg = arg;

	return 0;
}

void hp_epoll_uninit(hp_epoll * epo)
{
	if(!epo) return;

	free(epo->ev);
	free(epo->ed);
}

/////////////////////////////////////////////////////////////////////////////////////

hp_tuple2_t(hp_dofind_tuple, void * /*key*/, hp_cmp_fn_t /*on_cmp*/);

static int hp_epoll_do_find(const void * k, const void * e)
{
	assert(e && k);
	hp_dofind_tuple tu = *(hp_dofind_tuple *)k;
	hp_epolld * epfd = *(hp_epolld **)e;
	return !(tu._2? tu._2(tu._1, epfd->arg) : tu._1 == epfd->arg);
}

void * hp_epoll_find(hp_epoll * epo, void * key, hp_cmp_fn_t on_cmp)
{
	if(!(epo))
		return 0;
	hp_dofind_tuple k = { ._1 = key, ._2 = on_cmp };
	hp_epolld ** pepfd = hp_lfind(&k, epo->ed, epo->ev_len, sizeof(hp_epolld *), hp_epoll_do_find);
	return pepfd? (*pepfd)->arg : 0;
}

/////////////////////////////////////////////////////////////////////////////////////

static int find_by_fd(const void * k, const void * e)
{
	assert(e && k);
	hp_sock_t fd = *(hp_sock_t *)k;
	hp_epolld * epfd = *(hp_epolld **)e;

	return !(epfd->fd == fd);
}
int hp_epoll_add(hp_epoll * epo, hp_sock_t fd, int events,
		hp_epoll_cb_t  fn, hp_epoll_loop_t on_loop, void * arg)
{

	int rc = 0;

	if(!(epo && epo->ev && hp_sock_is_valid(fd))) return -1;

	epoll_event e = { .events = events };
	hp_epolld * epfd, ** pepfd = hp_lfind(&fd, epo->ed, epo->ev_len, sizeof(hp_epolld *), find_by_fd);
	epfd = pepfd? *pepfd : 0;

	if(!epfd && epo->ev_len >= epo->max_ev_len){ return -2; }

	if (!epfd){

		epfd = malloc(sizeof(hp_epolld)); assert(epfd);
		epfd->fd = fd;
		epfd->fn = fn;
		epfd->on_loop = on_loop;
		epfd->arg = arg;

		e.data.ptr = epfd;
		rc = epoll_ctl(epo->fd, EPOLL_CTL_ADD, fd, &e);
		if(rc == 0){

			epo->ed[epo->ev_len++] = epfd;
			return_(0);
		}
		else if(errno != EEXIST)
			return_(-2);
		//FIXME: should NOT come here, epfd should NOT NULL here
		assert(0);
	}
	else{
		assert(epfd->fd == fd);
		epfd->fn = fn;
		epfd->arg = arg;
		epfd->on_loop = on_loop;
	}

	e.data.ptr = epfd;
	if (!(epoll_ctl(epo->fd, EPOLL_CTL_MOD, fd, &e) == 0)){
		return_(-3);
	}

ret:
	if(rc != 0){
		hp_log(stderr, "%s: epoll_ctl failed, epollfd=%d, fd=%d, errno=%d, error='%s'\n"
			, __FUNCTION__, epo->fd, fd, errno, strerror(errno));
	}

	return rc;
}

/////////////////////////////////////////////////////////////////////////////////////

int hp_epoll_rm(hp_epoll * epo, int fd)
{

	int rc;
	if(!(epo)) return -1;

	hp_epolld ** pepfd = hp_lfind(&fd, epo->ed, epo->ev_len, sizeof(hp_epolld *), find_by_fd);

	epoll_event e = { .events = EPOLLIN | EPOLLOUT, .data = pepfd? *pepfd : 0 };
	if (epoll_ctl(epo->fd, EPOLL_CTL_DEL, fd, &e) != 0){
		if(hp_log_level > 1)
			hp_log(stderr, "%s: epoll_ctl failed, fd=%d, errno=%d, error='%s'\n"
				, __FUNCTION__, fd, errno, strerror(errno));
	}

	if(!pepfd)
		return -2;

	int index = pepfd - epo->ed;
	free(*pepfd);

	if( index + 1 < epo->ev_len ){
		memmove(epo->ed + index, epo->ed + index + 1, sizeof(hp_epolld *) * (epo->ev_len - (index + 1)));
	}
	--epo->ev_len;

	return 0;
}

char * hp_epoll_e2str(int events, char * buf, int len)
{
	if(!buf && len > 0) return 0;

	buf[0] = '\0';
	int n = snprintf(buf, len, "%s%s%s%s"
			, (events & EPOLLERR?   "EPOLLERR | " : "")
			, (events & EPOLLET?    "EPOLLET | " : "")
			, (events & EPOLLIN?    "EPOLLIN | " : "")
			, (events & EPOLLOUT?   "EPOLLOUT | " : "")
			);
	if(buf[0] != '\0' && n >= 3 )
		buf[n - 3] = '\0';

	int left = events & (~(EPOLLERR | EPOLLET | EPOLLIN | EPOLLOUT));
	if(!(left == 0))
		snprintf(buf + n, len - n, " | %d", left);

	return buf;
}

/////////////////////////////////////////////////////////////////////////////////////
//tests

#ifndef NDEBUG
#include "hp/hp_net.h"//hp_tcp_listen
#include "hp/hp_log.h"
#include "hp/hp_config.h"
#include "hp/hp_io.h"	//hp_rd
#include "hp/hp_err.h"	//hp_err
#include "hp/hp_assert.h"	//hp_assert
#define cfg hp_config_test
#define cfgi(key) atoi(hp_config_test(key))

/////////////////////////////////////////////////////////////////////////////////////

typedef struct server {
	hp_rd listenrd;
	int quit;
} server;

// for simplicity, we use the same "client" struct for both server and client side code
typedef struct client {
	hp_epoll * epo;
	hp_rd rd;
	hp_wr wr;
	sds in;
	hp_sock_t fd;
	server * s;
	int t;
	char addr[64];
} client;

static int client_init(client * c, hp_epoll * epo, hp_sock_t fd,
		int (* data)(char * buf, size_t * len, void * arg),
		void (* read_error)(hp_rd * rd, int err, void * arg),
		void (* write_error)(hp_wr * wr, int err, void * arg))
{
	int rc;
	if(!(c && epo)) return -1;
	c->in = sdsempty();
	c->epo = epo;
	c->fd = fd;
	rc = hp_rd_init(&c->rd, 0, data, read_error); assert(rc == 0);
	rc = hp_wr_init(&c->wr, 0, write_error); assert(rc == 0);
	return 0;
}
static void client_uninit(client * c)
{
	if(!c) return;
	hp_wr_uninit(&c->wr);
	hp_rd_uninit(&c->rd);
	sdsfree(c->in);
	return ;
}

static int test_hp_epoll_on_io(epoll_event * ev, void * arg,
		void (* on_error )(client * c, int err))
{
	int rc = 0;
	assert(arg && ev);
	client * c = arg;
	hp_epoll * epo = (hp_epoll *)c->epo;
	assert(epo);
	assert(on_error);

	if((ev->events & (EPOLLERR | EPOLLHUP))){
		char buf[128] = "";
		hp_epoll_e2str(ev->events, buf, sizeof(buf));

		on_error(c, ev->events);
		return -1;
	}
	if((ev->events & EPOLLIN)){
		hp_rd_read(&c->rd, c->fd, c);
		rc = c->rd.err;
	}
	if(rc != 0) return rc;

	if((ev->events & EPOLLOUT)){
		hp_wr_write(&c->wr, c->fd, c);
		rc = c->rd.err;
	}
	return rc;
}

static int server_on_data(char * buf, size_t * len, void * arg)
{
	int rc;
	assert(buf && len && arg);
	client * c = arg;

	c->in = sdscatlen(c->in, buf, *len);
	*len = 0;

	if(strncmp(c->in, "hello", strlen("hello")) == 0){

		rc = hp_wr_add(&c->wr, sdsnew("world"), strlen("world"), (hp_free_t)sdsfree, 0);
		assert(rc == 0);
	}

	return 0;
}

static void test_hp_epoll_server_on_error(client * c, int err)
{
	assert(c && c->epo);
	hp_err_t errstr = "%s";
	hp_log(stdout, "%s: delete TCP connection '%s', %d/'%s', IO total=%d\n", __FUNCTION__
			, c->addr, err, hp_err(err, errstr), c->epo->ev_len);

	hp_epoll_rm(c->epo, c->fd);
	hp_close(c->fd);

	client_uninit(c);
	free(c);
}
static void server_on_read_error(hp_rd * rd, int err, void * arg){ return test_hp_epoll_server_on_error(arg, err); }
static void server_on_write_error(hp_wr * wr, int err, void * arg){ return test_hp_epoll_server_on_error(arg, err); }

static int test_hp_epoll_server_on_io(epoll_event * ev, void * arg)
{
	return test_hp_epoll_on_io(ev, arg, test_hp_epoll_server_on_error);
}

static int test_on_accept(epoll_event * ev, void * arg)
{
	int rc;
	assert(ev && arg);
	hp_epoll * epo = (hp_epoll *)arg;
	if((ev->events & (EPOLLERR))){
		return -1;
	}

	for(;;){
		struct sockaddr_in addr;
		int len = (int)sizeof(addr);
		hp_sock_t confd = accept(hp_epolld_fd(ev), (struct sockaddr *)&addr, &len);

		if(!hp_sock_is_valid(confd)){
			if (errno == EINTR || errno == EAGAIN) { return 0; }

			hp_log(stderr, "%s: accept failed, errno=%d, error='%s'\n", __FUNCTION__, errno, strerror(errno));
			continue;
		}
		if(hp_tcp_nodelay(confd) < 0)
			{ hp_close(confd); continue; }

		client * c = calloc(1, sizeof(client));
		rc = client_init(c, epo, confd, server_on_data, server_on_read_error, server_on_write_error);  assert(rc == 0);

		rc = hp_epoll_add(epo, confd, EPOLLIN | EPOLLOUT | EPOLLET, test_hp_epoll_server_on_io, 0, c);
		assert(rc == 0);

		char * buf = c->addr;
		hp_log(stdout, "%s: new TCP connection from '%s', IO total=%d\n", __FUNCTION__
				, hp_addr4name(&addr, ":", buf, sizeof(buf)), epo->ev_len);
	}

	return 0;
}

static int client_on_data(char * buf, size_t * len, void * arg)
{
	int rc;
	assert(buf && len && arg);
	client * c = arg;
	assert(c->epo);

	c->in = sdscatlen(c->in, buf, *len);
	*len = 0;

	if(strncmp(buf, "world", strlen("world")) == 0){
		hp_log(stdout, "%s: client %d test done!\n", __FUNCTION__, c->fd);
		// client done
		shutdown(c->fd, SHUT_RDWR);
	}

	return 0;
}

static void client_on_error(client * c, int err)
{
	int rc;
	hp_log(stdout, "%s: disconnected err=%d\n", __FUNCTION__, err);

	rc = hp_epoll_rm(c->epo, c->fd);
	assert(rc == 0);

	hp_close(c->fd);
}
static void client_on_read_error(hp_rd * rd, int err, void * arg){ return client_on_error(arg, err); }
static void client_on_write_error(hp_wr * wr, int err, void * arg){ return client_on_error(arg, err); }
static int test_hp_epoll_client_on_io(epoll_event * ev, void * arg)
{
	return test_hp_epoll_on_io(ev, arg, client_on_error);
}
/////////////////////////////////////////////////////////////////////////////////////
static void add_remove_test(int n)
{
	int rc;
	hp_sock_t fd[1024], i;
	hp_epoll ghp_poobj = { 0 }, * epo = &ghp_poobj;
	hp_epoll_init(epo, n, 200, 0, 0);
	assert(hp_epoll_size(epo) == 0);

	for(i = 0; i < n; ++i){
		fd[i] = i;

		rc = hp_epoll_add(epo, fd[i], 0, 0, 0, 0);
		assert(rc == 0);

		//add same fd
		rc = hp_epoll_add(epo, fd[i], EPOLLIN | EPOLLOUT, test_hp_epoll_client_on_io, 0, 0);
		assert(rc == 0);

		assert(hp_epoll_size(epo) == i + 1);
	}
	rc = hp_epoll_add(epo, n + 1, EPOLLIN | EPOLLOUT, test_hp_epoll_client_on_io, 0, 0);
	assert(rc < 0);

	for(i = 0; i < n; ++i){
		rc = hp_epoll_rm(epo, fd[i]);
		assert(rc == 0);
	}

	assert(hp_epoll_size(epo) == 0);
	hp_epoll_uninit(epo);
}
/////////////////////////////////////////////////////////////////////////////////////

static int search_test_on_cmp(const void *k, const void *e)
{
	assert(e && k);
	hp_sock_t fd = *(hp_sock_t *)k;
	hp_sock_t fde = *(hp_sock_t *)e;

	return (fde == fd);
}
static void search_test(int n)
{
	int rc;
	hp_sock_t fd[1024], i;
	hp_epoll ghp_poobj = { 0 }, * epo = &ghp_poobj;
	hp_epoll_init(epo, n, 200, 0, 0);
	assert(hp_epoll_size(epo) == 0);

	for(i = 0; i < n; ++i){
		hp_sock_t confd = hp_tcp_connect("127.0.0.1", 1);
		hp_assert(hp_sock_is_valid(confd), "i=%i", i);
		fd[i] = confd;

		assert(!hp_epoll_find(epo, fd + i, search_test_on_cmp));

		rc = hp_epoll_add(epo, fd[i], 0, 0, 0, fd + i);
		assert(rc == 0);

		assert(hp_epoll_size(epo) == i + 1);

		assert(hp_epoll_find(epo, fd + i, search_test_on_cmp));

		rc = hp_epoll_rm(epo, fd[i]);
		assert(rc == 0);

		assert(!hp_epoll_find(epo, fd + i, search_test_on_cmp));

		rc = hp_epoll_add(epo, fd[i], 0, 0, 0, fd + i);
		assert(rc == 0);
	}

	assert(hp_epoll_find(epo, fd + 0, search_test_on_cmp));
	assert(hp_epoll_find(epo, fd + n / 2, search_test_on_cmp));
	assert(hp_epoll_find(epo, fd + n - 1, search_test_on_cmp));
	assert(!hp_epoll_find(epo, search_test_on_cmp, search_test_on_cmp));

	for(i = 0; i < n; ++i){
		rc = hp_epoll_rm(epo, fd[i]);
		assert(rc == 0);
		hp_close(fd[i]);
	}

	assert(hp_epoll_size(epo) == 0);
	hp_epoll_uninit(epo);
}

/////////////////////////////////////////////////////////////////////////////////////
static int checkt(const void * k, const void * e)
{
	assert(e && k);
	client * c = (client *)e;
	return !(c->t == 0);
}

static void client_server_echo_test(int nclient)
{
	int i, rc = 0;
	client * c = (client *)calloc(70000, sizeof(client));
	assert(c);
	int port = cfgi("tcp.port");
	hp_epoll ghp_poobj = { 0 }, * epo = &ghp_poobj;
	// +1 for listen_fd
	hp_epoll_init(epo, 2 * nclient + 1, 200, 0, 0);

	hp_sock_t listen_fd = hp_tcp_listen(port); assert(hp_sock_is_valid(listen_fd));
	rc = hp_epoll_add(epo, listen_fd, EPOLLIN, test_on_accept, 0, epo); assert(rc == 0);

	/* add connect socket */
	for(i = 0; i < nclient; ++i){

		hp_sock_t confd = hp_tcp_connect("127.0.0.1", port);
		hp_assert(hp_sock_is_valid(confd), "i=%i", i);

		rc = client_init(c + i, epo, confd, client_on_data, client_on_read_error, client_on_write_error);
		assert(rc == 0);

		rc = hp_epoll_add(epo, confd, EPOLLIN | EPOLLOUT | EPOLLET, test_hp_epoll_client_on_io, 0, c + i);
		assert(rc == 0);

		rc = hp_wr_add(&c[i].wr, sdsnew("hello"), strlen("hello"), (hp_free_t)sdsfree, 0);
		assert(rc == 0);
	}

	hp_log(stdout, "%s: listening on TCP port=%d, waiting for connection ...\n", __FUNCTION__, port);
	for(; hp_epoll_size(epo) > 1;){
		hp_epoll_run(epo, 1);

//		client * ic = hp_lfind(0, c, nclient, sizeof(client), checkt);
//		if(!ic) break;
	}

	/*clear*/
	for(i = 0; c[i].in; ++i){
		client_uninit(c + i);
	}
	hp_epoll_uninit(epo);
	hp_close(listen_fd);
	free(c);
}
/////////////////////////////////////////////////////////////////////////////////////

int test_hp_epoll_main(int argc, char ** argv)
{
	int rc;
	//basics
	{
		char buf[128] = "";
		assert(strcmp(hp_epoll_e2str(0, buf, sizeof(buf)), "") == 0);
		assert(strcmp(hp_epoll_e2str(EPOLLERR, buf, sizeof(buf)), "EPOLLERR") == 0);
		assert(strcmp(hp_epoll_e2str(EPOLLET, buf, sizeof(buf)), "EPOLLET") == 0);
		assert(strcmp(hp_epoll_e2str(EPOLLIN, buf, sizeof(buf)), "EPOLLIN") == 0);
		assert(strcmp(hp_epoll_e2str(EPOLLOUT, buf, sizeof(buf)), "EPOLLOUT") == 0);

		assert(strcmp(hp_epoll_e2str(EPOLLERR | EPOLLET, buf, sizeof(buf)), "EPOLLERR | EPOLLET") == 0);
		assert(strcmp(hp_epoll_e2str(EPOLLET | EPOLLIN, buf, sizeof(buf)), "EPOLLET | EPOLLIN") == 0);

		assert(strcmp(hp_epoll_e2str(EPOLLERR | EPOLLET | EPOLLIN, buf, sizeof(buf)), "EPOLLERR | EPOLLET | EPOLLIN") == 0);

		assert(strcmp(hp_epoll_e2str(EPOLLERR | EPOLLET | EPOLLIN | EPOLLOUT, buf, sizeof(buf)), "EPOLLERR | EPOLLET | EPOLLIN | EPOLLOUT") == 0);

		static hp_epoll ghp_efdsobj = { 0 }, * epo = &ghp_efdsobj;
		rc = hp_epoll_init(epo, 0, 200, 0, 0); assert(rc == 0); hp_epoll_uninit(epo);
		rc = hp_epoll_init(epo, 1, 200, 0, 0); assert(rc == 0); hp_epoll_uninit(epo);
		rc = hp_epoll_init(epo, 2, 200, 0, 0); assert(rc == 0); hp_epoll_uninit(epo);
	}

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
	//client,server echo test
	{
		client_server_echo_test(1);
		client_server_echo_test(2);
		client_server_echo_test(3);
		client_server_echo_test(500);
	}
	return 0;
}
#endif /* NDEBUG */

#endif /*HAVE_SYS_EPOLL_H*/
