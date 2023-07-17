/*!
* This file is PART of libxhhp project
* @author hongjun.liao <docici@126.com>, @date 2018/12/3
*
* an Win32 IOCP wrapper
* */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "Win32_Interop.h"
#include "redis/src/adlist.h"

#ifdef _MSC_VER
#ifdef LIBHP_WITH_WIN32_INTERROP
#define closesocket close
#define _fd_set(fd,set) FD_SET(FDAPI_get_ossocket(fd), set) 
#define _fd_isset(fd,set) FD_ISSET(FDAPI_get_ossocket(fd), set)
#else
#include <winsock2.h>
#define _fd_set(fd,set) FD_SET((fd), set) 
#define _fd_isset(fd,set) FD_ISSET(fd, set) 
#endif /* LIBHP_WITH_WIN32_INTERROP */

#include "hp/hp_iocp.h"    /* hp_iocp */
#include "hp/sdsinc.h" /* sds */
#include "hp/hp_log.h"  /* hp_log */
#include "hp/hp_libc.h" /* hp_tuple2_t */
#include "hp/hp_err.h"	 /* hp_err */
#include "hp/hp_net.h"	 /* read_a */
#include <process.h>    /* _beginthreadex */
//#include <sysinfoapi.h> /* GetSystemInfo */
#include <stdio.h>
#include <string.h>     /* memset, ... */
#include <assert.h>     /* define NDEBUG to disable assertion */
#include <stdlib.h>

#define OPTPARSE_IMPLEMENTATION
#define OPTPARSE_API static
#include "optparse/optparse.h"		/* option */
//#include "getopt.h"			/* getopt_long */

#include "c-vector/cvector.h"

/////////////////////////////////////////////////////////////////////////////////////

/* WM_USER + N */
#define HP_IOCP_WM_IO(c)        ((c)->wmuser + 1)
#define HP_IOCP_WM_FD(c)        ((c)->wmuser + 2)
#define HP_IOCP_WM_END(c)       ((c)->wmuser + 3)
/* add other WM_XXX here */

#define IS_HP_IOCP_WM(c,msg)      ((msg) >= HP_IOCP_WM_FIRST(c) && (msg) <= HP_IOCP_WM_LAST(c))

/* internal use */
#define HP_IOCP_WM_FD_ADD		(WM_USER + 10)
#define HP_IOCP_WM_FD_RM		(WM_USER + 11)
#define HP_IOCP_WM_FD_WINC		(WM_USER + 12)
#define HP_IOCP_WM_FD_WDEC		(WM_USER + 13)
#define HP_IOCP_WM_FD_END		(WM_USER + 14)

#define HP_IOCP_WM_FD_FIRST     HP_IOCP_WM_FD_ADD
#define HP_IOCP_WM_FD_LAST      HP_IOCP_WM_FD_END

#define HP_IOCP_WMF_FDR         (1 << 1)
#define HP_IOCP_WMF_FDW         (1 << 2)
#define HP_IOCP_WMF_FDE         (1 << 3)

#define HP_IOCP_RBUF  (1024 * 4)

/////////////////////////////////////////////////////////////////////////////////////
/*
 * the message between threads
 **/
struct hp_iocp_msg {
	int             id;         /* ID for hp_iocp_item */
	int             flags;      /* flags for this msg, see HP_IOCP_MSGF_XXX */
	int             err;        /* error for this I/O operation */
	char            errstr[64];

	WSAOVERLAPPED * overlapped; /* WSARecv overlapped */
	hp_sock_t       sock;
};

/* for write */
struct hp_iocp_obuf {
	void *         ptr;         /* ptr to free */
	hp_iocp_free_t free;        /* for free ptr */

	WSABUF         buf;         /* the buf, will change while writing */

	void *         BUF;         /* init buf */
	size_t         LEN;         /* init buf length */
};

/* the I/O context */
struct hp_iocp_item {
	int			  id;			/* ID for this item */
	hp_sock_t     sock;         /* the socket */

	/* for read request */
	size_t        rb_max;       /* max of read block */
	size_t        rb_size;      /* size per read block */

	size_t        n_rb;         /* number of read blocks */
	size_t        n_wb;			/* number of write requests */

	/* for write */
	struct hp_iocp_obuf ** obuf; /* bufs to write */
	/* for read */
	sds           ibuf;

	/* for stats */
	size_t        ibytes;       /* total bytes read */
	size_t        obytes;       /* total bytes written */

	/* callback to process data, in ibuf
	 * @return: >0: continue pending I/O request; =0: stop pending I/O request; <0: remove this I/O
	 *  */
	int (* on_data)(hp_iocp * iocpctx, int index, char * buf, size_t * len);
	/* callback if error */
	int (* on_error)(hp_iocp * iocpctx, int index, int err, char const * errstr);
	/*
	 * callback for connect
	 * */
	hp_sock_t(* connect)(hp_iocp * iocpctx, int index);

	int             err;        /* error for this I/O operation */
	hp_err_t        errstr;

	void * arg;
};

/*
 * overlapped in WSARecv/WSASend
 * */
struct hp_iocp_overlapped {
	int			    id;         /* ID for hp_iocp_item */
	char            io;         /* whether read or write for this I/O? 0 for read, else write */
	WSABUF *        bufs;       /* the buffers */
	size_t          n_bufs;     /* length of bufs */
	void *          arg;        /* user data */
};

/////////////////////////////////////////////////////////////////////////////////////
/*
 * functions for hp_iocp_msg
 * */
static inline struct hp_iocp_msg * hp_iocp_msg_alloc(hp_sock_t sock)
{
	struct hp_iocp_msg * msg = calloc(1, sizeof(struct hp_iocp_msg));
	msg->sock = sock;
	return msg;
}

/////////////////////////////////////////////////////////////////////////////////////
/*
 * functions for Windows message queue
 * */
static int hp_iocp_msg_post(int tid, HWND hwnd, int type, struct hp_iocp_msg * msg)
{
	int rc;

	for (;;) {
		if (hwnd) rc = PostMessage(hwnd, (UINT)type, (WPARAM)msg, (LPARAM)0);
		else      rc = PostThreadMessage((DWORD)tid, (UINT)type, (WPARAM)msg, (LPARAM)0);
		
		if(rc) { break; }
	}
	return rc;
}

/////////////////////////////////////////////////////////////////////////////////////
/*
 * socket
 * */
static void hp_iocp_close_socket(hp_iocp * iocpctx, hp_iocp_item * item, int flags)
{
	assert(iocpctx && item);

	if (!hp_sock_is_valid(item->sock)) {
		return;
	}
	closesocket(item->sock);

	if(flags){
		struct hp_iocp_msg * msg = calloc(1, sizeof(struct hp_iocp_msg));
		msg->id = item->id;
		msg->sock = item->sock;

		int tid = GetThreadId(iocpctx->sthread);
		hp_iocp_msg_post(tid, (HWND)0, HP_IOCP_WM_FD_RM, msg);
	}
	item->sock = hp_sock_invalid;
}

static int hp_iocp_do_socket(hp_iocp * iocpctx, hp_iocp_item * item)
{
	if (!(iocpctx && item))
		return -1;

#ifdef LIBHP_WITH_WIN32_INTERROP
	SOCKET sock = FDAPI_get_ossocket(item->sock);
#else
	SOCKET sock = item->sock;
#endif /* LIBHP_WITH_WIN32_INTERROP */


	if (!CreateIoCompletionPort((HANDLE)sock, (HANDLE)iocpctx->hiocp, (ULONG_PTR)item, (DWORD)0)) {
		int err = GetLastError();
		hp_err_t errstr = "CreateIoCompletionPort: %s";
		hp_log(stderr, "%s: CreateIoCompletionPort failed, error=%d/'%s'\n"
			, __FUNCTION__, err, hp_err(err, errstr));
		return -4;
	}

	struct hp_iocp_msg * msg = calloc(1, sizeof(struct hp_iocp_msg));
	msg->id = item->id;
	msg->sock = item->sock;
	int tid = GetThreadId(iocpctx->sthread);

	hp_iocp_msg_post(tid, (HWND)0, HP_IOCP_WM_FD_ADD, msg);
	return 0;
}

static int hp_iocp_connect(hp_iocp * iocpctx, hp_iocp_item * item)
{
	assert(iocpctx && item);

	int rc;
	if (hp_sock_is_valid(item->sock)) {
		if (!item->connect)
			return -1;
		item->sock = item->connect(iocpctx, item->id);
		if (!hp_sock_is_valid(item->sock)) {
			return -1;
		}
		rc = hp_iocp_do_socket(iocpctx, item);
		assert(rc == 0);
	}

	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////

/*
 * for hp_iocp_item
 */

 /**
 * when an item is available?
 * read blocks clean and write list clean
 */
static int is_item_avail(hp_iocp_item * item)
{
	assert(item);
	return !(
		hp_sock_is_valid(item->sock) ||  /* socket is OK */
		item->n_rb != 0              ||  /* still has read block */
		item->n_wb != 0              ||  /* still has write request */
		(item->obuf)					/* write list NOT empty: 
										 * 1.will add to "write request" later; 2.idle ones */
	);
}

static int is_item_done(hp_iocp_item * item)
{
	assert(item);
	return (!hp_sock_is_valid(item->sock) && item->n_rb == 0 && item->n_wb == 0);
}

static void rm_item_obuf(hp_iocp_item * item)
{
	assert(item);
	if (item && item->obuf) {
		for (; !cvector_empty(item->obuf); ) {
			struct hp_iocp_obuf * obuf = item->obuf[0];
			if (obuf->free)
				obuf->free(obuf->ptr);

			cvector_remove(item->obuf, item->obuf);
			free(obuf);
		}

		cvector_free(item->obuf);
		item->obuf = 0;
	}
	sdsclear(item->ibuf);
}

static void hp_iocp_on_item_error(hp_iocp * iocpctx, hp_iocp_item * item
	, int flags
	, int err, char const * errstr)
{
	hp_iocp_close_socket(iocpctx, item, flags);
	if (item->n_wb == 0) { rm_item_obuf(item); }

	if (is_item_done(item) && item->on_error) {
		item->on_error(iocpctx, item->id, err, errstr);
	}
}

/////////////////////////////////////////////////////////////////////////////////////

static int hp_iocp_def_pack(hp_iocp * iocpctx, int index, char * buf, size_t len)
{
	assert(iocpctx && buf && len);

	hp_log(stderr, "%s: dropped, len=%u\n" , __FUNCTION__, len);
	return 1; /* need one more bytes :) */
}

/////////////////////////////////////////////////////////////////////////////////////
/*
 * for hp_iocp_overlapped
 */
static struct hp_iocp_overlapped * hp_iocp_overlapped_get(WSAOVERLAPPED * overlapped)
{
	if (!overlapped) return 0;
	struct hp_iocp_overlapped * iocpol = 0;
	memcpy(&iocpol, (char *)overlapped - sizeof(struct hp_iocp_overlapped *), sizeof(struct hp_iocp_overlapped *));
	return iocpol;
}

static WSAOVERLAPPED * hp_iocp_overlapped_alloc(int id, hp_sock_t sock, char io, WSABUF * bufs, size_t n_bufs, void * arg)
{
	if (!(bufs && n_bufs > 0))
		return 0;
	struct hp_iocp_overlapped * iocpol = (struct hp_iocp_overlapped *)calloc(1, sizeof(struct hp_iocp_overlapped));
	iocpol->id = id;
	iocpol->io = io;
	iocpol->n_bufs = n_bufs;
	iocpol->bufs = (WSABUF *)calloc(n_bufs, sizeof(WSABUF));
	iocpol->arg = arg;
	memcpy(iocpol->bufs, bufs, sizeof(WSABUF) * n_bufs);

	void * addr = calloc(1, sizeof(struct hp_iocp_overlapped *) + sizeof(WSAOVERLAPPED));
	memcpy(addr, &iocpol, sizeof(struct hp_iocp_overlapped *));

	WSAOVERLAPPED * overlapped = (WSAOVERLAPPED *)((char *)addr + sizeof(struct hp_iocp_overlapped *));

	return overlapped;
}
static void hp_iocp_overlapped_free(WSAOVERLAPPED * overlapped)
{
	struct hp_iocp_overlapped * iocpol = hp_iocp_overlapped_get(overlapped);
	if (!iocpol) return;

	void * addr = (char *)overlapped - sizeof(struct hp_iocp_overlapped *);
	free(addr);
	free(iocpol->bufs);
	free(iocpol);
}

/* for users use */
#define wsaoverlapped_alloc(id, sock, io, bufs, n_bufs, arg) (hp_iocp_overlapped_alloc((id), (sock), (io), (bufs), (n_bufs), arg))
#define wsaoverlapped_free(overlapped) ((hp_iocp_overlapped_free(overlapped)))
#define wsaoverlapped_id(overlapped) ((hp_iocp_overlapped_get(overlapped))->id)
#define wsaoverlapped_io(overlapped) ((hp_iocp_overlapped_get(overlapped))->io)
#define wsaoverlapped_bufs(overlapped) ((hp_iocp_overlapped_get(overlapped))->bufs)
#define wsaoverlapped_n_bufs(overlapped) ((hp_iocp_overlapped_get(overlapped))->n_bufs)
#define wsaoverlapped_arg(overlapped) ((hp_iocp_overlapped_get(overlapped))->arg)

/////////////////////////////////////////////////////////////////////////////////////
/* for WSARecv */
static char zreadchar[1];

/*
 * the actual I/O functions
 * */
static int hp_iocp_do_read(hp_iocp * iocpctx, hp_iocp_item * item)
{
	assert((iocpctx && item));

	if (!hp_sock_is_valid(item->sock)) { return 0; }

	// Use zero length read with overlapped to get notification
	// of when data is available
	// see redis.win\src\Win32_Interop\win32_wsiocp.c\WSIOCP_QueueNextRead
	WSABUF bufs[1];
	bufs[0].buf = zreadchar;
	bufs[0].len = 0;

	WSAOVERLAPPED * overlapped = wsaoverlapped_alloc(item->id, item->sock, 0, bufs, 1, 0);
	DWORD flags = 0;
	int rc = WSARecv(item->sock, (LPWSABUF)wsaoverlapped_bufs(overlapped), (DWORD)(wsaoverlapped_n_bufs(overlapped))
		, 0, (LPDWORD)&flags, (LPWSAOVERLAPPED)overlapped, (LPWSAOVERLAPPED_COMPLETION_ROUTINE)0);
	DWORD err = WSAGetLastError();

	if ((rc == SOCKET_ERROR) && (WSA_IO_PENDING != err)) {
		wsaoverlapped_free(overlapped);

		hp_err_t errstr = "WSARecv: %s";
		hp_iocp_on_item_error(iocpctx, item, 1, err, hp_err(err, errstr));
		return -1;
	}
	else {
		++item->n_rb;
	}
	return 0;
}

static void hp_iocp_do_write(hp_iocp * iocpctx, hp_iocp_item *  item)
{
	assert ((iocpctx && item && item->obuf));

	if (!hp_sock_is_valid(item->sock)) { return; }
	if(cvector_empty(item->obuf)) { return; }

	struct hp_iocp_obuf * obuf = item->obuf[0];
	cvector_remove(item->obuf, item->obuf);

	WSABUF bufs[1];
	bufs[0].buf = obuf->buf.buf;
	bufs[0].len = obuf->buf.len;
	WSAOVERLAPPED * overlapped = wsaoverlapped_alloc(item->id, item->sock, 1, bufs, 1, obuf);
	DWORD flags = 0;
	int rc = WSASend(item->sock, (LPWSABUF)wsaoverlapped_bufs(overlapped)
		, (DWORD)wsaoverlapped_n_bufs(overlapped)
		, (LPDWORD)0, flags, (LPWSAOVERLAPPED)overlapped, (LPWSAOVERLAPPED_COMPLETION_ROUTINE)0);
	DWORD err = WSAGetLastError();

	if ((rc == SOCKET_ERROR) && (WSA_IO_PENDING != err)) {

		wsaoverlapped_free(overlapped);
		if (obuf->free) { obuf->free(obuf->ptr); }
		free(obuf);

		hp_err_t errstr = "WSASend: %s";
		hp_iocp_on_item_error(iocpctx, item, 1, err, hp_err(err, errstr));
	}
	else {
		++item->n_wb;
	}
}

/////////////////////////////////////////////////////////////////////////////////////
/*
 * message handler
 * */

static hp_iocp_item * hp_iocp_item_find_by_sock(hp_iocp * iocpctx, hp_sock_t  sock)
{
	int i;
	int sz = cvector_capacity(iocpctx->items);
	for (i = 0; i < sz; ++i) {
		hp_sock_t s = iocpctx->items[i].sock;

		if (hp_sock_is_valid(s) && s == sock)
			return iocpctx->items + i;
	}
	return 0;
}

int hp_iocp_handle_msg(hp_iocp * iocpctx, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (!iocpctx)
		return -2;
	if (!IS_HP_IOCP_WM(iocpctx, message))
		return -1;

	int rc;
	struct hp_iocp_msg * iocpmsg = (struct hp_iocp_msg *)wParam;
	assert(iocpmsg);
	hp_iocp_item * item;

	if(message == HP_IOCP_WM_FD(iocpctx)){
		item = hp_iocp_item_find_by_sock(iocpctx, iocpmsg->sock);
		if (item) {
			if (iocpmsg->flags & HP_IOCP_WMF_FDE) {
				hp_iocp_on_item_error(iocpctx, item, 0, 0, "fd exception");
			}
			else {
				if (iocpmsg->flags & HP_IOCP_WMF_FDR) {
					if(item->on_data) { rc = hp_iocp_do_read(iocpctx, item); }
					else { item->connect(iocpctx, item->id); }
				}
				if (iocpmsg->flags & HP_IOCP_WMF_FDW) {
					hp_iocp_do_write(iocpctx, item);
				}
			}
		}
	}
	else if (message == HP_IOCP_WM_IO(iocpctx)) {
		assert(cvector_in(iocpctx->items, iocpmsg->id));
		item = iocpctx->items + iocpmsg->id;
		assert(item);

		WSAOVERLAPPED * overlapped = iocpmsg->overlapped;
		assert(overlapped);
		WSABUF * wsabuf = wsaoverlapped_bufs(overlapped);
		assert(wsabuf);
		size_t nbuf = (size_t)overlapped->InternalHigh;
		/*
		* TODO:
		* hp_iocp_overlapped support gatter I/O, (e.g. writev/readv)
		* but for simplicity, current implementation(hp_iocp_read/hp_iocp_do_write) only read/write one buffer per time
		* */
		assert(wsaoverlapped_n_bufs(overlapped) == 1);

		if (wsaoverlapped_io(overlapped) == 0) { /* read done */
			assert(wsabuf && wsabuf->buf && wsabuf->len == 0 && nbuf == 0);
			int err = 0;
			--item->n_rb;
			nbuf = (int)read_a(item->sock, &err, item->ibuf + sdslen(item->ibuf), sdsavail(item->ibuf), 0);
			if (EAGAIN == err) {
				if (nbuf > 0) {
					sdsIncrLen(item->ibuf, nbuf);
					item->ibytes += nbuf;

					size_t ibufL, ibufl;
					ibufL = ibufl = sdslen(item->ibuf);
					rc = item->on_data(iocpctx, item->id, item->ibuf, &ibufl);
					sdsIncrLen(item->ibuf, ibufl - ibufL);

					if (rc < 0) { /* close by user */
						hp_iocp_close_socket(iocpctx, item, 1);
						if (item->n_wb == 0) { rm_item_obuf(item); }

						item->err = 0;
						strncpy(item->errstr, "user callback", sizeof(item->errstr));
					}
				}
			}
			else { /* read error, EOF? */
				hp_iocp_close_socket(iocpctx, item, 1);
				if (item->n_wb == 0) { rm_item_obuf(item); }

				item->err = iocpmsg->err;
				strncpy(item->errstr, iocpmsg->errstr, sizeof(item->errstr));
			}
		}
		else { /* write done */
			assert(nbuf <= wsabuf->len);    /* finished bytes should NOT bigger than committed */
			struct hp_iocp_obuf * obuf = wsaoverlapped_arg(overlapped);
			assert(obuf);
			--item->n_wb;

			if (nbuf > 0 && nbuf != wsabuf->len) {
				item->obytes += nbuf;
				/* write some, update for recommit */
				assert(item->n_wb == 0);

				obuf->buf.buf = (char *)wsabuf->buf + nbuf;
				obuf->buf.len = (wsabuf->len - nbuf);
				cvector_push_back(item->obuf, obuf);
			}
			else { /* write done or error, EOF? */
				if (obuf->free) { obuf->free(obuf->ptr); }
				free(obuf);

				if (nbuf == 0) {
					hp_iocp_close_socket(iocpctx, item, 1);
					if (item->n_wb == 0) { rm_item_obuf(item); }
				}
			}
		}
		/* this is a place where we need to check if item still works fine */
		if(!hp_sock_is_valid(item->sock) && item->n_wb == 0) { 
			rm_item_obuf(item); 
		}
		if (is_item_done(item) && item->on_error) {
			item->on_error(iocpctx, item->id, item->err, item->errstr);
		}
		wsaoverlapped_free(overlapped);
	}
	else{
		assert(0 && "hp/hp_iocp_handle_msg");
	}
	free(iocpmsg);
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////
/*
 * select thread function
 * */
hp_tuple2_t(hp_select_sock, hp_sock_t /* the socket */, int /* num of write blocks */);
static inline hp_select_sock * select_sock_alloc(hp_sock_t sock) {
	hp_select_sock * p = calloc(1, sizeof(hp_select_sock)); 
	p->_1 = sock; 
	return p;
};

static void select_sock_free(void *ptr)
{
	hp_select_sock * sock = (hp_select_sock *)ptr;
	assert(ptr);
	free(ptr);
}

static int select_sock_match(void *ptr, void *key)
{
	assert(ptr);
	hp_select_sock * sock = (hp_select_sock *)ptr;
	hp_sock_t k = key? *(hp_sock_t *)key : hp_sock_invalid;
	return (k == sock->_1);
}

static unsigned WINAPI hp_iocp_select_threadfn(void * arg)
{
#ifndef NDEBUG
	hp_log(stdout, "%s: enter thread function, id=%d\n", __FUNCTION__, GetCurrentThreadId());
#endif /* NDEBUG */
	hp_iocp * iocpctx = (hp_iocp *)arg;
	assert(iocpctx);
	int rc, exit_ = 0;
	list * socks = listCreate();
	listSetFreeMethod(socks, select_sock_free);
	listSetMatchMethod(socks, select_sock_match);
#ifndef LIBHP_WITH_WIN32_INTERROP

	struct fd_set rfds_obj, wfds_obj, exceptfds_obj;
	struct fd_set * rfds = &rfds_obj, *wfds = &wfds_obj, * exceptfds = &exceptfds_obj;
	struct timeval timeoutobj = { 0, iocpctx->stime_out * 1000 }, * timeout = &timeoutobj;

	for (;;) {
		/* reset fd_sets for select */
		FD_ZERO(rfds);
		FD_ZERO(wfds);
		FD_ZERO(exceptfds);
		/* try to get fd for select */
		MSG msgobj = { 0 }, *message = &msgobj;
		for(; PeekMessage((LPMSG)message, (HWND)-1
				, (UINT)HP_IOCP_WM_FD_FIRST, (UINT)HP_IOCP_WM_FD_LAST
				, PM_REMOVE | PM_NOYIELD); ) {
			struct hp_iocp_msg * msg = (struct hp_iocp_msg *)message->wParam;

			if(message->message == HP_IOCP_WM_FD_END) { exit_ = 1; }
			else {
				if(!msg) { continue; }

				if (message->message == HP_IOCP_WM_FD_ADD) {
					listAddNodeTail(socks, select_sock_alloc(msg->sock));
				}
				else{
					listNode * node = listSearchKey(socks, &msg->sock);
					if (node) {
						hp_select_sock * ssock = listNodeValue(node);

						if (message->message == HP_IOCP_WM_FD_RM) { listDelNode(socks, node); }
						else if (message->message == HP_IOCP_WM_FD_WINC && ssock) { ++ssock->_2; }
						else if (message->message == HP_IOCP_WM_FD_WDEC && ssock) { --ssock->_2; }
					}
				}
				free(msg);
			}
		}
		/* if no fd to select, just sleep */
		if (!(listLength(socks) > 0)) {
			if(exit_) { break; }
			Sleep(iocpctx->stime_out);
			continue;
		}

		listNode * node;
		listIter * iter = listGetIterator(socks, 0);
		for (node = 0; (node = listNext(iter));) {
			hp_select_sock * sock = listNodeValue(node);

			_fd_set(sock->_1, rfds);
			_fd_set(sock->_1, wfds);
			_fd_set(sock->_1, exceptfds);
		}
		listReleaseIterator(iter);

		int n = select(listLength(socks), rfds, wfds, (fd_set *)exceptfds, timeout);
		if(n == 0) /* timedout */
			continue; 
		else if (n == SOCKET_ERROR) {
			/* see https://docs.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-select
			 * for more error codes */
			int err = WSAGetLastError();
			hp_err_t errstr = "select: %s";
			hp_log(stderr, "%s: select error, err=%d/'%s'\n", __FUNCTION__, err, hp_err(err, errstr));
			if((err == WSAEINTR || err == WSAEINPROGRESS))
				continue;
			/* socket is closed by user? */
			listIter * iter = listGetIterator(socks, 0);
			for (node = 0; (node = listNext(iter));) {
				hp_select_sock * sock = listNodeValue(node);

				/* see https://docs.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-getsockopt */
				int optval = 0;
				int optlen = sizeof(int);
				rc = getsockopt(sock->_1, SOL_SOCKET, SO_TYPE, (char*)&optval, &optlen);
				if (rc != 0) {
					struct hp_iocp_msg * msg = hp_iocp_msg_alloc(sock->_1);
					msg->flags = HP_IOCP_WMF_FDE;

					hp_iocp_msg_post(iocpctx->ctid, iocpctx->hwnd, HP_IOCP_WM_FD(iocpctx), msg);
					listDelNode(socks, node);
				}
			}
			listReleaseIterator(iter);

			continue; /* stop this time */
		}

		iter = listGetIterator(socks, 0);
		for (; (node = listNext(iter));) {

			hp_select_sock * sock = listNodeValue(node);
			if (_fd_isset(sock->_1, rfds)
				|| (_fd_isset(sock->_1, wfds) && sock->_2 > 0)
				|| _fd_isset(sock->_1, exceptfds)) {

				struct hp_iocp_msg * msg = hp_iocp_msg_alloc(sock->_1);
				if (_fd_isset(sock->_1, exceptfds)) {
					listDelNode(socks, node);
					msg->flags = HP_IOCP_WMF_FDE;
				}
				else {
					if (_fd_isset(sock->_1, rfds))
						msg->flags |= HP_IOCP_WMF_FDR;
					if (_fd_isset(sock->_1, wfds) && sock->_2 > 0)
						msg->flags |= HP_IOCP_WMF_FDW;
				}

				hp_iocp_msg_post(iocpctx->ctid, iocpctx->hwnd, HP_IOCP_WM_FD(iocpctx), msg);
			}
		}
		listReleaseIterator(iter);

		Sleep(iocpctx->stime_out);
	}
#endif /* LIBHP_WITH_WIN32_INTERROP */			

	listRelease(socks);
	/* post "exit" message */
	hp_iocp_msg_post(iocpctx->ctid, iocpctx->hwnd, HP_IOCP_WM_END(iocpctx), GetCurrentThread());

#ifndef NDEBUG
	hp_log(stdout, "%s: exit thread function\n", __FUNCTION__);
#endif /* NDEBUG */
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////
/*
 * NOTE: the error handle is from book <Windows via c/c++,5th>
 */
static unsigned WINAPI hp_iocp_threadfn(void * arg)
{
#ifndef NDEBUG
	hp_log(stdout, "%s: enter thread function, id=%d\n", __FUNCTION__, GetCurrentThreadId());
#endif /* NDEBUG */
	hp_iocp * iocpctx = (hp_iocp *)arg;
	assert(iocpctx);

	BOOL rc;
	DWORD err = 0;
	DWORD iobytes = 0;
	void * key = 0;
	WSAOVERLAPPED * overlapped = 0;

	for (;;) {
		rc = GetQueuedCompletionStatus(iocpctx->hiocp, &iobytes, (LPDWORD)&key, (LPOVERLAPPED * )&overlapped, (DWORD)1000);
		err = WSAGetLastError();

		if (rc || overlapped) { /* OK or quit */
			if (rc && !overlapped) 
				break;  /* PostQueuedCompletionStatus called */
			assert(overlapped);
			struct hp_iocp_msg * msg = calloc(1, sizeof(struct hp_iocp_msg));
			msg->err = err;
			msg->overlapped = overlapped;
			msg->id = wsaoverlapped_id(overlapped);

			hp_iocp_msg_post(iocpctx->ctid, iocpctx->hwnd, HP_IOCP_WM_IO(iocpctx), msg);
		}
		else {
			if (err == WAIT_TIMEOUT)
				continue;
			else {
				char errstr[64] = "";
				hp_log(stderr, "%s: bad call to GetQueuedCompletionStatus, error=%d/'%s', IO=%d"
					", key=%p, overlapped=%p\n", __FUNCTION__
					, err, errstr, (int)iobytes, key, overlapped);
			}
		}
	}
	/* post "exit" message */
	hp_iocp_msg_post(iocpctx->ctid, iocpctx->hwnd, HP_IOCP_WM_END(iocpctx), GetCurrentThread());

#ifndef NDEBUG
	hp_log(stdout, "%s: exit thread function\n", __FUNCTION__);
#endif /* NDEBUG */
	return 0;
}

int hp_iocp_run(hp_iocp * iocpctx, int tid, HWND hwnd)
{
	if (!iocpctx)
		return -1;
	if (!(tid > 0 || hwnd))
		return -2;

	iocpctx->ctid = tid;
	iocpctx->hwnd = hwnd;
	/* IOCP and the threads */
	iocpctx->hiocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, (HANDLE)0, (ULONG_PTR)0, (DWORD)iocpctx->n_threads);
	assert(iocpctx->hiocp);

	int i;
	for (i = 0; i < iocpctx->n_threads; i++) {
		uintptr_t hdl = _beginthreadex(NULL, 0, hp_iocp_threadfn, (void *)iocpctx, 0, 0);
		assert(hdl);
		iocpctx->threads[i] = (HANDLE)hdl;
	} {
		uintptr_t hdl = _beginthreadex(NULL, 0, hp_iocp_select_threadfn, (void *)iocpctx, 0, 0);
		assert(hdl);
		iocpctx->sthread = (HANDLE)hdl;
	}

	return 0;
}

int hp_iocp_init(hp_iocp * iocpctx, int nthreads, int wmuser, int stime_out, void * user)
{
	int i;
	if(!iocpctx)
		return -1;
	memset(iocpctx, 0, sizeof(hp_iocp));

	if(wmuser < WM_USER)
		wmuser = WM_USER + 2021;
	iocpctx->wmuser = wmuser;

	if (nthreads > HP_IOCP_TMAX)
		nthreads = HP_IOCP_TMAX;
	if (nthreads <= 0) {
		SYSTEM_INFO sysInfo;
		GetSystemInfo(&sysInfo);
		nthreads = (int)sysInfo.dwNumberOfProcessors - 1;
	}
	if (nthreads <= 0) 
		nthreads = 1;

	iocpctx->n_threads = nthreads;
	iocpctx->stime_out = stime_out;
	iocpctx->user = user;
	
	cvector_init(iocpctx->items, 2);
	for (i = 0; i < cvector_capacity(iocpctx->items); ++i) {
		memset(iocpctx->items + i, 0, sizeof(hp_iocp_item));
		iocpctx->items[i].id = i;
		iocpctx->items[i].sock = hp_sock_invalid;
		iocpctx->items[i].ibuf = sdsempty();
	}
	return 0;
}

void hp_iocp_uninit(hp_iocp * iocpctx)
{
	int i, rc;
	if (!iocpctx) { return; }

	HANDLE * threads =  iocpctx->threads;
	int nthreads = iocpctx->n_threads + 1;
	int tid = GetThreadId(iocpctx->sthread);
	threads[iocpctx->n_threads] = iocpctx->sthread;
	int sz = cvector_capacity(iocpctx->items);

	/* close fds */
	for (i = 0; i < sz; ++i) {
		hp_sock_t s = iocpctx->items[i].sock;

		if (hp_sock_is_valid(iocpctx->items[i].sock)) {
			hp_iocp_close_socket(iocpctx, iocpctx->items + i, 1);
		}
	}

	/* wait for all I/O done */
	for (;;) {
	again:
		MSG msgobj = { 0 }, *msg = &msgobj;
		for (; PeekMessage((LPMSG)msg, (HWND)-1
			, (UINT)HP_IOCP_WM_FIRST(iocpctx), (UINT)HP_IOCP_WM_LAST(iocpctx)
			, PM_REMOVE | PM_NOYIELD); ) {

			assert(IS_HP_IOCP_WM(iocpctx, msg->message));
			rc = hp_iocp_handle_msg(iocpctx, msg->message, msg->wParam, msg->lParam);
		}

		/* check if all clear */
		for (i = 0; i < sz; ++i) {
			hp_iocp_item * item = iocpctx->items + i;

			if(hp_sock_is_valid(item->sock) ||  /* socket is OK */
				item->n_rb != 0 ||  /* still has read block */
				item->n_wb != 0     /* still has write request */
				) {
				goto again;
			}
		}
		break;
	}

	/* stop all working threads */
	for (i = 0; i < iocpctx->n_threads; i++) {
		PostQueuedCompletionStatus(iocpctx->hiocp, (DWORD)0, (ULONG_PTR)0, (LPOVERLAPPED)0);
	}
	hp_iocp_msg_post(tid, (HWND)0, HP_IOCP_WM_FD_END, 0);

	rc = WaitForMultipleObjects(nthreads, threads, TRUE, INFINITE);
	if (rc == WAIT_OBJECT_0) {
		CloseHandle(iocpctx->hiocp);
	}

	/* free*/
	for (i = 0; i < sz; ++i) {
		hp_iocp_item * item = iocpctx->items + i;
		rm_item_obuf(item);
		sdsfree(iocpctx->items[i].ibuf);
	}

	cvector_free(iocpctx->items);

	/* reset thread message queue */
	MSG msgobj = { 0 }, *msg = &msgobj;
	for (; PeekMessage((LPMSG)msg, (HWND)-1
		, (UINT)HP_IOCP_WM_FIRST(iocpctx), (UINT)HP_IOCP_WM_LAST(iocpctx)
		, PM_REMOVE | PM_NOYIELD); ) {

		assert(IS_HP_IOCP_WM(iocpctx, msg->message));

		if (msg->message == HP_IOCP_WM_END(iocpctx)) {
			for (i = 0; i < nthreads; i++) {
				if (threads[i] == msg->wParam) {
					threads[i] = 0;
				}
			}
		}
		else { 
			free((struct hp_iocp_msg *)msg->wParam); 
		}
	}
	/*TODO: assert threads[i]==0? */
	return;
}

int hp_iocp_add(hp_iocp * iocpctx
	, int rb_max, int rb_size
	, hp_sock_t sock
	, hp_sock_t(* on_connect)(hp_iocp * iocpctx, int index)
	, int (* on_data)(hp_iocp * iocpctx, int index, char * buf, size_t * len)
	, int (* on_error)(hp_iocp * iocpctx, int index, int on_error, char const * errstr)
	, void * arg
	)
{
	int rc;
	if (!(iocpctx))
		return -1;

	int is_c = (on_data && ((hp_sock_is_valid(sock)) || on_connect));
	int is_l = (!on_data && on_connect && (hp_sock_is_valid(sock)));
	if(!(is_c || is_l))
		return -2;
	
	if (!(iocpctx->ctid > 0 || iocpctx->hwnd))
		return -3;

	hp_iocp_item * item = 0;
	int i, j, k;
	for (k = 0;;) {
		for (i = k; i < cvector_capacity(iocpctx->items); ++i) {
			if (is_item_avail(iocpctx->items + i)) {
				item = iocpctx->items + i;
				goto item_done;
			}
		}
		k = cvector_capacity(iocpctx->items);
		cvector_grow(iocpctx->items, k * 2);

		for (j = k; j < cvector_capacity(iocpctx->items); ++j) {
			memset(iocpctx->items + j, 0, sizeof(hp_iocp_item));
			iocpctx->items[j].id = j;
			iocpctx->items[j].sock = hp_sock_invalid;
			iocpctx->items[j].ibuf = sdsempty();
		}
	}
item_done:
	item->sock = sock;
	item->rb_size = (rb_size <= 0 ? (int)HP_IOCP_RBUF : rb_size);
	item->rb_max = 0; //(rb_max <= 0 ? 4 : rb_max);
	item->connect = on_connect;
	item->on_data = on_data;
	item->on_error = on_error;
	item->arg = arg;
	item->ibuf = sdsMakeRoomFor(item->ibuf, 1024 * 128);
	/* connect */
	if (hp_sock_is_valid(item->sock)) {
		rc = hp_iocp_do_socket(iocpctx, item);
		if(rc != 0)
			return -3;
	}
	else {
		rc = hp_iocp_connect(iocpctx, item);
		if(rc != 0)
			return -3;
	}
	cvector_init(item->obuf, 1);

	return item->id;
}

int hp_iocp_size(hp_iocp * iocpctx)
{
	if(!iocpctx)
		return -1;

	int i, n = 0;
	for (i = 0; i < cvector_capacity(iocpctx->items); ++i) {
		if (!is_item_avail(iocpctx->items + i))
			++n;
	}
	return n;
}

void * hp_iocp_arg(hp_iocp * iocpctx, int index)
{   
	return ((iocpctx && cvector_in(iocpctx->items, index))? iocpctx->items[index].arg : 0);
}

int hp_iocp_write(hp_iocp * iocpctx, int index, void * data, size_t ndata
	, hp_iocp_free_t freecb, void * ptr)
{
	int rc = 0;
	if(!(iocpctx && cvector_in(iocpctx->items, index) && data && ndata > 0)){
		rc = -1;
		goto ret;
	}
	hp_iocp_item * item = iocpctx->items + index;
	if (!item->obuf) {
		rc = -2;
		goto ret;
	}

	struct hp_iocp_obuf * obuf = calloc(1, sizeof(struct hp_iocp_obuf));
	obuf->ptr = ptr;
	if (!obuf->ptr)
		obuf->ptr = data;

	if (freecb == (void *)-1) {
		obuf->ptr = obuf->BUF = obuf->buf.buf = malloc(ndata);
		obuf->LEN = obuf->buf.len = ndata;
		memcpy(obuf->BUF, data, ndata);
		obuf->free = free;
	}
	else {
		obuf->free = freecb;

		obuf->BUF = obuf->buf.buf = data;
		obuf->LEN = obuf->buf.len = ndata;
	}

	cvector_push_back(item->obuf, obuf);

	/* tell select thread */
	//struct hp_iocp_msg * msg = calloc(1, sizeof(struct hp_iocp_msg));
	//msg->id = item->id;
	//msg->sock = item->sock;
	//int tid = GetThreadId(iocpctx->sthread);
	//hp_iocp_msg_post(tid, (HWND)0, HP_IOCP_WM_FD_WINC, msg);
ret:
	if (rc != 0 && freecb && (freecb != (void *)-1)) { freecb(ptr ? ptr : data); }
	return rc;
}

int hp_iocp_try_write(hp_iocp * iocpctx, int index)
{
	if (!(iocpctx && cvector_in(iocpctx->items, index)))
		return -1;
	hp_iocp_item * item = iocpctx->items + index;
	if (!item->obuf)
		return -2;
	hp_iocp_do_write(iocpctx, item);

	return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
/* tests */
#ifndef NDEBUG

#include <time.h>			/* difftime */
#include "http-parser/http_parser.h"
#include "hp/hp_net.h" //hp_net_connect
#include "hp/str_dump.h"   /* dumpstr */
#include "hp/string_util.h"
#include "gbk-utf8/utf8.h"

static char server_ip[128] = "";
static char s_url[1024] = "";
static int server_port = 80;

struct http_get_file {
	sds fname;
	HANDLE s_hfile;
	size_t s_written;
	size_t s_total;
	struct http_parser parser;
	time_t startt;
	int s_quit;
};

static int on_body(http_parser*, const char *at, size_t length);
static int on_headers_complete(http_parser*);
static struct http_parser_settings s_settings = {
		.on_headers_complete = on_headers_complete,
		.on_body = on_body
};

/* Replace escape sequences in an URL (or a part of an URL) */
/* works like strcpy(), but without return argument,
   except that outbuf == inbuf is allowed */
static void url_unescape_string(char *outbuf, const char *inbuf)
{
	unsigned char c, c1, c2;
	int i, len = strlen(inbuf);
	for (i = 0; i < len; i++) {
		c = inbuf[i];
		if (c == '%' && i < len - 2) { //must have 2 more chars
			c1 = toupper(inbuf[i + 1]); // we need uppercase characters
			c2 = toupper(inbuf[i + 2]);
			if (((c1 >= '0' && c1 <= '9') || (c1 >= 'A' && c1 <= 'F')) &&
				((c2 >= '0' && c2 <= '9') || (c2 >= 'A' && c2 <= 'F'))) {
				if (c1 >= '0' && c1 <= '9') c1 -= '0';
				else c1 -= 'A' - 10;
				if (c2 >= '0' && c2 <= '9') c2 -= '0';
				else c2 -= 'A' - 10;
				c = (c1 << 4) + c2;
				i = i + 2; //only skip next 2 chars if valid esc
			}
		}
		*outbuf++ = c;
	}
	*outbuf++ = '\0'; //add nullterm to string
}

#define url_field(u, url, fn, ubuf)                                  \
do {                                                                 \
  if ((u)->field_set & (1 << (fn))) {                                \
    memcpy(ubuf, (url) + (u)->field_data[(fn)].off,   \
      (u)->field_data[(fn)].len);                                    \
    (ubuf)[(u)->field_data[(fn)].len] = '\0';                          \
  } else {                                                           \
    ubuf[0] = '\0';                                                  \
  }                                                                  \
} while(0)

/////////////////////////////////////////////////////////////////////////////////////

static int http_get_file__on_error(hp_iocp * iocpctx, int index, int err, char const * errstr)
{
	assert(iocpctx && iocpctx->user);
	struct http_get_file * fctx = (struct http_get_file *)iocpctx->user;

	hp_log(stdout, "%s: disconnected, err=%d/'%s'\n", __FUNCTION__
		, err, errstr);
	if (fctx->s_hfile) {
		CloseHandle(fctx->s_hfile);
		fctx->s_hfile = 0;
	}
	fctx->s_quit = 1;

	return 0;
}

/* create the socket */
static hp_sock_t http_get_file__on_connect(hp_iocp * iocpctx, int index)
{
	assert(iocpctx);
	hp_sock_t sock = hp_net_connect(server_ip, server_port);

	sds ssock = sdsfromlonglong(sock);
	hp_log(stdout, "%s: connect to  server='%s:%d', socket=%s\n", __FUNCTION__
		, server_ip, server_port, ssock);
	sdsfree(ssock);
	return sock;
}

int http_get_file__on_unpack(hp_iocp * iocpctx, int index, char * buf, size_t * len)
{
	assert(buf && len);
	assert(iocpctx && iocpctx->user);
	struct http_get_file * fctx = (struct http_get_file *)iocpctx->user;

	size_t buflen = *len,  nparsed = http_parser_execute(&fctx->parser, &s_settings, buf, *len);

	if (nparsed > 0) {
		memmove(buf, buf + nparsed, sizeof(char) * (buflen - nparsed));
		*len -= nparsed;
	}

	if (fctx->parser.upgrade) { /* handle new protocol */
		return nparsed;
	}
	else if (nparsed != buflen) {
		/* Handle error. Usually just close the connection. */
		hp_log(stderr, "%s: http_parser_execute failed, fd=%d, parsed/buf=%zu/%d, buf='%s'\n"
			, __FUNCTION__, -1
			, nparsed, buflen
			, dumpstr(buf + nparsed, buflen - nparsed, 128));

		return -1;
	}
	return 0;
}

static int on_headers_complete(http_parser* p)
{
	assert(p && p->data);
	struct http_get_file * fctx = (struct http_get_file *)p->data;

	if(p->content_length > 0)	
		fctx->s_total = p->content_length;

	return 0;
}

static int on_body(http_parser* p, const char *at, size_t length)
{
	assert(p && p->data && at);
	struct http_get_file * fctx = (struct http_get_file *)p->data;

	int rc;
	char const * buf = strchr(s_url, '/');
	if(buf) buf +=1;

	rc = 0;
	if (!fctx->s_hfile) {
		fctx->fname = sdsMakeRoomFor(sdsempty(), strlen(buf));
		url_unescape_string(buf, buf);
		utf8_to_gb(buf, fctx->fname, sdsavail(fctx->fname));

		fctx->s_hfile = CreateFileA(fctx->fname, (DWORD)GENERIC_WRITE, (DWORD)0, 0, (DWORD)CREATE_ALWAYS, (DWORD)0, (DWORD)0);

		if (!fctx->s_hfile) {
			hp_log(stderr, "%s: open '%s' for write failed, err=%d/'%s'\n", __FUNCTION__,
				"test.flv", GetLastError(), "");
			return -1;
		}

		fctx->startt = time(0);
	}

	DWORD nwrite = 0;
	BOOL b = WriteFile(fctx->s_hfile, (LPVOID)at, (DWORD)length, &nwrite, (LPOVERLAPPED)0);
	assert(b && nwrite == length);

	fctx->s_written +=nwrite;

	if (fctx->s_total > 0 && fctx->s_written == fctx->s_total) {

		fctx->s_quit = 1;
		CloseHandle(fctx->s_hfile);
		/* */
		rc = -1;
		//closesocket(s);
	}

	static time_t lastlog = 0;
	if (fctx->s_quit || difftime(time(0), lastlog) > 0) {
		char s1[128] = "", s2[128] = "";

		hp_log(stdout, "%s: saved %s, %s/%s in %.0f s \n", __FUNCTION__
			, fctx->fname
			, byte_to_mb_kb_str_r(fctx->s_written, "%-3.1f %cB", s1)
			, byte_to_mb_kb_str_r(fctx->s_total, "%-3.1f %cB", s2)
			, difftime(time(0), fctx->startt)
			);

		lastlog = time(0);
	}

	return rc;
}
/////////////////////////////////////////////////////////////////////////////////////

int test_hp_iocp_main(int argc, char ** argv)
{
	int i,rc;
	hp_sock_t sock = 3;
	sds url = 0;

	/* parse argc/argv */
	struct optparse_long longopts[] = {
		{"url", 'u', OPTPARSE_REQUIRED},
		{0}
	};
	struct optparse options;
	optparse_init(&options, argv);

	for (; (rc = optparse_long(&options, longopts, NULL)) != -1; ) {
		switch (rc) {
		case 'u':
			url = sdsnew(options.optarg ? options.optarg : "");
			break;
		case '?':
			fprintf(stdout, "%s --url,-u=STRING\n", argv[0]);
			break;
		}
	}
	if(!url)	
		return -2;

	/* parse URL */
	struct http_parser_url uobj, *u = &uobj;
	if (http_parser_parse_url(url, sdslen(url), 0, u) != 0)
		return -1;

	url_field(u, url, UF_PATH, s_url);
	url_field(u, url, UF_HOST, server_ip);
	server_port = u->port > 0? u->port : 80;

	if(!(strlen(s_url) > 0 && strlen(server_ip) > 0))
		return -2;

	/* simple test */
	{
		WSAOVERLAPPED * ol = wsaoverlapped_alloc(1, sock, 0, 0, 0, 0);
		assert(!ol);
	} 
	{ 
		WSAOVERLAPPED * ol = wsaoverlapped_alloc(1, sock, 0, 0, 1, 0); 
		assert(!ol);
	}
	{
		WSABUF buf[1];
		WSAOVERLAPPED * ol = wsaoverlapped_alloc(1, sock, 0, buf, 0, 0);
		assert(!ol);
	}
	{
		WSABUF buf[1] = { 0 };
		buf[0].len = 10;
		buf[0].buf = malloc(10);

		WSAOVERLAPPED * ol = wsaoverlapped_alloc(1, sock, 0, buf, sizeof(buf) / sizeof(buf[0]), 0);
		assert(ol);
		assert(wsaoverlapped_id(ol) == 1);
		assert(wsaoverlapped_io(ol) == 0);
		assert(wsaoverlapped_n_bufs(ol) == 1);

		WSABUF * bufs = wsaoverlapped_bufs(ol);
		assert(memcmp(bufs, buf, sizeof(buf)) == 0);
		assert(bufs[0].buf == buf[0].buf);
		assert(memcmp(buf[0].buf, bufs[0].buf, 10) == 0); 

		wsaoverlapped_free(ol);
		free(buf[0].buf);
	}
	/* tests for hp_iocp_overlapped::arg */
	{
		WSABUF buf[1] = { 0 };
		buf[0].len = 10;
		buf[0].buf = malloc(10);

		WSAOVERLAPPED * ol = wsaoverlapped_alloc(1, sock, 0, buf, sizeof(buf) / sizeof(buf[0]), buf);
		assert(ol);
		assert(wsaoverlapped_io(ol) == 0);
		assert(wsaoverlapped_n_bufs(ol) == 1);
		assert(wsaoverlapped_arg(ol) == buf);

		WSABUF * bufs = wsaoverlapped_bufs(ol);
		assert(memcmp(bufs, buf, sizeof(buf)) == 0);
		assert(bufs[0].buf == buf[0].buf);
		assert(memcmp(buf[0].buf, bufs[0].buf, 10) == 0);

		wsaoverlapped_free(ol);
		free(buf[0].buf);
	}
	{
		WSABUF buf[1024] = { 0 };
		buf[0].len = 10;
		buf[0].buf = malloc(10);
		buf[1023].len = 10;
		buf[1023].buf = malloc(1024 * 1024 * 10);

		WSAOVERLAPPED * ol = wsaoverlapped_alloc(1, sock, 1, buf, sizeof(buf) / sizeof(buf[0]), 0);
		assert(ol);
		assert(wsaoverlapped_io(ol) == 1);
		assert(wsaoverlapped_n_bufs(ol) == 1024);

		WSABUF * bufs = wsaoverlapped_bufs(ol);
		assert(memcmp(bufs, buf, sizeof(buf)) == 0);
		assert(bufs[0].buf == buf[0].buf);
		assert(bufs[1023].buf == buf[1023].buf);

		assert(memcmp(buf[0].buf, bufs[0].buf, 10) == 0);
		assert(memcmp(buf[1023].buf, bufs[1023].buf, 1024 * 1024 * 10) == 0);

		wsaoverlapped_free(ol);

		free(buf[0].buf);
		free(buf[1023].buf);
	}
	
	{
		struct hp_iocp iocpcctxobj = { 0 }, *iocpctx = &iocpcctxobj;
		int index;
		rc = hp_iocp_init(iocpctx, 2, WM_USER + 100, 200, 0);
		assert(rc == 0);
		index = hp_iocp_add(iocpctx, 0, 0, 0, 0, 0, 0, 0); assert(index < 0);
	}

	/* hp_iocp basics */
	{
		struct hp_iocp iocpcctxobj = { 0 }, *iocpctx = &iocpcctxobj;
		rc = hp_iocp_init(iocpctx, 2, WM_USER + 100, 200,0);
		assert(rc == 0);

		int tid = (int)GetCurrentThreadId();
		rc = hp_iocp_run(iocpctx, tid, 0);
		assert(rc == 0);

		assert(hp_iocp_size(iocpctx) == 0);

		hp_iocp_uninit(iocpctx);
	}
	{
		struct hp_iocp iocpcctxobj = { 0 }, *iocpctx = &iocpcctxobj;
		rc = hp_iocp_init(iocpctx, 2, WM_USER + 100, 200, 0);
		assert(rc == 0);

		int tid = (int)GetCurrentThreadId();
		rc = hp_iocp_run(iocpctx, tid, 0);
		assert(rc == 0);

		size_t i;
		for (i = 0; i < 1; ++i) {
			int index = hp_iocp_add(iocpctx, 0, 0, 0, http_get_file__on_connect
				, 0, 0, 0);
			assert(index >= 0);
		}
		assert(hp_iocp_size(iocpctx) == 1);

		hp_iocp_uninit(iocpctx);
	}
	{
		struct hp_iocp iocpcctxobj = { 0 }, *iocpctx = &iocpcctxobj;
		rc = hp_iocp_init(iocpctx, 2, WM_USER + 100, 200, 0);
		assert(rc == 0);

		int tid = (int)GetCurrentThreadId();
		rc = hp_iocp_run(iocpctx, tid, 0);
		assert(rc == 0);

		size_t i;
		for (i = 0; i < 2; ++i) {
			int index = hp_iocp_add(iocpctx, 0, 0, 0, http_get_file__on_connect
				, 0, 0, 0);
			assert(index >= 0);
		}
		assert(hp_iocp_size(iocpctx) == 2);

		hp_iocp_uninit(iocpctx);
	}
	//
	/////////////////////////////////////////////////////////////////////////////////////
	// test: HTTP client get HTTP file
	{
		struct http_get_file s_httpgetfileobj = { 0 }, *fctx = &s_httpgetfileobj;
		http_parser_init(&fctx->parser, HTTP_RESPONSE);
		fctx->parser.data = fctx;

		/* the IOCP context */
		struct hp_iocp iocpcctxobj = { 0 }, *iocpctx = &iocpcctxobj;
		rc = hp_iocp_init(iocpctx, 2, WM_USER + 100, 200, fctx);
		assert(rc == 0);

		int tid = (int)GetCurrentThreadId();
		rc = hp_iocp_run(iocpctx, tid, 0);
		assert(rc == 0);

		/* prepare for I/O */
		int index = hp_iocp_add(iocpctx, 0, 0
			, 0, http_get_file__on_connect
			, http_get_file__on_unpack
			, http_get_file__on_error
			, 0);
		assert(index >= 0);

		sds data = sdscatprintf(sdsempty(), "GET %s HTTP/1.1\r\nHost: %s:%d\r\n\r\n"
					, s_url, server_ip, server_port);

		rc = hp_iocp_write(iocpctx, index, data, strlen(data), 0, 0);
		assert(rc == 0);

		hp_log(stdout, "%s: %s\n", __FUNCTION__, dumpstr(data, sdslen(data), sdslen(data)));

		for (rc = 0; !fctx->s_quit;) {
			MSG msgobj = { 0 }, *msg = &msgobj;
			if (PeekMessage((LPMSG)msg, (HWND)0, (UINT)0, (UINT)0, PM_REMOVE)) {
				rc = hp_iocp_handle_msg(iocpctx, msg->message, msg->wParam, msg->lParam);
			}
		}
		hp_iocp_uninit(iocpctx);
		sdsfree(data);

		sdsfree(fctx->fname);
	}
	/////////////////////////////////////////////////////////////////////////////////////

	sdsfree(url);

	return 0;
}
#endif /* NDEBUG */

#endif /* _MSC_VER */

