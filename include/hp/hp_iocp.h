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

#ifdef _MSC_VER

#ifdef _WIN32
#include "redis/src/Win32_Interop/Win32_Portability.h"
#include "redis/src/Win32_Interop/win32_types.h"
#endif

#include <winsock2.h>    /*  */
#include <windows.h>
#include <stdint.h>      /* size_t */
#include "libhp.h"
 /////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

#define HP_IOCP_TMAX  16         /* IOCP thread max */

typedef struct hp_iocp hp_iocp;
typedef struct hp_iocp_item hp_iocp_item;
typedef	void  (* hp_iocp_free_t)(void * ptr);

struct hp_iocp {
	HANDLE        hiocp;                 /* handle of IOCP */
	int           ctid;                  /* user thread ID */
	HWND          hwnd;                  /* user thread HWND */
	HANDLE        sthread;               /* select threads */
	HANDLE        threads[HP_IOCP_TMAX]; /* IOCP threads */
	int           n_threads;              /* number of IOCP threads  */
	hp_iocp_item *items;				  /*  */
	int           flags;
	UINT          wmuser;                /* WM_USER + N */
	int           stime_out;             /* timeout in ms for select threads */
	void *        user;					 /* user data, ignored by hp_iocp */
};

/*
 * init
 * @param nthreads:  number of IOCP threads, if 0, then use SYSTEM_INFO
 * @param rb_max:    for read, see @rb_max
 * @param rb_size:   for read, read (@param rb_max * @param rb_size) bytes at max
 * @param wmuser:    WM_USER + N, hp_iocp use [WM_USER + N, WM_USER + N + 10]
 * @param stime_out: timeout in ms for select
 * @param user:      user data, ignored by hp_iocp
 * @return:          0 on OK
 * */
int hp_iocp_init(hp_iocp * iocpctx, int nthreads, int wmuser, int stime_out, void * user);
void hp_iocp_uninit(hp_iocp * iocpctx);

/*
 * start working threads
 * NOTE: either @param tid or @param hwnd must be valid
 * @param tid:       user thread ID
 * @param hwnd:      then HWND to receive messages
 * @return:          0 on OK
 * */
int hp_iocp_run(hp_iocp * iocpctx, int tid, HWND hwnd);

#define HP_IOCP_CLR_IOBYTES (1 <<0)  // clear I/O bytes if disconnect?
/*
 * add an I/O context
 * @iocpctx            which hp_iocp to add to
 * @param rb_max, rb_size: for read blocks, count and size
 * @param sock:		   the socket which I/O will go to
 * @param on_connect:  callback if @param sock invalid
 * @param on_data:     callback when data coming
 *                       return <0 for close
 * @param on_error:    callback when error
 *                       return >0 for reconnect
 * @param flags:       flags
 * @return:            an index for this new I/O context, >= 0 on OK, <0 on error
 * NOTES:
 * call order: hp_iocp_init=>hp_iocp_run=>hp_iocp_add=>hp_iocp_write
 * */
int hp_iocp_add(hp_iocp * iocpctx
	, int rb_max, int rb_size
	, SOCKET sock
	, SOCKET (* on_connect)(hp_iocp * iocpctx, int index)
	, int (* on_data)(hp_iocp * iocpctx, int index, char * buf, size_t len)
	, int (* on_error)(hp_iocp * iocpctx, int index, int on_error, char const * errstr)
	, int flags
	);
int hp_iocp_size(hp_iocp * iocpctx);
/*
 * write something async
 * @param index:      return value from @hp_iocp_add
 * @param data:       data to write
 * @param ndata:      bytes of @param data
 * @param free:       callback to free @param data when written
 * @param ptr:        param to call @param free, if 0, then @param data
 * @param user_data:  user data for this write call
 * @return:           0 on OK
 * */
int hp_iocp_write(hp_iocp * iocpctx, int index, void * data, size_t ndata
	, hp_iocp_free_t free, void * ptr);

/*
 * because this IOCP wrapper use Windows message queue to sync
 * user thread must call this function in message process routine
 * @see https://docs.microsoft.com/zh-cn/windows/desktop/winmsg/using-messages-and-message-queues
 * 
 * @param message:    the message context, message ID, see hp_iocp_init
 * @param wParam:     the message context
 * @param lParam:     the message context
 * 
 * @return:           0 on OK
 * */
int hp_iocp_handle_msg(hp_iocp * iocpctx, UINT message, WPARAM wParam, LPARAM lParam);

#ifndef NDEBUG
LIBHP_EXT int test_hp_iocp_main(int argc, char ** argv);
#endif //NDEBUG

#ifdef __cplusplus
}
#endif

#endif /* _MSC_VER */

#endif /* LIBHP_IOCP_H */

