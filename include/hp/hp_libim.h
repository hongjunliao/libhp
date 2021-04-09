/*!
 * This file is PART of libim project
 * @author hongjun.liao <docici@126.com>, @date 2019/1/3
 *
 * libim protocol impl
 * */

#ifndef LIBHP_LIBIM_H__
#define LIBHP_LIBIM_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifndef _MSC_VER
#include "sds/sds.h"    /* sds */
#include <hiredis/async.h>
#include "klist.h"
#include "hp_curl.h"    /* hp_curlm */
#include "hp_io.h"      /* hp_eti,... */
#include "hp_epoll.h"   /* hp_epoll */
#include "hp_timerfd.h" /* hp_timerfd */
#include "redis/src/adlist.h"	/* list */

/////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////////
/**!
 * message header, defined by user
 *
 * 1.new from libim_proto.unpack
 * 2.freed by libim_proto.dispatch
 *   */
typedef union libimhdr libimhdr;


typedef struct libim_cli libim_cli;
typedef struct libim_proto libim_proto;
typedef struct libim_ctx libim_ctx;

/**/
struct libim_cli {
	libim_ctx * 	ctx;
	struct hp_eti 	eti; /*  */
	struct hp_eto 	eto; /* */
	struct hp_epolld epolld;

	int flags;
	int fd;		/*   */
	sds sid; 	/* session id for this client, currently is UUID */
	int id;

	struct list_head list;
};

int libim_cli_init(libim_cli * client
		, int fd, libim_ctx * ctx);
void libim_cli_uninit(libim_cli * client);

/////////////////////////////////////////////////////////////////////////////////////////

struct libim_proto{
	/* callback when new connection coming */
	libim_cli * (* new)(int fd, libim_ctx * ctx);

	/* callback when data coming */
	size_t (* unpack)(int magic, char * buf, size_t * len, int flags
			, libimhdr ** hdrp, char ** bodyp);

	/* callback when new message coming */
	int (* dispatch)(libim_cli * client, libimhdr * imhdr, char * body);
	/**
	 * callback in event loop, 
	 * this function will be hi-frequently called 
	 * DO NOT block TOO LONG
	 * @return: return <0 to ask to close the client
	*/
	int (* loop)(libim_cli * client);
	/* callback when disconnect */
	void (* delete)(libim_cli * cli);
	int flags;
};

struct libim_ctx {
	hp_epoll * efds;
	hp_epolld  ed;

	int tcp_keepalive;
	int cid;

	struct list_head cli;
	int n_cli;

	libim_proto proto;
	unsigned int magic;
};

/////////////////////////////////////////////////////////////////////////////////////////

libim_proto libim_default_proto_get();

int xhmdm_libim_init(libim_ctx * ctx, hp_epoll * efds
		, int fd, int tcp_keepalive
		, libim_proto proto
		, unsigned int magic);

libim_cli * xhmdm_libim_find_if(libim_ctx * ctx, void * key,
		int (*cmp)(libim_cli * node, void * key));

void xhmdm_libim_uninit(libim_ctx * ctx);

/////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif
#endif /* _MSC_VER */
#endif /* LIBHP_LIBIM_H__ */
