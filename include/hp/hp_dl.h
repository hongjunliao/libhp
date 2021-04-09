/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/823
 *
 * dlopen with inotify
 * */
#ifndef LIBHP_DL_H__
#define LIBHP_DL_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_DEPRECADTED

#include "hp_epoll.h"    /* hp_epoll */
/////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

#define XH_DL_LOG_LEVEL     0

typedef struct hp_dl hp_dl;
typedef struct hp_dl_ient hp_dl_ient;
typedef int (* hp_dl_reload_t)(void * addr, void * hdl);

struct hp_dl {
	struct hp_epoll *        efds;
	int                      ifd;         /* inotify fd */
	struct hp_epolld         iepolld;
	hp_dl_ient *             ients;
	int                      ient_len;    /* length for ients */
	int                      IENT_MAX;    /* max length for ients */

	/* init */
	int(* init)(hp_dl * dl, struct hp_epoll * efds);
	/* uninit */
	void(* uninit)(hp_dl * dl);
	/* add dl
	 * @param reload: hp_dl_reload_t */
	int (* add)(hp_dl * dl, char const * dlpath, char const * dlname, char const * reload, void * addr);
	/* del dl
	 * */
	int (* del)(hp_dl * dl, char const * dlname);
	/* reload all dl */
	int(* reload)(hp_dl * dl);
	/* inotify events to c_str */
	char * (* iev_to_str)(int iev, char * buf, int len);

	/* set by user */
	int * loglevel;              /* log level */
};

int hp_dl_init(hp_dl * dl, struct hp_epoll * efds);
void hp_dl_uninit(hp_dl * dl);

#ifdef __cplusplus
}
#endif
#endif /* LIBHP_DEPRECADTED */
#endif /* LIBHP_DL_H__ */
