 /*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2020/7/12
 *
 * */

#ifndef LIBHP_CONFIG_H__
#define LIBHP_CONFIG_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */


#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////////
typedef char const * (* hp_config_t)(char const * id);

/////////////////////////////////////////////////////////////////////////////////////////
/**
 * default configure for all test functions
 * init it if you want to call libhp's tests
 *
 * e.g.:
 *  in test functions:
 * 	char const * mqtt_addr = hp_config_test("mqtt.addr");
 * 	...
 */
#ifndef NDEBUG
extern hp_config_t hp_config_test;
int test_hp_config_main(int argc, char ** argv);
#endif

#ifdef __cplusplus
}
#endif

#endif /* LIBHP_CONFIG_H__ */
