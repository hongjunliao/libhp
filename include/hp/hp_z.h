/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/11/2
 *
 * zlib support
 * */

#ifndef LIBHP_Z_H
#define LIBHP_Z_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef __cplusplus
extern "C" {
#endif

#ifdef LIBHP_WITH_ZLIB
/* zlib support */
int hp_zc_buf(unsigned char * out, int * olen, int z_level, char const * buf, int len);
int hp_zd_buf(unsigned char * out, int * olen, char const * buf, int len);
#endif /* XHHP_NO_ZLIB */

#ifndef NDEBUG
int test_hp_z_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif /* LIBHP_WITH_ZLIB */

#endif /* LIBHP_Z_H */
