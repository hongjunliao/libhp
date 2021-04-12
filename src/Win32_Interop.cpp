/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2021/4/9
 *
 * networking I/O, using epoll on *nix and IOCP on Win32
 * */

#ifdef _MSC_VER

#include "redis/src/Win32_Interop/win32_types.h"

#include "redis/src/Win32_Interop/Win32_FDAPI.h"
#include "redis/src/Win32_Interop/win32_rfdmap.h"
#include <exception>
#include <mswsock.h>
#include <sys/stat.h>
#include "redis/src/Win32_Interop/Win32_fdapi_crt.h"
#include "redis/src/Win32_Interop/Win32_variadicFunctor.h"
#include "redis/src/Win32_Interop/Win32_ANSI.h"
#include "redis/src/Win32_Interop/Win32_RedisLog.h"
#include "redis/src/Win32_Interop/Win32_Common.h"
#include "redis/src/Win32_Interop/Win32_Error.h"
#include "redis/src/Win32_Interop/Win32_Assert.h"

using namespace std;

#define CATCH_AND_REPORT()  catch(const std::exception &){::redisLog(REDIS_WARNING, "FDAPI: std exception");}catch(...){::redisLog(REDIS_WARNING, "FDAPI: other exception");}

extern "C" {
	SOCKET FDAPI_get_ossocket(int fd);
}

SOCKET FDAPI_get_ossocket(int fd)
{
	try {
		SOCKET socket = RFDMap::getInstance().lookupSocket(fd);
		if (socket == INVALID_SOCKET) {
			errno = FDAPI_WSAGetLastError();
		}
		return socket;
	} CATCH_AND_REPORT();

	errno = EBADF;
	return INVALID_SOCKET;
}

#endif /* _MSC_VER */
