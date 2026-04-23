 /*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2020/7/12
 *
 * 2024/5/3 update
 * simple, Redis's dict - based .ini configure file system
 * */
/////////////////////////////////////////////////////////////////////////////////////////

#ifndef LIBHP_CONFIG_H__
#define LIBHP_CONFIG_H__

#ifdef HAVE_CONFIG_H
#include "hp_config.h"
#endif /* HAVE_CONFIG_H */


#ifdef __cplusplus
extern "C" {
#endif

#include "redis/src/dict.h" //dict
/////////////////////////////////////////////////////////////////////////////////////////
typedef struct hp_ini hp_ini;
/**!
 * @param user: hp_ini
 */
typedef int (*hp_ini_cb_t)(void* user, const char* section, const char* name, const char* value);

struct hp_ini {
	dict * dict;
	hp_ini_cb_t parser;
	char section[64];
};
/////////////////////////////////////////////////////////////////////////////////////////
/*!
 * @param k: #load,#set,#unset,#unload,#show
 */
char const * hp_ini_exec(hp_ini * ini, char const * k);
char const * hp_ini_execv(hp_ini * ini, char const * fmt, ...);


#ifndef NDEBUG
int test_hp_config_main(int argc, char ** argv);
#endif

#ifdef __cplusplus
}
#endif

#endif /* LIBHP_CONFIG_H__ */
