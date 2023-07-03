 /*!
 * This file is PART of xhhp project
 * @author hongjun.liao <docici@126.com>, @date 2020/7/9
 *
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "libhp.h"
#include "hp/hp_config.h"
/////////////////////////////////////////////////////////////////////////////////////////////
static char const * cfg(char const * id) {
	return ("test_btc.cpp/cfg");
}
hp_config_t g_conf = cfg;
int gloglevel = 9;
int main(int argc, char ** argv) { return libhp_all_tests_main(argc, argv); }
/////////////////////////////////////////////////////////////////////////////////////////
