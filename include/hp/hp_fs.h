/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2021/4/23
 *
 * filesystem util
 * */

#ifndef LIBHP_FS_H__
#define LIBHP_FS_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "uv.h"

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////////

#ifndef _MSC_VER
/* fsutil */
void hp_fsutil_mkdir_p(const char *dir);
int hp_fsutil_rm_r(const char *path);
int hp_fsutil_chown_R(const char *dir, char const * ug);
#endif /* _MSC_VER */

/**!
 * loop dir @param dirstr and for each file, call @param fn and pass the
 * filename @fname
 *
 * */
int hp_foreach_file(uv_loop_t * loop,
		char const * dirstr, char const * posfix, int (* fn)(char const * fname));

/////////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
int test_hp_fs_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif

#endif /* LIBHP_FS_H__ */
