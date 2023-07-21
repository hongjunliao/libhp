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

#include "redis/src/adlist.h" /* list */
#include "hp_sock_t.h"  /* hp_sock_t */
#include <stddef.h> 	/* size_t */
#include <netinet/in.h>
#if !defined(__linux__) && !defined(_MSC_VER)
#include "hp_io.h"      /* hp_eti,... */
#include "hp_poll.h"   /* hp_poll */
#elif !defined(_MSC_VER)
#include "hp_io.h"      /* hp_eti,... */
#include "hp_epoll.h"   /* hp_epolld */
#else
#include "hp_iocp.h"    /* hp_iocp */
#endif /* _MSC_VER */
 /////////////////////////////////////////////////////////////////////////////////////////

 /**!
 * message header, defined by user
 * it is fine that you don't define it if you never use it
 *   */
//typedef union hp_iohdr hp_iohdr_t;

typedef struct hp_io_t hp_io_t;
typedef struct hp_iohdl hp_iohdl;
typedef struct hp_io_ctx hp_io_ctx;

typedef	void  (* hp_io_free_t)(void * ptr);

/////////////////////////////////////////////////////////////////////////////////////////

struct hp_iohdl {
	/* callback when new connection coming */
	hp_io_t * (* on_new)(hp_io_t * cio, hp_sock_t fd);
	/* callback when data coming
	* @return: >0-parse OK, got a message; 0: need more data; <0 parse failed
	*/
	int (* on_parse)(hp_io_t * io, char * buf, size_t * len
			, void ** hdrp, void ** bodyp);

	/* callback when new message coming */
	int(* on_dispatch)(hp_io_t * io, void * hdr, void * body);
	/* callback when disconnect */
	void(* on_delete)(hp_io_t * io);
	/**
	 * callback in loop,
	 * this function will be hi-frequently called
	 * DO NOT block TOO LONG
	 * @return: return <0 to ask to close the client
	*/
	int (* on_loop)(hp_io_t * io);
#ifdef _MSC_VER
	int wm_user; /* WM_USER + N */
	HWND hwnd;   /* see hp_iocp for more details */
#endif /* _MSC_VER */
};

struct hp_io_t {
	int id;			/* ID for this I/O, more safe than fd? */
	struct sockaddr_in addr;
#if defined(_MSC_VER)
	int	   index;
#elif defined(__linux__)
	hp_eti 	eti; 	/* for in data */
	hp_eto 	eto; 	/* for out data */
	hp_epolld ed;	/* context */
	hp_io_ctx * ioctx;     /* hp_io_ctx */
#elif !defined(_WIN32)
	hp_eti 	eti; 	/* for in data */
	hp_eto 	eto; 	/* for out data */
	hp_poll fds;
#else
					/* select() */
#endif /* _MSC_VER */
	hp_iohdl iohdl;	/*io handle set by user*/
	void * user;	/* ignored by hp_io_t */
} ;

struct hp_io_ctx {

#if defined(_MSC_VER)
	hp_iocp iocp;
#elif defined(__linux__)
	hp_epoll efds;
#elif !defined(_WIN32)
	hp_poll fds;
#else
#endif /* _MSC_VER */
	list * iolist;
	int ioid; /* for hp_io_t::id */
} ;


/////////////////////////////////////////////////////////////////////////////////////////

int hp_io_init(hp_io_ctx * ioctx);

int hp_io_add(hp_io_ctx * ioctx, hp_io_t * io, hp_sock_t fd, hp_iohdl iohdl);
int hp_io_write(hp_io_t * io
	, void * buf, size_t len, hp_io_free_t free, void * ptr);
int hp_io_run(hp_io_ctx * ioctx, int interval, int mode);
int hp_io_uninit(hp_io_ctx * ioctx);

int hp_io_count(hp_io_ctx * ioctx);
hp_sock_t hp_io_fd(hp_io_t * io);
/////////////////////////////////////////////////////////////////////////////////////////
#ifndef NDEBUG
int test_hp_io_t_main(int argc, char ** argv);
#endif //

#ifdef __cplusplus
}
#endif

#endif /* LIBHP_IO_T_H__ */
