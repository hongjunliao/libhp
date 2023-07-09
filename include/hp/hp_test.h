 /*!
 * This file is PART of libxhhp project
 * @author hongjun.liao <docici@126.com>, @date 2020/08/18
 *
 * utility for test
 * */

#ifndef HP_TEST_H__
#define HP_TEST_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "libhp.h"

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////////
/**
 * @brief: find and call functions like:
 *     int (* test)(int argc, char *argv[])
 * usually for running tests
 * 
 * @param flags: 
 * 	1: stop running left tests if error occurred
 * e.g.:
 * rc = hp_test("fn1,fn2", argc, argv, 0, 0);
 */
int hp_test(char const * test, int argc, char ** argv, int (* all)(int argc, char *argv[]), int flags);

//int hp_test_prepare(char const  * dirs[], char const * files[]);

#ifndef NDEBUG
LIBHP_EXT int test_hp_test_main(int argc, char ** argv);
#endif //NDEBUG

#ifdef __cplusplus
}
#endif

#endif /* HP_TEST_H__ */
