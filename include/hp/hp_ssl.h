/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2021/4/7
 *
 * libhp SSL
 * */

#ifndef LIBHP_HP_SSL_H
#define LIBHP_HP_SSL_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_WITH_SSL
#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////////
int hp_ssl_rsa128(char const * pubkey, char const * msg, char ** outbuf);
char * hp_ssl_base64(const unsigned char *input, int length);

#ifndef NDEBUG
int test_hp_ssl_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif

#endif /* LIBHP_WITH_SSL */
#endif /* LIBHP_HP_SSL_H */

