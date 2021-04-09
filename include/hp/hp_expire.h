 /*!
 * This file is PART of libxhhp project
 * @author hongjun.liao <docici@126.com>, @date 2020/7/9
 *
 * */

#ifndef LIBHP_EXPIRE_H__
#define LIBHP_EXPIRE_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_DEPRECADTED
#ifdef LIBHP_WITH_TIMERFD

#include "sdsinc.h"        /* sds */
#include "redis/src/dict.h" /* dict */
#include "hp_timerfd.h"

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////////
typedef struct hp_expire hp_expire;
struct hp_expire{
	dict * dic;
	int ms;
	void (* free)(void *);
	hp_timerfd tfd;
};

int hp_expire_init(hp_expire * e, hp_epoll * efds, int timeout_ms, void (* free)(void *));
int hp_expire_init2(hp_expire * e, hp_epoll * efds, int tfd, int timeout_ms, void (* free)(void *));
void * hp_expire_get(hp_expire * e, char const * id, void * ( *fn)(char const * id));
void hp_expire_uninit(hp_expire * e);
/////////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
int test_hp_expire_main(int argc, char ** argv);
#endif

#ifdef __cplusplus
}
#endif

#endif /* LIBHP_WITH_TIMERFD */
#endif /* LIBHP_DEPRECADTED */
#endif /* LIBHP_EXPIRE_H__ */
