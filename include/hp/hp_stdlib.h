/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2023/7/31
 *
 * random on Win32
 * */
/////////////////////////////////////////////////////////////////////////////////////////

#ifndef LIBHP_STDLIB_H
#define LIBHP_STDLIB_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef __cplusplus
extern "C" {
#endif


#ifdef _MSC_VER
int random();
#endif /* _MSC_VER */

#ifdef __cplusplus
}
#endif

#endif /* LIBHP_STDLIB_H */
