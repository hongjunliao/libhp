/*!
* This file is PART of libhp project
* @author hongjun.liao <docici@126.com>, @date 2018/12/3
*
* an Win32 IOCP wrapper
* */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */


#ifdef _MSC_VER
#include "hp/hp_iocp.h"   /* hp_iocp */
#include <winsock2.h>
#include "redis/src/adlist.h"
#include "hp/sdsinc.h"    /* sds */
#include "hp/hp_log.h"    /* hp_log */
#include "hp/hp_tuple.h"  /* hp_tuple2_t */
#include "hp/hp_err.h"	  /* hp_err */
#include "hp/hp_search.h" /* hp_lfind */
#include "hp/hp_tuple.h"  /* hp_tuple2_t */
#include "hp/hp_net.h"  /* read_a */
#include <process.h>    /* _beginthreadex */
#include <sysinfoapi.h> /* GetSystemInfo */
#include <stdio.h>
#include <string.h>     /* memset, ... */
#include <assert.h>     /* define NDEBUG to disable assertion */
#include <stdlib.h>
#include "c-vector/cvector.h" //cvector_size

/////////////////////////////////////////////////////////////////////////////////////
#define vecsize cvector_size
#define vecpush cvector_push_back
#define vecempty cvector_empty
#define vecrm cvector_remove
#define vecfree cvector_free
#define vecinit cvector_init
#define veccap cvector_capacity
/////////////////////////////////////////////////////////////////////////////////////

/* WM_USER + N */
#define HP_IOCP_WM_IO(c)        ((c)->wmuser + 1) // I/O done
#define HP_IOCP_WM_FD(c)        ((c)->wmuser + 2) // fd event: POLLIN?
#define HP_IOCP_WM_ADD(c)       ((c)->wmuser + 3) // add a fd to poll()
#define HP_IOCP_WM_RM(c)        ((c)->wmuser + 4) // rm a fd from poll()

/* add other WM_XXX here */

#define HP_IOCP_WM_FIRST(c)     HP_IOCP_WM_IO(c)
#define HP_IOCP_WM_LAST(c)      HP_IOCP_WM_RM(c)

#define IS_HP_IOCP_WM(c,msg)    ((msg) >= HP_IOCP_WM_FIRST(c) && (msg) <= HP_IOCP_WM_LAST(c))
/////////////////////////////////////////////////////////////////////////////////////
#define HP_IOCP_RBUF  (1024 * 4)
#define hp_iocp_is_quit(iocp) (iocp->ioid == -1)
/////////////////////////////////////////////////////////////////////////////////////
#define SOLISTEN 0x1024
/////////////////////////////////////////////////////////////////////////////////////

typedef struct hp_iocp_ol hp_iocp_ol;
typedef struct hp_iocp_item hp_iocp_item;

static int hp_iocp_msg_post(int tid, HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
/////////////////////////////////////////////////////////////////////////////////////
/* WSAOVERLAPPED */

struct hp_iocp_ol {
	WSAOVERLAPPED  wsaol;
	int       id;   /* ID for this item */
	void *    ptr;  /* ptr to free */
	hp_free_t free; /* for free ptr */
	WSABUF    buf;  /* the buf, will change while writing */
	int *     lastr;//bytes last read
};

static hp_iocp_ol * hp_iocp_ol_newi(int id, int n, int * lastr)
{
	assert(lastr);
	// Use zero length read with ol to get notification
	// of when data is available
	// see redis.win\src\Win32_Interop\win32_wsiocp.c\WSIOCP_QueueNextRead
	static char zreadchar[1];
//	WSABUF buf = {.len = *n, .buf = sdsnewlen(0, *n)};

	hp_iocp_ol * ol = calloc(1, sizeof(hp_iocp_ol) + n);
	ol->id = id;
	ol->buf.len = n;
	ol->buf.buf = (char *)(ol + 1);
	ol->lastr = lastr;

	return ol;
}

static hp_iocp_ol * hp_iocp_ol_newo(int id, void * data, size_t ndata
		, hp_free_t freecb, void * ptr)
{
	hp_iocp_ol * ol = calloc(1, sizeof(hp_iocp_ol));

	ol->id = id;
	ol->ptr = ptr;
	if (!ol->ptr)
		ol->ptr = data;

	ol->free = freecb;
	ol->buf.buf = data;
	ol->buf.len = ndata;

	return ol;
}

static void hp_iocp_ol_del(hp_iocp_ol * ol)
{
	assert(ol);
	if (ol->free)
		ol->free(ol->ptr);
	free(ol);
}

#define ol_newi hp_iocp_ol_newi
#define ol_newo hp_iocp_ol_newo
#define ol_buf(ol)  (&(ol)->buf)
#define ol_read(ol) (!(ol)->ptr)
#define ol_del  hp_iocp_ol_del

/////////////////////////////////////////////////////////////////////////////////////
/* the I/O context */

struct hp_iocp_item {
	int			  id;			/* ID for this item */
	SOCKET        fd;           /* the socket */

	/* for write */
	hp_iocp_ol ** obuf; /* bufs to write */
	/* for read */
	sds           ibuf;

	/* for stats */
	size_t        ibytes;       /* total bytes read */
	size_t        obytes;       /* total bytes written */

	/*
	 * callback for on_accept
	 * */
	int (* on_accept)(hp_iocp * iocp, void * arg);
	/* callback to process data, in ibuf
	 * @return: >0: continue pending I/O request; =0: stop pending I/O request; <0: remove this I/O
	 *  */
	int (* on_data)(hp_iocp * iocp, void * arg, char * buf, size_t * len);
	/* callback if error */
	int (* on_error)(hp_iocp * iocp, void * arg, int err, char const * errstr);
	int (* on_loop)(void * arg);
	int             err;        /* error for this I/O operation */
	hp_err_t        errstr;

	void * arg;
};

static int hp_iocp_item_find_by_id(const void * k, const void * e)
{
	assert(e && k);
	int kid = *(int *)k;
	hp_iocp_item * item = (hp_iocp_item *)e;

	return !(item->id == kid);
}

static int hp_iocp_item_find_by_id2(const void * k, const void * e)
{
	assert(e && k);
	int kid = *(int *)k;
	hp_iocp_item * item = (hp_iocp_item *)e;

	if(kid < item->id)       return -1;
	else if(kid == item->id) return 0;
	else                     return 1;
}

/*
 * use bsearch() is safe in most cases: "2147483647/(24*3600*50)==497"
 */
#define hp_iocp_item_find(iocp, id) (hp_iocp_item *)bsearch\
	((id), ((iocp)->items), vecsize((iocp)->items), sizeof(hp_iocp_item), hp_iocp_item_find_by_id2)

#define hp_iocp_item_is_removed(item) \
	((item)->on_accept == hp_iocp_def_on_accept || (item)->on_data == hp_iocp_def_on_data)

static int hp_iocp_item_on_error(hp_iocp * iocp, hp_iocp_item * item)
{
	assert((iocp && item));
	int rc = 0;
	vecfree(item->obuf);
	sdsfree((item)->ibuf);

	hp_iocp_msg_post(iocp->ptid, (HWND)0, HP_IOCP_WM_RM(iocp), item->id, 0);

	if(item->on_error)
		item->on_error(iocp, item->arg, item->err, item->errstr);

	int i = item - iocp->items, n = vecsize(iocp->items);

	if (i + 1 < vecsize(iocp->items)) {
		memmove(iocp->items + (i), iocp->items + (i)+1, sizeof(hp_iocp_item) * (n - (i + 1)));
		rc = 1;
	}
	--vecsize(iocp->items);
	return rc;
}

/////////////////////////////////////////////////////////////////////////////////////
/*
 * functions for Windows message queue
 * */
static int hp_iocp_msg_post(int tid, HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	int rc;

	for (;;) {
		if (hwnd) rc = PostMessage(hwnd, message, wParam, lParam);
		else      rc = PostThreadMessage((DWORD)tid, message, wParam, lParam);

		if(rc) { break; }
	}
	return rc;
}

/////////////////////////////////////////////////////////////////////////////////////
/*
 * bind socket with IOCP
 * */

static int hp_iocp_do_socket(hp_iocp * iocp, hp_iocp_item * item)
{
	assert((iocp && item));

	if (!CreateIoCompletionPort((HANDLE)item->fd, (HANDLE)iocp->hiocp, (ULONG_PTR)item, (DWORD)0)) {
		int err = GetLastError();
		hp_err_t errstr = "CreateIoCompletionPort: %s";
		hp_log(stderr, "%s: CreateIoCompletionPort failed, error=%d/'%s'\n"
			, __FUNCTION__, err, hp_err(err, errstr));

		return -4;
	}

	//FIXME: combine to ONE message?
	hp_iocp_msg_post(iocp->ptid, (HWND)0, HP_IOCP_WM_ADD(iocp), (WPARAM)item->id, (LPARAM)item->fd);
	if(item->on_accept)
		hp_iocp_msg_post(iocp->ptid, (HWND)0, HP_IOCP_WM_RM(iocp), (WPARAM)item->id, POLLIN | SOLISTEN);

	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////
/*
 * the actual I/O functions
 * */
static int hp_iocp_do_read(hp_iocp * iocp, int id, SOCKET fd, int n, int * lastr, int * err)
{
	assert((iocp && lastr && err));
	int rc = 0;

	hp_iocp_ol * ol = ol_newi(id, n, lastr);
	DWORD flags = 0;
	rc = WSARecv(fd, (LPWSABUF)ol_buf(ol), (DWORD)1
		, 0, (LPDWORD)&flags, (LPWSAOVERLAPPED)ol, (LPWSAOVERLAPPED_COMPLETION_ROUTINE)0);
	*err = WSAGetLastError();

	if ((rc == SOCKET_ERROR) && (WSA_IO_PENDING != *err)) {
		ol_del(ol);
		rc = -1;
	}
	else {
		*err = 0;
		rc = 0;
	}

	return rc;
}

//TODO: gatter I/O, (e.g. writev/readv)
static int hp_iocp_do_write(hp_iocp * iocp, hp_iocp_item *  item)
{
	assert ((iocp && item && item->obuf));
	int rc = 0;

	if(vecempty(item->obuf)) { return 0; }

	hp_iocp_ol * ol = item->obuf[0];
	vecrm(item->obuf, item->obuf);

	DWORD flags = 0;
	rc = WSASend(item->fd, (LPWSABUF)ol_buf(ol)
		, (DWORD)1
		, (LPDWORD)0, flags, (LPWSAOVERLAPPED)ol, (LPWSAOVERLAPPED_COMPLETION_ROUTINE)0);
	DWORD err = WSAGetLastError();

	if ((rc == SOCKET_ERROR) && (WSA_IO_PENDING != err)) {

		ol_del(ol);

		item->err = err;
		hp_err(err, item->errstr);
		rc = -1;
	}
	else {
		item->err = 0;
		rc = 0;
	}

	return rc;
}

/////////////////////////////////////////////////////////////////////////////////////
/*
 * message handler
 * 
 * because this IOCP wrapper use Windows message queue to sync
 * user thread must call this function in message process routine, this is done by call hp_iocp_run()
 * @see https://docs.microsoft.com/zh-cn/windows/desktop/winmsg/using-messages-and-message-queues
 *
 * @param message:    the message context, message ID, see hp_iocp_init
 * @param wParam:     the message context
 * @param lParam:     the message context
 *
 * @return:           0 on OK
 * */
static int hp_iocp_handle_msg(hp_iocp * iocp, UINT message, WPARAM wParam, LPARAM lParam)
{
	assert((iocp));
	assert (IS_HP_IOCP_WM(iocp, message));
	int rc = 0;

	if(message == HP_IOCP_WM_FD(iocp)){
		int id = wParam, flags = lParam;;

		hp_iocp_item * item = hp_iocp_item_find(iocp, &id);
		if(!item) {
			++iocp->n_dmsg0;
			return 0;
		}

		if(flags & (POLLERR | POLLHUP)) { item->err = flags;}
		else{
			if (flags & POLLIN) {
				assert(item->on_accept);
				item->on_accept(iocp, item->arg);
				++iocp->n_on_accept;
			}
			if ((flags & POLLOUT)){
				rc = hp_iocp_do_write(iocp, item);
				++iocp->n_on_pollout;
			}
		}
		if(rc != 0 || item->err)
			hp_iocp_item_on_error(iocp, item);
	}
	else if (message == HP_IOCP_WM_IO(iocp)) {
		hp_iocp_ol * ol = (hp_iocp_ol *)wParam;
		assert(ol);

		hp_iocp_item * item = hp_iocp_item_find(iocp, &ol->id);
		if(item){
			size_t nbuf = (size_t)ol->wsaol.InternalHigh;
			if (ol_read(ol) && nbuf > 0) { /* read done */
				item->ibytes += nbuf;
				InterlockedExchange(ol->lastr, nbuf);

				if(sdslen(item->ibuf) > 0) {

					for(; sdsavail(item->ibuf) < nbuf;){ //确保有足够空间容纳新来的数据
						size_t len = sdslen(item->ibuf);
						rc = item->on_data(iocp, item->arg, item->ibuf, &len);
						sdsIncrLen(item->ibuf, -(int)(sdslen(item->ibuf) - len));
						if(rc < 0) break;
					}
					if(rc >= 0){
						memcpy(item->ibuf + sdslen(item->ibuf), ol->buf.buf, nbuf);
						sdsIncrLen(item->ibuf, nbuf);

						size_t len = sdslen(item->ibuf);
						rc = item->on_data(iocp, item->arg, item->ibuf, &len);
						sdsIncrLen(item->ibuf, -(int)(sdslen(item->ibuf) - len));
					}
				} else {
					rc = item->on_data(iocp, item->arg, ol->buf.buf, &nbuf);
					if(rc >= 0 && nbuf > 0){
						memcpy(item->ibuf, ol->buf.buf, nbuf);
						sdsIncrLen(item->ibuf, nbuf);
					}
				}
				if(rc < 0) 
					hp_iocp_item_on_error(iocp, item);
			}
			else if (!ol_read(ol) && nbuf > 0) { /* write done */
				++iocp->n_on_o;
				assert(nbuf <= ol->buf.len);    /* finished bytes should NOT bigger than committed */
				if (nbuf > 0 && nbuf != ol->buf.len) {
					item->obytes += nbuf;
					/* write some, update for recommit */
					ol->buf.buf += nbuf;
					ol->buf.len -= nbuf;
					vecpush(item->obuf, ol);

					hp_iocp_msg_post(iocp->ptid, (HWND)0, HP_IOCP_WM_RM(iocp), item->id, POLLIN | POLLOUT);
				}
			}
			else if ((ol_read(ol) && nbuf == 0) || (!ol_read(ol) && nbuf == 0)){ /* read/write error, EOF? */
				item->err = lParam;
				hp_err(item->err, item->errstr);
				hp_iocp_item_on_error(iocp, item);
				++iocp->n_on_i;
			}
			else assert(0);
		}
		else  ++iocp->n_dmsg1;

		ol_del(ol);
	}
	else assert(0);

	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////
//select/poll fds

typedef struct hp_iocp_polld {
	int    id;
	SOCKET fd;
	int lastr, lastR;
#ifndef LIBHP_WITH_WSAPOLL
	int    flags;
#endif
} hp_iocp_polld;

#define hp_iocp_poll_rm_at(pds, nfds, i) do {                               \
	memmove(pds + (i), pds + (i) + 1, sizeof(hp_iocp_polld) * (nfds - (i + 1))); \
}while(0)

static int hp_iocp_polld_find_by_fd(const void * k, const void * e)
{
	assert(e && k);
	SOCKET kfd = *(SOCKET *)k;
	hp_iocp_polld * pd = (hp_iocp_polld *)e;

	return !(pd->fd == kfd);
}

static int hp_iocp_polld_find_by_id2(const void * k, const void * e)
{
	assert(e && k);
	int kid = *(int *)k;
	hp_iocp_polld * pd = (hp_iocp_polld *)e;

	if(kid < pd->id)       return -1;
	else if(kid == pd->id) return 0;
	else                     return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
/*
 * select thread function
 * */
static unsigned WINAPI hp_iocp_select_threadfn(void * arg)
{
#ifndef NDEBUG
	int n_msgadd = 0, n_msgrm = 0;
	hp_log(stdout, "%s: enter thread function, id=%d\n", __FUNCTION__, GetCurrentThreadId());
#endif /* NDEBUG */
	hp_iocp * iocp = (hp_iocp *)arg;
	assert(iocp);

	int rc = 0, i, nfds = 0;
	hp_iocp_polld * pds = calloc(iocp->maxfd, sizeof(hp_iocp_polld));

	struct fd_set rfds_obj, wfds_obj, exceptfds_obj;
	struct fd_set * rfds = &rfds_obj, *wfds = &wfds_obj, * exceptfds = &exceptfds_obj;
	struct timeval timeoutobj = { 0, 0 }, * timeout = &timeoutobj;

	for (;;) {
		/* reset fd_sets for select */
		FD_ZERO(rfds);
		FD_ZERO(wfds);
		FD_ZERO(exceptfds);
		/* try to get fd for select */
		MSG msgobj = { 0 }, *msg = &msgobj;
		for(; PeekMessage((LPMSG)msg, (HWND)-1
				, (UINT)HP_IOCP_WM_FIRST(iocp), (UINT)HP_IOCP_WM_LAST(iocp)
				, PM_REMOVE | PM_NOYIELD); ) {
			if(hp_iocp_is_quit(iocp)) { break; }

			int id = msg->wParam;
			if (msg->message == HP_IOCP_WM_ADD(iocp)) {
				SOCKET fd = msg->lParam;

				hp_iocp_polld * pd = hp_lfind(&fd, pds, nfds, sizeof(hp_iocp_polld), hp_iocp_polld_find_by_fd);
				assert(!pd);


				pds[nfds].id = id;
				pds[nfds].fd = fd;
				pds[nfds].flags = POLLIN;
				pds[nfds].lastr =  pds[nfds].lastR = HP_IOCP_PENDING_BUF;
				++nfds;

//				hp_log(stdout, "%s: HP_IOCP_WM_ADD=%d, left=%d\n", __FUNCTION__, ++n_msgadd, nfds);
			}
			else if (msg->message == HP_IOCP_WM_RM(iocp)) {
				hp_iocp_polld * pd = bsearch(&id, pds, nfds, sizeof(hp_iocp_polld), hp_iocp_polld_find_by_id2);
				assert(pd);

				pd->flags =  msg->lParam;
				if(pd->flags == 0){
					i = pd - pds;
					if(i + 1 < nfds){
						hp_iocp_poll_rm_at(pds, nfds, i);
					}
					--nfds;
//					hp_log(stdout, "%s: HP_IOCP_WM_RM=%d, left=%d\n", __FUNCTION__, ++n_msgrm, nfds);
				}
			} else assert(0);
		}
		if(hp_iocp_is_quit(iocp)) { break; }
		if(nfds == 0) { Sleep(iocp->stime_out); continue; }

		for(i = 0; i < nfds; ++i){
			if(pds[i].flags & POLLIN)
				FD_SET(pds[i].fd, rfds);
			if(pds[i].flags & POLLOUT)
				FD_SET(pds[i].fd, wfds);
			FD_SET(pds[i].fd, exceptfds);
		}

		int n = select(nfds, rfds, wfds, exceptfds, timeout);
		if (n == 0) { Sleep(iocp->stime_out); continue; }
		else if (n == SOCKET_ERROR) {
			/* see https://learn.microsoft.com/zh-cn/windows/win32/api/winsock2/nf-winsock2-wsapoll
			 * for more error codes */
			int err = WSAGetLastError();
			if(hp_log_level > 0){
				hp_err_t errstr = "select: %s";
				hp_log(stderr, "%s: select error, err=%d/'%s'\n", __FUNCTION__, err, hp_err(err, errstr));
			}
			if((err == WSAEINTR || err == WSAEINPROGRESS))
				continue;
			continue; /* stop this time */
		}

		for(i = 0; i < nfds; ++i){

			if(!(FD_ISSET(pds[i].fd, rfds)
				|| (FD_ISSET(pds[i].fd, wfds))
				|| FD_ISSET(pds[i].fd, exceptfds))) continue;

			int flags = 0;
			if(FD_ISSET(pds[i].fd, rfds) && (pds[i].flags & SOLISTEN)) flags |= POLLIN;
			if(FD_ISSET(pds[i].fd, wfds)) { flags |= POLLOUT; pds[i].flags &= ~POLLOUT; }
			if(FD_ISSET(pds[i].fd, exceptfds)) flags = POLLERR;

			if(flags != POLLERR && FD_ISSET(pds[i].fd, rfds) && !(pds[i].flags & SOLISTEN)) {
				int err = 0;

				int lastr = InterlockedExchangeAdd(&pds[i].lastr, 0);
				pds[i].lastR = (lastr == pds[i].lastR? pds[i].lastR * 2 : lastr + 1);

//				hp_log(stdout, "%s: lastR=%dMiB\n", __FUNCTION__, pds[i].lastR / 1024 / 1024);
				rc = hp_iocp_do_read(iocp, pds[i].id, pds[i].fd, pds[i].lastR, &pds[i].lastr, &err);
				if(rc != 0) flags = POLLERR;
			}
			hp_iocp_msg_post(iocp->ctid, iocp->hwnd, HP_IOCP_WM_FD(iocp), pds[i].id, flags);
		}
//		float msec = iocp->stime_out * (1.0 - nfds / (float)iocp->maxfd);
		Sleep(iocp->stime_out);
	}

	free(pds);
#ifndef NDEBUG
	hp_log(stdout, "%s: exit thread function, nfds=%d, HP_IOCP_WM_ADD=%d, HP_IOCP_WM_RM=%d\n",
			__FUNCTION__, nfds, n_msgadd, n_msgrm);
#endif /* NDEBUG */
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////
//poll version

static unsigned WINAPI hp_iocp_poll_threadfn(void * arg)
{
#ifndef NDEBUG
	hp_log(stdout, "%s: enter thread function, id=%d\n", __FUNCTION__, GetCurrentThreadId());
#endif /* NDEBUG */
	hp_iocp * iocp = (hp_iocp *)arg;
	assert(iocp);

	int rc = 0, i, nfds = 0;

	WSAPOLLFD * fds = calloc(iocp->maxfd, sizeof(WSAPOLLFD));
	hp_iocp_polld * pds = calloc(iocp->maxfd, sizeof(hp_iocp_polld));

	for (;;) {
		/* try to get fd for select */
		MSG msgobj = { 0 }, *msg = &msgobj;
		for(; PeekMessage((LPMSG)msg, (HWND)-1
				, (UINT)HP_IOCP_WM_FIRST(iocp), (UINT)HP_IOCP_WM_LAST(iocp)
				, PM_REMOVE | PM_NOYIELD); ) {
			if(hp_iocp_is_quit(iocp)) { break; }

			int id = msg->wParam;
			if (msg->message == HP_IOCP_WM_ADD(iocp)) {
				SOCKET fd = msg->lParam;

				pds[nfds].id = id;
				pds[nfds].fd = fd;

				fds[nfds].fd = fd;
				fds[nfds].events = POLLIN;

				++nfds;
			}
			else{
				hp_iocp_polld * pd = bsearch(&id, pds, nfds, sizeof(hp_iocp_polld), hp_iocp_polld_find_by_id2);
				if(!pd) continue;
				i = pd - pds;

				int flags =  msg->lParam;
				fds[i].events = flags;

				if(fds[i].events == 0){
					if(i + 1 < nfds){
						memmove(fds + (i), fds + (i) + 1, sizeof(WSAPOLLFD) * (nfds - (i + 1)));
						hp_iocp_poll_rm_at(pds, nfds, i);
					}
					--nfds;
				}
			}
		}
		if(hp_iocp_is_quit(iocp)) { break; }
		if(nfds == 0) { Sleep(iocp->stime_out); continue; }

		int n = WSAPoll(fds, nfds, 0);
		if (n == 0) { Sleep(iocp->stime_out); continue; }
		else if (n == SOCKET_ERROR) {
			/* see https://learn.microsoft.com/zh-cn/windows/win32/api/winsock2/nf-winsock2-wsapoll
			 * for more error codes */
			int err = WSAGetLastError();
			if(hp_log_level > 0){
				hp_err_t errstr = "WSAPoll: %s";
				hp_log(stderr, "%s: WSAPoll error, err=%d/'%s'\n", __FUNCTION__, err, hp_err(err, errstr));
			}
			if((err == WSAEINTR || err == WSAEINPROGRESS))
				continue;
			continue; /* stop this time */
		}

		for(i = 0; i < nfds; ++i){
			if(fds[i].revents == 0) continue;
			
			hp_iocp_msg_post(iocp->ctid, iocp->hwnd, HP_IOCP_WM_FD(iocp), pds[i].id, fds[i].revents);
			//POLLERR | POLLHUP | POLLNVAL
			if(fds[i].revents & (POLLERR | POLLHUP)) {
				//fd closed(by user?) or exception
				if(i + 1 < nfds){
					memmove(fds + (i), fds + (i) + 1, sizeof(WSAPOLLFD) * (nfds - (i + 1)));
					hp_iocp_poll_rm_at(pds, nfds, i);
					//i+1 becomes i
					--i;
				}
				--nfds;
			}
		}
	}

	free(fds);
	free(pds);
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
	hp_iocp * iocp = (hp_iocp *)arg;
	assert(iocp);

	BOOL rc;
	DWORD err = 0;
	DWORD iobytes = 0;
	void * key = 0;
	WSAOVERLAPPED * overlapped = 0;

	for (;;) {
		rc = GetQueuedCompletionStatus(iocp->hiocp, &iobytes, (LPDWORD)&key, (LPOVERLAPPED * )&overlapped,
										INFINITE);
		err = WSAGetLastError();

		if (rc || overlapped) { /* OK or quit */
			if (rc && !overlapped) 
				break;  /* PostQueuedCompletionStatus called */
			assert(overlapped);

			hp_iocp_msg_post(iocp->ctid, iocp->hwnd, HP_IOCP_WM_IO(iocp), overlapped, err);
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
#ifndef NDEBUG
	hp_log(stdout, "%s: exit thread function\n", __FUNCTION__);
#endif /* NDEBUG */
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////

int hp_iocp_run(hp_iocp * iocp, int mode)
{
	if(!(iocp))
		return -1;

	int i;
	for (; ;) {
		MSG msgobj = { 0 }, *msg = &msgobj;
		for (; PeekMessage((LPMSG)msg, (HWND)-1
			, (UINT)HP_IOCP_WM_FIRST(iocp), (UINT)HP_IOCP_WM_LAST(iocp)
			, PM_REMOVE | PM_NOYIELD); ) {
			hp_iocp_handle_msg(iocp, msg->message, msg->wParam, msg->lParam);
		}

		for (i = 0; i < vecsize(iocp->items); ++i) {
			hp_iocp_item * item = iocp->items + i;
			if (item->on_loop && item->on_loop(item->arg) < 0){
				if(hp_iocp_item_on_error(iocp, item))
					--i;
			}
		}

		if (mode != 0)
			break;
	}

	return 0;
}

int hp_iocp_init(hp_iocp * iocp, int maxfd, HWND hwnd, int nthreads, int wmuser, int stime_out, void * user)
{
	int i;
	if(!(iocp && maxfd >= 0 && nthreads >= 0))
		return -1;

	if (nthreads > HP_IOCP_TMAX)
		nthreads = HP_IOCP_TMAX;
	if (nthreads == 0) {
		SYSTEM_INFO sysInfo;
		GetSystemInfo(&sysInfo);
		nthreads = (int)sysInfo.dwNumberOfProcessors - 1;
	}
	if(wmuser < WM_USER)
		wmuser = WM_USER + 2021;

	if(maxfd == 0)
		maxfd = 65535;

	memset(iocp, 0, sizeof(hp_iocp));
	iocp->ctid = (int)GetCurrentThreadId();

	if (!(iocp->ctid > 0 || iocp->hwnd))
		return -3;

	iocp->hwnd = hwnd;
	iocp->wmuser = wmuser;
	iocp->n_threads = nthreads;
	iocp->stime_out = stime_out;
	iocp->maxfd = maxfd;
	iocp->user = user;
	vecinit(iocp->items, maxfd);

	/* IOCP and the threads */
	iocp->hiocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, (HANDLE)0, (ULONG_PTR)0, (DWORD)iocp->n_threads);
	assert(iocp->hiocp);

	for (i = 0; i < iocp->n_threads; i++) {
		uintptr_t hdl = _beginthreadex(NULL, 0, hp_iocp_threadfn, (void *)iocp, 0, 0);
		assert(hdl);
		iocp->threads[i] = (HANDLE)hdl;
	}
	{
		unsigned (* poll_fn)(void * arg) =
#ifndef LIBHP_WITH_WSAPOLL
			hp_iocp_select_threadfn;
#else
			hp_iocp_poll_threadfn;
#endif //#ifndef LIBHP_WITH_WSAPOLL
		uintptr_t hdl = _beginthreadex(NULL, 0, poll_fn, (void *)iocp, 0, 0);
		assert(hdl);
		iocp->sthread = (HANDLE)hdl;
		iocp->ptid = GetThreadId(iocp->sthread);
	}

	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////

static int hp_iocp_def_on_accept(hp_iocp * iocp, void * arg)
{
	assert(iocp);
	return -1;
}
static int hp_iocp_def_on_data(hp_iocp * iocp, void * arg, char * buf, size_t * len)
{
	assert(iocp && buf && len);
	return -1;
}

static int hp_iocp_def_on_loop(void * arg) { return -1; }

static int hp_iocp_def_on_error(hp_iocp * iocp, void * arg, int err, char const * errstr)
{
	assert(iocp);
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////

void hp_iocp_uninit(hp_iocp * iocp)
{
	int i, rc;
	if (!iocp) { return; }

	/* stop all working threads */
	for (i = 0; i < iocp->n_threads; i++) {
		PostQueuedCompletionStatus(iocp->hiocp, (DWORD)0, (ULONG_PTR)0, (LPOVERLAPPED)0);
	}

	rc = WaitForMultipleObjects(iocp->n_threads, iocp->threads, TRUE, INFINITE);
	if (rc == WAIT_OBJECT_0) {
		CloseHandle(iocp->hiocp);
	}
	for(i = 0; i < iocp->n_threads; ++i){
		CloseHandle(iocp->threads[i]);
	}
	//reset to defaults to avoid notifications to user
	for (i = 0; i < vecsize(iocp->items); ++i) {
		hp_iocp_item * item = iocp->items + i;
		item->on_accept = item->on_accept ? hp_iocp_def_on_accept : 0;
		item->on_data = item->on_data ? hp_iocp_def_on_data : 0;
		item->on_error = item->on_error ? hp_iocp_def_on_error : 0;
	}
	/* reset thread message queue */
	MSG msgobj = { 0 }, *msg = &msgobj;
	for (; PeekMessage((LPMSG)msg, (HWND)-1
		, (UINT)HP_IOCP_WM_FIRST(iocp), (UINT)HP_IOCP_WM_LAST(iocp)
		, PM_REMOVE | PM_NOYIELD); ) {

		hp_iocp_handle_msg(iocp, msg->message, msg->wParam, msg->lParam);
	}

	iocp->ioid = -1;
	rc = WaitForSingleObject(iocp->sthread, INFINITE);
	if (rc == WAIT_OBJECT_0) {
		CloseHandle(iocp->sthread);
	}

	/* free */
	vecfree(iocp->items);

#ifndef NDEBUG
			hp_log(stdout, "%s: HP_IOCP_WM_FD message drooped, total=%d\n", __FUNCTION__, iocp->n_dmsg0);
			hp_log(stdout, "%s: _____________________n_on_accept=%d\n", __FUNCTION__, iocp->n_on_accept);
			hp_log(stdout, "%s: _____________________n_on_pollout=%d\n", __FUNCTION__, iocp->n_on_pollout);
			hp_log(stdout, "%s: _____________________n_on_i=%d, nbuf=%zu\n", __FUNCTION__, iocp->n_on_i, 0);
			hp_log(stdout, "%s: _____________________n_on_o=%d\n", __FUNCTION__, iocp->n_on_o);
			hp_log(stdout, "%s: HP_IOCP_WM_IO message drooped, total=%d\n", __FUNCTION__, iocp->n_dmsg1);
#endif /* NDEBUG */

	return;
}

/////////////////////////////////////////////////////////////////////////////////////

static int find_by_fd(const void * k, const void * e)
{
	assert(e && k);
	SOCKET kfd = *(SOCKET *)k;
	hp_iocp_item * item = (hp_iocp_item *)e;

	return !(item->fd == kfd);
}

int hp_iocp_add(hp_iocp * iocp
	, int nibuf , SOCKET fd
	, int (* on_accept)(hp_iocp * iocp, void * arg)
	, int (* on_data)(hp_iocp * iocp, void * arg, char * buf, size_t * len)
	, int (* on_error)(hp_iocp * iocp, void * arg, int on_error, char const * errstr)
	, int (* on_loop)(void * arg)
	, void * arg)
{
	int rc;
	if (!(iocp && nibuf >= 0 && fd != INVALID_SOCKET && (on_data || on_accept)))
		return -1;
	if(nibuf == 0)
		nibuf = HP_IOCP_RBUF;

	hp_iocp_item * item = (hp_iocp_item *)hp_lfind(&fd, (iocp)->items, vecsize((iocp)->items)
			, sizeof(hp_iocp_item), find_by_fd);
	if(item){
		assert(item->fd == fd);
		item->on_accept = on_accept;
		item->on_data = on_data;
		item->on_error = on_error;
		item->on_loop = on_loop;
		item->arg = arg;
		if(nibuf > sdslen(item->ibuf))
			item->ibuf = sdsMakeRoomFor(item->ibuf, nibuf - sdslen(item->ibuf));
		return item->id;
	}

	if(vecsize(iocp->items) == veccap(iocp->items))
		return -2;
	if(iocp->ioid == INT_MAX - 1){
		iocp->ioid = 0;
	}

	item = iocp->items + vecsize(iocp->items);
	memset(item, 0, sizeof(hp_iocp_item));

	item->id = iocp->ioid;
	item->fd = fd;
	item->on_accept = on_accept;
	item->on_data = on_data;
	item->on_error = on_error;
	item->on_loop = on_loop;
	item->arg = arg;

	rc = hp_iocp_do_socket(iocp, item);
	if(rc != 0)
		return -3;

	item->ibuf = sdsMakeRoomFor(sdsempty(), nibuf);
	vecinit(item->obuf, 1);
	++iocp->ioid;
	++vecsize(iocp->items);

	return item->id;
}

/////////////////////////////////////////////////////////////////////////////////////

int hp_iocp_rm(hp_iocp * iocp, int id)
{
	if(!(iocp && id >= 0)) return -1;

	hp_iocp_item * item = hp_iocp_item_find(iocp, &id);
	if(!item) return -2;

	//reset to defaults
	item->on_accept = item->on_accept? hp_iocp_def_on_accept : 0;
	item->on_data = item->on_data? hp_iocp_def_on_data : 0;
	item->on_loop = item->on_loop? hp_iocp_def_on_loop : 0;
	item->on_error = item->on_error? hp_iocp_def_on_error : 0;

	return 0;
}

int hp_iocp_size(hp_iocp * iocp)
{
	if(!(iocp))
		return -1;
	int i, n;

	n = 0;
	for(i = 0; i < vecsize(iocp->items); ++i){
		hp_iocp_item * item = iocp->items + i;
		if(!hp_iocp_item_is_removed(item))
			++n;
	}
	return n;
}

/////////////////////////////////////////////////////////////////////////////////////

hp_tuple2_t(hp_dofind_tuple, void * /*key*/, hp_cmp_fn_t /*on_cmp*/);

static int hp_iocp_do_find(const void * k, const void * e)
{
	assert(e && k);
	hp_dofind_tuple tu = *(hp_dofind_tuple *)k;
	hp_iocp_item * item = (hp_iocp_item *)e;
	return !(tu._2? tu._2(tu._1, item->arg) : tu._1 == item->arg);
}

void * hp_iocp_find(hp_iocp * iocp, void * key, int (* on_cmp)(const void *key, const void *ptr))
{
	if(!(iocp))
		return 0;
	hp_iocp_item * item = 0;
	if(on_cmp){
		hp_dofind_tuple k = { ._1 = key, ._2 = on_cmp };
		item = (hp_iocp_item *)hp_lfind(&k, iocp->items, vecsize(iocp->items)
				, sizeof(hp_iocp_item), hp_iocp_do_find);
	}
	else if(key){
		item = hp_iocp_item_find(iocp, (int *)key);
	}
	return item && !hp_iocp_item_is_removed(item)? item->arg : 0;
}

/////////////////////////////////////////////////////////////////////////////////////

int hp_iocp_write(hp_iocp * iocp, int id, void * data, size_t ndata
	, hp_free_t freecb, void * ptr)
{
	int rc = 0;
	hp_iocp_item * item;
	hp_iocp_ol * ol;

	if(!(iocp && data && ndata > 0)){
		rc = -1;
		goto ret;
	}
	item = hp_iocp_item_find(iocp, &id);
	if (!item || hp_iocp_item_is_removed(item)) {
		rc = -2;
		goto ret;
	}
	assert(item->obuf);

	ol = ol_newo(id, data, ndata, freecb, ptr);
	vecpush(item->obuf, ol);

	hp_iocp_msg_post(iocp->ptid, (HWND)0, HP_IOCP_WM_RM(iocp), item->id, POLLIN | POLLOUT);
ret:
	if (rc != 0 && freecb) { freecb(ptr ? ptr : data); }
	return rc;
}

/////////////////////////////////////////////////////////////////////////////////////
/* tests */
#ifndef NDEBUG

#include <time.h>			/* difftime */
#include "http-parser/http_parser.h"
#include "hp/hp_net.h" //hp_tcp_connect
#include "hp/hp_assert.h" //hp_assert
#include "hp/str_dump.h"   /* dumpstr */
#include "hp/string_util.h"
#include "gbk-utf8/utf8.h"
#include "hp/hp_config.h"	//hp_config_test
#include "hp/hp_log.h"	//hp_log
#include "hp/hp_stdlib.h"	//hp_free_t
#define cfg hp_config_test
#define cfgi(key) atoi(hp_config_test(key))

/////////////////////////////////////////////////////////////////////////////////////
static int simple_tests()
{
	int rc;
	{
		sds s = sdsnewlen(0, 10);
		assert(sdslen(s) == 10);
		sdsIncrLen(s, -10);
		assert(sdslen(s) == 0);
		sdsfree(s);
	}
	{
		int n = 0;
		hp_iocp_ol * ol = ol_newi(0, 10, &n);
		assert(ol_read(ol) && ol->buf.buf && ol->buf.len == 10);
		ol_del(ol);
	}
	{
		hp_iocp_ol * ol = ol_newo(0/*id*/, sdsnew("hello"), strlen("hello"), (hp_free_t)sdsfree, 0/*ptr*/);
		assert(!ol_read(ol) && ol->buf.buf && ol->buf.len == strlen("hello"));
		ol_del(ol);
	}
	{
		hp_iocp iocpobj = { 0 }, *iocp = &iocpobj;
		int id;
		rc = hp_iocp_init(iocp, 0/*maxfd*/, 0/*hwnd*/, 2/*nthreads*/, 0/*WM_USER*/, 200/*timeout*/, 0/*arg*/);
		assert(rc == 0);
		id = hp_iocp_add(iocp, 0/*nibuf*/, 0/*fd*/,
						0/*on_accept*/, 0/*on_data*/, 0/*on_error*/, 0/*on_loop*/, 0/*arg*/);
		assert(id < 0);
		hp_iocp_uninit(iocp);
	}
	/* hp_iocp basics */
	{
		hp_iocp iocpobj = { 0 }, *iocp = &iocpobj;
		rc = hp_iocp_init(iocp, 0/*maxfd*/, 0/*hwnd*/, 2/*nthreads*/, 0/*WM_USER*/, 200/*timeout*/, 0/*arg*/);
		assert(rc == 0);
		assert(hp_iocp_size(iocp) == 0);
		hp_iocp_uninit(iocp);
	}

	return rc;
}

/////////////////////////////////////////////////////////////////////////////////////

typedef struct server {
	int test;
	hp_sock_t listen_fd;
} server;

// for simplicity, we use the same "client" struct for both server and client side code
typedef struct client {
	int id;
	hp_sock_t fd;
	sds in;
	sds out;
	int wi;
	server * s;
	char addr[64];
	int test;
} client;

//write the last char only
#define writelchr(iocp, id, out, i) do { \
		assert(hp_iocp_write(iocp, id, (out) + i, 1, 0/*free*/, 0) == 0); \
		--(i);                                                            \
	} while(0)

static int test4_total = 100 * 1024 * 1024;


static int s_on_error(hp_iocp * iocp, void * arg, int err, char const * errstr)
{
	assert(iocp && arg);
	client * req = (client *)arg;
	assert(req->s);
	closesocket(req->fd);

	hp_log(stdout, "%s: delete TCP connection '%s', %d/'%s', IO total=%d\n", __FUNCTION__
			, req->addr, err, errstr, hp_iocp_size(iocp));
	return 0;
}

int s_on_data(hp_iocp * iocp, void * arg, char * buf, size_t * len)
{
	assert(iocp && arg && buf && len);
	client * c = (client *)arg;
	assert(c->s);

	int rc;

	if(c->test == 1){
		c->in = sdscatlen(c->in, buf, *len);
		*len = 0;
		if(strncmp(c->in, "hello", strlen("hello")) == 0){
			rc = hp_iocp_write(iocp, c->id, sdsnew("world"), strlen("world"), (hp_free_t)(sdsfree), 0);
			assert(rc == 0);
			c->test = 0;
		}
	}
	if(c->test == 2){
		c->in = sdscatlen(c->in, buf, *len);
		*len = 0;
		if(strncmp(c->in, "hello", strlen("hello")) == 0){
			rc = hp_iocp_write(iocp, c->id, sdsnew("world"), strlen("world"), (hp_free_t)(sdsfree), 0);
			assert(rc == 0);
			shutdown(c->fd, SD_RECEIVE); //stop receiving
			c->test = 0;
		}

	}
	if(c->test == 3){
		if(c->wi >= 0){
//			hp_log(stdout, "%s: I/O='%s','%c'\n", __FUNCTION__, dumpstr(buf, *len, *len), c->out[c->wi]);
			writelchr(iocp, c->id, c->out, c->wi);
		}

		if(*len < strlen("hello")){
			return 0; //长度不不够,暂不处理
		}

		c->in = sdscatlen(c->in, buf, *len);
		*len = 0;
		if(strncmp(c->in, "hello", strlen("hello")) == 0){
			c->test = 0;
		}
	}
	if(c->test == 4){
		static size_t tlen = 0;
		tlen += *len;
		*len = 0;

		static time_t t = 0;
		if(difftime(time(0), t) > 0){
			int p = (int)(tlen * 100 / (float)test4_total);
			t = time(0);
			hp_log(stdout, "%s: %d, received: %02d%%/%dMiB\n", __FUNCTION__, c->id, p, tlen / 1024 / 1024);
		}
		if(tlen == test4_total) {
			c->test = 0;
			shutdown(c->fd, SD_BOTH);
			return -1;
		}
	}

	return 0;
}

static int s_on_accept(hp_iocp * iocp, void * arg)
{
	assert(iocp && arg);
	server * s = (server * )arg;
	int rc;

	for(;;){
		struct sockaddr_in addr;
		int len = (int)sizeof(addr);
		hp_sock_t confd = accept(s->listen_fd, (struct sockaddr *)&addr, &len);
		if(confd == INVALID_SOCKET){
			int err = WSAGetLastError();
			if(WSAEWOULDBLOCK == err) { return 0; }
			hp_err_t errstr = "accept: %s";
			hp_log(stderr, "%s: accept failed, errno=%d, error='%s'\n", __FUNCTION__, err, hp_err(err, errstr));
			return -1;
		}
		u_long sockopt = 1;
		if(ioctlsocket(confd, FIONBIO, &sockopt) < 0)
			{ hp_close(confd); continue; }

		client * req = calloc(1, sizeof(client));
		req->s = s;
		req->in = sdsempty();
		req->test = s->test;
		if(req->test == 3){
			req->out = sdsnew("dlrow");
			req->wi = sdslen(req->out) - 1;
		}

		req->id = hp_iocp_add(iocp, 0/*nibuf*/, confd, 0/*on_accept*/, s_on_data , s_on_error, 0/*on_loop*/, req);
		assert(req->id >= 0);

		hp_log(stdout, "%s: new TCP connection from '%s', IO total=%d\n", __FUNCTION__
				, hp_addr4name(&addr, ":", req->addr, sizeof(req->addr)), hp_iocp_size(iocp));
	}

	return 0;
}


int c_on_data(hp_iocp * iocp, void * arg, char * buf, size_t * len)
{
	assert(iocp && arg && buf && len);
	client * c = (client *)arg;
	assert(!c->s);
	int rc;


	if(c->test == 1){
		c->in = sdscatlen(c->in, buf, *len);
		*len = 0;
		if(strncmp(c->in, "world", strlen("world")) == 0){
			hp_log(stdout, "%s: client %d test done!\n", __FUNCTION__, c->id);
			// client done
			shutdown(c->fd, SD_BOTH);
			c->test = 0;
			return -1;
		}
	}
	if(c->test == 2) {
		c->in = sdscatlen(c->in, buf, *len);
		*len = 0;
		if(strncmp(c->in, "world", strlen("world")) == 0){
			//try write sth again, but peer side shutdown(write)
			shutdown(c->fd, SD_SEND);
			rc = hp_iocp_write(iocp, c->id, sdsnew("hello2"), strlen("hello2"), (hp_free_t)(sdsfree), 0);
			assert(rc == 0);
		}
	}
	if(c->test == 3) {
		if(c->wi >= 0){
//			hp_log(stdout, "%s: I/O='%s','%c'\n", __FUNCTION__, dumpstr(buf, *len, *len), c->out[c->wi]);
			writelchr(iocp, c->id, c->out, c->wi);
		}
		if(*len < strlen("world")){
			return 0; //长度不不够,暂不处理
		}
		c->in = sdscatlen(c->in, buf, *len);
		*len = 0;
		if(strncmp(c->in, "world", strlen("world")) == 0){
			shutdown(c->fd, SD_BOTH);
			c->test = 0;
			return -1;
		}
	}
	return 0;
}

static int c_on_error(hp_iocp * iocp, void * arg, int err, char const * errstr)
{
	assert(iocp && arg);
	client * c = (client *)arg;
	assert(!c->s);
	closesocket(c->fd);

	if(c->test == 2)
		c->test = 0; //done

	hp_log(stdout, "%s: %d disconnected err=%d/'%s'\n", __FUNCTION__, c->id, err, errstr);

	return 0;
}

static int checkiftestrunning(const void *key, const void *ptr)
{
	assert(ptr && key);
	//ignore listen fd
	if(ptr == key)
		return 0;
	client * c = (client *)ptr;
	return (c->test != 0);
}

/////////////////////////////////////////////////////////////////////////////////////

static int search_test_on_cmp(const void *k, const void *e)
{
	assert(e && k);
	int kid = *(int *)k;
	int id = *(int *)e;

	return (kid == id);
}
static void search_test(int n)
{
	int rc, i;
	hp_sock_t fd[1024];
	int id[1024] = { 0 };
	hp_iocp iocpobj = { 0 }, * iocp = &iocpobj;

	rc = hp_iocp_init(iocp, n/*maxfd*/, 0/*hwnd*/, 2/*nthreads*/, 0/*WM_USER*/,
						200/*timeout*/, 0/*arg*/);
	assert(rc == 0);
	assert(hp_iocp_size(iocp) == 0);

	for(i = 0; i < n; ++i){
		hp_sock_t confd = hp_tcp_connect("127.0.0.1", 1);
		hp_assert(hp_sock_is_valid(confd), "i=%i", i);
		fd[i] = confd;

		id[i] = hp_iocp_add(iocp, 0/*nibuf*/, fd[i]/*fd*/ ,
						0/*on_accept*/, c_on_data/*on_data*/, 0/*on_error*/, 0/*on_loop*/, id + i/*arg*/);
		assert(id[i] >= 0);

		assert(hp_iocp_size(iocp) == i + 1);

		assert(hp_iocp_find(iocp, id + i, search_test_on_cmp));

		rc = hp_iocp_rm(iocp, id[i]);
		assert(rc == 0);

		assert(!hp_iocp_find(iocp, id + i, search_test_on_cmp));

		id[i] = hp_iocp_add(iocp, 0/*nibuf*/, fd[i]/*fd*/ ,
					0/*on_accept*/ , c_on_data/*on_data*/, 0/*on_error*/, 0/*on_loop*/, id + i/*arg*/);
		assert(id[i] >= 0);
	}

	for(i = 0; i < n; ++i){
		rc = hp_iocp_rm(iocp, id[i]);
		assert(rc == 0);
	}
	assert(hp_iocp_size(iocp) == 0);
	hp_iocp_uninit(iocp);

	for(i = 0; i < n; ++i){
		hp_close(fd[i]);
	}
}

/////////////////////////////////////////////////////////////////////////////////////

static int add_remove_test(int n)
{
	int rc = 0, i, id;
	hp_sock_t fd[1024];

	hp_sock_t listenfd = hp_tcp_listen(cfgi("test_hp_iocp_main.port"));
	assert(hp_sock_is_valid(listenfd));

	hp_iocp iocpobj = { 0 }, *iocp = &iocpobj;
	rc = hp_iocp_init(iocp, n + 1/*maxfd*/, 0/*hwnd*/, 2/*nthreads*/, 0/*WM_USER*/, 200/*timeout*/, 0/*arg*/);
	assert(rc == 0);
	// add listen fd
	for (i = 0; i < 2; ++i) {
		id = hp_iocp_add(iocp, 0/*nibuf*/, listenfd/*fd*/ ,
						s_on_accept , 0/*on_data*/, 0/*on_error*/, 0/*on_loop*/, 0/*arg*/);
		assert(id >= 0);
	}
	assert(hp_iocp_size(iocp) == 1);

	// add connect fd
	for (i = 0; i < n; ++i) {

		hp_sock_t confd = hp_tcp_connect(cfg("test_hp_iocp_main.ip"), cfgi("test_hp_iocp_main.port") + 1);
		assert(hp_sock_is_valid(confd));
		fd[i] = confd;

		id = hp_iocp_add(iocp, 0/*nibuf*/, fd[i], 0/*on_accept*/, c_on_data , c_on_error, 0/*on_loop*/, 0/*arg*/);
		assert(id >= 0);
		//add same fd
		int id2 = hp_iocp_add(iocp, 0/*nibuf*/, fd[i], 0/*on_accept*/, c_on_data, c_on_error, 0/*on_loop*/, 0/*arg*/);
		assert(id == id2);
	}
	assert(hp_iocp_size(iocp) == i + 1);
	{
		hp_sock_t confd = hp_tcp_connect("127.0.0.1", 1);
		hp_assert(hp_sock_is_valid(confd), "i=%i", i);

		id = hp_iocp_add(iocp, 0/*nibuf*/, fd[i], 0/*on_accept*/, c_on_data , c_on_error, 0/*on_loop*/, 0/*arg*/);
		assert(id < 0);
		hp_close(confd);
	}

	hp_iocp_uninit(iocp);
	for (i = 0; i < n; ++i) {
		hp_close(fd[i]);
	}

	hp_close(listenfd);

	return rc;
}

/////////////////////////////////////////////////////////////////////////////////////

static int client_server_echo_test(int test, int n)
{
	int rc, i;

	server sobj = { 0 }, * s = &sobj;
	hp_iocp iocpctxbj, * iocp = &iocpctxbj;

	client * c = (client *)calloc(70000, sizeof(client));
	assert(c);

	hp_sock_t listen_fd = hp_tcp_listen(cfgi("tcp.port"));
	assert(hp_sock_is_valid(listen_fd));

	s->test = test;
	s->listen_fd = listen_fd;

	rc = hp_iocp_init(iocp, 2 * n + 1/*maxfd*/, 0/*hwnd*/, 2/*nthreads*/, 0/*WM_USER*/,
			100/*timeout*/, s/*arg*/);
	assert(rc == 0);

	/* add listen socket */
	int id = hp_iocp_add(iocp, 0/*nibuf*/ , listen_fd/*fd*/ ,
					s_on_accept, 0/*on_data*/, s_on_error, 0/*on_loop*/, s/*arg*/);
	assert(id >= 0);

	/* add connect socket */
	for(i = 0; i < n; ++i){

		c[i].in = sdsempty();
		c[i].test = test;

		hp_sock_t confd = hp_tcp_connect("127.0.0.1", cfgi("tcp.port"));
		assert(hp_sock_is_valid(confd));

		c[i].fd = confd;

		c[i].id = hp_iocp_add(iocp, 0/*nibuf*/ , confd/*fd*/ , 0/*on_accept*/, c_on_data, c_on_error, 0/*on_loop*/, c + i/*arg*/);
		assert(c[i].id >= 0);

		if(test == 1 || test == 2){
			rc = hp_iocp_write(iocp, c[i].id, sdsnew("hello"), strlen("hello"), (hp_free_t)(sdsfree), 0);
			assert(rc == 0);
		}
		if(test == 3){
			c[i].out = sdsnew("olleh");
			c[i].wi = sdslen(c[i].out) - 1;
			writelchr(iocp, c[i].id, c[i].out, c[i].wi);
		}
		if(test == 4){
			char * p = malloc(test4_total);
			assert(p);
			rc = hp_iocp_write(iocp, c[i].id, p, test4_total, free, 0);
			assert(rc == 0);
		}
	}
	hp_log(stdout, "%s: listening on TCP port=%d, test=%d, n=%d, waiting for connection ...\n",
			__FUNCTION__, cfgi("tcp.port"), test, n);
	/* run event loop, 1 for listenio  */
	int s_tdone = 0;
	for (;; ) {
		hp_iocp_run(iocp, 1/*mode*/);

		if(s_tdone && hp_iocp_size(iocp) <= 1) break;
		if(s_tdone == 0){

			client * fc = (client *)hp_iocp_find(iocp, s, checkiftestrunning);
			if(!fc)
				s_tdone = 1;
		}
	}

	hp_iocp_uninit(iocp);

	/*clear*/
	for(i = 0; c[i].in; ++i){
		sdsfree(c[i].in);
		if(c[i].out) sdsfree(c[i].out);
	}
	hp_close(listen_fd);
	free(c);

	return rc;
}

/////////////////////////////////////////////////////////////////////////////////////

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

static int http_get_file__on_error(hp_iocp * iocp, void * arg, int err, char const * errstr)
{
	assert(iocp && iocp->user);
	struct http_get_file * fctx = (struct http_get_file *)iocp->user;

	hp_log(stdout, "%s: disconnected, err=%d/'%s'\n", __FUNCTION__
		, err, errstr);
	if (fctx->s_hfile) {
		CloseHandle(fctx->s_hfile);
		fctx->s_hfile = 0;
	}
	fctx->s_quit = 1;

	return 0;
}

int http_get_file__on_data(hp_iocp * iocp, void * arg, char * buf, size_t * len)
{
	assert(buf && len);
	assert(iocp && iocp->user);
	struct http_get_file * fctx = (struct http_get_file *)iocp->user;

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
		//hp_close(s);
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

static int get_http_file()
{
	int rc;
	sds url = sdsnew(cfg("http://127.0.1:7006/")); assert(sdslen(url) > 0);

	/* parse URL */
	struct http_parser_url uobj, *u = &uobj;
	if (http_parser_parse_url(url, sdslen(url), 0, u) != 0)
		return -1;

	url_field(u, url, UF_PATH, s_url);
	url_field(u, url, UF_HOST, server_ip);
	server_port = u->port > 0 ? u->port : 80;

	if (!(strlen(s_url) > 0 && strlen(server_ip) > 0))
		return -2;

	hp_sock_t confd = hp_tcp_connect(server_ip, server_port);
	assert(hp_sock_is_valid(confd));

	struct http_get_file s_httpgetfileobj = { 0 }, *fctx = &s_httpgetfileobj;
	http_parser_init(&fctx->parser, HTTP_RESPONSE);
	fctx->parser.data = fctx;

	/* the IOCP context */
	hp_iocp iocpobj = { 0 }, *iocp = &iocpobj;
	rc = hp_iocp_init(iocp, 2/*maxfd*/, 0/*hwnd*/, 2/*nthreads*/, 0/*WM_USER*/, 200/*timeout*/, fctx/*arg*/);
	assert(rc == 0);

	/* prepare for I/O */
	int id = hp_iocp_add(iocp, 0/*nibuf*/ , confd, 0 /*on_accept*/
		, http_get_file__on_data , http_get_file__on_error, 0/*on_loop*/, 0/*arg*/);
	assert(id >= 0);

	sds data = sdscatprintf(sdsempty(), "GET %s HTTP/1.1\r\nHost: %s:%d\r\n\r\n"
				, s_url, server_ip, server_port);

	rc = hp_iocp_write(iocp, id, data, strlen(data), 0, 0);
	assert(rc == 0);

	hp_log(stdout, "%s: %s\n", __FUNCTION__, dumpstr(data, sdslen(data), sdslen(data)));

	for (rc = 0; !fctx->s_quit;) {
		hp_iocp_run(iocp, 1/*mode*/);
	}
	hp_iocp_uninit(iocp);
	sdsfree(data);

	sdsfree(fctx->fname);
	sdsfree(url);

	return 0;
}
/////////////////////////////////////////////////////////////////////////////////////

int test_hp_iocp_main(int argc, char ** argv)
{
	int rc;
//	{
//		hp_log(stdout, "%s: POLLIN=%d,POLLOUT=%d,POLLERR=%d,POLLHUP=%d,POLLNVAL=%d"
//				",(POLLOUT | POLLHUP)=%d,INT_MAX=%d, %d/%d, %d/%d,SSIZE_MAX=%I64d,FD_SETSIZE=%d\n", __FUNCTION__,
//			POLLIN, POLLOUT, POLLERR, POLLHUP
//			, POLLNVAL, (POLLOUT | POLLHUP),INT_MAX, sizeof(time_t), sizeof(int)
//			, sizeof(WPARAM), sizeof(LPARAM), SSIZE_MAX
//			, FD_SETSIZE);
//	}
//	{
//		int flags = SOLISTEN | POLLIN | POLLOUT | POLLERR;
//		assert((flags & SOLISTEN) && (flags & POLLIN) && (flags & POLLOUT) && (flags & POLLERR));
//		flags &= ~POLLIN;
//		assert(!(flags & POLLIN));
//		flags &= ~SOLISTEN;
//		assert(!(flags & SOLISTEN));
//	}
//	/* simple test */
//	simple_tests();
//	//add,remove test
//	{
//		rc = add_remove_test(1); assert(rc == 0);
//		rc = add_remove_test(2); assert(rc == 0);
//		rc = add_remove_test(3); assert(rc == 0);
//		rc = add_remove_test(500); assert(rc == 0);
//	}
//	// search test
//	{
//		search_test(1);
//		search_test(2);
//		search_test(3);
//		search_test(500);
//	}	/*
//	 * simple echo server, client sent "hello", server reply with "world"
//	 *  */
//	{
//		rc = client_server_echo_test(1, 1); assert(rc == 0);
//		rc = client_server_echo_test(1, 2); assert(rc == 0);
//		rc = client_server_echo_test(1, 3); assert(rc == 0);
//		rc = client_server_echo_test(1, 500); assert(rc == 0);
//	}
//	/* write error: server shutdown(write), client should detect it
//	 * 关闭socket读部分,对端产生写错误
//	 */
//	{
//		rc = client_server_echo_test(2, 1); assert(rc == 0);
//		rc = client_server_echo_test(2, 2); assert(rc == 0);
//		rc = client_server_echo_test(2, 3); assert(rc == 0);
//		rc = client_server_echo_test(2, 500); assert(rc == 0);
//	}
//	//收到的数据未完全"消费"或解析,拼接接下来读到的数据
//	{
//		rc = client_server_echo_test(3, 1); assert(rc == 0);
//		rc = client_server_echo_test(3, 2); assert(rc == 0);
//		rc = client_server_echo_test(3, 3); assert(rc == 0);
//		rc = client_server_echo_test(3, 10); assert(rc == 0);
//		rc = client_server_echo_test(3, 500); assert(rc == 0);
//	}
	//仅部分写完成,再次提交
	{
		rc = client_server_echo_test(4, 1); assert(rc == 0);
		rc = client_server_echo_test(4, 2); assert(rc == 0);
		rc = client_server_echo_test(4, 3); assert(rc == 0);
		rc = client_server_echo_test(4, 500); assert(rc == 0);
	}
	//
	/////////////////////////////////////////////////////////////////////////////////////
	// test: HTTP client get HTTP file
//	get_http_file();
	/////////////////////////////////////////////////////////////////////////////////////


	return 0;
}
#endif /* NDEBUG */

#endif /* _MSC_VER */

