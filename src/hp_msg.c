
/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2017/9/11
 *
 * a simple msg struct, see sds implementation
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifndef WIN32
#include <netinet/in.h>  /* ntohl */
#else
#include "redis/src/Win32_Interop/Win32_Portability.h"
#include "redis/src/Win32_Interop/win32_types.h"
#endif /* WIN32 */


#include "hp_libc.h"
#include "hp_msg.h"
#include "hp_z.h"
#include "hp_log.h"
#include "str_dump.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#ifdef _MSC_VER
#define snprintf _snprintf
#endif /* _MSC_VER */
/////////////////////////////////////////////////////////////////////////////////////
/*
 * host(Linux): 0x32603723
 * net:         0x23376032
 * */
int PM_MAGIC = 0x32603723;

struct hp_msg * hp_msg_new(int nbuf)
{
	struct hp_msg * ret = (struct hp_msg * )calloc(1, sizeof(struct hp_msg));
	int mlen = (nbuf < 0? 0 : ((nbuf == 0? (int)HM_BUFSZ : nbuf)));
	ret->buf = (char *)malloc(mlen);
	return ret;
}

hp_msg * hp_msg_dup(hp_msg * m)
{
	if(!m) return 0;
	hp_msg * msg = hp_msg_new(m->len);
	msg->id = m->id;
	msg->type = m->type;
	msg->sid = m->sid;
	memcpy(msg->buf, m->buf, m->len);
	msg->len = m->len;

	return msg;
}

void hp_msg_del(struct hp_msg* msg)
{
	if(!msg) return;
	free(msg->buf);
	free(msg);
}

char * hp_msg_header(hp_msg * msg)
{
	if(!msg)
		return 0;

	char * hdr = malloc(PM_HEADER_LEN);
	/* net-byte-order */
	int ntype = (int)htonl((uint32_t)msg->type)
		, nid = (int)htonl((uint32_t)msg->id)
		, nsid = (int)htonl((uint32_t)msg->sid)
		, nlen = (int)htonl((uint32_t)msg->len);

	memcpy(hdr + 0,                                  &PM_MAGIC, sizeof(PM_MAGIC));
	memcpy(hdr + sizeof(PM_MAGIC),                   &nid, sizeof(int));
	memcpy(hdr + sizeof(PM_MAGIC) + sizeof(int),     &ntype, sizeof(int));
	memcpy(hdr + sizeof(PM_MAGIC) + sizeof(int) * 2, &nsid, sizeof(int));
	memcpy(hdr + sizeof(PM_MAGIC) + sizeof(int) * 3, &nlen, sizeof(int));

	return hdr;
}


/////////////////////////////////////////////////////////////////////////////////////
#define PM_ID(hdr)           ((int)ntohl((uint32_t)(*(int *)((char *)hdr + sizeof(PM_MAGIC)))))
#define PM_TYPE(hdr)         ((int)ntohl((uint32_t)(*(int *)((char *)hdr + sizeof(int) + sizeof(PM_MAGIC)))))
#define PM_SESSION(hdr)      ((int)ntohl((uint32_t)(*((int *)((char *)hdr+ sizeof(int) + sizeof(PM_MAGIC) + sizeof(int))))))
#define PM_BODY_LEN(hdr)     ((int)ntohl((uint32_t)(*(int *)((char *)hdr + sizeof(int) + sizeof(PM_MAGIC) + sizeof(int) + sizeof(int)))))
#define PM_BODY(hdr)         ((char *)hdr + PM_HEADER_LEN)

size_t hp_pm_pack(char * buf, size_t * buflenp, int flags, struct hp_msg ** msgp)
{
	if(!(buf && buflenp && msgp))
		return -1;

	*msgp = 0;
	char errstr[128];

	for(;;){
		if(*buflenp < PM_HEADER_LEN)
			return 0;

		/* now parse the msg from buffer
		 * magic first
		 *  */
		if(!(memcmp(buf, &PM_MAGIC, sizeof(PM_MAGIC)) == 0)){
			if((flags & PMRW_NOSKIP)){
				snprintf(errstr, sizeof(errstr), "PM_MAGIC mismatch, PMRW_NOSKIP set, len=%d, data='0x%x'"
						, (int)*buflenp, *(int *)buf);
				hp_log(stderr, "%s: %s\n" , __FUNCTION__, errstr);

				memmove(buf, buf + 1, *buflenp - 1);
				*buflenp -= 1;

				return -1;
			}

			/* try to find PMMSG header */
			size_t n = 1;
			int match = 0;
			for(; n < *buflenp - sizeof(PM_MAGIC); ++n){
				if((memcmp(buf + n, &PM_MAGIC, sizeof(PM_MAGIC)) == 0)){
					match = 1;
					break;
				}
			}
			memmove(buf, buf + n, *buflenp - n);
			*buflenp -= n;

			if(match){
				hp_log(stderr, "%s: found PM_MAGIC, lenr=%zu, skip_len=%d\n"
					, __FUNCTION__, *buflenp, n);
			}
			continue; /* rw->lenr changed, need check again */
		}

		/* then parse left of msg */
		size_t bodylen = PM_BODY_LEN(buf);
		if((*buflenp - PM_HEADER_LEN >= bodylen)){
			struct hp_msg * msg = *msgp = hp_msg_new(bodylen);
			assert(msg);

			msg->id = PM_ID(buf);
			msg->type = PM_TYPE(buf);
			msg->sid = PM_SESSION(buf);

			int msglen = PM_HEADER_LEN + bodylen;
			int rc = msglen;
#ifdef LIBHP_WITH_ZLIB
			if(msg->type & PMT_Z){
				msg->len = sizeof(msg->buf);
				if(hp_zd_buf((unsigned char *)msg->buf, &msg->len, PM_BODY(buf), bodylen) != 0){
					hp_log(stderr, "%s: z: hp_zd_buf failed! drop, sid=%d, type=%d, len=%d, buf='%s'\n", __FUNCTION__
						, msg->sid
						, msg->type
						, bodylen
						, dumpstr(PM_BODY(buf), bodylen, 8));

					hp_msg_del(msg);
					*msgp = 0;
					rc = 0;
				}
				else  msg->type &= ~PMT_Z;
			}
			else{
				msg->len = bodylen;
				memcpy(msg->buf, PM_BODY(buf), bodylen);
			}
#else
			msg->len = bodylen;
			memcpy(msg->buf, PM_BODY(buf), bodylen);
#endif /* LIBHP_WITH_ZLIB */

			memmove(buf, buf + msglen, *buflenp - msglen);
			*buflenp -= msglen;
			return rc;
		}

		/* NOT a complete msg, need more data */
		return 0;

	} /* end of for loop */

	/* never come here */
	assert(0);
	return -1;
}

/////////////////////////////////////////////////////////////////////////////////////////
#ifndef NDEBUG
#include <string.h>        /* strcmp */
#include <assert.h>        /* assert */
#include <stdint.h>	       /* SIZE_MAX */

int test_hp_msg_main(int argc, char ** argv)
{
	hp_msg * msg = hp_msg_new(128 * 1024);
	msg->len = 127 * 1024;

	hp_msg * msg2 = hp_msg_dup(msg);

	assert(msg->id == msg2->id);
	assert(msg->type == msg2->type);
	assert(msg->sid == msg2->sid);
	assert(msg->len == msg2->len);

	assert(memcmp(msg->buf, msg2->buf, msg->len) == 0);

	hp_msg_del(msg);
	hp_msg_del(msg2);

	return 0;
}

#endif /* NDEBUG */
