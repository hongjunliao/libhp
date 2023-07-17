/*!
* This file is PART of libhp project
* @author hongjun.liao <docici@126.com>, @date 2017/9/11
*
* log
* */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "hp/hp_log.h"     /* hp_log */
#include <stdio.h>
#include <stdarg.h>     /* va_list, ... */
#include <time.h>       /* time.h */
#ifndef _MSC_VER
#include <unistd.h>     /* getpid, isatty */
#include <sys/time.h>   /* gettimeofday */
#include "hp/sdsinc.h"        /* sds */
#else
#include <windows.h>
#include <io.h>         /* isatty */
#include <sysinfoapi.h> /* GetTickCount */
#include <processthreadsapi.h> /* GetCurrentThreadId */
#include "hp/sdsinc.h"		/* sds */
#endif /* _MSC_VER */

#ifdef LIBHP_WITH_ZLOG
#include <stdarg.h> /* for va_list */
#include "zlog.h"
#endif
/////////////////////////////////////////////////////////////////////////////////////////
int hp_log_level =
#ifndef NDEBUG
		0;
#else
		9;
#endif
/////////////////////////////////////////////////////////////////////////////////////////
#ifndef LIBHP_WITH_ZLOG

void hp_log(void * f, char const * fmt, ...)
{
	FILE * fp = (FILE *)f;
	int color = (fileno(fp) == fileno(stderr) ? 31 : 0); /* 31 for red, 0 for default */
#ifdef _MSC_VER
	if (fp == stderr)
		fp = stdout;
#endif /* XHCHAT_NO_STDERR */

	if (!(fp && fmt)) return;

	sds buf = sdsempty();
	buf = sdsMakeRoomFor(buf, 512);

#ifndef _MSC_VER
	struct timeval tv;
	gettimeofday(&tv, NULL);
	pid_t pid = getpid();

	int off1 = strftime(buf, sdsavail(buf), "[%Y-%m-%d %H:%M:%S", localtime(&tv.tv_sec));
	int off2 = snprintf(buf + off1, sdsavail(buf) - off1, ".%03d]/%d ",
		(int)tv.tv_usec / 1000, pid);

	sdsIncrLen(buf, off1 + off2);
#else
	time_t t = time(0);
	int pid = (int)GetCurrentThreadId();

	int off1 = strftime(buf, sdsavail(buf), "[%Y-%m-%d %H:%M:%S", localtime(&t));
	int off2 = _snprintf(buf + off1, sdsavail(buf) - off1, ".%03d]/%d ",
		(int)GetTickCount() % 1000, pid);

	sdsIncrLen(buf, off1 + off2);
#endif /* _MSC_VER */

	va_list ap;
	va_start(ap, fmt);
	buf = sdscatvprintf(buf, fmt, ap);
	va_end(ap);

#ifndef _MSC_VER
	if (isatty(fileno(fp)) && color != 0)
		fprintf(fp, "\e[%dm%s\e[0m", color, buf);
	else fputs(buf, fp);
#else
	fputs(buf, fp);
#endif /* _MSC_VER */

	fflush(fp);
	sdsfree(buf);
}

#else

void hp_log(void * f, char const * fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if(f == stderr) { vdzlog_error(fmt, ap); }
	else 		    { vdzlog_debug(fmt, ap); } 
	va_end(ap);
}

#endif

//TODO: remove this
void _serverAssert(int a, int b, char *, int d) {  }
/////////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
int test_hp_log_main(int argc, char ** argv)
{
	hp_log(stdout, "%s: hello, hp_log\n", __FUNCTION__);
	return 0;
}
#endif
