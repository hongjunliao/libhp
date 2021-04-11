/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2021/4/9
 *
 * networking I/O, using epoll on *nix and IOCP on Win32
 * */

#ifndef LIBHP_IO_T_H__
#define LIBHP_IO_T_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifndef _MSC_VER
#include "hp_io.h"      /* hp_eti,... */
#include "hp_epoll.h"   /* hp_epolld */
#else
#include "hp_iocp.h"    /* hp_iocp */
#endif /* _MSC_VER */
 /////////////////////////////////////////////////////////////////////////////////////////
typedef struct hp_io_t hp_io_t;
typedef struct hp_io_ctx hp_io_ctx;

#ifdef _MSC_VER
typedef SOCKET hp_sock_t;
#else
typedef int hp_sock_t;
#endif /*_MSC_VER*/

typedef	void  (* hp_io_free_t)(void * ptr);
typedef int(* hp_io_on_accept)(hp_sock_t fd);
typedef int(* hp_io_on_data)(hp_io_t * io, char * buf, size_t * len);
typedef int(* hp_io_on_error)(hp_io_t * io, int err, char const * errstr);

struct hp_io_t {
#ifndef _MSC_VER
	hp_eti 	eti; /* for in data */
	hp_eto 	eto; /* for out data */
	hp_epolld ed;/* context */
#else
	hp_iocp *io;
	int	   index;
#endif /* _MSC_VER */

	hp_sock_t fd;
	hp_io_on_data on_data;
	hp_io_on_error on_error;
} ;

struct hp_io_ctx {
#ifndef _MSC_VER
	hp_epoll efds;
	hp_epolld epolld;
#else
	hp_iocp iocpctx;
	int	   index;
#endif /* _MSC_VER */
	hp_io_on_accept on_accept;
	int n_accept;
} ;

int hp_io_init(hp_io_ctx * io, hp_sock_t fd, hp_io_on_accept on_accept);
int hp_io_add(hp_io_ctx * ioctx, hp_io_t * io, hp_sock_t fd
	, hp_io_on_data on_data, hp_io_on_error on_error);
int hp_io_write(hp_io_t * io
	, void * buf, size_t len, hp_io_free_t free, void * ptr);
int hp_io_run(hp_io_ctx * ioctx, int interval);
int hp_io_uninit(hp_io_ctx * ioctx);

#ifndef NDEBUG
int test_hp_io_t_main(int argc, char ** argv);
#endif //

#endif /* LIBHP_IO_T_H__ */
