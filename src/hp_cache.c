/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/4/9
 *
 * cache system
 *
 * FIXME: add timeout logic
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifndef _MSC_VER

#include "hp_log.h"     /* hp_log */
#include "str_dump.h"   /* dumpstr */
#include "hp_epoll.h"    /*  */
#include "hp_cache.h"   /*  */
#include <sys/stat.h>	/*fstat*/
#include <stdlib.h> 	/* calloc */
#include <string.h>     /* memset, ... */
#include <errno.h>      /* errno */
#include <assert.h>     /* define NDEBUG to disable assertion */
#include <time.h>

//#include <search.h>     /* hsearch_r,hsearch_data, ... */
#include "sds/sds.h"

/////////////////////////////////////////////////////////////////////////////////////
extern int gloglevel;

#define ENTRY_MAX         10
struct htab_entry {
	char   key[64];
	void * data;
};

struct hp_cache {
	int                  flag;
	struct htab_entry    htab[ENTRY_MAX];
};

static struct hp_cache ghpcache = { 0 };

/////////////////////////////////////////////////////////////////////////////////////
struct hp_cache_entry * hp_cache_entry_alloc(size_t bufsize)
{
	struct hp_cache_entry * entp = (struct hp_cache_entry *)malloc(sizeof(struct hp_cache_entry));
	entp->buf = malloc(bufsize);
	return entp;
}

void hp_cache_entry_free(struct hp_cache_entry * entp)
{
	free(entp->buf);
	free(entp);
}

int hp_cache_get(char const * key, char const * dir, struct hp_cache_entry ** entpp)
{
	int i, ret = 0;

	if(!(key && key[0] != '\0' && dir && entpp))
		return -3;

	struct stat fs;
	char path[HPCACHE_KEYMAX] = "";
	snprintf(path, sizeof(path), "%s%s", dir, key);

	if(stat(path, &fs) < 0){
		if(gloglevel > 5)
			hp_log(stderr, "%s: stat('%s') failed, errno=%d, error='%s'\n"
				, __FUNCTION__, path, errno, strerror(errno));
		return 1;
	}

	struct hp_cache_entry * entp = 0;

	int old = -1, new_ = -1;;
	for(i = 0; i < ENTRY_MAX; ++i){
		struct htab_entry * htab = ghpcache.htab + i;
		if(htab->key[0] == '\0'){
			if(new_ < 0)
				new_ = i;
			continue;
		}

		if(strcasecmp(key, htab->key) == 0){
			entp = *entpp = (struct hp_cache_entry *)htab->data;
			if(fs.st_mtim.tv_sec == entp->chksum)
				return 0;

			if(fs.st_size != entp->size)
				entp->buf = realloc(entp->buf, fs.st_size);

			old = i;
			break;
		}
	}
	if(!entp)
		entp = *entpp = hp_cache_entry_alloc(fs.st_size);

	FILE * f = fopen(path, "r");
	if(!f){
		hp_log(stderr, "%s: fopen('%s') failed, errno=%d, error='%s'\n"
				, __FUNCTION__, path, errno, strerror(errno));
		ret = -5;
		goto failed;
	}

	entp->buf[0] = '\0';
	entp->size = fs.st_size;
	entp->chksum = fs.st_mtim.tv_sec;

	size_t size = fread(entp->buf, sizeof(char), entp->size, f);
	fclose(f);

	if(size != entp->size){
		ret = -6;
		goto failed;
	}

	if(old >= 0)
		ghpcache.htab[old].data = entp;
	else if(new_ >= 0){
		strncpy(ghpcache.htab[new_].key, key, sizeof(ghpcache.htab[new_].key));
		ghpcache.htab[new_].data = entp;
	}
	else{
		hp_log(stderr, "%s: table full, max=%d\n", __FUNCTION__, ENTRY_MAX);
		ret = -8;
		goto failed;
	}

	return 0;
failed:
	hp_cache_entry_free(entp);
	return ret;
}

/////////////////////////////////////////////////////////////////////////////////////////
/* tests */
#ifndef NDEBUG
#include <assert.h>

int test_hp_cache_main(int argc, char ** argv)
{
	int rc;
	struct hp_cache_entry * ent = 0;
	rc = hp_cache_get("ls", "/bin/", &ent);
	assert(rc == 0 && ent);
	rc = hp_cache_get("/webadm/this_file_NOT_exist", ".", &ent);
	assert(rc != 0);

	return 0;
}

#endif /* NDEBUG */
#endif /*_MSC_VER*/
