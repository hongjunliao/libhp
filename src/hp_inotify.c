/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2020/05/30
 *
 * inotify tools
 * */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */
#ifdef __linux__
#include "hp/hp_inotify.h"
#include <unistd.h>
#include <sys/inotify.h> /* inotify */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <search.h>
#include <assert.h>     /* define NDEBUG to disable assertion */
#include <errno.h>      /* errno */
#include "hp/hp_log.h"      /* hp_log */
#include "hp/hp_epoll.h"    /* hp_epoll */
#include "hp/sdsinc.h"     /* sds */
#include "c-vector/cvector.h"
#include <sys/epoll.h>  /* epoll_event */

/////////////////////////////////////////////////////////////////////////////////////

struct hp_inotify_ient {
	int fd;
	int wd; 		/* inotify whatch fd */
	sds path;		 /* inotify whatched file */
	int (* open)(char const * path, void * d);
	int (* fn)(epoll_event * ev, void * arg);
	void * d;
};

/////////////////////////////////////////////////////////////////////////////////////

sds hp_inotify_iev_to_str(int iev)
{
	sds buf = sdscatfmt(sdsempty(), "%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s"
			, (iev & IN_ACCESS?        "IN_ACCESS | " : "")
			, (iev & IN_MODIFY?        "IN_MODIFY | " : "")
			, (iev & IN_ATTRIB?        "IN_ATTRIB | " : "")
			, (iev & IN_CLOSE_WRITE?   "IN_CLOSE_WRITE | " : "")
			, (iev & IN_CLOSE_NOWRITE? "IN_CLOSE_NOWRITE | " : "")
			, (iev & IN_CLOSE?         "IN_CLOSE | " : "")
			, (iev & IN_OPEN?          "IN_OPEN | " : "")
			, (iev & IN_MOVED_FROM?    "IN_MOVED_FROM | " : "")
			, (iev & IN_CREATE?        "IN_CREATE | " : "")
			, (iev & IN_DELETE?        "IN_DELETE | " : "")
			, (iev & IN_DELETE_SELF?   "IN_DELETE_SELF | " : "")
			, (iev & IN_MOVE_SELF?     "IN_MOVE_SELF | " : "")
			, (iev & IN_UNMOUNT?       "IN_UNMOUNT | " : "")
			, (iev & IN_Q_OVERFLOW?    "IN_Q_OVERFLOW | " : "")
			, (iev & IN_IGNORED?       "IN_IGNORED | " : "")
			);

	int left = iev & (~(IN_ACCESS | IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE | IN_CLOSE_NOWRITE |
			IN_CLOSE | IN_OPEN | IN_MOVED_FROM | IN_CREATE | IN_DELETE | IN_DELETE_SELF |
			IN_UNMOUNT | IN_Q_OVERFLOW | IN_IGNORED));
	if(!(left == 0))
		hp_log(stderr, "%s: type=%d, str='%s', left=%d\n"
				, __FUNCTION__, iev, buf, left);

	if(sdslen(buf) >= 2)
		buf[sdslen(buf) - 2] = '\0';

	return buf;
}

static void hp_inotify_all(hp_inotify * dl, int events)
{
	assert(dl);
	int i;
	for(i  = 0; i < cvector_size(dl->ients); ++i){
		epoll_event evobj = { events}, * ev = &evobj;
		dl->ients[i]->fn(ev, dl->ients + i);
	}
}

static int epoll_before_wait(struct hp_epoll * epo)
{
	assert(epo && epo->arg);
	hp_inotify * dl = (hp_inotify *)epo->arg;

	int i;
	for(i = 0; i < cvector_size(dl->ients); ++i){
		hp_inotify_ient * ent = dl->ients[i];
		if(ent->wd == 0){

			if(access(ent->path, F_OK) != 0)
				continue;

			int wd = inotify_add_watch(dl->ifd, ent->path, IN_ALL_EVENTS);
			if (wd == -1) {
				hp_log(stderr, "%s: inotify_add_watch failed, file='%s' err=%d/'%s'"
						, __FUNCTION__, ent->path, errno, strerror(errno));
			}
			else {
				ent->wd = wd;
				if(ent->open){
					int fd  = ent->open(ent->path, ent->d);
					ent->fd = (fd > 0? fd : 0);
				}
			}
		}
	}

	return 0;
}

/*
 * @see libuv/src/unix/linux-inotify.c
 *
 * */
static int hp_inotify_iread_event(hp_inotify * dl)
{
	assert(dl);
	/* Some systems cannot read integer variables if they are not
	 properly aligned. On other systems, incorrect alignment may
	 decrease performance. Hence, the buffer used for reading from
	 the inotify file descriptor should have the same alignment as
	 struct inotify_event. */

	char buf[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
	const struct inotify_event *event;
	int i;
	ssize_t len;
	char *ptr;

	/* Loop while events can be read from inotify file descriptor. */
	int fd = dl->ifd;
	for (;;) {
		/* Read some events. */
		len = read(fd, buf, sizeof(buf));
		while (len == -1 && errno == EINTR);

	    if (len == -1) {
	      assert(errno == EAGAIN || errno == EWOULDBLOCK);
	      break;
	    }

	    assert(len > 0); /* pre-2.6.21 thing, size=0 == read buffer too small */

		/* Loop over all events in the buffer */
		for (ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + event->len) {

			event = (const struct inotify_event *) ptr;

			for(i  = 0; i < cvector_size(dl->ients); ++i){
				if ((event->wd == dl->ients[i]->wd)){

					if(access(dl->ients[i]->path, F_OK) != 0){
						sds s = hp_inotify_iev_to_str(event->mask);
						hp_log(stdout, "%s: inotify: path='%s', event='%s', access failed, deleted?\n"
						, __FUNCTION__, dl->ients[i]->path, s);
						sdsfree(s);

						inotify_rm_watch(fd, event->wd);
						dl->ients[i]->wd = 0;
					}
					else if(event->mask & IN_MODIFY){
						epoll_event evobj = { EPOLLIN };
						dl->ients[i]->fn(&evobj, dl->ients + i);
					}
				}
			}
		}
	}
	return 0;
}

static int epoll_dl_handle_ii(epoll_event * ev, void * arg)
{
	assert(ev && arg);
	hp_inotify * dl = (hp_inotify *)arg;
	assert(dl);

	if((ev->events & EPOLLERR)){
		hp_log(stderr, "%s:  EPOLLERR, errno=%d/'%s'\n"
				, __FUNCTION__, errno, strerror(errno));

		hp_inotify_all(dl, EPOLLERR);

		hp_epoll_rm(dl->epo, dl->ifd);
		close(dl->ifd);
		dl->ifd = 0;

		return 0;
	}

	if((ev->events & EPOLLIN)){
		int rc = hp_inotify_iread_event(dl);
		assert(rc == 0);
	}

	return 0;
}

int hp_inotify_add(hp_inotify * dl, char const * path
		, int (* open)(char const * path, void * d)
		, int (* fn)(epoll_event * ev, void * arg), void * d)
{
	if(!(dl && (path && strlen(path) > 0) && fn))
		return -1;

	int wd = inotify_add_watch(dl->ifd, path, IN_ALL_EVENTS);
	if (wd == -1) {
		hp_log(stderr, "%s: inotify_add_watch failed, file='%s' err=%d/'%s'"
				, __FUNCTION__, path, errno, strerror(errno));
		return -2;
	}
	int fd = (open? open(path, d) : 0);
	hp_inotify_ient * ent = calloc(1, sizeof(hp_inotify_ient));
	ent->path = sdsnew(path);
	ent->wd = wd;
	ent->open = open;
	ent->fn = fn;
	ent->d = d;

	cvector_push_back(dl->ients, ent);

	return 0;
}

static int cmp_by_name(const void * a, const void * b)
{
	assert(a && b);
	hp_inotify_ient * ent1 = *(hp_inotify_ient **)a, * ent2 = *(hp_inotify_ient **)b;
	return (strncmp(ent1->path, ent2->path, sdslen(ent1->path)) != 0);
}

int hp_inotify_del(hp_inotify * dl, char const * dlname)
{
	if(!(dl && dlname && dlname[0] != '\0'))
		return 0;

	hp_inotify_ient keyobj = { .path = sdsnew(dlname) }, * key = &keyobj
			, * ient, ** iter = cvector_lfind(dl->ients, lfind, &key, cmp_by_name);
	sdsfree(key->path);
	if(!iter)
		return -1;
	ient = *iter;

	inotify_rm_watch(dl->ifd, ient->wd);

	epoll_event evobj = { .events = EPOLLERR };
	ient->fn(&evobj, ient);

	sdsfree(ient->path);
	free(ient);

	cvector_remove(dl->ients, iter);

	return 0;
}

int hp_inotify_init(hp_inotify * dl, struct hp_epoll * epo)
{
	if(!(dl && epo))
		return -1;

	memset(dl, 0, sizeof(hp_inotify));

	dl->epo = epo;
	cvector_init(dl->ients, 2);

	int fd = inotify_init1(IN_NONBLOCK);
	if (fd == -1) {
		hp_log(stderr, "%s: inotify_init1, err=%d/'%s'", __FUNCTION__, errno, strerror(errno));
		return -1;
	}

	dl->ifd = fd;
	if(hp_epoll_add(epo, dl->ifd, EPOLLIN | EPOLLET, epoll_dl_handle_ii, 0, dl) != 0)
		return -1;

	return 0;
}

void hp_inotify_uninit(hp_inotify * dl)
{
	if(!(dl))
		return;

	int i;
	for(i = 0; i < cvector_size(dl->ients); ++i){
		hp_inotify_ient * ient = dl->ients[i];

		inotify_rm_watch(dl->ifd, ient->wd);

		epoll_event evobj = { EPOLLERR };
		ient->fn(&evobj, dl);

		sdsfree(ient->path);
		free(ient);
	}
	cvector_free(dl->ients);

	if(dl->ifd > 0)
		close(dl->ifd);
}

/////////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
#if defined(HAVE_SYS_UIO_H)
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include "hp/hp_io.h"	/*  */
#include "hp/string_util.h"/* hp_fread  */
#include "hp/str_dump.h"   /* dumpstr */

#define Ino HP_INOTIFY_TOOL

typedef struct file_read_notify {
	hp_rd eti;
	int fd;
	int flags;
} file_read_notify;

static hp_epoll efdsobj, * epo = &efdsobj;
static int test_flag = 0;

static int read_cb(char * buf, size_t * len, void * arg)
{
	assert(arg);
	epoll_event* ev = (epoll_event*) arg;
	assert(ev);
	file_read_notify * ctx = (file_read_notify *)arg;
	assert(ctx);
	assert(buf && len);

	if(!(*len > 0))
		return EAGAIN;

	hp_log(stdout, "%s: read from fd=%d, %d/'%s'\n"
			, __FUNCTION__, ctx->fd, *len, dumpstr(buf, *len, *len));

	*len = 0;

	++test_flag;

	return EAGAIN;
}

static void error_cb(struct hp_rd * eti, int err, void * arg)
{
	assert(arg);
	epoll_event* ev = (epoll_event*) arg;
	file_read_notify * ctx = (file_read_notify *)arg;
	assert(ctx);

	ev->events = 0;
	/* NOTE: we here are reading a file,
	 * a error of 0 means EOF -- end of file, this is NOT actually a error
	 *  */
	if(err != 0){
		close(ctx->fd);
		hp_log(stderr, "%s: read fd=%d failed %d/'%s'\n", __FUNCTION__, ctx->fd, err,
				(err == EPOLLERR? "EPOLLERR" : strerror(err)));
		test_flag = -1;
	}
}


static int epoll_cb(epoll_event * ev, void * arg)
{
	assert(ev && arg);

	file_read_notify * ctx = (file_read_notify *)arg;
	assert(ctx);

	if((ev->events & EPOLLERR)){
		error_cb(&ctx->eti, EPOLLERR, ev);
		return 0;
	}

	if((ev->events & EPOLLIN)){
		hp_rd_read(&ctx->eti, ctx->fd, ev);
	}

	return 0;
}

static void * write_routine (void * arg)
{
	assert(arg);

	int n;
	for(n = 0; n < 10; ++n){
		FILE * f = popen("echo \"hello\\n\" >>  /tmp/a.log", "r");
		assert(f);
		pclose(f);

		usleep(300 * 1000);
	}
	return 0;
}

static int open_cb(char const * path, void * d)
{
	assert(path && d);
	file_read_notify * fnotify = (file_read_notify *)d;

	int fd = open(path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	assert(fd > 0);

	fnotify->fd = fd;

	/* if re-open, do NOT senk to end */
	if(!fnotify->flags){
		lseek(fd, 0, SEEK_END);
		fnotify->flags = 1;
	}

	return fd;
}

int test_hp_inotify_main(int argc, char ** argv)
{
	int r = 0, rc;

    signal(SIGCHLD, SIG_IGN);

    {
		hp_inotify inotifyobj = { 0 }, * inotify = &inotifyobj;
		struct file_read_notify fnotifyobj = { 0 }, * fnotify = &fnotifyobj;

		hp_rd * eti = &fnotify->eti;
		rc = hp_rd_init(eti, 1024 * 8, read_cb, error_cb); /* 8K read buffer */
		assert(rc == 0 && "hp/hp_eti_init");

		rc = hp_epoll_init(epo, 1024, 100, epoll_before_wait, inotify);
		assert(rc == 0);

		r = hp_inotify_init(inotify, epo);
		assert(r == 0);

		FILE * f = fopen("/tmp/a.log", "w");
		assert(f);
		fclose(f);

		r = hp_inotify_add(inotify, "/tmp/a.log", open_cb, epoll_cb, fnotify);
		assert(r == 0);

		pthread_t tid;
		rc = pthread_create(&tid, 0, write_routine, (void *)inotify);
		assert(rc == 0);

		for(;;){
			hp_epoll_run(epo, 1);

			if(test_flag < 0 || test_flag == 10)
				break;
		}

		r = hp_inotify_del(inotify, "/tmp/a.log");
		assert(r == 0);

		hp_inotify_uninit(inotify);
		hp_epoll_uninit(epo);
    }

	return r;
}
#endif //defined(HAVE_SYS_UIO_H)
#endif /* NDEBUG */

#endif /* _MSC_VER */
