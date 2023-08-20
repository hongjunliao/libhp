/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2022/1/8
 *
 * the poll event system
 *
 * 2023/8/1 updated:
 * hp_poll::fds memory layout:
 *
 * memory data:            | hp_polld    | hp_polld         |...| pollfd  | pollfd  | ... |  pollfd    |
 * access by hp_poll::fds: | .fds[-nfds] | .fds[-nfds + 1]  |...| .fds[0] | .fds[1] | ... | .fds[nfds] |
 * hp_poll::fds point to-------------------------------------------->|
 *
 * hp_poll::fds[0] is NOT used, start with .fds[1]
 * */

/////////////////////////////////////////////////////////////////////////////////////

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef HAVE_POLL_H

#include "hp/hp_poll.h"   /*  */
#include "hp/hp_search.h" /* hp_lfind */
#include <stdio.h>     /* fprintf */
#include <poll.h>  /* poll */
#include <search.h>  /* lfind */
#include <unistd.h>
#include <string.h> 	/* strlen */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>     /* memset, ... */
#include <errno.h>      /* errno */
#include <assert.h>		//assert
#include "hp/hp_tuple.h" /* hp_tuple2_t */
#include "hp/hp_stdlib.h" /* min */

/////////////////////////////////////////////////////////////////////////////////////
typedef struct hp_polld hp_polld;

struct hp_polld {
	hp_poll_cb_t  fn;
	void * arg;
	int (* on_loop)(void * arg);
};

/////////////////////////////////////////////////////////////////////////////////////

static int hp_poll_rm_at(hp_poll * po, int index)
{
	if(!(po && index >= 1 && index <= po->nfds)) return -1;

	if( index < po->nfds ){
		//mv hp_polld data
		hp_polld * pdstart = (hp_polld *)((void *)po->fds - sizeof(hp_polld) * po->nfds);
		void * cpy = malloc(sizeof(hp_polld) * (po->nfds - index));
		memcpy(cpy, pdstart, sizeof(hp_polld) * (po->nfds - index));
		memcpy(pdstart + 1, cpy, sizeof(hp_polld) * (po->nfds - index));
		free(cpy);
		//mv pollfd data
		memmove(po->fds + index, po->fds + index + 1, sizeof(pollfd) * (po->nfds - index));
	}
	--po->nfds;

	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////

static int checkifnotpolled(pollfd * pfd) { assert(pfd); return pfd->revents != 0; }
static int checkifnotlooped(pollfd * pfd) { assert(pfd); return pfd->revents == 0; }

static hp_polld * get_next_to_run(hp_poll * po, int nfds, int (* cb)(pollfd * pfd), pollfd ** ppfd)
{
	assert(po && cb && ppfd);
	int i;
	for(i = 1; i <= nfds; ++i){
		if(cb(po->fds + i)){
			*ppfd = po->fds + i;
			hp_polld * pd = (hp_polld *)po->fds;
			return pd - i;
		}
	}

	return 0;
}

int hp_poll_run(hp_poll * po, int mode)
{
	if(!(po)) return -1;

	int i, n, rc;

	int runed = 0;
	for(; !runed;){
		if(mode){
			if(po->before_wait){
				int rc = po->before_wait(po);
				if(rc != 0) return rc;
			}
			runed = 1;
		}

		/* timeout: @see redis.conf/hz */
		n = poll(po->fds + 1, po->nfds, po->timeout);
		if(n < 0){
			if(errno == EINTR || errno == EAGAIN)
				continue;

			fprintf(stderr, "%s: poll failed, errno=%d, error='%s'\n"
					, __FUNCTION__, errno, strerror(errno));
			return -7;
		}
		else if(n == 0) continue;

		int nfds;
		//callback for every polled
		nfds = po->nfds;
		for(;;){
			hp_polld * pd = 0;
			pollfd * pfd = 0;
			pd = get_next_to_run(po, min(nfds, po->nfds), checkifnotpolled, &pfd);
			if(!pd){ break; }

			assert(pfd);
			if(pd->fn){
				pd->fn(pfd, pd->arg);
			}
		}

		//loop for every existing fd

		hp_polld * pd = (hp_polld *)po->fds;
		for (i = 1; i <= po->nfds; ++i) {
			if(pd[-i].on_loop)
				pd[-i].on_loop(pd[-i].arg);
		}
//		nfds = po->nfds;
//		for(;;){
//			hp_polld * pd = 0;
//			pollfd * pfd = 0;
//			pd = get_next_to_run(po, min(nfds, po->nfds), checkifnotlooped, &pfd);
//			if(!pd){ break; }
//
//			assert(pfd);
//			if(pd->on_loop){
//				pd->on_loop(pd->arg);
//			}
//			pfd->revents = 1;
//		}

		//another impl: the copy version

//		void * p = malloc((sizeof(hp_polld) + sizeof(pollfd)) * (n + 1));
//		pollfd * cpy = (pollfd *)(p + sizeof(hp_polld) * (n + 1));
//		n = 0;
//
//		for (i = 1; i <= po->nfds; ++i) {
//			if(po->fds[i].revents != 0){
//				hp_polld * pd = (hp_polld *)po->fds;
//				if(pd[-i].fn){
//					++n;
//					cpy[n] = po->fds[i];
//					((hp_polld *)cpy)[-n] = pd[-i];
//				}
//			}
//		}
//		for (i = 1; i <= n; ++i) {
//			rc = ((hp_polld *)cpy)[-i].fn(cpy + i, ((hp_polld *)cpy)[-i].arg);
//		}
//		free(p);

	} /* */

	return 0;
}

int hp_poll_init(hp_poll * po, int max_fd, int timeout
		, int (* before_wait)(hp_poll * po), void * arg)
{
	if(!po) { return -1; }
	if(max_fd <= 0)
		max_fd = 65535;
	else ++max_fd;

	memset(po, 0, sizeof(hp_poll));

	void * p = malloc(sizeof(hp_polld) * max_fd + sizeof(pollfd) * (max_fd));
	po->fds = (pollfd *)(p + sizeof(hp_polld) * max_fd);

	po->max_fd = max_fd - 1;
	po->timeout = timeout;
	po->before_wait = before_wait;
	po->arg = arg;

	return 0;
}

void hp_poll_uninit(hp_poll * po)
{
	if(!(po && po->fds)) return;

	void * p = (void *)po->fds - sizeof(hp_polld) * (po->max_fd + 1);
	free(p);
}

static int find_by_fd(const void * k, const void * e)
{
	assert(e && k);
	hp_sock_t fd = *(hp_sock_t *)k;
	pollfd * pfd = (pollfd *)e;

	return !(pfd->fd == fd);
}
int hp_poll_add(hp_poll * po, hp_sock_t fd, int events
		, hp_poll_cb_t  fn, hp_poll_loop_t on_loop, void * arg)
{
	int rc = 0;
	if(!(po && po->fds && hp_sock_is_valid(fd))) return -1;

	int index;
	pollfd * pfd = (pollfd *)hp_lfind(&fd, po->fds + 1, po->nfds, sizeof(pollfd), find_by_fd);
	if(pfd){
		index = pfd - po->fds;
		assert(po->fds[index].fd == fd);
	}
	else{
		if(po->nfds >= po->max_fd)
			return -2;
		index = ++po->nfds;
		po->fds[index].fd = fd;
	}

	po->fds[index].events = events;
	po->fds[index].revents = 0;

	hp_polld * pd = (hp_polld *)po->fds;
	pd[-index].fn = fn;
	pd[-index].on_loop = on_loop;
	pd[-index].arg = arg;

	return 0;
}

int hp_poll_rm(hp_poll * po, hp_sock_t fd)
{
	int rc;
	if(!(po)) return -1;

	pollfd * pfd = (pollfd *)hp_lfind(&fd, po->fds + 1, po->nfds, sizeof(pollfd), find_by_fd);
	if(pfd){
		int index = pfd - po->fds;
		rc = hp_poll_rm_at(po, index);
		assert(rc == 0);
	}

	return pfd? 0 : -1;
}

char * hp_poll_e2str(int events, char * buf, int len)
{
	if(!buf && len > 0) return 0;

	buf[0] = '\0';
	int n = snprintf(buf, len, "%s%s%s%s"
			, (events & POLLERR?   "POLLERR | " : "")
			, (events & POLLIN?    "POLLIN | " : "")
			, (events & POLLOUT?   "POLLOUT | " : "")
			, (events & POLLHUP?   "POLLHUP | " : "")
			);
	if(buf[0] != '\0' && n >= 3 )
		buf[n - 3] = '\0';

	int left = events & (~(POLLERR  | POLLIN | POLLOUT | POLLHUP));
	if(!(left == 0))
		snprintf(buf + n, len - n, " | %d", left);

	return buf;
}

/////////////////////////////////////////////////////////////////////////////////////

hp_tuple3_t(hp_dofind_tuple, void * /*key*/, hp_cmp_fn_t /*on_cmp*/
		, pollfd * /*fds*/);

static int hp_poll_do_find(const void * k, const void * e)
{
	assert(e && k);
	hp_dofind_tuple tu = *(hp_dofind_tuple *)k;
	assert(tu._3);

	pollfd * pfd = (pollfd *)e;
	void * arg = ((hp_polld *)tu._3)[-(pfd - (tu._3))].arg ;
	return !(tu._2? tu._2(tu._1, arg) : tu._1 == arg);
}

void * hp_poll_find(hp_poll * po, void * key, hp_cmp_fn_t cb)
{
	if(!(po))
		return 0;
	hp_dofind_tuple k = { ._1 = key, ._2 = cb, ._3 = po->fds };
	pollfd * pfd = (pollfd *)hp_lfind(&k, po->fds + 1, po->nfds, sizeof(pollfd), hp_poll_do_find);
	return pfd? ((hp_polld *)po->fds)[-(pfd - (po->fds))].arg : 0;
}

/////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
#include "hp/hp_net.h"//hp_tcp_listen
#include "hp/hp_log.h"
#include <uv.h>		//uv_ip4_name
#include <stdio.h>		//uv_ip4_name
#include "hp/hp_config.h"
#include "hp/hp_io.h"	//hp_rd
#include "hp/hp_err.h"	//hp_err
#include "hp/hp_assert.h"	//hp_assert
#define cfg hp_config_test
#define cfgi(key) atoi(hp_config_test(key))

/////////////////////////////////////////////////////////////////////////////////////

typedef struct test_hp_io_t_server {
	hp_rd listenrd;
	int quit;
} server;

// for simplicity, we use the same "client" struct for both test_hp_io_t_server and client side code
typedef struct client {
	hp_poll * po;
	hp_rd rd;
	hp_wr wr;
	sds in;
	hp_sock_t fd;
	server * s;

	char addr[64];
	int t;
} client;

static int client_init(client * c, hp_poll * po, hp_sock_t fd,
		int (* data)(char * buf, size_t * len, void * arg),
		void (* read_error)(hp_rd * rd, int err, void * arg),
		void (* write_error)(hp_wr * wr, int err, void * arg))
{
	int rc;
	if(!(c && po)) return -1;
	c->in = sdsempty();
	c->po = po;
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

static int test_hp_poll_on_io(pollfd * pfd, void * arg,
		void (* on_error )(client * c, int err))
{
	int rc = 0;
	assert(arg && pfd);
	client * c = arg;
	hp_poll * po = (hp_poll *)c->po;
	assert(po);
	assert(on_error);

	if((pfd->revents & (POLLERR | POLLHUP))){
		char buf[128] = "";
		hp_poll_e2str(pfd->revents, buf, sizeof(buf));

		on_error(c, pfd->revents);
		pfd->revents = 0;
		return -1;
	}
	if((pfd->revents & POLLIN)){
		hp_rd_read(&c->rd, pfd->fd, c);
		rc = c->rd.err;
	}
	if(rc != 0) {
		pfd->revents = 0;
		return rc;
	}

	if((pfd->revents & POLLOUT)){
		hp_wr_write(&c->wr, pfd->fd, c);
		rc = c->rd.err;
	}
	pfd->revents = 0;
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

static void test_hp_poll_server_on_error(client * c, int err)
{
	assert(c && c->po);

	hp_err_t errstr = "%s";
	hp_log(stdout, "%s: delete TCP connection '%s', %d/'%s', IO total=%d\n", __FUNCTION__
			, c->addr, err, hp_err(err, errstr), c->po->nfds);

	hp_poll_rm(c->po, c->fd);
	hp_close(c->fd);

	client_uninit(c);
	free(c);
}
static void server_on_read_error(hp_rd * rd, int err, void * arg){ return test_hp_poll_server_on_error(arg, err); }
static void server_on_write_error(hp_wr * wr, int err, void * arg){ return test_hp_poll_server_on_error(arg, err); }

static int test_hp_poll_server_on_io(pollfd * pfd, void * arg)
{
	return test_hp_poll_on_io(pfd, arg, test_hp_poll_server_on_error);
}

static int test_on_accept(pollfd * pfd, void * arg)
{
	int rc = 0;
	assert(pfd && arg);
	hp_poll * po = (hp_poll *)arg;

	if((pfd->revents & (POLLERR | POLLHUP))){
		pfd->revents = 0;
		return -1;
	}

	for(;;){
		struct sockaddr_in addr;
		int len = (int)sizeof(addr);
		hp_sock_t confd = accept(pfd->fd, (struct sockaddr *)&addr, &len);

		if(!hp_sock_is_valid(confd)){
			if (errno == EINTR || errno == EAGAIN) { break; }

			hp_log(stderr, "%s: accept failed, errno=%d, error='%s'\n", __FUNCTION__, errno, strerror(errno));
			continue;
		}
		if(hp_tcp_nodelay(confd) < 0)
			{ hp_close(confd); continue; }

		client * c = calloc(1, sizeof(client));
		rc = client_init(c, po, confd, server_on_data, server_on_read_error, server_on_write_error);  assert(rc == 0);

		rc = hp_poll_add(po, confd, POLLIN | POLLOUT, test_hp_poll_server_on_io, 0, c);
		assert(rc == 0);

		char * buf = c->addr;
		hp_log(stdout, "%s: new TCP connection from '%s', IO total=%d\n", __FUNCTION__
				, hp_addr4name(&addr, ":", buf, sizeof(buf)), po->nfds);
	}

	pfd->revents = 0;
	return 0;
}

static int client_on_data(char * buf, size_t * len, void * arg)
{
	int rc;
	assert(buf && len && arg);
	client * c = arg;
	assert(c->po);

	c->in = sdscatlen(c->in, buf, *len);
	*len = 0;

	if(strncmp(buf, "world", strlen("world")) == 0){
		hp_log(stdout, "%s: client test done! disconnecting ...\n", __FUNCTION__);
		// client done
		shutdown(c->fd, SHUT_RDWR);
	}

	return 0;
}

static void client_on_error(client * c, int err)
{
	int rc;
	hp_log(stdout, "%s: fd=%d, disconnected err=%d\n", __FUNCTION__, c->fd, err);

	rc = hp_poll_rm(c->po, c->fd);
	assert(rc == 0);

	hp_close(c->fd);
}
static void client_on_read_error(hp_rd * rd, int err, void * arg){ return client_on_error(arg, err); }
static void client_on_write_error(hp_wr * wr, int err, void * arg){ return client_on_error(arg, err); }
static int test_hp_poll_client_on_io(pollfd * pfd, void * arg)
{
	return test_hp_poll_on_io(pfd, arg, client_on_error);
}
/////////////////////////////////////////////////////////////////////////////////////
static void add_remove_test(int n)
{
	int rc;
	hp_sock_t fd[1024], i;
	hp_poll ghp_poobj = { 0 }, * po = &ghp_poobj;
	hp_poll_init(po, n, 200, 0, 0);
	assert(hp_poll_size(po) == 0);

	for(i = 0; i < n; ++i){
		fd[i] = i;

		rc = hp_poll_add(po, fd[i], POLLIN, 0, 0, 0);
		assert(rc == 0);

		//add same fd
		rc = hp_poll_add(po, fd[i], POLLIN | POLLOUT, test_hp_poll_client_on_io, 0, 0);
		assert(rc == 0);

		assert(hp_poll_size(po) == i + 1);
	}
	rc = hp_poll_add(po, n + 1, POLLIN | POLLOUT, test_hp_poll_client_on_io, 0, 0);
	assert(rc < 0);

	for(i = 0; i < n; ++i){
		rc = hp_poll_rm(po, fd[i]);
		assert(rc == 0);
	}

	assert(hp_poll_size(po) == 0);
	hp_poll_uninit(po);
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
	hp_poll ghp_poobj = { 0 }, * po = &ghp_poobj;
	hp_poll_init(po, n, 200, 0, 0);
	assert(hp_poll_size(po) == 0);

	for(i = 0; i < n; ++i){
		hp_sock_t confd = hp_tcp_connect("127.0.0.1", 1);
		hp_assert(hp_sock_is_valid(confd), "i=%i", i);
		fd[i] = confd;

		assert(!hp_poll_find(po, fd + i, search_test_on_cmp));
		rc = hp_poll_add(po, fd[i], POLLIN, 0, 0, fd + i);
		assert(rc == 0);

		//add same fd
		rc = hp_poll_add(po, fd[i], POLLIN | POLLOUT, test_hp_poll_client_on_io, 0, fd + i);
		assert(rc == 0);

		assert(hp_poll_size(po) == i + 1);
		assert(hp_poll_find(po, fd + i, search_test_on_cmp));

		rc = hp_poll_rm(po, fd[i]);
		assert(rc == 0);
		assert(!hp_poll_find(po, fd + i, search_test_on_cmp));

		rc = hp_poll_add(po, fd[i], POLLIN | POLLOUT, test_hp_poll_client_on_io, 0, fd + i);
		assert(rc == 0);
	}
	{
		hp_sock_t confd = hp_tcp_connect("127.0.0.1", 1);
		hp_assert(hp_sock_is_valid(confd), "i=%i", i);

		rc = hp_poll_add(po, confd, POLLIN | POLLOUT, test_hp_poll_client_on_io, 0, 0);
		assert(rc < 0);

		hp_close(confd);
	}

	for(i = 0; i < n; ++i){
		rc = hp_poll_rm(po, fd[i]);
		assert(rc == 0);
		hp_close(fd[i]);
	}

	assert(hp_poll_size(po) == 0);
	hp_poll_uninit(po);
}

/////////////////////////////////////////////////////////////////////////////////////
static void client_server_echo_test(int nclient)
{
	int i, rc = 0;
	client * c = (client *)calloc(70000, sizeof(client));
	assert(c);
	int port = cfgi("tcp.port");
	hp_poll ghp_poobj = { 0 }, * po = &ghp_poobj;
	// +1 for listen_fd
	hp_poll_init(po, 2 * nclient + 1, 200, 0, 0);

	hp_sock_t listen_fd = hp_tcp_listen(port); assert(hp_sock_is_valid(listen_fd));
	rc = hp_poll_add(po, listen_fd, POLLIN, test_on_accept, 0, po); assert(rc == 0);

	/* add connect socket */
	for(i = 0; i < nclient; ++i){

		hp_sock_t confd = hp_tcp_connect("127.0.0.1", port); assert(hp_sock_is_valid(confd));
		rc = client_init(c + i, po, confd, client_on_data, client_on_read_error, client_on_write_error);
		assert(rc == 0);

		rc = hp_poll_add(po, confd, POLLIN | POLLOUT, test_hp_poll_client_on_io, 0, c + i);
		assert(rc == 0);

		rc = hp_wr_add(&c[i].wr, sdsnew("hello"), strlen("hello"), (hp_free_t)sdsfree, 0);
		assert(rc == 0);
	}

	hp_log(stdout, "%s: listening on TCP port=%d, waiting for connection ...\n", __FUNCTION__, port);
	for(; hp_poll_size(po) > 1;){
		hp_poll_run(po, 1);
	}

	/*clear*/
	for(i = 0; c[i].in; ++i){
		client_uninit(c + i);
	}
	hp_poll_uninit(po);
	hp_close(listen_fd);
	free(c);
}
/////////////////////////////////////////////////////////////////////////////////////

int test_hp_poll_main(int argc, char ** argv)
{
	{ char buf[128] = ""; assert(strcmp(hp_poll_e2str(0, buf, sizeof(buf)), "") == 0); }
	{ char buf[128] = ""; assert(strcmp(hp_poll_e2str(POLLERR, buf, sizeof(buf)), "POLLERR") == 0); }
	{ char buf[128] = ""; assert(strcmp(hp_poll_e2str(POLLIN, buf, sizeof(buf)), "POLLIN") == 0); }
	{ char buf[128] = ""; assert(strcmp(hp_poll_e2str(POLLOUT, buf, sizeof(buf)), "POLLOUT") == 0); }
	{ char buf[128] = ""; assert(strcmp(hp_poll_e2str(POLLERR  | POLLIN, buf, sizeof(buf)), "POLLERR | POLLIN") == 0); }
	{ char buf[128] = ""; assert(strcmp(hp_poll_e2str(POLLERR  | POLLIN | POLLOUT, buf, sizeof(buf)), "POLLERR | POLLIN | POLLOUT") == 0); }

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
	//client,test_hp_io_t_server echo test
	{
		client_server_echo_test(1);
		client_server_echo_test(2);
		client_server_echo_test(3);
		client_server_echo_test(500);
	}
	return 0;
}
#endif /* NDEBUG */

#endif /**/
