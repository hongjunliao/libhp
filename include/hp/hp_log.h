/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/11/1
 *
 * log
 * */

#ifndef LIBHP_LOG_H__
#define LIBHP_LOG_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * you can simply replace
 *  hp_log(f, fmt, ...); to fprintf(f, fmt, ...);
 */
void hp_log(void * f, char const * fmt, ...);

#ifndef NDEBUG
int test_hp_log_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif
#endif /* LIBHP_LOG_H__ */
