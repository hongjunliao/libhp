/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2017/9/13
 *
 * signal and handler
 * NOTE: the main code is from redis, redis: https://github.com/antirez/redis
 * copyright below:
 *
 * Copyright (c) 2009-2016, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 */
#ifndef LIBHP_SIG_H__
#define LIBHP_SIG_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */
#ifndef _MSC_VER
/////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hp_sig hp_sig;

struct hp_sig{
	int (* init)(hp_sig * sig
			, void (*on_chld)(void * arg)
			, void (*on_exit)(void * arg)
			, void (*on_usr1)(void * arg)
			, void (*on_usr2)(void * arg)
			, void * arg);
	void (* uninit)(hp_sig * sig);

	/* this callback is call when receive SIGCHLD */
	void (*on_chld)(void * arg);
	void (*on_exit)(void * arg);
	void (*on_usr1)(void * arg);
	void (*on_usr2)(void * arg);

	void * arg;
};

int hp_sig_init(hp_sig * sig
		, void (*on_chld)(void * arg)
		, void (*on_exit)(void * arg)
		, void (*on_usr1)(void * arg)
		, void (*on_usr2)(void * arg)
		, void * arg);
void hp_sig_uninit(hp_sig * sig);

#ifdef __cplusplus
}
#endif
#endif /* _MSC_VER */
#endif /* LIBHP_SIG_H__ */
