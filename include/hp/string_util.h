/*!
 * This file is Part of libhp project
 * @author: hongjun.liao<docici@126.com>
 */
#ifndef HP_STRING_UTIL_H_
#define HP_STRING_UTIL_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include "sdsinc.h"

#ifdef __cplusplus
extern "C"{
#endif
//////////////////////////////////////////////////////////////////////////////////
/* a string [beg, end) */
typedef struct {
	char * beg;
	char * end;
}str_t;

#define str_t_is_null(str) (!s.beg && !s.end)
#define str_t_printable(str) (std::string(str.beg, str.end).c_str())

int str_t_fprint(str_t const * s, FILE * f);

//////////////////////////////////////////////////////////////////////////////////

#if (defined __GNUC__) && !(defined __CYGWIN__)
char *strlwr(char *s);
/*to upper string*/
char *strupr(char *s);
#endif	/*(defined __GNUC__) && !(defined __CYGWIN__)*//*to lower string*/

//////////////////////////////////////////////////////////////////////////////////

/*string_util.cpp*/
/*_r version is thread-safe*/

/* checksum.cpp */
/*@param buff[33] at least(include NULL)*/
char * md5sum_r(char const * str, int len, char * buff);
/*@param buff[41] at least(include NULL)*/
char * sha1sum_r(char const * str, int len, char * buff);

/* @param f, file to calculate md5 */
char * md5sum_file_r(char const * f, char * buff);

//////////////////////////////////////////////////////////////////////////////////
char const * byte_to_mb_kb_str(size_t bytes, char const * fmt);
char * byte_to_mb_kb_str_r(size_t bytes, char const * fmt, char * buff);

/* like strrchr except that @param buf endwith '\0' NOT requried */
char const * strnrchr(char const * buf, int sz, char ch);

/* just like std::atoi */
int myatoi(char const * str, size_t len);

/* sort using qsort and strcmp */
void strutil_qsort(char ** strs, int size);
/*
 * find the maximum vlaue less than @param str
 * */
char const * strutil_findmax(char const * str, char ** strs, int size);

char * str_upper(char *str);
/*
 * NOTE: free @return after used
 * @return: new string
 * */
char *hp_str_replace(char *search , char *replace , char *subject);
/*
 * modify HTTP header
 * @param hdr: header to modify, e.g. "Host"
 * @param old: old value for this header, if not NULL, check it first
 * @param n:   new value for this header
 * */
int hp_http_modify_header(char * buf, size_t * len
		, char const * hdr
		, char const * old, char const * n);

/*
 * find @param val in string @param str
 * e.g.:
 * find 0 in string: "0,2,3" returns TRUE
 * find 4 in string: "0,2,3" returns FALSE
 * */
int hp_str_find(int val, char const * str);

/////////////////////////////////////////////////////////////////////////////////////////

/*
 * extensions for sds
 * */
sds sds_cpy_null(sds sdsstr, char const * s);
sds sds_cpylen_null(sds sdsstr, char const * s, size_t len);
void sdsfree_null(sds sdsstr);
static inline size_t sdslen_null(sds sdsstr){ return sdsstr? sdslen(sdsstr) : 0; }
sds sds_quote(sds s);
#define sdsup(s,v) s = sdscpy(s,v)

/*
 * cmp @param s1 and @param s2 first, then join ordered
 * @return: a new sds joined by 2 after ordered
 * e.g.
 * sds_key("hello", "jack") returns "hellojack", and
 * sds_key("jack", "hello") returns "hellojack" too
 * */
sds sds_key(sds s1, sds s2);

char const * hp_2str(char ** p, char const * fmt, ...);

/////////////////////////////////////////////////////////////////////////////////////////
sds hp_fread(char const *f);

int hp_strcasecmp(const void * a, const void * b);

/**
 * cmpare for version string: 2.1.3.7 2.2.1.0
 * @return: >0 for 2.2.1.0 > 2.1.3.7; =0 for 2.1.3.7=2.1.3.7; <0 for 2.1.3.7 < 2.1.3.8
 * */
int hp_vercmp(char const * ver, char const * cmp);

/**
 * return time str, e.g. 2023-7-1 12:20:34
 * if @param fmt was NULL, then use default "%Y-%m-%d %H:%M:%S"
 */
sds hp_timestr(time_t t, char const * fmt);

/////////////////////////////////////////////////////////////////////////////////////////
#ifdef _MSC_VER
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif
/////////////////////////////////////////////////////////////////////////////////////////
#ifndef NDEBUG
int test_hp_str_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif
//#endif /* _MSC_VER */

#endif /*HP_STRING_UTIL_H_*/
