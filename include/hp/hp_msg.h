/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/11/2
 *
 * a simple msg parser for I/O, format:

 * the protocol between proxys: PMMSG
 * PMMSG structure: magic|mesage_id|type|session|body_length|body
 */

#ifndef LIBHP_MSG_H
#define LIBHP_MSG_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
//////////////////////////////////////////////////////////////////////////////////////
/* PMMSG header */
extern int PM_MAGIC;
#define PM_HEADER_LEN                            (sizeof(PM_MAGIC) + sizeof(int) + sizeof(int) + sizeof(int) + sizeof(int))
#define HM_BUFSZ                                 (1024 * 64 - PM_HEADER_LEN)

/* this msg is compressed by zlib */
#define PMT_Z									   (1 << 11)

typedef struct hp_msg hp_msg;

struct hp_msg {
	int                 id;            /* id */
	int                 type;          /* type */
	int                 sid;           /* session id */
	char * 				buf;           /* buffer */
	int                 len;           /* length for buf */
};

//////////////////////////////////////////////////////////////////////////////////////
/*
 * @param nbuf: <0 for set buf to NULL, 0 default size: PM_BUF_MAX, else nbuf
 * */
struct hp_msg * hp_msg_new(int nbuf);
void hp_msg_del(struct hp_msg * msg);
char * hp_msg_header(hp_msg * msg);
hp_msg * hp_msg_dup(hp_msg * msg);
/*
 * skip unrecognized data and continue read and parse?
 * if NOT set, return as parse error
 *  */
#define PMRW_NOSKIP    (1 << 0)

size_t hp_pm_pack(char * buf, size_t * buflenp, int flags, struct hp_msg ** msgp);

#ifndef NDEBUG
int test_hp_msg_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif
#endif /* LIBHP_MSG_H */
