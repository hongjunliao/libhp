 /*!
 * This file is PART of lbhp project
 * @author hongjun.liao <docici@126.com>, @date 2021/3/31
 *
 * utility for cmdline
 * */

#ifndef HP_OPT_H__
#define HP_OPT_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_WITH_OPTPARSE

#include "libhp.h"

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////////
/**
 * @brief: cmdline to argc/argv, e.g.: " --url  index -F  -i 5 "
 * @return: argc
 */
int hp_opt_argv(char * cmdline, char * argv[]);

#ifndef NDEBUG
LIBHP_EXT int test_hp_opt_main(int argc, char ** argv);
#endif //NDEBUG

#ifdef __cplusplus
}
#endif
#endif //LIBHP_WITH_OPTPARSE
#endif /* HP_OPT_H__ */
