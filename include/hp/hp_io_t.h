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

#ifdef __cplusplus
extern "C" {
#endif

#include "hp_net.h"  /* hp_sock_t */
#include "hp_stdlib.h"	//hp_free_t
#include <stddef.h> 	/* size_t */

#ifdef HAVE_SYS_UIO_H
#include "hp_io.h"      /* hp_rd,... */
#endif
#if defined(_MSC_VER)
#include "hp_iocp.h"    /* hp_iocp */
#elif defined(HAVE_SYS_EPOLL_H)
#include "hp_epoll.h"   /* hp_epoll */
#elif defined(HAVE_POLL_H)
#include "hp_poll.h"   /* hp_poll */
#else
#include <sys/select.h> //select
#endif //defined(_MSC_VER)

 /////////////////////////////////////////////////////////////////////////////////////////
typedef struct hp_io_t hp_io_t;
typedef struct hp_iohdl hp_iohdl;
typedef struct hp_io_ctx hp_io_ctx;
typedef struct hp_ioopt hp_ioopt;

/////////////////////////////////////////////////////////////////////////////////////////

struct hp_iohdl {
	/* callback when new connection coming
	 * @return: NULL to close the connection */
	hp_io_t * (* on_new)(hp_io_t * cio, hp_sock_t fd);
	/* callback when data coming
	* @return: >0-parse OK, got a message and on_dispatch is called
	* 		   ==0: need more data;
	* 		   <0 to close the client(parse failure?)
	*/
	int (* on_parse)(hp_io_t * io, char * buf, size_t * len
			, void ** hdrp, void ** bodyp);

	/* callback when new message coming
	 * @return: <0 to close the client(protocol failure?) */
	int(* on_dispatch)(hp_io_t * io, void * hdr, void * body);
	/**
	 * callback in loop,
	 * this function will be hi-frequently called
	 * DO NOT block TOO LONG
	 * @return: <0 to close the client
	*/
	int (* on_loop)(hp_io_t * io);
	/* callback when disconnect */
	void (* on_delete)(hp_io_t * io, int err, char const * errstr);
};

struct hp_io_t {
	hp_io_ctx * ioctx;	/* hp_io_ctx */
	int         id;		/* ID for this I/O, more safe than fd? */
	hp_sock_t   fd;	/* fd */
	struct sockaddr_in addr;

#if defined(HAVE_SYS_UIO_H)
	hp_rd 	rd; 	/* for in data */
	hp_wr 	wr; 	/* for out data */
#endif //defined(HAVE_SYS_UIO_H)
	hp_iohdl iohdl;	/*io handle set by user*/
	void *   user;	/* ignored by hp_io_t */
} ;

struct hp_io_ctx {

#if defined(_MSC_VER)
	hp_iocp iocp;
#elif defined(HAVE_SYS_EPOLL_H)
	hp_epoll epo;
#elif defined(HAVE_POLL_H)
	hp_poll po;
#else
	//select
#endif //defined(_MSC_VER)
} ;

/* options for init hp_io_ctx */
struct hp_ioopt {
	int maxfd;
	int timeout;
#ifdef _MSC_VER
	int wm_user; /* WM_USER + N */
	HWND hwnd;   /* see hp_iocp for more details */
	int nthreads;
#endif /* _MSC_VER */
};

/////////////////////////////////////////////////////////////////////////////////////////

int hp_io_init(hp_io_ctx * ioctx, hp_ioopt opt);

/*!
 * @return: 0 on OK, else error
 * */
int hp_io_add(hp_io_ctx * ioctx, hp_io_t * io, hp_sock_t fd, hp_iohdl iohdl);
/*!
 * @param id: hp_io_t::id
 */
int hp_io_rm(hp_io_ctx * ioctx, int id);
int hp_io_write(hp_io_t * io, void * buf, size_t len, hp_free_t free, void * ptr);
hp_io_t * hp_io_find(hp_io_ctx * ioctx, void * key, int (* on_cmp)(const void *key, const void *ptr));
int hp_io_run(hp_io_ctx * ioctx, int interval, int mode);
int hp_io_uninit(hp_io_ctx * ioctx);

int hp_io_size(hp_io_ctx * ioctx);
hp_sock_t hp_io_fd(hp_io_t * io);
/////////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
int test_hp_io_t_main(int argc, char ** argv);
#endif //NDEBUG

#ifdef __cplusplus
}
#endif

#endif /* LIBHP_IO_T_H__ */
