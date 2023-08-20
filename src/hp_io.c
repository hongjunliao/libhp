/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/5/25
 *
 * read or write fd until 'again' or error, using iovec if available

 * */
/////////////////////////////////////////////////////////////////////////////////////

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef HAVE_UNISTD_H
#include "hp/hp_io.h"
#include "hp/hp_log.h"     /* hp_log */
#include "hp/hp_libc.h"    /* hp_min */
#include <unistd.h>     /* read, sysconf, ... */
#include <stdio.h>
#include <stddef.h>		//size_t
#include <string.h>     /* memset, ... */
#include <errno.h>      /* errno */
#include <assert.h>     /* define NDEBUG to disable assertion */
#include <stdlib.h>
#include <limits.h>		/* IOV_MAX */

#ifndef IOV_MAX
#define IOV_MAX 1024
#endif
 
/////////////////////////////////////////////////////////////////////////////////////
#ifndef HAVE_SYS_UIO_H
typedef struct iovec
  {
    void *iov_base;	/* Pointer to data.  */
    size_t iov_len;	/* Length of data.  */
  } iovec;
#endif //#ifndef HAVE_SYS_UIO_H

typedef struct hp_uio_item {
	void *        ptr;    /* ptr to free */
	hp_free_t     free;   /* for free ptr */
	iovec  vec;    /* will change while writing */
} hp_uio_item;
/////////////////////////////////////////////////////////////////////////////////////

#ifdef HAVE_SYS_UIO_H
static size_t iovec_total_bytes(iovec * vec, int count)
{
	size_t bytes = 0;
	int i = 0;
	for(; i < count; ++i)
		bytes += vec[i].iov_len;

	return bytes;
}

/* NOTE: will change @param vec if needed */
static void before_writev(iovec * vec, int count,
		size_t nwrite, int * vec_n)
{
	size_t r = 0;
	int i = *vec_n;
	for(; i != count; ++i){
		r += vec[i].iov_len;
		if(nwrite < r){
			size_t left = r - nwrite;
			vec[i].iov_base = (char *)(vec[i].iov_base) + vec[i].iov_len - left;
			vec[i].iov_len = left;

			*vec_n = i;
			break;
		}
	}
}

ssize_t hp_uio_write(int fd, int * err, iovec * vec, int count, int * n, size_t bytes)
{
	if(!(err && vec && n)) return -1;

	ssize_t nwrite  = 0;
	*n = 0;

	for(;;){
		ssize_t w = writev(fd, vec + *n, count - *n);
		if ( w <= 0) {
			if (w < 0 && (errno == EINTR || errno == EAGAIN))
				*err = EAGAIN;
			else{
				if(!((errno == EINTR || errno == EAGAIN)))
					*err = errno;
				else *err = EIO;
			}
			break;
		}

		nwrite += w;
		if(nwrite == bytes){
			*n = count;
			*err = 0;
			break;
		}

		before_writev(vec, count, w, n);
	}

	return nwrite;
}

#endif //#ifdef HAVE_SYS_UIO_H

int hp_wr_init(struct hp_wr * wr, int n,
		void (* write_error)(struct hp_wr * wr, int err, void * arg))
{
	if(!wr)
		return -1;
	memset(wr, 0, sizeof(struct hp_wr));
	if(n <= 0)
		n = 8;

	wr->O_ITEMS_INIT_LEN = wr->O_ITEMS_LEN = n;
	wr->o_items = ( hp_uio_item *)malloc(wr->O_ITEMS_LEN * sizeof( hp_uio_item));
	wr->write_error = write_error;

	return 0;
}

void hp_wr_uninit(struct hp_wr * wr)
{
	if(!wr)
		return;
	hp_wr_clear(wr);
	free(wr->o_items);
	wr->o_items = 0;
}

int hp_rd_init(struct hp_rd * rd, int bufrlen,
		int (* data)(char * buf, size_t * len, void * arg),
		void (* read_error)(struct hp_rd * rd, int err, void * arg))
{
	if(!rd)
		return -1;
	memset(rd, 0, sizeof(struct hp_rd));

	if(bufrlen <= 0)
		bufrlen = 1024 * 8;
	rd->I_BUF_MAX = bufrlen;
	rd->data = data;
	rd->read_error = read_error;

	return 0;
}

void hp_rd_uninit(struct hp_rd * rd)
{
	if(!rd)
		return;

	free(rd->i_buf);
}

static void hp_eto_reserve(struct hp_wr * wr, int n)
{
	wr->O_ITEMS_LEN = n;
	wr->o_items = ( hp_uio_item *)realloc(wr->o_items, wr->O_ITEMS_LEN * sizeof( hp_uio_item));
}

int hp_wr_add(struct hp_wr * wr, void * iov_base, size_t iov_len, hp_free_t freecb, void * ptr)
{
	if(!(wr && iov_base))
		return -1;

	if(wr->O_ITEMS_LEN - wr->o_items_len <= 3){
		if(hp_log_level > 8)
			hp_log(stdout, "%s: hp_eto::o_items_len reached max, MAX=%d, realloc\n"
				, __FUNCTION__, wr->O_ITEMS_LEN);

		/* @see https://www.zhihu.com/question/36538542 */
		hp_eto_reserve(wr, wr->O_ITEMS_LEN * 2);
	}
	else if((wr->O_ITEMS_LEN > wr->O_ITEMS_INIT_LEN) &&
			(wr->o_items_len + 2 < wr->O_ITEMS_LEN / 2))
		hp_eto_reserve(wr, wr->O_ITEMS_LEN / 2);

	 hp_uio_item * item = wr->o_items + wr->o_items_len;

	item->ptr = ptr;
	if(!item->ptr)
		item->ptr = iov_base;

	item->free = freecb;

	if(freecb == (void *)-1){
		item->ptr = item->vec.iov_base = malloc(iov_len);
		item->vec.iov_len = iov_len;
		memcpy(item->vec.iov_base, iov_base, iov_len);
		item->free = free;
	}
	else{
		item->vec.iov_base = iov_base;
		item->vec.iov_len = iov_len;
	}

	++wr->o_items_len;

	return 0;
}

static void hp_etio_del_n(struct hp_wr *etio, int n)
{
	int i;
	for(i = 0; i < n; ++i){
		 hp_uio_item * item = etio->o_items + i;
		if(item->free && item->ptr){
			item->free(item->ptr);
		}
	}
	memmove(etio->o_items, etio->o_items + n, sizeof( hp_uio_item) * (etio->o_items_len - n));
	etio->o_items_len -= n;
}

int hp_wr_bytes(hp_wr * wr)
{
	if(!wr)
		return 0;
	int i;

	int n = 0;
	for(i = 0; i < wr->o_items_len; ++i){
		 hp_uio_item * item = wr->o_items + i;
		n += item->vec.iov_len;
	}

	return n;
}

void hp_wr_clear(struct hp_wr * wr)
{
	return hp_etio_del_n(wr, wr->o_items_len);
}

#ifndef HAVE_SYS_UIO_H
static ssize_t hp_wr_do_write(int fd, int * err, iovec * vec)
{
	if(!(err && vec)) return -1;

	ssize_t nwrite  = 0;

	for(;;){
		ssize_t w = write(fd, vec->iov_base, vec->iov_len);
		if ( w <= 0) {
			if (w < 0 && (errno == EINTR || errno == EAGAIN))
				*err = EAGAIN;
			else{
				if(!((errno == EINTR || errno == EAGAIN)))
					*err = errno;
				else *err = EIO;
			}
			break;
		}

		nwrite += w;
		if(w == vec->iov_len){
			*err = 0;
			break;
		}
		else{
			vec->iov_base += w;
			vec->iov_len -= w;
		}
	}

	return nwrite;
}
#endif //#ifndef HAVE_SYS_UIO_H

size_t hp_wr_write(struct hp_wr * wr, int fd, void * arg)
{
	if(!wr)
		return 0;
	if(wr->o_items_len == 0)
		return 0;

	int iov_max = sysconf(_SC_IOV_MAX);
	if(iov_max <= 0)
		iov_max = IOV_MAX;

	size_t o_bytes = 0; /* out bytes this time */
	for(;;){
		/* all write done, return */
		if(wr->o_items_len == 0){
			return o_bytes;
		}

		int i;
		int n = 0, err = 0;
		int vec_len = hp_min(iov_max, wr->o_items_len);
#ifdef HAVE_SYS_UIO_H
		iovec * vec = calloc(vec_len, sizeof(iovec));

		/* prepare data for iovec */
		for(i = 0; i < vec_len; ++i){
			 hp_uio_item * item = wr->o_items + i;
			vec[i] = item->vec;
		}
		size_t W = iovec_total_bytes(vec, vec_len);  /* bytes to write this loop */
		if(W == 0){
			free(vec);
			hp_etio_del_n(wr, vec_len);
			continue;
		}

		/* now time to write */
		ssize_t w = hp_uio_write(fd, &err, vec, vec_len, &n, W);
		wr->o_bytes += w;
		o_bytes += w;

		/* adjust the last one for next write */
		if(n < vec_len)
			wr->o_items[n].vec = vec[n];

		/* free iovec */
		free(vec);

#else //normal write
		for(i = 0; i < vec_len; ++i){
			err = 0;
			iovec vec = wr->o_items[i].vec;
			ssize_t w = hp_wr_do_write(fd, &err, &vec);

			wr->o_bytes += w;
			o_bytes += w;

			if(w < wr->o_items[i].vec.iov_len){
				wr->o_items[i].vec = vec;
				break;
			}
			else ++n;
		}
#endif //#ifdef HAVE_SYS_UIO_H

		/* clear these written ones: [0,n) */
		hp_etio_del_n(wr, n);

		/* now check the write result  */
		if(err == 0){
			/* all written this time!  go to next write loop */
			continue;
		}
		else if(err == EAGAIN){
			/* resources NOT available this moment,
			 * call this function later */
			break;
		}
		else {
#ifndef NDEBUG
			if(hp_log_level > 0)
				hp_log(stderr, "%s: writev ERROR, fd=%d, return=%d, errno=%d/'%s'\n", __FUNCTION__
					, fd, err, errno, strerror(errno));
#endif /* NDEBUG */
			/* write error occurred, the fd expect to be reset */
			if(wr->write_error)
				wr->write_error(wr, err, arg);
			wr->err = err;
			break;
		}
	} /* end of write loop */

	return o_bytes;
}

size_t hp_rd_read(struct hp_rd * rd, int fd, void * arg)
{
	int rc;
	if(!(rd && rd->data))
		return 0;

	if(!rd->i_buf){
		rd->i_buf = (char * )malloc(rd->I_BUF_MAX);
		assert(rd->i_buf);
	}

	size_t i_bytes = 0; /* in bytes this time */
	for(;;){
		if(rd->i_buflen > 0){
			rc = rd->data(rd->i_buf, &rd->i_buflen, arg);
			if(rc < 0){
				rd->err = rc;
				break;
			}
		}

		if(!(rd->i_buflen < rd->I_BUF_MAX)){
			rd->I_BUF_MAX *= 2;
			rd->i_buf = realloc(rd->i_buf, rd->I_BUF_MAX);

			if(hp_log_level > 0)
				hp_log(stdout, "%s: realloc to %d\n", __FUNCTION__, rd->I_BUF_MAX);
		}

		ssize_t n = read(fd, rd->i_buf + rd->i_buflen, rd->I_BUF_MAX - rd->i_buflen);
		if(n > 0){
			rd->i_bytes += n;
			rd->i_buflen += n;
			i_bytes += n;

			hp_iostat_update(rd->stat, n);

			continue; /* read some, try to data */
		}
		else{
			if(n < 0 && (errno == EINTR || errno == EAGAIN)){
				/* read again later */
			}
			else{
				rd->err = (errno == 0? EIO : errno);
#ifndef NDEBUG
				if((n == 0 && hp_log_level > 9) || (n != 0 && hp_log_level > 1))
					hp_log(stderr, "%s: read %s, fd=%d, return=%d, errno=%d/'%s'\n", __FUNCTION__
						,(n == 0? "EOF" : "failed"), fd, n, rd->err, strerror(rd->err));
#endif /* NDEBUG */
				if(rd->i_buflen > 0)
					rd->data(rd->i_buf, &rd->i_buflen, arg);

				if(rd->read_error)
					rd->read_error(rd, rd->err, arg);
			}
			break;
		}
	} /* loop for read and parse */

	return i_bytes;
}

/////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
void dbg_eto_write_error(struct hp_wr * wr, int err, void * arg)
{
	assert(err == 0);
	assert(arg == 0);
}

int test_hp_io_main(int argc, char ** argv)
{
	int rc = 0;
	//1
	{struct hp_wr pioobj  = { 0 }, * etio = &pioobj;
	etio->write_error = dbg_eto_write_error;

	rc = hp_wr_init(etio, 8, 0);
	assert(rc == 0);

	hp_wr_add(etio, "1", 1, 0, 0);
	hp_wr_add(etio, "22", 2, 0, 0);
	hp_wr_add(etio, "333", 3, 0, 0);
	hp_wr_add(etio, "4444", 4, 0, 0);
	hp_wr_add(etio, "55555", 5, 0, 0);
	hp_wr_add(etio, "666666", 6, 0, 0);
	assert(etio->O_ITEMS_LEN == 16);

	size_t n = hp_wr_write(etio, STDOUT_FILENO, 0);

	assert(n == 1 + 2 + 3 + 4 + 5 + 6);
	assert(etio->o_bytes == n);
	assert(etio->o_items_len == 0);

	hp_wr_add(etio, "1", 1, 0, 0);
	assert(etio->O_ITEMS_LEN == 8);

	hp_wr_uninit(etio);
	assert(etio->o_items_len == 0);}

	//2
	{struct hp_wr pioobj  = { 0 }, * wr = &pioobj;
	wr->write_error = dbg_eto_write_error;

	int rc = hp_wr_init(wr, 5, 0);
	assert(rc == 0);

	hp_wr_add(wr, "1", 1, 0, 0);
	hp_wr_add(wr, "22", 2, 0, 0);
	assert(wr->O_ITEMS_LEN == 5);
	hp_wr_add(wr, "333", 3, 0, 0);
	assert(wr->O_ITEMS_LEN == 10);
	hp_wr_add(wr, "4444", 4, 0, 0);
	hp_wr_add(wr, "55555", 5, 0, 0);
	hp_wr_add(wr, "666666", 6, 0, 0);
	hp_wr_add(wr, "7777777", 7, 0, 0);
	hp_wr_add(wr, "88888888", 8, 0, 0);
	assert(wr->O_ITEMS_LEN == 20);

	size_t n = hp_wr_write(wr, STDOUT_FILENO, 0);
	write(STDOUT_FILENO, "\n", 1);

	assert(n == 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8);
	assert(wr->o_bytes == n);
	assert(wr->o_items_len == 0);

	hp_wr_add(wr, "1", 1, 0, 0);
	assert(wr->O_ITEMS_LEN == 10);

	hp_wr_uninit(wr);
	assert(wr->o_items_len == 0);}

	return rc;
}
#endif /* NDEBUG */
#endif //#ifndef HAVE_UNISTD_H


