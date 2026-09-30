/*!
* This file is PART of libhp project
* @author hongjun.liao <docici@126.com>, @date 2017/9/11
*
* log
* */
#include "hp_config.h"

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

sds hp_log_hdr()
{
	sds buf = sdsempty();
	buf = sdsMakeRoomFor(buf, 512);

#ifndef _MSC_VER
	struct timeval tv;
	gettimeofday(&tv, NULL);
	pid_t pid = getpid();

	int off1 = strftime(buf, sdsavail(buf), "[%Y-%m-%d %H:%M:%S",
			localtime(&tv.tv_sec));
	int off2 = snprintf(buf + off1, sdsavail(buf) - off1, ".%03d]/%05d ",
			(int) tv.tv_usec / 1000, pid);

	sdsIncrLen(buf, off1 + off2);
#else
	time_t t = time(0);
	int pid = (int)GetCurrentThreadId();

	int off1 = strftime(buf, sdsavail(buf), "[%Y-%m-%d %H:%M:%S", localtime(&t));
	int off2 = _snprintf(buf + off1, sdsavail(buf) - off1, ".%03d]/%05d ",
		(int)GetTickCount() % 1000, pid);

	sdsIncrLen(buf, off1 + off2);
#endif /* _MSC_VER */

	return buf;
}

void hp_log(void * f, char const * fmt, ...)
{
	FILE * fp = (FILE *)f;
	int color = (fileno(fp) == fileno(stderr) ? 31 : 0); /* 31 for red, 0 for default */
#ifdef _MSC_VER
	if (fp == stderr)
		fp = stdout;
#endif /* XHCHAT_NO_STDERR */

	if (!(fp && fmt)) return;

	sds buf = hp_log_hdr();
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

#endif //LIBHP_WITH_ZLOG

//TODO: remove this
void _serverAssert(int a, int b, char * c, int d) {  }
/////////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
int test_hp_log_main(int argc, char ** argv)
{
	int rc;
	hp_log(stdout, "%s: hello, hp_log\n", __FUNCTION__);

	hp_log(stdout, "0 s, 0 char *\n");
	hp_log(stdout, "0 s, 1 char *\n", __FUNCTION__);
	hp_log(stdout, "1 s, 0 char *: '%s'\n");
	hp_log(stdout, "1 s, 1 char *: '%s'\n", __FUNCTION__);

	hp_log(stdout, "0 s, 0 string\n");
	// hp_log(stdout, "0 s, 1 string\n", std::string("hello"));
	hp_log(stdout, "1 s, 0 string: '%s'\n");
	// hp_log(stdout, "1 s, 1 string: '%s'\n", std::string("hello"));


	// hp_log(stdout, "2 s, 0 string: '%s' '%s'\n");
	// hp_log(stdout, "2 s, 1 string: '%s' '%s'\n", std::string("hello"));
	// hp_log(stdout, "0 s, 2 string\n", std::string("hello"), std::string("world"));
	// hp_log(stdout, "1 s, 2 string: '%s'\n", std::string("hello"), std::string("world"));
	// hp_log(stdout, "2 s, 2 string: '%s' '%s'\n", std::string("hello"), std::string("world"));
	//
	// hp_log(stdout, "2 s, 1 string, 1 int: '%s' '%s'\n", std::string("hello"), (int)5);
	// hp_log(stdout, "2 d, 1 string, 1 int: '%d' '%d'\n", std::string("hello"), (int)5);

	hp_log(stdout, "");
	// hp_log(stdout, "%");
	hp_log(stdout, "%%");
	// hp_log(stdout, "%%%");
	hp_log(stdout, "%%%%");
	hp_log(stdout, "\n");

	// hp_log(stdout, "%%'%s'%%'%s'%%\n", "hello", std::string("world") );
	// hp_log(stdout, "'%s'%%%'%s'\n", "hello", std::string("world") );

	hp_log(stdout, "%%p=%p\n", &rc);
	return 0;
}
#endif
