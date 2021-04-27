
/*!
* This file is PART of libxhhp project
* @author hongjun.liao <docici@126.com>, @date 2021/4/24
*
* fix for WIN32_INTEROP
* */

#ifndef HP_WIN32_INTEROP_H
#define HP_WIN32_INTEROP_H

#include "config.h"

#ifdef _MSC_VER
#ifdef LIBHP_WITH_WIN32_INTERROP
#include "redis/src/Win32_Interop/Win32_Portability.h"
#include "redis/src/Win32_Interop/win32_types.h"
#include "redis/src/Win32_Interop/Win32_FDAPI.h"
#include "redis/src/Win32_Interop/Win32_Time.h"

#define WSARecv FDAPI_WSARecv
#define WSASend FDAPI_WSASend
#define WSAGetLastError FDAPI_WSAGetLastError
#define ioctl fcntl

#ifdef __cplusplus
extern "C"{
#endif

SOCKET FDAPI_get_ossocket(int fd);
extern int random();

#ifdef __cplusplus
}
#endif
#else
#endif /* LIBHP_WITH_WIN32_INTERROP */		
#endif /* _MSC_VER */		
#endif /* HP_WIN32_INTEROP_H */
