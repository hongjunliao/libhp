/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2021/4/12
 *
 * hp_sock_t, the socket
 * */

#ifndef LIBHP_SOCK_T_H
#define LIBHP_SOCK_T_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

 /////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _MSC_VER
#ifdef LIBHP_WITH_WIN32_INTERROP
#include "redis/src/Win32_Interop/Win32_FDAPI.h"
typedef int hp_sock_t;
/* invalid socket */
#define hp_sock_invalid 0
#define hp_sock_close close
#define hp_sock_is_valid(fd) (((fd) > 0))
#else
#include <winsock2.h>
typedef SOCKET hp_sock_t;
#define hp_sock_invalid INVALID_SOCKET
#define hp_sock_close closesocket
#define hp_sock_is_valid(fd) (((fd) && (fd) != hp_sock_invalid))
#endif /* LIBHP_WITH_WIN32_INTERROP */
#else
typedef int hp_sock_t;
#define hp_sock_invalid (-1)
#define hp_sock_close close
#define hp_sock_is_valid(fd) ((fd) >= 0)
#endif /* _MSC_VER */

#ifdef __cplusplus
}
#endif
#endif /* LIBHP_SOCK_T_H */

