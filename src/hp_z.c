/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2017/12/25
 *
 * compress/decompress using zlib
 * some source code from zlib: zlib/examples/zpipe.c
 *
 * @author hongjun.liao <docici@126.com>, @date 2018/8/21
 * 	   add hp_zc/hp_zd
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_WITH_ZLIB

#include "hp/hp_z.h"
#include "hp/hp_log.h"     /* hp_log */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "zlib.h"

/////////////////////////////////////////////////////////////////////////////////////////

#define Z_CHUNK (1024 * 128)
static unsigned char gz_out[Z_CHUNK];

int hp_zc_buf(unsigned char * out, int * olen, int z_level, char const * buf, int len)
{
	if(!(buf && len > 0 && out && olen && *olen > 0))
		return -1;

	int ret, have;
    z_stream strm;
    /* Set zalloc and zfree to Z_NULL, so that deflateInit upadates them
     to use default allocation functions.
     */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    // Set "src" as an input and "dest" as an output
    strm.next_in = (Bytef *)buf;
    strm.avail_in  = len;

    if (deflateInit(&strm, z_level) != Z_OK){
    	hp_log(stderr, "%s: deflateInit failed\n", __FUNCTION__);
    	return -1;
    }
    int offset = 0;
    do{
		strm.next_out = gz_out;
		strm.avail_out = Z_CHUNK;      // Size of the output
		ret = deflate(&strm, Z_FINISH);
		assert(ret != Z_STREAM_ERROR);  /* state not clobbered */

		have = Z_CHUNK - strm.avail_out;
		if(*olen - offset < have){
			deflateEnd(&strm);
			return 1;
		}
		memcpy(out + offset, gz_out, have);
		offset += have;

    }while(strm.avail_out == 0);

    assert(strm.avail_in == 0);     /* all input will be used */
    deflateEnd(&strm);
    if(ret != Z_STREAM_END)        /* stream will be complete */
    	return -1;

    *olen = strm.total_out;
    return 0;
}

int hp_zd_buf(unsigned char * out, int * olen, char const * buf, int len)
{
	if(!(buf && len > 0 && out && olen && *olen > 0))
		return -1;

    z_stream strm;
    int ret, have;
    /* Set zalloc and zfree to Z_NULL, so that inflateInit upadates them
     to use default allocation functions.
     */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    // Set "src" as an input and "dest" as an output
    strm.next_in = (Bytef *)buf;
    strm.avail_in = len;

    if (inflateInit(&strm) != Z_OK){
        hp_log(stderr, "%s: inflateInit failed\n", __FUNCTION__);
        return -1;
    }

    /* run inflate() on input until output buffer not full */
    int offset = 0;
    do {
		strm.next_out = gz_out;
		strm.avail_out = Z_CHUNK;      // Size of the output
        ret = inflate(&strm, Z_NO_FLUSH);
        assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
        switch (ret) {
        case Z_NEED_DICT:
//            ret = Z_DATA_ERROR;     /* and fall through */
        case Z_DATA_ERROR:
        case Z_MEM_ERROR:
        	(void)inflateEnd(&strm);
        	return -1;
        }

		have = Z_CHUNK - strm.avail_out;
		/*
		 * see hp_zc_buf
		 * */
		assert(*olen - offset >= have);
		memcpy(out + offset, gz_out, have);
		offset += have;

    } while (strm.avail_out == 0);

    /* clean up and return */
    (void)inflateEnd(&strm);

    if(ret != Z_STREAM_END)
    	return -1;

    *olen = strm.total_out;

    return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////
/* test */
#ifndef NDEBUG
#include "errno.h"

int test_hp_z_main(int argc, char ** argv)
{
	{
		char buf[] = "hello";
		int N = strlen(buf);
		char out1[256], out2[256];
		int olen1 = sizeof(out1);
		int olen2 = sizeof(out2);

		int rc = hp_zc_buf((unsigned char *)out1, &olen1, 2, buf, N);
		assert(rc == 0 || rc == 1);
		if(rc == 0){
			rc = hp_zd_buf((unsigned char *)out2, &olen2, out1, olen1);
			assert(rc == 0);
			assert(N == olen2);
			assert(memcmp(out2, buf, olen2) == 0);
		}

	}
	{
		char msg[2048];
		char out1[2048], out2[2048] = "hello";
		int olen1 = sizeof(out1), olen2 = sizeof(out2);

		int rc = hp_zc_buf((unsigned char *)out1, &olen1, 2, msg, sizeof(msg));
		assert(rc == 0 || rc == 1);

		if(rc == 0){
			rc = hp_zd_buf((unsigned char *)out2, &olen2, out1, olen1);
			assert(rc == 0);

			assert(sizeof(msg) == olen2);
			assert(memcmp(out2, msg, olen2) == 0);
		}
	}

	return 0;
}

/* sample: z --z-infile./src/hp_zk.c --z-outfile/tmp/hp_zk.c */
char const * help_pm_z()
{
	return "--z-infile=STRING --z-outfile=STRING";
}

#endif  /* NDEBUG */

#endif /* LIBHP_WITH_ZLIB */
