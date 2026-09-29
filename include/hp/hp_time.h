/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2023/7/31
 *
 * time on Win32
 * */
/////////////////////////////////////////////////////////////////////////////////////////

#ifndef LIBHP_TIME_H
#define LIBHP_TIME_H

#include "hp_config.h"

#ifdef __cplusplus
extern "C" {
#endif


#ifdef _MSC_VER
#include <winsock2.h>
int gettimeofday(struct timeval *tv, void *tz);
#else
#include <sys/time.h>
#endif /* _MSC_VER */

#ifdef __cplusplus
}
#endif

#endif /* LIBHP_TIME_H */
