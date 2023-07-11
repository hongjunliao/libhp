/*!
 * This file is Part of libhp project
 * @author: hongjun.liao<docici@126.com>
 */
#ifndef HP_ASSERT_H_
#define HP_ASSERT_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "sdsinc.h"	//sdscatfmt
#include "hp_log.h"
#include <assert.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C"{
#endif
//////////////////////////////////////////////////////////////////////////////////
/**
 * print a formated message before calling assert
 * using sdscatfmt
 */
#ifndef NDEBUG
#define hp_assert(expre, fmt, args...) do { \
		if(!(expre) && fmt) { sds hp_assert_s = sdscatfmt(sdsempty(), fmt, ##args); \
			hp_log(stderr, "%s: assert failed: %s\n", __FUNCTION__, hp_assert_s); sdsfree(hp_assert_s); assert(expre); } \
		else assert(expre); } while(0)
#else
#	define hp_assert(expre, fmt, args...) assert(expre)
#endif

/////////////////////////////////////////////////////////////////////////////////////////
/**
 * F: DIR, REG, ...
 */
#ifdef _MSC_VER
#define hp_assert_path(path, F) do{ struct stat hp_assert_path_info; \
	hp_assert(stat(path, &hp_assert_path_info) == 0 && (S_IF##F & hp_assert_path_info.st_mode)), \
			"path '%s ' NOT exist", path); } while(0)
#else
#define hp_assert_path(path, F) do{ struct stat hp_assert_path_info; \
	hp_assert(stat(path, &hp_assert_path_info) == 0 && S_IS##F(hp_assert_path_info.st_mode), \
			"path '%s' NOT exist", path); } while(0)
#endif


//////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif

#endif /*HP_ASSERT_H_*/
