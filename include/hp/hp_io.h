/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/5/25
 *
 * eto: read or write until 'again' or error, using iovec
 * called epoll-style IO(ET mode)

 * */

#ifndef LIBHP_IO_H__
#define LIBHP_IO_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifndef _MSC_VER
#ifndef _WIN32
#include "hp_iostat.h"  /* hp_iostat */
#include <stddef.h>
#include <sys/uio.h>    /* iovec */

#ifdef __cplusplus
extern "C" {
#endif

#define XH_ETIO_LOG_LEVEL     0
#define HP_ETIO_VEC           8

typedef struct hp_eto hp_eto;
typedef struct hp_eti hp_eti;

typedef	void  (* hp_eto_free_t)(void * ptr);

struct hp_eto_item {
	void *             ptr;         /* ptr to free */
	hp_eto_free_t     free;        /* for free ptr */

	struct iovec       vec;         /* will change while writing */
};

struct hp_eti {
	char *               i_buf;
	size_t               i_buflen;
	size_t               I_BUF_MAX;

	size_t               i_bytes;     /* total bytes read */

	struct hp_iostat *   stat;        /* for statistics */
	/*
	 * callback for pack msg
	 * @return:
	 * EAGAIN: pack OK       --> continue reading for next pack
	 * else: pack failed     --> maybe error occurred, or NO more data needed, stop reading and return
	 * */
	int (* pack)(char * buf, size_t * len, void * arg);
	/*
	 * callback for read error
	 *  */
	void (* read_error)(struct hp_eti * eti, int err, void * arg);
	/* set by user */
	int * loglevel;              /* log level */
};

struct hp_eto {
	struct hp_eto_item * o_items;
	int                  O_ITEMS_INIT_LEN;
	int                  o_items_len;
	int                  O_ITEMS_LEN;
	size_t               o_bytes;     /* total bytes written */
	/*
	 * callback if write done
	 * */
	void (* write_done)(struct hp_eto * eto, int err, void * arg);

	/*
	 * callback if write error
	 * */
	void (* write_error)(struct hp_eto * eto, int err, void * arg);

	/* set by user */
	int * loglevel;              /* log level */
};

int hp_eti_init(struct hp_eti * eti, int bufrlen);
void hp_eti_uninit(struct hp_eti * eti);

int hp_eto_init(struct hp_eto * eto, int n);
void hp_eto_uninit(struct hp_eto * eto);
int hp_eto_add(struct hp_eto * eto, void * iov_base, size_t iov_len, hp_eto_free_t free, void * ptr);
void hp_eto_clear(struct hp_eto * eto);
/* total bytes to write */
int hp_eto_obytes(hp_eto * eto);

#define hp_eto_try_write(eto, epolld) \
do { \
	struct epoll_event hetw_evobj, * __hetw_ev = &hetw_evobj; \
	__hetw_ev->data.ptr = (epolld); \
	__hetw_ev->events = EPOLLOUT; \
	hp_eto_write((eto), (epolld)->fd, __hetw_ev); \
}while(0)

#define hp_eti_try_read(eti, epolld) \
do { \
	struct epoll_event hetw_evobj, * __hetw_ev = &hetw_evobj; \
	__hetw_ev->data.ptr = (epolld); \
	__hetw_ev->events = EPOLLIN; \
	hp_eti_read((eti), (epolld)->fd, __hetw_ev); \
}while(0)

/* @see writev_a */
size_t hp_eto_write(struct hp_eto *eto, int fd, void * arg);
size_t hp_eti_read(struct hp_eti * eti, int fd, void * arg);

void hp_eio_uninit(struct hp_eto *eto);

/**
 * write @param vec until 'again' or error or write done
 * @return: return @bytes for written
 *          @param err: >0: EAGAIN for again,
 *                     0 for OK, else for error
 * @note: call again if  write incomplete and no error
 */
ssize_t writev_a(int fd, int * err, struct iovec * vec, int count, int * n, size_t bytes);

#ifndef NDEBUG
int test_hp_io_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif
#endif  /* _WIN32 */
#endif /* _MSC_VER */
#endif /* LIBHP_IO_H__ */
