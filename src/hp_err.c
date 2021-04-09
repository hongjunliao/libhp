/*!
* This file is PART of libhp project
* @author hongjun.liao <docici@126.com>, @date 2021/4/1
*
* wrapper for errno/strerror() or WIN32 GetLastError()
* */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "hp_err.h"     /*  */

#ifdef _MSC_VER
#include <windows.h>
#else
#include <errno.h>
#endif //_MSC_VER

#include <assert.h>
#include <string.h>
#include <string.h> /* strerror_r */
#include "sdsinc.h" /* sds */
#include "hp_libc.h"/* hp_min */

#ifdef _MSC_VER
static HMODULE s_hDll = 0;
#endif //_MSC_VER

/////////////////////////////////////////////////////////////////////////////////////////

char const * hp_err(int err, hp_err_t errstr)
{
	if(errstr[0] == '\0')
		strcpy(errstr, "%s");

#ifdef _MSC_VER
	HLOCAL hlocal = NULL;   // Buffer that gets the error message string

	// Use the default system locale since we look for Windows messages.
	// Note: this MAKELANGID combination has 0 as value
	DWORD systemLocale = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);

	// Get the error code's textual description
	BOOL fOk = FormatMessage(
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS |
		FORMAT_MESSAGE_ALLOCATE_BUFFER,
		NULL, err, systemLocale,
		(PTSTR)&hlocal, 0, NULL);

	if (!fOk) {
		// Is it a network-related error?
		if (!s_hDll) {
			s_hDll = LoadLibraryEx(TEXT("netmsg.dll"), NULL,
				DONT_RESOLVE_DLL_REFERENCES);
		}
		if (s_hDll) {
			fOk = FormatMessage(
				FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS |
				FORMAT_MESSAGE_ALLOCATE_BUFFER,
				s_hDll, err, systemLocale,
				(PTSTR)&hlocal, 0, NULL);
			//FreeLibrary(hDll);
		}
	}

	if (fOk && (hlocal != NULL)) {
		sds serr = sdscatprintf(sdsempty(), errstr, (char const *)LocalLock(hlocal), sizeof(hp_err_t) - 1);
		errstr[0]='\0'; // clear current
		strncpy(errstr, serr, hp_min(sdslen(serr), sizeof(hp_err_t) - 1));
		sdsfree(serr);
		LocalFree(hlocal);
	}

#else
	hp_err_t buf = "";
	strerror_r(err, buf, sizeof(hp_err_t));
	sds serr = sdscatprintf(sdsempty(), errstr, (buf[0]=='\0'? strerror(err) : buf));
	errstr[0]='\0'; // clear current
	strncpy(errstr, serr, hp_min(sdslen(serr), sizeof(hp_err_t) - 1));

	sdsfree(serr);
#endif //_MSC_VER

	return errstr;
}

char const * hp_lerr() {
	static hp_err_t errstr = "%s";
	int err = errno;
#ifdef _MSC_VER
		err = GetLastError();
#endif
	return hp_err(err, errstr);
}
/////////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
int test_hp_err_main(int argc, char ** argv)
{
	hp_err_t errstr = "%s";
	char const * errs = 0;
	errs = hp_err(0, errstr);
	assert(errstr == errs);
	errs = hp_err(EAGAIN, errstr);
	errs = hp_lerr();

	errs = hp_lerr();

	return 0; 
}
#endif
