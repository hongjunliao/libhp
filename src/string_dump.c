/*!
 * This file is Part of cpp-test
 * @author: hongjun.liao<docici@126.com>
 *
 * dump a buffer to FILE or to another buffer, escape some special chars, e.g. \n -> \\n, \0 -> \\0
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "str_dump.h"   /* */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fdump_chr(FILE * f, char chr, char const * beg/* = 0*/, char const * end/* = 0*/)
{
	if(beg)
		fprintf(f, "%s", beg);
	if(chr == '\r')
		fprintf(f, "\\r");
	if(chr == '\n')
		fprintf(f, "\\n");
	else if(chr == '\0')
		fprintf(f, "\\0");
	else if(chr == '\t')
		fprintf(f, "\\t");
	else
		fprintf(f, "%c", chr);
	if(end)
		fprintf(f, "%s", end);
}

int sdump_chr(char * buf, char chr, char const * beg/* = 0*/, char const * end/* = 0*/)
{
	size_t n = 0;
	if(beg){
		strcpy(buf, beg);
		n += strlen(beg);
	}

	switch(chr){
	default:   { buf[n++] = chr; buf[n] = '\0'; }  break;
	case '\r': { strcpy(buf + n, "\\r"); n += 2; } break;
	case '\n': { strcpy(buf + n, "\\n"); n += 2; } break;
	case '\t': { strcpy(buf + n, "\\t"); n += 2; } break;
	case '\0': { strcpy(buf + n, "\\0"); n += 2; } break;
	}

	if(end){
		strcpy(buf + n, end);
		n += strlen(end);
	}
	return n;
}

void fdump_str(FILE * f, char const * buf, size_t len
		, char const * beg/* = 0*/, char const * end/* = 0*/)
{
	if(!(f && buf && len > 0))
		return;

	if(beg)
		fprintf(f, "%s", beg);
	size_t i = 0;
	for(; i < len; ++i){
		fdump_chr(f, buf[i], 0, 0);
	}
	if(end)
		fprintf(f, "%s", end);
	fflush(f);
}

/* sample:
 * in:  "hello\r\ngo\tthis\nis\0a test\n"
 * out: "hello\\r\\ngo\\tthis\\nis\\0a test\\n"
 */
char * sdump_str(char * out, char const * buf, size_t len
		, char const * beg/* = 0*/, char const * end/* = 0*/)
{
	if(!(out && buf))
		return 0;
	if(len == 0)
		return "";

	size_t n = 0;
	if(beg){
		strcpy(out, beg);
		n += strlen(beg);
	}

	size_t i = 0;
	for(; i < len; ++i)
		n += sdump_chr(out + n, buf[i], 0, 0);

	if(end){
		strcpy(out + n, end);
		n += strlen(end);
	}

	out[n] = '\0';
	return out;
}

char const * dumpstr(char const * buf, size_t len, size_t dumplen)
{
    static char dumpbuf[1024];
    dumpbuf[0] = '\0';

    size_t alen = dumplen <= (int)sizeof(dumpbuf) - 1? (dumplen < len? dumplen : len):  sizeof(dumpbuf) - 1;
	return sdump_str(dumpbuf, buf, alen, 0, dumplen < len? "..." : 0);
}

char const * dumpstr_r(char * out, char const * buf, size_t len, size_t dumplen)
{
    size_t alen = (dumplen < len? dumplen : len);
	return sdump_str(out, buf, alen, 0, dumplen < len? "..." : 0);
}

