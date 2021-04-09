/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2019/1/3
 *
 * libim
 * */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */
#ifndef _MSC_VER

#include "hp_libim.h"
#include <unistd.h>
#include <sys/time.h>   /* gettimeofday */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>     /* assert */
#include "hp_io.h"      /* hp_eti,... */
#include "hp_log.h"
#include "hp_epoll.h"   /* hp_epoll */
#include "hp_net.h"     /* hp_net_connect */
#include "hp_cjson.h"
#include "hp_config.h"	/* hp_config_t */

extern hp_config_t g_conf;
extern int gloglevel;
/////////////////////////////////////////////////////////////////////////////////////////
/**
 * libim default protocol
 * */
static libim_cli * libim_cli_new(int confd, libim_ctx * ctx)
{
	int rc;

	libim_cli * client = calloc(1, sizeof(libim_cli));
	if(client){
		rc = libim_cli_init(client, confd, ctx);
		assert(rc == 0);
	}

	return client;
}

static void libim_cli_del(libim_cli * client)
{
	if(!(client))
		return;

	libim_cli_uninit(client);
	free(client);
}

static int libim_def_dispath(libim_cli * client, libimhdr * imhdr, char * body) { return 0; }

static libim_proto const libim_defproto = {
	.new = libim_cli_new,
	.delete = libim_cli_del,
	.dispatch = libim_def_dispath,
};

libim_proto libim_default_proto_get(){ return libim_defproto; }
/////////////////////////////////////////////////////////////////////////////////////////

/* tests */
#ifndef NDEBUG
#include "sds/sds.h"    /* sds */
#include "string_util.h"

int test_hp_libim_main(int argc, char ** argv)
{
	int rc;

	return 0;
}
#endif /* NDEBUG */
#endif /* _MSC_VER */
