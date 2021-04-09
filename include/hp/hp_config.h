 /*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2020/7/12
 *
 * */

#ifndef LIBHP_CONFIG_H__
#define LIBHP_CONFIG_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */


#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////////
typedef char const * (* hp_config_t)(char const * id);

/////////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG

#endif

#ifdef __cplusplus
}
#endif

#endif /* LIBHP_CONFIG_H__ */
