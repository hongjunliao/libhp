/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/12/3
 *
 * sds
 * */

/* for those use sds on Win32, include following first
 * #ifdef _MSC_VER
 * #include "redis/src/Win32_Interop/Win32_Portability.h"
 * #include "redis/src/Win32_Interop/win32_types.h"
 * #endif
 */

#ifndef HP_SDSINC_H
#define HP_SDSINC_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(LIBHP_WITH_WIN32_INTERROP)
#include "redis/src/sds.h"  /* sds */
#elif defined(_MSC_VER)
#include "../../deps/hiredis/sds.h"
#else
#include "sds/sds.h"		/* sds */
#endif /* _MSC_VER */

#ifdef __cplusplus
}
#endif

#endif /* HP_SDSINC_H */

