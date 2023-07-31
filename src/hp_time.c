/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2023/7/31
 *
 * time on Win32
 * */
/////////////////////////////////////////////////////////////////////////////////////////

#ifdef _MSC_VER
#include "hp/hp_time.h"
#include <windows.h>
#include <stdint.h>
int gettimeofday(struct timeval *tv, void *tz)
{
	FILETIME ft;
	unsigned __int64 tmpres = 0;

	if (tv != NULL) {
		GetSystemTimeAsFileTime(&ft);
		tmpres |= ft.dwHighDateTime;
		tmpres <<= 32;
		tmpres |= ft.dwLowDateTime;
		tmpres /= 10;  // convert into microseconds
		tmpres -= (int64_t)11644473600000000;
		tv->tv_sec = (long)(tmpres / 1000000UL);
		tv->tv_usec = (long)(tmpres % 1000000UL);
	}
	(void)tz;
	return 0;
}
#endif /* _MSC_VER */
