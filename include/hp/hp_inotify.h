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
#include "hp/sdsinc.h"
/////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hp_inotify hp_inotify;
typedef struct hp_inotify_ient hp_inotify_ient;

struct hp_inotify {
	hp_epoll * efds;
	int ifd; 			/* inotify fd */
	hp_epolld iepolld;
	hp_inotify_ient ** ients;
};

struct HP_INOTIFY_TOOL_PKG {
	/* init */
	int(* init)(hp_inotify * dl, hp_epoll * efds);
	/* uninit */
	void(* uninit)(hp_inotify * dl);
	/* add dl
	 * @param reload: hp_inotify_reload_t */
	int (* add)(hp_inotify * dl, char const * path
			, int (* open)(char const * path, void * d)
			, int (* fn)(struct epoll_event * ev), void * d);
	/* del dl */
	int (* del)(hp_inotify * dl, char const * name);
	/* inotify events to c_str */
	sds (* iev_to_str)(int iev);

};
extern struct HP_INOTIFY_TOOL_PKG const HP_INOTIFY_TOOL;

#ifndef NDEBUG
int test_hp_inotify_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif
#endif /* _MSC_VER */
#endif /* LIBHP_INOTIFY_H__ */
