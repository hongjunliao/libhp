/*!
* This file is PART of lbhp project
* @author hongjun.liao <docici@126.com>, @date 2021/4/1
*
* */

#ifndef LIB_HP_H__
#define LIB_HP_H__
#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef _WIN32
  /* Windows - set up dll import/export decorators. */
# if 1 //defined(BUILD_SHARED_LIBS)
    /* Building shared library. */
#   define LIBHP_EXT __declspec(dllexport)
# elif defined(USING_LIBHP_SHARED)
    /* Using shared library. */
#   define LIBHP_EXT __declspec(dllimport)
# else
    /* Building static library. */
#   define LIBHP_EXT /* nothing */
# endif
#elif __GNUC__ >= 4
# define LIBHP_EXT __attribute__((visibility("default")))
#else
# define LIBHP_EXT /* nothing */
#endif

#ifdef __cplusplus
}
#endif
#endif /* LIB_HP_H__ */
