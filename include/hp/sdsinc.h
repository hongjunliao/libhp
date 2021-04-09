/*!
 * This file is PART of libxhhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/12/3
 *
 * sds
 * */

#ifndef HP_SDSINC_H
#define HP_SDSINC_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _MSC_VER
#include "sds/sds.h"		/* sds */
#else
#include "redis/src/sds.h"  /* sds */
#endif /* _MSC_VER */

#ifdef __cplusplus
}
#endif

#endif /* HP_SDSINC_H */

