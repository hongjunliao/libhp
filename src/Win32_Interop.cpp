/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2021/4/9
 *
 * networking I/O, using epoll on *nix and IOCP on Win32
 * */
#include "config.h"

#ifdef _MSC_VER
#ifdef LIBHP_WITH_WIN32_INTERROP
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
#include "Win32_Interop.h"
using namespace std;

#define CATCH_AND_REPORT()  catch(const std::exception &){::redisLog(REDIS_WARNING, "FDAPI: std exception");}catch(...){::redisLog(REDIS_WARNING, "FDAPI: other exception");}

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
#else
#include <Windows.h>
/* Replace MS C rtl rand which is 15bit with 32 bit */
typedef BOOLEAN(_stdcall* RtlGenRandomFunc)(void * RandomBuffer, ULONG RandomBufferLength);
static RtlGenRandomFunc RtlGenRandom = 0;

extern "C" {

int random()
{
    unsigned int x = 0;
    if (RtlGenRandom == NULL) {
        // Load proc if not loaded
        HMODULE lib = LoadLibraryA("advapi32.dll");
        RtlGenRandom = (RtlGenRandomFunc) GetProcAddress(lib, "SystemFunction036");
        if (RtlGenRandom == NULL) return 1;
    }
    RtlGenRandom(&x, sizeof(unsigned int));
    return (int) (x >> 1);
}

}

#endif
#endif /* _MSC_VER */
