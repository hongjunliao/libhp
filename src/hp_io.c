/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/5/16
 *
 * the  I/O system
 * */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */
#ifndef _MSC_VER
#ifndef _WIN32
#include "hp_io.h"
#include "hp_log.h"     /* hp_log */
#include "hp_libc.h"    /* hp_min */
#include <unistd.h>     /* read, sysconf, ... */
#include <stdio.h>
#include <string.h>     /* memset, ... */
#include <errno.h>      /* errno */
#include <assert.h>     /* define NDEBUG to disable assertion */
#include <stdlib.h>
#include <limits.h>		/* IOV_MAX */

#ifndef IOV_MAX
#define IOV_MAX 1024
#endif
 
#define gloglevel(etio) ((etio)->loglevel? *((etio)->loglevel) : XH_ETIO_LOG_LEVEL)
/////////////////////////////////////////////////////////////////////////////////////

size_t iovec_total_bytes(struct iovec * vec, int count)
{
	size_t bytes = 0;
	int i = 0;
	for(; i < count; ++i)
		bytes += vec[i].iov_len;

	return bytes;
}

/* NOTE: will change @param vec if needed */
void before_writev(struct iovec * vec, int count,
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

ssize_t writev_a(int fd, int * err, struct iovec * vec, int count, int * n, size_t bytes)
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

int hp_eto_init(struct hp_eto * eto, int n)
{
	if(!eto)
		return -1;
	memset(eto, 0, sizeof(struct hp_eto));

	eto->O_ITEMS_INIT_LEN = eto->O_ITEMS_LEN = n;
	eto->o_items = (struct hp_eto_item *)malloc(eto->O_ITEMS_LEN * sizeof(struct hp_eto_item));

	return 0;
}

void hp_eto_uninit(struct hp_eto * eto)
{
	if(!eto)
		return;
	hp_eto_clear(eto);
	free(eto->o_items);
	eto->o_items = 0;
}

int hp_eti_init(struct hp_eti * eti, int bufrlen)
{
	if(!eti)
		return -1;
	memset(eti, 0, sizeof(struct hp_eti));

	if(bufrlen <= 0)
		bufrlen = 1024 * 8;
	eti->I_BUF_MAX = bufrlen;

	return 0;
}

void hp_eti_uninit(struct hp_eti * eti)
{
	if(!eti)
		return;

	free(eti->i_buf);
}

static void hp_eto_reserve(struct hp_eto * eto, int n)
{
	eto->O_ITEMS_LEN = n;
	eto->o_items = (struct hp_eto_item *)realloc(eto->o_items, eto->O_ITEMS_LEN * sizeof(struct hp_eto_item));
}

int hp_eto_add(struct hp_eto * eto, void * iov_base, size_t iov_len, hp_eto_free_t freecb, void * ptr)
{
	if(!(eto && iov_base))
		return -1;

	if(eto->O_ITEMS_LEN - eto->o_items_len <= 3){
		if(gloglevel(eto) > 8)
			hp_log(stdout, "%s: hp_eto::o_items_len reached max, MAX=%d, realloc\n"
				, __FUNCTION__, eto->O_ITEMS_LEN);

		/* @see https://www.zhihu.com/question/36538542 */
		hp_eto_reserve(eto, eto->O_ITEMS_LEN * 2);
	}
	else if((eto->O_ITEMS_LEN > eto->O_ITEMS_INIT_LEN) &&
			(eto->o_items_len + 2 < eto->O_ITEMS_LEN / 2))
		hp_eto_reserve(eto, eto->O_ITEMS_LEN / 2);

	struct hp_eto_item * item = eto->o_items + eto->o_items_len;

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

	++eto->o_items_len;

	return 0;
}

static void hp_etio_del_n(struct hp_eto *etio, int n)
{
	int i;
	for(i = 0; i < n; ++i){
		struct hp_eto_item * item = etio->o_items + i;
		if(item->free && item->ptr){
			item->free(item->ptr);
		}
	}
	memmove(etio->o_items, etio->o_items + n, sizeof(struct hp_eto_item) * (etio->o_items_len - n));
	etio->o_items_len -= n;
}

int hp_eto_obytes(hp_eto * eto)
{
	if(!eto)
		return 0;
	int i;

	int n = 0;
	for(i = 0; i < eto->o_items_len; ++i){
		struct hp_eto_item * item = eto->o_items + i;
		n += item->vec.iov_len;
	}

	return n;
}

void hp_eto_clear(struct hp_eto * eto)
{
	return hp_etio_del_n(eto, eto->o_items_len);
}

size_t hp_eto_write(struct hp_eto * eto, int fd, void * arg)
{
	if(!eto)
		return 0;

	if(eto->o_items_len == 0)
		return 0;

	int iov_max = sysconf(_SC_IOV_MAX);
	if(iov_max <= 0)
		iov_max = IOV_MAX;

	size_t o_bytes = 0; /* out bytes this time */
	for(;;){
		/* all write done, return */
		if(eto->o_items_len == 0){
			if(eto->write_done)
				eto->write_done(eto, 0, arg);
			return o_bytes;
		}

		int vec_len = hp_min(iov_max, eto->o_items_len);
		struct iovec * vec = calloc(vec_len, sizeof(struct iovec));

		/* prepare data for iovec */
		int i;
		for(i = 0; i < vec_len; ++i){
			struct hp_eto_item * item = eto->o_items + i;
			vec[i] = item->vec;
		}
		size_t W = iovec_total_bytes(vec, vec_len);  /* bytes to write this loop */
		if(W == 0){
			free(vec);
			hp_etio_del_n(eto, vec_len);
			continue;
		}

		/* now time to write */
		int n = 0, err = 0;
		ssize_t w = writev_a(fd, &err, vec, vec_len, &n, W);
		eto->o_bytes += w;
		o_bytes += w;

		/* adjust the last one for next write */
		if(n < vec_len)
			eto->o_items[n].vec = vec[n];

		/* free iovec */
		free(vec);

		/* clear these written ones: [0,n) */
		hp_etio_del_n(eto, n);

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
			if(gloglevel(eto) > 0)
				hp_log(stderr, "%s: writev_a ERROR, fd=%d, return=%d, errno=%d/'%s'\n", __FUNCTION__
					, fd, err, errno, strerror(errno));
#endif /* NDEBUG */
			/* write error occurred, the fd expect to be reset */
			if(eto->write_error)
				eto->write_error(eto, err, arg);
			break;
		}
	} /* end of write loop */

	return o_bytes;
}

size_t hp_eti_read(struct hp_eti * eti, int fd, void * arg)
{
	if(!(eti && eti->pack))
		return 0;

	if(!eti->i_buf){
		eti->i_buf = (char * )malloc(eti->I_BUF_MAX);
		assert(eti->i_buf);
	}

	size_t i_bytes = 0; /* in bytes this time */
	for(;;){
		int r = eti->pack(eti->i_buf, &eti->i_buflen, arg);
		if(r != EAGAIN)
			break;

		if(!(eti->i_buflen < eti->I_BUF_MAX)){
			eti->I_BUF_MAX *= 2;
			eti->i_buf = realloc(eti->i_buf, eti->I_BUF_MAX);

			if(gloglevel(eti) > 0)
				hp_log(stdout, "%s: realloc to %d\n", __FUNCTION__, eti->I_BUF_MAX);
		}

		ssize_t n = read(fd, eti->i_buf + eti->i_buflen, eti->I_BUF_MAX - eti->i_buflen);
		if(n > 0){
			eti->i_bytes += n;
			eti->i_buflen += n;
			i_bytes += n;

			hp_iostat_update(eti->stat, n);

			continue; /* read some, try to pack */
		}
		else if(n < 0) {
			if((errno == EINTR || errno == EAGAIN)){
				/* read again later */
			}
			else{
#ifndef NDEBUG
			if(gloglevel(eti) > 0)
				hp_log(stderr, "%s: read ERROR, fd=%d, return=%d, errno=%d/'%s'\n", __FUNCTION__
					, fd, n, errno, strerror(errno));
#endif /* NDEBUG */
				if(eti->i_buflen > 0)
					eti->pack(eti->i_buf, &eti->i_buflen, arg);

				if(eti->read_error)
					eti->read_error(eti, errno, arg);
			}
			break;
		}
		else if(n == 0){ /* EOF */
#ifndef NDEBUG
			if(gloglevel(eti) > 9)
				hp_log(stdout, "%s: read EOF, fd=%d, return=%d, errno=%d/'%s'\n", __FUNCTION__
					, fd, n, errno, strerror(errno));
#endif /* NDEBUG */

			if(eti->i_buflen > 0)
				eti->pack(eti->i_buf, &eti->i_buflen, arg);

			if(eti->read_error)
				eti->read_error(eti, 0, arg);
			break;
		}
	} /* loop for read and parse */

	return i_bytes;
}

/////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
void dbg_eto_write_error(struct hp_eto * eto, int err, void * arg)
{
	assert(err == 0);
	assert(arg == 0);
}

int test_hp_io_main(int argc, char ** argv)
{
	int rc = 0;
	//1
	{struct hp_eto pioobj  = { 0 }, * etio = &pioobj;
	etio->write_error = dbg_eto_write_error;

	rc = hp_eto_init(etio, 8);
	assert(rc == 0);

	hp_eto_add(etio, "1", 1, 0, 0);
	hp_eto_add(etio, "22", 2, 0, 0);
	hp_eto_add(etio, "333", 3, 0, 0);
	hp_eto_add(etio, "4444", 4, 0, 0);
	hp_eto_add(etio, "55555", 5, 0, 0);
	hp_eto_add(etio, "666666", 6, 0, 0);
	assert(etio->O_ITEMS_LEN == 16);

	size_t n = hp_eto_write(etio, STDOUT_FILENO, 0);

	assert(n == 1 + 2 + 3 + 4 + 5 + 6);
	assert(etio->o_bytes == n);
	assert(etio->o_items_len == 0);

	hp_eto_add(etio, "1", 1, 0, 0);
	assert(etio->O_ITEMS_LEN == 8);

	hp_eto_uninit(etio);
	assert(etio->o_items_len == 0);}

	//2
	{struct hp_eto pioobj  = { 0 }, * etio = &pioobj;
	etio->write_error = dbg_eto_write_error;

	int rc = hp_eto_init(etio, 5);
	assert(rc == 0);

	hp_eto_add(etio, "1", 1, 0, 0);
	hp_eto_add(etio, "22", 2, 0, 0);
	assert(etio->O_ITEMS_LEN == 5);
	hp_eto_add(etio, "333", 3, 0, 0);
	assert(etio->O_ITEMS_LEN == 10);
	hp_eto_add(etio, "4444", 4, 0, 0);
	hp_eto_add(etio, "55555", 5, 0, 0);
	hp_eto_add(etio, "666666", 6, 0, 0);
	hp_eto_add(etio, "7777777", 7, 0, 0);
	hp_eto_add(etio, "88888888", 8, 0, 0);
	assert(etio->O_ITEMS_LEN == 20);

	size_t n = hp_eto_write(etio, STDOUT_FILENO, 0);
	write(STDOUT_FILENO, "\n", 1);

	assert(n == 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8);
	assert(etio->o_bytes == n);
	assert(etio->o_items_len == 0);

	hp_eto_add(etio, "1", 1, 0, 0);
	assert(etio->O_ITEMS_LEN == 10);

	hp_eto_uninit(etio);
	assert(etio->o_items_len == 0);}

	return rc;
}
#endif /* NDEBUG */
#endif  /* _WIN32 */
#endif /* _MSC_VER */


