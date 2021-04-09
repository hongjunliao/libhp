
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "string_util.h"
#include <search.h>        /* lfind, ... */
#include <stdio.h>
#include <math.h>          /* pow */
#include <string.h>        /* strcmp */
#include <stdlib.h>        /* qsort */
#include <ctype.h>         /* toupper */
#include <assert.h>        /* define NDEBUG to disable assertion */
#include <sys/stat.h>	/*fstat*/
#include "hp_libc.h"

int str_t_fprint(str_t const * s, FILE * f)
{
	int r = 0;
	char * c = s->beg;
	for(; c != s->end; ++c){
		if(*c == '\0')
			r = fprintf(f, "\\0");
		else if(*c == '\n')
			r = fprintf(f, "\\n");
		else
			r = fprintf(f, "%c", *c);
	}
	if(s->beg)
		fprintf(f, "\n");
	return r;
}

#if (defined __GNUC__) && !(defined __CYGWIN__)

char *strlwr(char *s)
{
	char *str = s;
	for(; *str; ++str) {
		if (*str >= 'A' && *str <= 'Z') {
			*str += ('a' - 'A');
		}
	}
	return s;
}

char *strupr(char *s)
{
	char *str = s;
	for(; *str; ++str) {
		if (*str >= 'a' && *str <= 'z') {
			*str -= ('a' - 'A');
		}
	}
	return s;
}

#endif	/*(defined __GNUC__) && !(defined __CYGWIN__)*/

/*@param unit 'K': KB, 'M':MB, 'G':GB, ' ':B*/
static double byte_to_mb_kb(size_t bytes, char * unit)
{
	if(!unit) return 0;

	if(bytes >= 1024 * 1024 && bytes < 1024 * 1024 * 1024){
		*unit = 'M';
		return bytes / (1024.0 * 1024);
	}
	else if(bytes >= 1024 * 1024 * 1024){
		*unit = 'G';
		return bytes / (1024.0 * 1024 * 1024);
	}
	else if(bytes >= 1024){
		*unit = 'K';
		return bytes / 1024.0;
	}
	else{
		*unit = ' ';
		return bytes / 1.0;
	}
}

char * byte_to_mb_kb_str_r(size_t bytes, char const * fmt, char * buff)
{
	if(!(fmt && buff))
		return 0;
	buff[0] = '\0';

	char c;
	double b = byte_to_mb_kb(bytes, &c);
	snprintf(buff, 64, fmt, b, c);
	return buff;
}
/*@param fmt: "%-.2f %cB*/
char const * byte_to_mb_kb_str(size_t bytes, char const * fmt)
{
	static char buff[64] = "";
	return byte_to_mb_kb_str_r(bytes, fmt, buff);
}


char const * strnrchr(char const * buf, int sz, char ch)
{
	char const * p = buf + sz - 1;
	for(; p != buf - 1; --p) {
		if(*p == ch)
			return p;
	}
	return 0;
}

/* FIXME: overflow */
int myatoi(char const * str, size_t len)
{
	if(!(str && str[0] != '\0' && len > 0))
		return 0;

	int f = 0;
	if(str[0] == '+' || str[0] == '-'){
		if(str[0] == '-')
			f = 1;
		++str;
		--len;
	}

	int ints[64];
	int i = 0;
	char const * p = str;
	for(; p != str + len; ++p){
		if(!(*p >= '0' && *p <= '9'))
			break;
		ints[i++] = *p - '0';
	}

	int r = 0;
	int j = 0;
	for(; j < i; ++j){
		r += ints[j] * pow(10, i - j - 1);
	}
	return f? -r : r;
}

static int compare_strs(const void* a, const void* b)
{
	return strcmp(*(const char **)a, *(const char **)b);
}

void strutil_qsort(char ** strs, int size)
{
	return qsort(strs, size, sizeof(char *), compare_strs);
}

char const * strutil_findmax(char const * str, char ** strs, int size)
{
	qsort(strs, size, sizeof(char *), compare_strs);

	int i = 0;
	for(; i < size; ++i){
		if(strncmp(strs[i], str, strlen(strs[i])) == 0 && i > 0)
				return strs[i - 1];
	}
	return 0;
}

char * str_upper(char *str)
{
	while (*str++) {
		*str = toupper(*str);
	}
	return str;
}

/////////////////////////////////////////////////////////////////////////////////////////

sds sds_cpy_null(sds sdsstr, char const * s)
{
	if(sdsstr) return sdscpy(sdsstr, s);
	else       return sdsnew(s);
}

sds sds_cpylen_null(sds sdsstr, char const * s, size_t len)
{
	if(sdsstr) return sdscpylen(sdsstr, s, len);
	else       return sdsnewlen(s, len);
}

void sdsfree_null(sds sdsstr) { if(sdsstr) sdsfree(sdsstr); }
void sdsclear_null(sds sdsstr) { if(sdsstr) sdsclear(sdsstr); }

sds sds_quote(sds s)
{
	sds ret = sdscatfmt(sdsempty(), "'%S'", s);
	sdsfree(s);
	return ret;
}

sds sds_key(sds uid1, sds uid2)
{
	assert(uid1 && uid2);

	int r = sdscmp(uid1, uid2);
	if(r > 0){
		sds tmp = uid1;
		uid1 = uid2;
		uid2 = tmp;
	}
	sds keystr = sdscatfmt(sdsempty(), "%S%S", uid1, uid2);

	return keystr;
}
/////////////////////////////////////////////////////////////////////////////////////////

/*
 * code from https://www.binarytides.com/hp_str_replace-for-c/
 *
 * Search and replace a string with another string , in a string
 * */
char *hp_str_replace(char *search , char *replace , char *subject)
{
    char  *p = NULL , *old = NULL , *new_subject = NULL ;
    int c = 0 , search_size;

    search_size = strlen(search);

    //Count how many occurences
    for(p = strstr(subject , search) ; p != NULL ; p = strstr(p + search_size , search))
    {
        c++;
    }

    //Final size
    c = ( strlen(replace) - search_size )*c + strlen(subject);

    //New subject with new size
    new_subject = malloc( c );

    //Set it to blank
    strcpy(new_subject , "");

    //The start position
    old = subject;

    for(p = strstr(subject , search) ; p != NULL ; p = strstr(p + search_size , search))
    {
        //move ahead and copy some text from original subject , from a certain position
        strncpy(new_subject + strlen(new_subject) , old , p - old);

        //move ahead and copy the replacement text
        strcpy(new_subject + strlen(new_subject) , replace);

        //The new start position after this search match
        old = p + search_size;
    }

    //Copy the part after the last search match
    strcpy(new_subject + strlen(new_subject) , old);

    return new_subject;
}

/*
 * code from https://github.com/stephenmathieson/str-replace.c/blob/master/src/str-replace.c
 * Replace all occurrences of `sub` with `replace` in `str`
 */
//
//char * hp_str_replace_(const char *str, const char *sub, const char *replace)
//{
//	char *pos = (char *) str;
//	int count = occurrences(sub, str);
//
//	if (0 >= count)
//		return strdup(str);
//
//	int size = (strlen(str) - (strlen(sub) * count) + strlen(replace) * count)
//			+ 1;
//
//	char *result = (char *) malloc(size);
//	if (NULL == result)
//		return NULL;
//	memset(result, '\0', size);
//	char *current;
//	while ((current = strstr(pos, sub))) {
//		int len = current - pos;
//		strncat(result, pos, len);
//		strncat(result, replace, strlen(replace));
//		pos = current + strlen(sub);
//	}
//
//	if (pos != (str + strlen(str))) {
//		strncat(result, pos, (str - pos));
//	}
//
//	return result;
//}

/*
 * "XXXX\r\nHost: 192.168.1.106:80\r\nXXXX";
 * */
int hp_http_modify_header(char * buf, size_t * len
		, char const * hdr
		, char const * nvalue, char const * ovalue)
{
	if(!(buf && len && hdr)) return -1;

	size_t i;
	int nhdr = strlen(hdr);
	for(i = 0; i < *len - nhdr; ++i){
		/* find header first */
		if(strncasecmp(buf + i, hdr, nhdr) == 0){
			char * p, * v = 0, * vend = 0;
			for(p = buf + i + nhdr; *p != '\n'; ++p){
				if(!v && !(*p == ':' || *p == ' ' || *p == '\t'))
					v = p;
				if(!vend && (*p == '\r' && *(p + 1) == '\n'))
					vend = p;
				if(v && vend) break;
			}
			if(!(v && vend)) return -1;

			/*
			 * check header value
			 * header value may empty */
			int nhdrvalue = vend - v;
			if(ovalue && !(strlen(ovalue) == nhdrvalue
					&& strncasecmp(v, ovalue, nhdrvalue) == 0))
				return -1;

			/* move left */
			int nnvalue = nvalue? strlen(nvalue) : 0, nc = nnvalue - nhdrvalue;
			if(nc != 0){
				memmove(vend + nc, vend, *len - (vend - buf));
				*len += nc;
			}
			/* update header value */
			if(nvalue)
				memcpy(v, nvalue, nnvalue);
			return 0;
		}
	}
	return 1;
}

static int hp_str_cmp_int(const void * a, const void * b)
{
	return !(*(int const *)a == *(int const *)b);
}

int hp_str_find(int val, char const * str)
{
	if(!(str && str[0] != '\0'))
		return 0;

	int protos[256];
	char const * p, * q = 0, * end = str + strlen(str);
	size_t n = 0;
	for(p = str; ;){
		q = strchr(p, ',');
		if(!q) q = end;

		if(q - p > 0){
			protos[n] = myatoi(p, q - p);
			++n;
		}
		if(q == end) break;
		p = q + 1;
	}
	return lfind(&val, protos, &n, sizeof(int), hp_str_cmp_int)? 1 : 0;
}

char const * hp_2str(char ** p, char const * fmt, ...)
{
	if(!(p && fmt))
		return 0;

	char const * buf = *p;

	va_list ap;
	va_start(ap, fmt);
	int n = vsprintf(*p, fmt, ap);
	va_end(ap);

	*p += (n + 1);

	return buf;
}

sds hp_fread(char const *fname)
{
	if(!fname)
		return 0;

	int rc;
	FILE * f = fopen(fname, "r");
	if(!f)
		return 0;

	struct stat fsobj, * fs = &fsobj;
	rc = fstat(fileno(f), fs);
	if(rc != 0){
		fclose(f);
		return 0;
	}

	sds str = sdsMakeRoomFor(sdsempty(), fs->st_size + 2);
	size_t size = fread(str, sizeof(char), fs->st_size, f);
	assert(size == fs->st_size);

	sdsIncrLen(str, size);

	return str;
}

int hp_strcasecmp(const void * a, const void * b)
{
	assert(a && b);
	char * ent1 = *(char **)a, * ent2 = *(char **)b;
	return strcasecmp(ent1, ent2) != 0;
}

int hp_vercmp(char const * ver, char const * cmp)
{
	int rc = 0;
	ver = ver? ver : "";
	cmp = cmp? cmp : "";

	int i;
	int c_ver = 0, c_cmp = 0;
	sds * s_ver = sdssplitlen(ver, strlen(ver), ".", 1, &c_ver);
	sds * s_cmp = sdssplitlen(cmp, strlen(cmp), ".", 1, &c_cmp);

	for(i = 0; i < hp_max(c_ver, c_cmp); ++i){
		int i_ver = atoi(i < c_ver? s_ver[i] : "0");
		int i_cmp = atoi(i < c_cmp? s_cmp[i] : "0");

		if(i_ver > i_cmp){
			rc = 1;
			break;
		}
		else if(i_ver < i_cmp){
			rc = -1;
			break;
		}
	}
	sdsfreesplitres(s_ver, c_ver);
	sdsfreesplitres(s_cmp, c_cmp);

	return rc;
}
//////////////////////////////////////////////////////////////
#ifndef NDEBUG
int test_string_util_main(int argc, char ** argv)
{
	//hp_vercmp
	{
		assert(hp_vercmp(0, 0) == 0);
		assert(hp_vercmp(0, "") == 0);
		assert(hp_vercmp("", 0) == 0);
		assert(hp_vercmp("", "") == 0);
		assert(hp_vercmp("1", "") > 0);
		assert(hp_vercmp("", "1") < 0);
		assert(hp_vercmp("1", "1") == 0);

		assert(hp_vercmp("1", "1.0") == 0);
		assert(hp_vercmp("1.0", "1") == 0);
		assert(hp_vercmp("1.1", "1") > 0);
		assert(hp_vercmp("1", "1.1") < 0);
		assert(hp_vercmp("1", "1.0.0") == 0);
		assert(hp_vercmp("1.0.0", "1") == 0);

		assert(hp_vercmp("1.0.1", "1") > 0);
		assert(hp_vercmp("1", "1.0.1") < 0);

		assert(hp_vercmp("1.0", "1.1") < 0);
		assert(hp_vercmp("1.1", "1.0") > 0);

		assert(hp_vercmp("1.1.0", "1.1") == 0);
		assert(hp_vercmp("1.1", "1.1.0") == 0);

		assert(hp_vercmp("1.1.1", "1.1.0") > 0);
		assert(hp_vercmp("1.0.1", "1.1.0") < 0);
	}

	/*sds_quote*/
	{
		{
		sds s = sds_quote(sdsnew("hello"));
		assert(strcmp(s, "'hello'") == 0);
		sdsfree(s);
		}
		{
		sds s = sds_quote(sdsnew(""));
		assert(strcmp(s, "''") == 0);
		sdsfree(s);
		}
	}
	/* byte_to_mb_kb_str */
	{
		char buf[128] = "";
		assert(strcmp(byte_to_mb_kb_str(1024 + 1024 / 2, "%-.1f %cB"), "1.5 KB") == 0);
		snprintf(buf, 64, "%d %cB", 10, 'K');
		assert(strcmp(byte_to_mb_kb_str(1024 * 10, "%.0f %cB"), "10 KB") == 0);

	}
	{
		assert(myatoi("", 0) == 0);
		assert(myatoi("-", 0) == 0);
		assert(myatoi("-1", 2) == -1);
		assert(myatoi("0", 1) == 0);
		assert(myatoi("1", 1) == 1);
		assert(myatoi("+1", 2) == 1);
		assert(myatoi("-1a", 3) == -1);
		assert(myatoi("+1a", 3) == 1);
		assert(myatoi("0a", 2) == 0);
		assert(myatoi("1a", 2) == 1);
		assert(myatoi("a", 1) == 0);
		assert(myatoi("a0", 2) == 0);
		assert(myatoi("--1", 3) == 0);
	}
	{
		char buf[512], * p = buf;
		assert(strcmp(hp_2str(&p, "%d", 1), "1") == 0);
		assert(strcmp(hp_2str(&p, "%s", "hello"), "hello") == 0);
		assert(strcmp(hp_2str(&p, "%d", 20), "20") == 0);
		assert(strncmp(buf, "1\0hello\020\0", 11) == 0);
	}
	char data[] = "If-Modified-Since: Sun, 21 Oct 2018 07:34:06 GMT\r\n\r\n";
	char * v = data + 19, * end = data + 19 + 29;
	memmove(v, end, strlen(data) - (end - data));

	char const * hdr = "Host";
	/* header NOT found: NONE http header */
	{
		char data[512] = "hello";
		size_t len = sizeof(data) - 20, olen = len;;
		int rc = hp_http_modify_header(data, &len, hdr, "192.168.1.106:80", 0);
		assert(rc == 1);
		assert(len == olen);
	}
	/* header NOT found */
	{
		char data[512] = "XXXX\r\nHost: 192.168.1.105:9201\r\nXXXX";
		size_t len = sizeof(data) - 20, olen = len;;
		int rc = hp_http_modify_header(data, &len, "If-None-Match", "192.168.1.106:80", 0);
		assert(rc == 1);
		assert(len == olen);
	}
	/* old mismatch: header value empty */
	{
		char data[512] = "XXXX\r\nHost:\r\nXXXX";
		size_t len = sizeof(data) - 20, olen = len;;
		int rc = hp_http_modify_header(data, &len, hdr, "192.168.1.106:80", "192.168.1.105:9201");
		assert(rc == -1);
		assert(len == olen);
	}
	/* OK: header value empty */
	{
		char data[512] = "XXXX\r\nHost: \r\nXXXX", DATA[] = "XXXX\r\nHost: 192.168.1.106:80\r\nXXXX";
		size_t len = sizeof(data) - 20, olen = len;;
		int rc = hp_http_modify_header(data, &len, hdr, "192.168.1.106:80", 0);
		assert(rc == 0);
		assert(memcmp(data, DATA, strlen(DATA)) == 0);
		assert(len == olen + 14);
	}
	/* OK: header value empty, no blanks in header */
	{
		char data[512] = "XXXX\r\nHost:\r\nXXXX", DATA[] = "XXXX\r\nHost:192.168.1.106:80\r\nXXXX";
		size_t len = sizeof(data) - 20, olen = len;;
		int rc = hp_http_modify_header(data, &len, hdr, "192.168.1.106:80", 0);
		assert(rc == 0);
		assert(memcmp(data, DATA, strlen(DATA)) == 0);
		assert(len == olen + 14);
	}
	/* OK: old NULL */
	{
		char data[512] = "XXXX\r\nHost: 192.168.1.105:9201\r\nXXXX", DATA[] = "XXXX\r\nHost: 192.168.1.106:80\r\nXXXX";
		size_t len = sizeof(data) - 20, olen = len;
		int rc = hp_http_modify_header(data, &len, hdr, "192.168.1.106:80", 0);
		assert(rc == 0);
		assert(memcmp(data, DATA, strlen(DATA)) == 0);
		assert(len == olen - 2);
	}
	/* old mismatch */
	{
		char data[512] = "XXXX\r\nHost: 192.168.1.105:9201\r\nXXXX";
		size_t len = sizeof(data) - 20, olen = len;
		int rc = hp_http_modify_header(data, &len, hdr, "192.168.1.106:80", "192.168.1.105:9202");
		assert(rc == -1);
		assert(len == olen);
	}

	/* OK: old replaced with new, length NOT change */
	{
		char data[512] = "XXXX\r\nHost: 192.168.1.105:9201\r\nXXXX", DATA[] = "XXXX\r\nHost: 168.66.8.22:8080\r\nXXXX";;
		size_t len = sizeof(data) - 20, olen = len;
		int rc = hp_http_modify_header(data, &len, hdr, "168.66.8.22:8080", "192.168.1.105:9201");
		assert(rc == 0);
		assert(memcmp(data, DATA, strlen(DATA)) == 0);
		assert(len == olen);
	}
	/* OK: old replaced with new, length inc */
	{
		char data[512] = "XXXX\r\nHost: 192.168.1.105:9201\r\nXXXX", DATA[] = "XXXX\r\nHost: 168.166.888.222:8080\r\nXXXX";;
		size_t len = sizeof(data) - 20, olen = len;
		int rc = hp_http_modify_header(data, &len, hdr, "168.166.888.222:8080", "192.168.1.105:9201");
		assert(rc == 0);
		assert(memcmp(data, DATA, strlen(DATA)) == 0);
		assert(len == olen + 4);
	}
	/* OK: old replaced with new, length dec */
	{
		char data[512] = "XXXX\r\nHost: 192.168.1.105:9201\r\nXXXX", DATA[] = "XXXX\r\nHost: 192.168.1.106:80\r\nXXXX";;
		size_t len = sizeof(data) - 20, olen = len;
		int rc = hp_http_modify_header(data, &len, hdr, "192.168.1.106:80", "192.168.1.105:9201");
		assert(rc == 0);
		assert(memcmp(data, DATA, strlen(DATA)) == 0);
		assert(len == olen - 2);
	}

	/* OK: old replaced with new, and new empty */
	{
		char data[512] = "XXXX\r\nHost: 192.168.1.105:9201\r\nXXXX", DATA[] = "XXXX\r\nHost: \r\nXXXX";;
		size_t len = sizeof(data) - 20, olen = len;
		int rc = hp_http_modify_header(data, &len, hdr, 0, "192.168.1.105:9201");
		assert(rc == 0);
		assert(memcmp(data, DATA, strlen(DATA)) == 0);
		assert(len == olen - 16);
	}
	/* OK: set old to empty */
	{
		char data[512] = "XXXX\r\nHost: 192.168.1.105:9201\r\nXXXX", DATA[] = "XXXX\r\nHost: \r\nXXXX";;
		size_t len = sizeof(data) - 20, olen = len;
		int rc = hp_http_modify_header(data, &len, hdr, 0, 0);
		assert(rc == 0);
		assert(memcmp(data, DATA, strlen(DATA)) == 0);
		assert(len == olen - 16);
	}
	/* OK: set old to empty, header valued empty */
	{
		char data[512] = "XXXX\r\nHost: \r\nXXXX", DATA[] = "XXXX\r\nHost: \r\nXXXX";;
		size_t len = sizeof(data) - 20, olen = len;
		int rc = hp_http_modify_header(data, &len, hdr, 0, 0);
		assert(rc == 0);
		assert(memcmp(data, DATA, strlen(DATA)) == 0);
		assert(len == olen);
	}
	//////////////////////////////////////////////////////////
	/*real test*/
	{
		char data[2048] =
				"GET http://192.168.1.105:9261/ewebeditor/UploadFile_2015_5/20181021073406592.jpg HTTP/1.1\r\n"
				"Host: 192.168.1.105:9261\r\n"
				"Connection: keep-alive\r\n"
				"Cache-Control: max-age=0\r\n"
				"Upgrade-Insecure-Requests: 1\r\n"
				"User-Agent: Mozilla/5.0 (Windows NT 6.1; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/68.0.3440.106 Safari/537.36\r\n"
				"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8\r\n"
				"Accept-Encoding: gzip, deflate\r\n"
				"Accept-Language: zh-CN,zh;q=0.9,en;q=0.8\r\n"
				"Cookie: JSESSIONID=aNQon2bnYSQh\r\n"
				"If-None-Match: \"AAAAWaVi5xJ\"\r\n"
				"If-Modified-Since: Sun, 21 Oct 2018 07:34:06 GMT\r\n"
				"\r\n"
				;
		char DATA[2048] =
				"GET http://192.168.1.105:9261/ewebeditor/UploadFile_2015_5/20181021073406592.jpg HTTP/1.1\r\n"
				"Host: 192.168.1.106:8085\r\n"
				"Connection: keep-alive\r\n"
				"Cache-Control: max-age=0\r\n"
				"Upgrade-Insecure-Requests: 1\r\n"
				"User-Agent: Mozilla/5.0 (Windows NT 6.1; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/68.0.3440.106 Safari/537.36\r\n"
				"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8\r\n"
				"Accept-Encoding: gzip, deflate\r\n"
				"Accept-Language: zh-CN,zh;q=0.9,en;q=0.8\r\n"
				"Cookie: JSESSIONID=aNQon2bnYSQh\r\n"
				"If-None-Match: \"\"\r\n"
				"If-Modified-Since: \r\n"
				"\r\n"
				;
		int rc;
		size_t len = strlen(data) + 10, olen = len;

		rc = hp_http_modify_header(data, &len, "Host", "192.168.1.106:8085", "192.168.1.105:9261");
		assert(rc == 0);
		rc = hp_http_modify_header(data, &len, "If-None-Match", "\"\"", 0);
		assert(rc == 0);
		rc = hp_http_modify_header(data, &len, "If-Modified-Since", 0, 0);
		assert(rc == 0);

		assert(memcmp(data, DATA, strlen(DATA)) == 0);
		assert(len == olen - (strlen("Sun, 21 Oct 2018 07:34:06 GMT")
				+ strlen("\"AAAAWaVi5xJ\"") - strlen("\"\"")
				+ strlen("192.168.1.105:9261") - strlen("192.168.1.106:8085")));
	}

	assert(!hp_str_find(0, 0));
	assert(!hp_str_find(0, ""));
	assert(!hp_str_find(0, "1"));
	assert(hp_str_find(1, "1"));
	assert(hp_str_find(1, "1,2"));
	assert(hp_str_find(2, "1,2"));
	assert(!hp_str_find(3, "1,2"));

	assert(hp_str_find(0, "0,2,3"));
	assert(hp_str_find(2, "0,2,3"));
	assert(hp_str_find(3, "0,2,3"));
	assert(!hp_str_find(4, "0,2,3"));
	assert(hp_str_find(4, "0,2,3,4,"));
	assert(!hp_str_find(5, "0,2,3,4,"));

	return 0;
}

#endif /* NDEBUG */


