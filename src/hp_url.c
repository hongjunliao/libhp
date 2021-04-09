/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/4/12
 *
 * URL encode/decode
 * from https://github.com/happyfish100/libfastcommon
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_WITH_HTTP

#include "hp_url.h"
#include <string.h> 	/* strncmp */
#include "libyuarel/yuarel.h"   /* yuarel_parse_query */

#define IS_UPPER_HEX(ch) ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F'))
#define IS_HEX_CHAR(ch)  (IS_UPPER_HEX(ch) || (ch >= 'a' && ch <= 'f'))

char *urldecode_ex(const char *src, const int src_len, char *dest, int *dest_len)
{
#define HEX_VALUE(ch, value) \
	if (ch >= '0' && ch <= '9') \
	{ \
		value = ch - '0'; \
	} \
	else if (ch >= 'a' && ch <= 'f') \
	{ \
		value = ch - 'a' + 10; \
	} \
	else \
	{ \
		value = ch - 'A' + 10; \
	}

	const unsigned char *pSrc;
	const unsigned char *pEnd;
	char *pDest;
	unsigned char cHigh;
	unsigned char cLow;
	int valHigh;
	int valLow;

	pDest = dest;
	pSrc = (unsigned char *)src;
	pEnd = (unsigned char *)src + src_len;
	while (pSrc < pEnd)
	{
		if (*pSrc == '%' && pSrc + 2 < pEnd)
		{
			cHigh = *(pSrc + 1);
			cLow = *(pSrc + 2);

			if (IS_HEX_CHAR(cHigh) && IS_HEX_CHAR(cLow))
			{
				HEX_VALUE(cHigh, valHigh)
				HEX_VALUE(cLow, valLow)
				*pDest++ = (valHigh << 4) | valLow;
				pSrc += 3;
			}
			else
			{
				*pDest++ = *pSrc;
				pSrc++;
			}
		}
		else if (*pSrc == '+')
		{
			*pDest++ = ' ';
			pSrc++;
		}
		else
		{
			*pDest++ = *pSrc;
			pSrc++;
		}
	}

	*dest_len = pDest - dest;
	return dest;
}


char * hp_urldecode(const char *src, const int src_len, char *dest, int *dest_len)
{
    (void)urldecode_ex(src, src_len, dest, dest_len);
    *(dest + *dest_len) = '\0';
    return dest;
}

///*
// * 'a=a&'
// * 'a=a&b'
// * 'a=a&b='
// * 'a=a&b=b'
// * 'a=a&b=b&'
// * 'a=a&b=b&c'
// * */
//int hp_url_query_find(char const * query, int len, char const * key, char * value)
//{
//	int i;
//	int klen = strlen(key);
//
//	for(i = 0; i < len - 1; ++i){
//		if((query[i] == '&') && (len - (i + 1) >= klen) && strncmp(query + i + 1, key, klen) == 0){
//			if(query[i + klen] == '='){
//				char * p = strchr(query + i + klen, '&');
////				memncpy(value, query + i + klen + 1, )
//
//			}
//		}
//
//	}
//}

int hp_url_query(char * query, struct yuarel_param * params, int np, struct yuarel_param * q, int nq, char sep)
{
	if(!(query && params && np > 0 && q && nq > 0))
		return -1;


	int p = yuarel_parse_query(query, (sep == 0? '&' : sep), params, np);

	int n = 0;
	int i, j;
	for(i = 0; i < p; ++i){
		for(j = 0; j < nq; ++j){
			if(strcmp(params[i].key, q[j].key) == 0){
				q[j].val = params[i].val;
				++n;
			}
		}
	}
	return n;
}

/////////////////////////////////////////////////////////////////////////////////////////
/* tests */
#ifndef NDEBUG
#include <assert.h>

int test_hp_url_main(int argc, char ** argv)
{
	char query[] = "s=\"192.168.1.105:9201\"&d=\"192.168.1.105:80\"";
	struct yuarel_param params[256], q[] = { {"s", 0}, {"d", 0} };
	int n = hp_url_query(query
				, params, sizeof(params) / sizeof(params[0])
				, q, sizeof(q) / sizeof(q[0]), 0);

	assert(n == 2);
	assert(strcmp(q[0].val, "\"192.168.1.105:9201\"") == 0);
	assert(strcmp(q[1].val, "\"192.168.1.105:80\"") == 0);

	return 0;
}

#endif /* NDEBUG */
#endif
