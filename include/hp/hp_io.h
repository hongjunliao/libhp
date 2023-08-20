/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/5/25
 *
 * read or write fd until 'again' or error, using iovec if available

 * */
/////////////////////////////////////////////////////////////////////////////////////

#ifndef LIBHP_IO_H__
#define LIBHP_IO_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef HAVE_UNISTD_H

#include "hp/libhp.h"	//hp_free_t
#include "hp_iostat.h"  /* hp_iostat */
#include <stddef.h>
#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>    /* iovec */
#endif //#ifdef HAVE_SYS_UIO_H
/////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hp_wr hp_wr;
typedef struct hp_rd hp_rd;

struct hp_rd {
	char *               i_buf;
	size_t               i_buflen;
	size_t               I_BUF_MAX;

	size_t               i_bytes;     /* total bytes read */

	struct hp_iostat *   stat;        /* for statistics */
	/*
	 * callback for data msg
	 * @return:
	 * >=0: OK, continue reading for next data
	 * <0: failed, maybe parse data failed
	 * */
	int (* data)(char * buf, size_t * len, void * arg);
	/*
	 * callback for read error
	 *  */
	void (* read_error)(struct hp_rd * rd, int err, void * arg);
	/**
	 * error value: return of data() if failed,, or read_error(err)
	 * 0 means OK
	 */
	int err;
};

struct hp_wr {
	struct hp_uio_item * o_items;
	int                  O_ITEMS_INIT_LEN;
	int                  o_items_len;
	int                  O_ITEMS_LEN;
	size_t               o_bytes;     /* total bytes written */
	/*
	 * callback if write error
	 * */
	void (* write_error)(struct hp_wr * wr, int err, void * arg);
	/**
	 * error value: write_error(err)
	 * 0 means OK
	 */
	int err;
};

int hp_rd_init(struct hp_rd * rd, int bufrlen,
		int (* data)(char * buf, size_t * len, void * arg),
		void (* read_error)(struct hp_rd * rd, int err, void * arg));
/*!
 * check rd->err for errors
 * */
size_t hp_rd_read(struct hp_rd * rd, int fd, void * arg);

void hp_rd_uninit(struct hp_rd * rd);

int hp_wr_init(struct hp_wr * wr, int n, void (* write_error)(struct hp_wr * wr, int err, void * arg));
int hp_wr_add(struct hp_wr * wr, void * iov_base, size_t iov_len, hp_free_t free, void * ptr);
/*!
 * check wr->err for errors
 * */
size_t hp_wr_write(struct hp_wr *wr, int fd, void * arg);

void hp_wr_clear(struct hp_wr * wr);
/* total bytes to write */
int hp_wr_bytes(hp_wr * wr);
void hp_wr_uninit(struct hp_wr * wr);

/////////////////////////////////////////////////////////////////////////////////////

#ifdef HAVE_SYS_UIO_H
/**
 * write @param vec until 'again' or error or write done
 * @return: return @bytes for written
 *          @param err: >0: EAGAIN for again,
 *                     0 for OK, else for error
 * @note: call again if  write incomplete and no error
 */
typedef struct iovec iovec;
ssize_t hp_uio_write(int fd, int * err, iovec * vec, int count, int * n, size_t bytes);
#endif //#ifdef HAVE_SYS_UIO_H

#ifndef NDEBUG
int test_hp_io_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif


#endif //#ifdef HAVE_UNISTD_H
#endif /* LIBHP_IO_H__ */
