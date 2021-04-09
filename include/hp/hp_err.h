/*!
* This file is PART of libhp project
* @author hongjun.liao <docici@126.com>, @date 2021/4/1
*
* wrapper for errno/strerror() or WIN32 GetLastError()
* */

#ifndef LIBHP_ERR_H__
#define LIBHP_ERR_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "libhp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef char(hp_err_t)[128];
/*
 * @param errstr, a printf-style format str, e.g.: "error:%s"
 * @return: @param errstr
 */
char const * hp_err(int err, hp_err_t errstr);
/* 
 * @brief: err = GetLastError()
 * @return local static hp_err_t
 */
char const * hp_lerr();

#ifndef NDEBUG
LIBHP_EXT int test_hp_err_main(int argc, char ** argv);
#endif //NDEBUG

#ifdef __cplusplus
}
#endif
#endif /* LIBHP_ERR_H__ */
