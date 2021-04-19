/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/6/21
 *
 * statistics for I/O
 * */

#ifndef LIBHP_IOSTAT_H__
#define LIBHP_IOSTAT_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifndef _MSC_VER

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hp_iostat hp_iostat;

/* stat for fd */
struct hp_iostat{
	size_t t1, t2, t3;
	int T1, T2, T3;
	size_t bytes1, bytes2, bytes3;
	size_t bps1, bps2, bps3;  /* last I/O in bytes per T1/T2/T3s */
};

void hp_iostat_init(struct hp_iostat * stat, int T1, int T2, int T3);
void hp_iostat_update(struct hp_iostat * stat, size_t bytes);
size_t hp_iostat_bps(struct hp_iostat * stat);

#ifndef NDEBUG
int test_hp_stat_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif

#endif /* _MSC_VER */
#endif /* XHHP_IOSTAT_H__ */

