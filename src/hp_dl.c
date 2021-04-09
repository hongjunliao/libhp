/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/823
 *
 * dlopen .so, using inotify
 * see `man inotify` for more help about inotify
 * */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_DEPRECADTED

#include "hp_dl.h"
#include "hp_log.h"      /* hp_log */
#include "hp_epoll.h"    /* hp_epoll */
#include <unistd.h>
#include <dlfcn.h>      /* dlopen */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>     /* define NDEBUG to disable assertion */
#include <errno.h>      /* errno */
#include <sys/stat.h>	/* stat */
#include <sys/inotify.h> /* inotify */
#include "sds/sds.h"     /* sds */

/////////////////////////////////////////////////////////////////////////////////////

struct hp_dl_ient {
	void *                   hdl;         /* handle for dlopen */
	void *                   addr;        /* mouduel addr */
	int                      wd;          /* inotify whatch fd */
	sds                      name;        /* e.g. xhhp-ftp */
	sds                      file;        /* inotify whatched file */
	time_t                   mtime1;      /* stat */
	time_t                   mtime2;      /* stat */
	sds                      reload;      /* function name while reloading so */
};

#define gloglevel(dl) ((dl)->loglevel? *((dl)->loglevel) : XH_DL_LOG_LEVEL)
/////////////////////////////////////////////////////////////////////////////////////

static void * hp_dl_open(char const * file, int mode, int timeout)
{
	if(!(file))
		return 0;
	int left = timeout * 1000 * 1000; /* 10s */
	for(;;){
		void * hdl = dlopen(file, (mode <= 0? RTLD_LAZY : mode));
		if(hdl)
			return hdl;
		if(left <= 0)
			return 0;

		usleep(10 * 1000);
		left -= 10 * 1000;
	}

	return 0;
}

static int hp_dl_reopen(hp_dl * dl, hp_dl_ient * ient)
{
	if(!(dl && ient))
		return -1;
	if(ient->hdl){
		dlclose(ient->hdl);
		ient->hdl = 0;
	}
	/* FIXME: ugly but may work in most time, the problem is:
	 *
	 * (1)dlclose just only decrease reference count of a shared oject
	 * (2)one should call dlopen again after all childs called dlclose
	 * TODO: use pthread_cond_t to sync
	 * */
	sleep(2);

	void * hdl = hp_dl_open(ient->file, RTLD_LAZY, 10);
	if(!hdl) {
		hp_log(stderr, "%s: dlopen('%s') failed, error='%s'\n"
				, __FUNCTION__, ient->file, dlerror());
		return -1;
	}

	struct stat fs;
	if(stat(ient->file, &fs) < 0){
		hp_log(stderr, "%s: WARNING, stat('%s') failed, errno=%d, error='%s'\n"
				, __FUNCTION__, ient->file, errno, strerror(errno));
	}
	else {
		ient->mtime2 = ient->mtime1;
		ient->mtime1 = fs.st_mtim.tv_sec;;
	}

	ient->hdl = hdl;

	if(ient->reload){
		if(gloglevel(dl) > 0)
			hp_log(stdout, "%s: reload dl='%s', mtime=%zu/%zu\n"
					, __FUNCTION__, ient->file, ient->mtime1, ient->mtime2);

		hp_dl_reload_t fn = (hp_dl_reload_t)dlsym(hdl, ient->reload);
		assert(fn);
		if(fn) return fn(ient->addr, ient->hdl);
	}
	return 0;
}

static int hp_dl_reload(hp_dl * dl)
{
	if(!dl)
		return -1;

	int i;
	for(i  = 0; i < dl->ient_len; ++i){
		if(hp_dl_reopen(dl, dl->ients + i) != 0){
			return -3;
		}
	}
	return 0;
}

static int hp_dl_iread_event(hp_dl * dl)
{
	if(!dl) return 0;
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
		if (len == -1 && errno != EAGAIN) {
			return -1;
		}

		if (len <= 0)
			break;

		/* Loop over all events in the buffer */
		for (ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + event->len) {

			event = (const struct inotify_event *) ptr;

			for(i  = 0; dl->ients[i].file; ++i){
				if ((event->wd == dl->ients[i].wd)){
#ifndef NDEBUG
					if(gloglevel(dl) > 0){
						char ievbuf[1024];
						ievbuf[0] = '\0';
						hp_log(stdout, "%s: inotify: dl='%s', name='%s', event='%s'\n"
							, __FUNCTION__, dl->ients[i].file, (event->len? event->name : "")
							, dl->iev_to_str(event->mask, ievbuf, sizeof(ievbuf)));
					}
#endif /* NDEBUG */
					hp_dl_reopen(dl, dl->ients + i);

					inotify_rm_watch(fd, event->wd);
					int wd = inotify_add_watch(fd, dl->ients[i].file, IN_MODIFY | IN_ATTRIB);
					if (wd == -1) {
						close(fd);
						dl->ifd = 0;
						hp_log(stderr, "%s: inotify_add_watch, file='%s' err=%d/'%s'\n"
								, __FUNCTION__, dl->ients[i].file, errno, strerror(errno));
					}
					else dl->ients[i].wd = wd;
				}

			}
		}
	}
	return 0;
}

static int epoll_dl_handle_ii(struct epoll_event * ev)
{
	hp_dl * dl = (hp_dl *)hp_epoll_arg(ev);
	assert(dl);
	if(!(ev && hp_epoll_arg(ev)))
		return -1;
#ifndef NDEBUG
	if(gloglevel(dl) > 6){
		char buf[128];
		hp_log(stdout, "%s: fd=%d, events='%s'\n", __FUNCTION__
				, hp_epoll_fd(ev), hp_epoll_e2str(ev->events, buf, sizeof(buf)));
	}
#endif /* NDEBUG */

	if((ev->events & EPOLLERR)){
		hp_epoll_del(dl->efds, dl->ifd, EPOLLIN | EPOLLET, &dl->iepolld);
		close(dl->ifd);
		dl->ifd = 0;
		return 0;
	}

	if((ev->events & EPOLLIN)){
		int rc = hp_dl_iread_event(dl);
		if(rc != 0){
			hp_epoll_del(dl->efds, dl->ifd, EPOLLIN | EPOLLET, &dl->iepolld);
			close(dl->ifd);
			dl->ifd = 0;
			return 0;
		}
	}

	return 0;
}

static char * hp_dl_iev_to_str(int iev, char * buf, int len)
{
	if(!buf && len > 0) return 0;

	buf[0] = '\0';
	int n = snprintf(buf, len, "%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s"
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
	if(buf[0] != '\0' && n >= 3 )
		buf[n - 3] = '\0';

	int left = iev & (~(IN_ACCESS | IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE | IN_CLOSE_NOWRITE |
			IN_CLOSE | IN_OPEN | IN_MOVED_FROM | IN_CREATE | IN_DELETE | IN_DELETE_SELF |
			IN_UNMOUNT | IN_Q_OVERFLOW | IN_IGNORED));
	if(!(left == 0))
		hp_log(stderr, "%s: type=%d, str='%s', left=%d\n"
				, __FUNCTION__, iev, buf, left);
	return buf;
}

static int hp_dl_add(hp_dl * dl, char const * dlpath, char const * dlname
		, char const * reload, void * addr)
{
	if(!(dl && dlpath && dlname))
		return -1;
	if(dl->ient_len == dl->IENT_MAX)
		return -1;

	struct hp_dl_ient *  ent = dl->ients + dl->ient_len;
	ent->addr = addr;
	ent->reload = sdsnew(reload);
	ent->name = sdsnew(dlname);
	ent->file = sdscatprintf(sdsempty(), "%s/lib%s.so", dlpath, dlname);
	++dl->ient_len;

	int wd = inotify_add_watch(dl->ifd, ent->file, IN_ALL_EVENTS);
	if (wd == -1) {
		hp_log(stderr, "%s: inotify_add_watch failed, file='%s' err=%d/'%s'"
				, __FUNCTION__, ent->file, errno, strerror(errno));
		return -2;
	}

	ent->wd = wd;

	if(gloglevel(dl) > 3){
		hp_log(stdout, "%s: inotify watch added, file='%s'\n", __FUNCTION__, ent->file);
	}

	return hp_dl_reopen(dl, ent);
}

static int hp_dl_del(hp_dl * dl, char const * dlname)
{
	if(!(dl && dlname && dlname[0] != '\0'))
		return 0;

	int i;
	for(i = 0; i < dl->ient_len; ++i){
		hp_dl_ient * ient = dl->ients + i;
		if(strncmp(ient->name, dlname, sdslen(ient->name)) != 0)
			continue;

		inotify_rm_watch(dl->ifd, ient->wd);
		if(ient->hdl){
			if(gloglevel(dl) > 3)
				hp_log(stdout, "%s: unload dl='%s', mtime=%zu/%zu\n"
					, __FUNCTION__, ient->file, ient->mtime1, ient->mtime2);
			dlclose(ient->hdl);
		}

		sdsfree(ient->name);
		sdsfree(ient->file);
		sdsfree(ient->reload);
		if(i + 1 < dl->ient_len)
			memmove(dl->ients + i, dl->ients + i + 1, (dl->ient_len - (i + 1)) * sizeof(hp_dl_ient));
		--dl->ient_len;
		memset(dl->ients + dl->ient_len, 0, sizeof(hp_dl_ient));

		return 1;
	}
	return 0;
}

int hp_dl_init(hp_dl * dl, struct hp_epoll * efds)
{
	if(!(dl && efds))
		return -1;

	memset(dl, 0, sizeof(hp_dl));

	dl->efds = efds;
	dl->init = hp_dl_init;
	dl->uninit = hp_dl_uninit;
	dl->reload = hp_dl_reload;
	dl->add = hp_dl_add;
	dl->del = hp_dl_del;
	dl->iev_to_str = hp_dl_iev_to_str;
	dl->ient_len = 0;
	dl->IENT_MAX = 8;
	dl->ients = calloc(dl->IENT_MAX + 1, sizeof(hp_dl_ient));

	int fd = inotify_init1(IN_NONBLOCK);
	if (fd == -1) {
		hp_log(stderr, "%s: inotify_init1, err=%d/'%s'", __FUNCTION__, errno, strerror(errno));
		return -1;
	}

	dl->ifd = fd;
	hp_epolld_set(&dl->iepolld, dl->ifd, epoll_dl_handle_ii, dl);

	if(hp_epoll_add(efds, dl->ifd, EPOLLIN | EPOLLET, &dl->iepolld) != 0)
		return -1;

	return 0;
}

void hp_dl_uninit(hp_dl * dl)
{
	if(!(dl))
		return;

	for(;dl->ient_len > 0;){
		hp_dl_del(dl, dl->ients[dl->ient_len - 1].name);
	}
	free(dl->ients);

	close(dl->ifd);
}
#endif /* LIBHP_DEPRECADTED */
