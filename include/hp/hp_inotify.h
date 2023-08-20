/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2020/05/30
 *
 * inotify tools
 * */
#ifndef LIBHP_INOTIFY_H__
#define LIBHP_INOTIFY_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */
#ifdef __linux__

#include "hp_epoll.h"    /* hp_epoll */
#include "sdsinc.h"
/////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hp_inotify hp_inotify;
typedef struct hp_inotify_ient hp_inotify_ient;

struct hp_inotify {
	hp_epoll * epo;
	int ifd; 			/* inotify fd */
	hp_inotify_ient ** ients;
};

	/* init */
int hp_inotify_init(hp_inotify * dl, hp_epoll * epo);
/* add dl
 * @param reload: hp_inotify_reload_t */
int hp_inotify_add(hp_inotify * dl, char const * path
		, int (* open)(char const * path, void * d)
		, int (* fn)(epoll_event * ev, void * arg), void * d);
/* del dl */
int hp_inotify_del(hp_inotify * dl, char const * name);
/* uninit */
void hp_inotify_uninit(hp_inotify * dl);

sds hp_inotify_iev_to_str(int iev);
/////////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
int test_hp_inotify_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif
#endif /* _MSC_VER */
#endif /* LIBHP_INOTIFY_H__ */
