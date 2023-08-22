/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/12/3
 *
 * an Win32 IOCP wrapper, using Windows message queue to sync
 * */

#ifndef LIBHP_IOCP_H
#define LIBHP_IOCP_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include  "hp_stdlib.h"  //hp_free_t
#ifdef _MSC_VER

#include <stdint.h>      /* size_t */

#ifndef FD_SETSIZE
#define FD_SETSIZE 1024  //before winsock2.h
#endif //#ifndef FD_SETSIZE
#include <winsock2.h>
 /////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////

#define HP_IOCP_TMAX         64    /* IOCP thread max */
#define HP_IOCP_PENDING_BUF  1024  /* initial buffer size pending read */

typedef struct hp_iocp hp_iocp;
typedef struct hp_iocp_item hp_iocp_item;

struct hp_iocp {
	HANDLE        hiocp;                 /* handle of IOCP */
	int           ctid;                  /* user thread ID */
	HWND          hwnd;                  /* user thread HWND */
	HANDLE        sthread;               /* select threads */
	HANDLE        threads[HP_IOCP_TMAX + 1]; /* IOCP threads */
	int           n_threads;              /* number of IOCP threads  */
	hp_iocp_item *items;				  /*  */
	UINT          wmuser;                /* WM_USER + N */
	int           stime_out;             /* timeout in ms for select threads */
	int           ioid;					 //for items::id
	int           maxfd;				//for maxfd
	int			  ptid;					// select/poll thread id
	void *        user;					 /* user data, ignored by hp_iocp */
#ifndef NDEBUG
	int 		  n_on_accept;
	int           n_on_data;
	int 		  n_on_pollout;
	int 		  n_on_i;
	int 		  n_on_o;
	int 		  n_dmsg0;
	int 		  n_dmsg1;
#endif //#ifndef NDEBUG
};

/*
 * init
 * @param maxfd:     maxfd
 * @param hwnd:      then HWND to receive messages
 * @param nthreads:  number of IOCP threads, if 0, then use SYSTEM_INFO
 * @param rb_max:    for read, see @rb_max
 * @param rb_size:   for read, read (@param rb_max * @param rb_size) bytes at max
 * @param wmuser:    WM_USER + N, hp_iocp use [WM_USER + N, WM_USER + N + 10]
 * @param stime_out: timeout in ms for select
 * @param user:      user data, ignored by hp_iocp
 * @return:          0 on OK
 * */
int hp_iocp_init(hp_iocp * iocp, int maxfd, HWND hwnd, int nthreads, int wmuser, int stime_out, void * user);
void hp_iocp_uninit(hp_iocp * iocp);

/*
 * start working threads
 * NOTE: either @param tid or @param hwnd must be valid
 * @mode:    0 for forever, 1 for once
 * @return:  0 on OK
 * */
int hp_iocp_run(hp_iocp * iocp, int mode);

/*
 * add an I/O context
 * @iocp            which hp_iocp to add to
 * @param nibuf:    for read buf size
 * @param fd:		   the socket which I/O will go to
 * @param on_accept:  callback if accept
 * @param on_data:     callback when data coming
 *                       return <0 for close
 * @param on_error:    callback when error
 * @param flags:       user data
 * @return:            an id for this new I/O context, >= 0 on OK, <0 on error
 * */
int hp_iocp_add(hp_iocp * iocp
	, int nibuf , SOCKET fd
	, int (* on_accept)(hp_iocp * iocp, void * arg)
	, int (* on_data)(hp_iocp * iocp, void * arg, char * buf, size_t * len)
	, int (* on_error)(hp_iocp * iocp, void * arg, int on_error, char const * errstr)
	, int (* on_loop)(void * arg)
	, void * arg);
int hp_iocp_rm(hp_iocp * iocp, int id);
void * hp_iocp_find(hp_iocp * iocp, void * key, int (* on_cmp)(const void *key, const void *ptr));
int hp_iocp_size(hp_iocp * iocp);
/*
 * write something async
 * @param id:      	  return value from @hp_iocp_add
 * @param data:       data to write
 * @param ndata:      bytes of @param data
 * @param free:       callback to free @param data when written
 * @param ptr:        param to call @param free, if 0, then @param data
 * @param user_data:  user data for this write call
 * @return:           0 on OK
 * NOTE: @param data/ptr is freed even if failed
 * */
int hp_iocp_write(hp_iocp * iocp, int id, void * data, size_t ndata
	, hp_free_t freecb, void * ptr);
//int hp_iocp_try_write(hp_iocp * iocp, int index);

#ifndef NDEBUG
int test_hp_iocp_main(int argc, char ** argv);
#endif //NDEBUG

#ifdef __cplusplus
}
#endif

#endif /* _MSC_VER */

#endif /* LIBHP_IOCP_H */

