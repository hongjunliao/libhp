/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2017/8/31
 *
 * internal variables for config file
 * */

#ifndef LIBHP_VAR_H__
#define LIBHP_VAR_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_DEPRECADTED
#ifdef LIBHP_WITH_CJSON
#include "sdsinc.h"		/* sds */

/////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hp_var_map hp_var_map;

struct hp_var_map{
	char const * var;
	char const * val;
};

/////////////////////////////////////////////////////////////////////////////////////
struct HP_VAR_PKG_TOOL_ {
	char * (* replace)(char const * str, char * out
			, hp_var_map const * vmap, int len);
	sds (* eval)(char const * str, char const * (* fn)(char const * key));
};
extern struct HP_VAR_PKG_TOOL_ const HP_VAR_PKG_TOOL;

#ifndef NDEBUG
int test_hp_var_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif
#endif //LIBHP_WITH_CJSON
#endif //LIBHP_DEPRECADTED
#endif /* LIBHP_VAR_H__ */

/////////////////////////////////////////////////////////////////////////////////////////
