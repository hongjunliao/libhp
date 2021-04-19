/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/6/21
 *
 * statistics for I/O
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifndef _MSC_VER

#include "hp_iostat.h"
#include <unistd.h>      /* close */
#include <sys/time.h>    /* gettimeofday */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>      /* memset */
#include <time.h>
#include <assert.h>

void hp_iostat_init(struct hp_iostat * stat, int T1, int T2, int T3)
{
	if(!stat)
		return;
	memset(stat, 0, sizeof(struct hp_iostat));
	struct timeval tv;
	gettimeofday(&tv, 0);
	stat->t1 = stat->t2 = stat->t3 =
			tv.tv_sec * 1000 + tv.tv_usec / 1000.0;
	stat->T1 = T1;
	stat->T2 = T2;
	stat->T3 = T3;
}

void hp_iostat_update(struct hp_iostat * stat, size_t bytes)
{
	if(!stat) return;

	struct timeval tv;
	gettimeofday(&tv, 0);
	size_t nowt = tv.tv_sec * 1000 + tv.tv_usec / 1000.0;

	stat->bytes1 += bytes;
	stat->bps1 = (stat->bytes1) * 1000.0 / (nowt - stat->t1);
	if(nowt - stat->t1 > stat->T1){
		stat->bytes1 = 0;
		stat->t1 = nowt;
	}

	stat->bytes2 += bytes;
	stat->bps2 = (stat->bytes2) * 1000.0 / (nowt - stat->t2);
	if(nowt - stat->t2 > stat->T2){
		stat->bytes2 = 0;
		stat->t2 = nowt;
	}

	stat->bytes3 += bytes;
	stat->bps3 = (stat->bytes3) * 1000.0 / (nowt - stat->t3);
	if(nowt - stat->t3 > stat->T3){
		stat->bytes3 = 0;
		stat->t3 = nowt;
	}
}

size_t hp_iostat_bps(struct hp_iostat * stat)
{
	if(!stat) return 0;
//	return stat->bps2 * 0.5 + stat->bps3 * 0.3 + stat->bps1 * 0.2;
	return stat->bps2;
}

/////////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
#include <assert.h>

int test_hp_stat_main(int argc, char ** argv)
{
	srandom(time(0));

	struct timeval tv;
	gettimeofday(&tv, 0);
	size_t nowt = tv.tv_sec * 1000 + tv.tv_usec / 1000.0;

	struct hp_iostat stat;
	hp_iostat_init(&stat, 1000 * 2, 1000 * 10, 1000 * 30);

	FILE * f = fopen("/tmp/xhhp-bps.txt", "w");
	assert(f);

	int i;
	for(i = 0; i < 2;){

		size_t bytes = 1 + random() % (1024 * 20 - 1);
		int ms = 1 + random() % 999;
		usleep(ms * 100);

		hp_iostat_update(&stat, bytes);

		gettimeofday(&tv, 0);
		size_t tmpt = tv.tv_sec * 1000 + tv.tv_usec / 1000.0;
		if((tmpt - nowt) / 1000.0 >= 1){
			++i;
			nowt = tmpt;

			fprintf(f, "%d, %.1f, %.1f, %.1f, %.1f\n"
					, i
					, stat.bps1 / 1024.0, stat.bps2 / 1024.0, stat.bps3 / 1024.0, hp_iostat_bps(&stat) / 1024.0);

			fprintf(stdout, "%s: bytes=%.1f, ibps2=%.1f, ibps5=%.1f, ibps10=%.1f, ibps=%.1f            \r"
					, __FUNCTION__
					, bytes / 1024.0
					, stat.bps1 / 1024.0, stat.bps2 / 1024.0, stat.bps3 / 1024.0, hp_iostat_bps(&stat) / 1024.0);
		}
	}
	fclose(f);
	fprintf(stdout, "\n");

	return 0;
}
#endif /* NDEBUG */

#endif /* _MSC_VER */
