/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2017/8/31
 *
 * internal variables for config file
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_WITH_CJSON
#include "hp/hp_var.h"        /* hp_var */
#include <string.h>	       /* strncpy */
#include <stdlib.h>	       /* free */

/////////////////////////////////////////////////////////////////////////////////////////
static char const * hp_var_map_find(char const * varstr, hp_var_map const * vmap, int len)
{
	if(!(varstr && vmap && len > 0))
		return 0;

	int i;
	for(i = 0; i < len; ++i){
		if(vmap[i].var && strlen(varstr) == strlen(vmap[i].var)
				&& strncmp(vmap[i].var, varstr, strlen(vmap[i].var)) == 0){
			return vmap[i].val;
		}
	}
	return 0;
}
/*
 * convert string like ".${FILE_NAME}.xhhp" to actual ".1.xhhp"
 *
 * @return: @param str
 * */
static char * hp_var_do_replace(char const * str
		, char * out, char const * (* fn)(char const * key))
{
	if(!(str && str[0] != '\0' && out && fn))
		return out;
	char * offset = out;
	char const * end = str + strlen(str);
	char const * p;
	for(p = str; p != end; ++p){
		if(*p == '$'){
			if(p + 1 == end) goto def;
			if(*(p + 1) == '$'){
				++p;
				goto def;
			}
			++p;
			char const * e = p;
			while((*e >= '0' && *e <= '9') || (*e >= 'a' && *e <= 'z') || (*e >= 'A' && *e <= 'Z') || *e == '_')
				++e;
			char * varstr = strndup(p, e - p);
			int n = e - p;
			p = varstr;
			char const * val = fn(varstr);
			if(val) {
				n = strlen(val);
				p = val;
			}
			else {
				*offset = '$';
				++offset;
			}
			strcpy(offset, p);
			free(varstr);
			offset += n;
			p = e - 1;
			continue;
		}
def:
		*offset = *p;
		++offset;
	}
	*offset = '\0';
	return out;
}
static char * hp_var_map_replace(char const * str
		, char * out, hp_var_map const * vmap, int len)
{
	if(!(str && str[0] != '\0' && out && vmap))
		return out;
	if(len == 0){
		strncpy(out, str, strlen(str));
		return out;
	}
	char const * val(char const * key){ return hp_var_map_find(key, vmap, len); }
	return hp_var_do_replace(str, out, val);
}

static sds hp_var_eval(char const * str, char const * (* fn)(char const * key))
{
	if(!(str && fn))
		return 0;
	if(strlen(str) == 0)
		return sdsempty();

	sds s = sdsMakeRoomFor(sdsempty(), strlen(str) * 2);

	char const * out = hp_var_do_replace(str, s, fn);
	sdssetlen(s, strlen(out));

	return s;
}
struct HP_VAR_PKG_TOOL_ const HP_VAR_PKG_TOOL = {
	.replace = hp_var_map_replace,
	.eval = hp_var_eval,
};
/////////////////////////////////////////////////////////////////////////////////////////
#ifndef NDEBUG
#include <assert.h>
#include "cjson/cJSON.h"
#include "hp/hp_cjson.h"
#define Var HP_VAR_PKG_TOOL

int test_hp_var_main(int argc, char ** argv)
{
	int rc = 0;
	/* .eval */
	{
		cJSON * json = cJSON_Parse("{\"policeId\":\"admin\",\"app_name\":\"Antox\",\"app_version\":\"0.25.515\"}");
		assert(json);
		char const * test_val(char const * key) { return cjson_sval(json, key, ""); }
		sds s = Var.eval("1 $policeId _ $app_name-$app_version", test_val);
		assert(s && memcmp(s, "1 admin _ Antox-0.25.515", sdslen(s)) == 0);
		sdsfree(s);
		cJSON_Delete(json);
	}
	{
		hp_var_map vmap[] = { { "FILE_NAME", "12"} };
		char out[128] = "";
		char * str = Var.replace("a", out, vmap, (sizeof(vmap) / sizeof(vmap[0])));
		assert(str == out);
		assert(strcmp(str, "a") == 0);
	}
	{
		hp_var_map vmap[] = { { "FILE_NAME", "12"} };
		char out[128] = "";
		char * str = Var.replace("$", out, vmap, (sizeof(vmap) / sizeof(vmap[0])));
		assert(str == out);
		assert(strcmp(str, "$") == 0);
	}
	{
		hp_var_map vmap[] = { { "FILE_NAME", "12"} };
		char out[128] = "";
		char * str = Var.replace("$$", out, vmap, (sizeof(vmap) / sizeof(vmap[0])));
		assert(str == out);
		assert(strcmp(str, "$") == 0);
	}
	{
		assert(rc == 0);
		hp_var_map vmap[] = { { "FILE_NAME", "12"} };
		char out[128] = "";
		char * str = Var.replace("$$a", out, vmap, (sizeof(vmap) / sizeof(vmap[0])));
		assert(str == out);
		assert(strcmp(str, "$a") == 0);
	}
	{
		assert(rc == 0);
		hp_var_map vmap[] = { { "FILE_NAME", "12"} };
		char out[128] = "";
		char * str = Var.replace("a$$", out, vmap, (sizeof(vmap) / sizeof(vmap[0])));
		assert(str == out);
		assert(strcmp(str, "a$") == 0);
	}
	{
		assert(rc == 0);
		hp_var_map vmap[] = { { "FILE_NAME", "12"} };
		char out[128] = "";
		char * str = Var.replace("$a", out, vmap, (sizeof(vmap) / sizeof(vmap[0])));
		assert(str == out);
		assert(strcmp(str, "$a") == 0);
	}
	{
		assert(rc == 0);
		hp_var_map vmap[] = { { "FILE_NAME", "12"} };
		char out[128] = "";
		char * str = Var.replace("$FILE_NAME", out, vmap, (sizeof(vmap) / sizeof(vmap[0])));
		assert(str == out);
		assert(strcmp(str, "12") == 0);
	}
	{
		assert(rc == 0);
		hp_var_map vmap[] = { { "FILE_NAME", "12"} };
		char out[128] = "";
		char * str = Var.replace("$FILE_NAME$$", out, vmap, (sizeof(vmap) / sizeof(vmap[0])));
		assert(str == out);
		assert(strcmp(str, "12$") == 0);
	}
	{
		assert(rc == 0);
		hp_var_map vmap[] = { { "FILE_NAME", "12"} };
		char out[128] = "";
		char * str = Var.replace("$$FILE_NAME", out, vmap, (sizeof(vmap) / sizeof(vmap[0])));
		assert(str == out);
		assert(strcmp(str, "$FILE_NAME") == 0);
	}
	{
		assert(rc == 0);
		hp_var_map vmap[] = { { "FILE_NAME", "12"} };
		char out[128] = "";
		char * str = Var.replace("$$$FILE_NAME", out, vmap, (sizeof(vmap) / sizeof(vmap[0])));
		assert(str == out);
		assert(strcmp(str, "$12") == 0);
	}
	{
		assert(rc == 0);
		hp_var_map vmap[] = { { "FILE_NAME", "12"} };
		char out[128] = "";
		char * str = Var.replace("$$$FILE_NAME$a", out, vmap, (sizeof(vmap) / sizeof(vmap[0])));
		assert(str == out);
		assert(strcmp(str, "$12$a") == 0);
	}
	/////////////////////////////////////////////////////////////////////////////////////////
	{  /* 2 */
		assert(rc == 0);
		hp_var_map vmap[] = { { "FILE_NAME", "12"}, { "a", "A"} };
		char out[128] = "";
		char * str = Var.replace("$FILE_NAME.$a", out, vmap, (sizeof(vmap) / sizeof(vmap[0])));
		assert(str == out);
		assert(strcmp(str, "12.A") == 0);
	}
	{  /* 2 */
		assert(rc == 0);
		hp_var_map vmap[] = { { "FILE_NAME", "12"}, { "a", "A"} };
		char out[128] = "";
		char * str = Var.replace("$FILE_NAME.$a$b", out, vmap, (sizeof(vmap) / sizeof(vmap[0])));
		assert(str == out);
		assert(strcmp(str, "12.A$b") == 0);
	}
	{  /* 2 */
		assert(rc == 0);
		hp_var_map vmap[] = { { "FILE_NAME", "12"}, { "a", "A"} };
		char out[128] = "";
		char * str = Var.replace("$FILE_NAME.$a$$b", out, vmap, (sizeof(vmap) / sizeof(vmap[0])));
		assert(str == out);
		assert(strcmp(str, "12.A$b") == 0);
	}
	/* multi */
	{
		assert(rc == 0);
		hp_var_map vmap[] = { { "CHAIN_INDEX", "0"}, { "CHAIN_INDEX_NEXT", "1"}, { "WORKER_ID", "1"}, { "FILE_NAME", "1"} };
		char out[128] = "0111";
		char * str = Var.replace("$CHAIN_INDEX$CHAIN_INDEX_NEXT$WORKER_ID$FILE_NAME", out, vmap, (sizeof(vmap) / sizeof(vmap[0])));
		assert(str == out);
		assert(strcmp(str, "0111") == 0);
	}
	{
		assert(rc == 0);
		hp_var_map vmap[] = { { "CHAIN_INDEX", "1"}, { "CHAIN_INDEX_PREV", "0"}, { "WORKER_ID", "1"}, { "FILE_NAME", "1"} };
		char out[128] = "1011";
		char * str = Var.replace("$CHAIN_INDEX$CHAIN_INDEX_PREV$WORKER_ID$FILE_NAME", out, vmap, (sizeof(vmap) / sizeof(vmap[0])));
		assert(str == out);
		assert(strcmp(str, "1011") == 0);
	}
	return 0;
}
#endif /* NDEBUG */
#endif //LIBHP_WITH_CJSON
