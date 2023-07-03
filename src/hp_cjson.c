
/*!
 *  This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2019/11/14
 *
 * cjson utils
 *
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */
#ifdef LIBHP_WITH_CJSON

#include <stdio.h>
#include <stdlib.h>	       /* malloc */
#include <errno.h>         /* errno */
#include <string.h>	       /* strncmp */
#include <ctype.h>	       /* isblank */
#ifndef _MSC_VER
#include <regex.h>
#else
#endif /* _MSC_VER */

#include <limits.h>	       /* INT_MAX, IOV_MAX */
#include <assert.h>        /* define NDEBUG to disable assertion */
#include "sdsinc.h"        /* sds */
#include "cjson/cJSON.h"	/* cJSON */
#include "hp_cjson.h"
#include "string_util.h"

hp_cjson const HP_CJSON_PKG = {
	.str = cJSON_PrintUnformatted,
	.size = cJSON_GetArraySize,
};
/////////////////////////////////////////////////////////////////////////////////////////

sds cjson_in(cJSON * array)
{
	if(!array)
		return sdsnew("('')");

	if(cJSON_IsArray(array)){
		if(cJSON_GetArraySize(array) > 0)
			return sdsmapchars(sdsnew(cJSON_PrintUnformatted(array)), "[]", "()", 2);
		return sdsnew("('')");
	}

	if(array->valuestring)
		return sdscatfmt(sdsempty(), "(\"%s\")", array->valuestring);

	return sdsnew("('')");
}

sds cjson_cmdline(char const * cmd, cJSON * args)
{
	sds s = sdsnew(cmd);
	if(!args) 
		return s;
	if(!cJSON_IsArray(args))
		return s;

	cJSON * obj = 0;
	cJSON_ArrayForEach(obj, args){
		s = sdscatfmt(s, " %s", obj->valuestring);
	}

	return s;
}

static cJSON * check_string(cJSON const * val, int min_len, int max_len)
{
	if(!(val && val->valuestring))
		return 0;

	sds s = sdstrim(sdsnew(val->valuestring), " ");

	if(!(sdslen(s) >= min_len && sdslen(s) <= max_len)){
		sdsfree(s);
		return 0;
	}

	cJSON * json = cJSON_CreateString(s);
	sdsfree(s);

	return json;

}

cJSON * check_phone(cJSON const * val)
{
	return check_string(val, 11, 11);
}

cJSON * check_meid(cJSON const * val)
{
	return check_string(val, 1, 15);
}

cJSON * check_iccid(cJSON const * val)
{
	return check_string(val, 1, 20);
}

cJSON * check_imei(cJSON const * val)
{
	return check_string(val, 1, 17);
}

cJSON * check_imsi(cJSON const * val)
{
	return check_string(val, 1, 15);
}

int cjson_check(cJSON * cjson, char const * key,
		cJSON * (* fn)(cJSON const * val))
{
	assert(cjson && key && fn);

	cJSON * val = fn(cjson_((cjson), (key)));
	if(val){
		cJSON_ReplaceItemInObject(cjson, key, val);
		return 1;
	}
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////

int cjson_exists(cJSON const * cjson, ...)
{
	if(!cjson)
		return 0;

	int i;
	int rc = 1;
	va_list ap;
	va_start(ap, cjson);
	for(;;){
		char const * val = va_arg(ap, char const *);
		if(!val)
			break;

		int count = 0;
		sds * s = sdssplitlen(val, strlen(val), "/", 1, &count);
		cJSON const * subjson = cjson;
		for(i = 0; i < count; ++i){
			if(sdslen(s[i]) == 0)
				continue;

			subjson = cJSON_GetObjectItem(subjson, s[i]);
			if(!subjson){
				rc = 0;
				break;
			}
		}
		sdsfreesplitres(s, count);
	}

	va_end(ap);

	return rc;
}

sds cjson_fmt(char const * fmt, cJSON const * cjson, char const * key)
{
	return (cJSON_GetObjectItem((cjson),(key))?
		((cJSON_IsString(cJSON_GetObjectItem((cjson),(key))))?
			((cJSON_GetObjectItem((cjson),(key))->valuestring && strlen(cJSON_GetObjectItem((cjson),(key))->valuestring) > 0)?
				sdscatprintf(sdsempty(), fmt, cJSON_GetObjectItem((cjson),(key))->valuestring)
			: sdsempty())
		: (sdscatprintf(sdsempty(), fmt, cJSON_GetObjectItem((cjson),(key))->valueint)))
	:sdsempty());
}
/////////////////////////////////////////////////////////////////////////////////////////

cJSON * cjson_(cJSON const * cjson, char const * key)
{
	if(!(cjson && key))
		return 0;

	int i, j, r;
	int count = 0;
	sds * s = sdssplitlen(key, strlen(key), "/", 1, &count);
	cJSON const * subjson = cjson;
	for(i = 0; i < count; ++i){
		if(sdslen(s[i]) == 0)
			continue;

		cJSON const * tmp = subjson;
		/* has 'or' syntarx? ( a | b) */
		int or_count = 0;
		sds * ss = sdssplitlen(s[i], strlen(s[i]), "|", 1, &or_count);

		for(j = 0; j < or_count; ++j){
			if(sdslen(ss[j]) == 0)
				continue;
#ifndef _MSC_VER
			/* array? */
			if(strrchr(ss[j], ']')){
				regex_t start_state;
				regmatch_t matchptr[2] = { 0 };
				const char *pattern = "\\[\([+-]?[0-9]+\)\\]$";
				r = regcomp(&start_state, pattern, REG_EXTENDED);
				assert(r == 0);

				int status = regexec(&start_state, ss[j], sizeof(matchptr) / sizeof(matchptr[0]), matchptr, 0);
				regfree(&start_state);

				if(status == 0){
					(ss[j])[matchptr[0].rm_so] = '\0';
					if(strlen(ss[j]) > 0){
						subjson = cJSON_GetObjectItem(subjson, ss[j]);
						if(!subjson)
							continue;
					}

					int n = cJSON_GetArraySize(subjson);
					int b = myatoi(ss[j] + matchptr[1].rm_so, matchptr[1].rm_eo - matchptr[1].rm_so);
					if(b < 0) b = 0;
					if(b > n - 1) b = n - 1;

					subjson = cJSON_GetArrayItem(subjson, b);

				} else subjson = cJSON_GetObjectItem(subjson, ss[j]);
			}
			else subjson = cJSON_GetObjectItem(subjson, ss[j]);
#else
			subjson = cJSON_GetObjectItem(subjson, ss[j]);
#endif /* _MSC_VER */

			if(subjson)
				break; /* got one of ( a | b), done! */
			/* failed, try another one */
			if(j + 1 < or_count)
				subjson = tmp;
		}

		sdsfreesplitres(ss, or_count);

		if(!subjson)
			break;
	}
	sdsfreesplitres(s, count);

	return subjson;
}

int cjson_ival(cJSON const * cjson, char const * key, int def)
{
	if(!(cjson && key))
		return 0;

	cJSON const * json = cjson_(cjson, key);
	return json? json->valueint : def;
}

double cjson_dval(cJSON const * cjson, char const * key, double def)
{
	if(!(cjson && key))
		return 0;

	cJSON const * json = cjson_(cjson, key);
	return json? json->valuedouble : def;
}

char const * cjson_sval(cJSON const * cjson, char const * key, char * def)
{
	if(!(cjson && key))
		return 0;

	int rc;

	sds k = sdsnew(key);
#ifndef _MSC_VER
	regex_t start_state;
	regmatch_t matchptr[8] = { 0 };
	const char *pattern = "\\[\([0-9]+\),\([0-9]+\)\\]$";
	rc = regcomp(&start_state, pattern, REG_EXTENDED);
	assert(rc == 0);

	int status = regexec(&start_state, k, sizeof(matchptr) / sizeof(matchptr[0]), matchptr, 0);
	regfree(&start_state);

	if(status == 0){
		char const * s = def;

		k[matchptr[0].rm_so] = '\0';
		cJSON const * json = cjson_(cjson, k);
		if(!(json && json->valuestring))
			goto ret;

		int n = strlen(json->valuestring);
		int b = myatoi(k + matchptr[1].rm_so, matchptr[1].rm_eo - matchptr[1].rm_so);
		int e = myatoi(k + matchptr[2].rm_so, matchptr[2].rm_eo - matchptr[2].rm_so);

		if(b < 0) b = 0;
		if(b > n - 1) goto ret;
		if(e < 0) e = 0;
		if(e > n - 1) e = n - 1;
		if(e < b)
			goto ret;

		strncpy(def, json->valuestring + b, e + 1 - b);
		def[e + 1 - b] = '\0';
	ret:
		sdsfree(k);
		return s;
	}
#else
#endif /* _MSC_VER */
	cJSON const * json = cjson_(cjson, k);
	sdsfree(k);
	return (json? (!(cJSON_IsObject(json) || cJSON_IsArray(json))?
				(json->valuestring? json->valuestring : def)
				: cJSON_PrintUnformatted(json))
			: def);
}

int cjson_is(cJSON * cjson, char const * key
	, cJSON_bool (* fn)(cJSON const * item))
{
	if(!(cjson && key && fn))
		return 0;

	cJSON const * json = cjson_(cjson, key);
	if(!json)
		return 0;
	return fn(json);
}
/////////////////////////////////////////////////////////////////////////////////////////

cJSON * cjson_col(cJSON const * cjson, char const * key)
{
	if(!(cjson && key))
		return 0;

	int i;
	cJSON * json = 0;
	cJSON const * arr = 0;
	char const * p = strrchr(key, '/');
	if(p){
		sds k = sdsnewlen(key, p - key);
		arr = cjson_(cjson, k);
		++p;
		sdsfree(k);
	}
	else{
		p = key;
		arr = cjson;
	}
	if(!arr){
		goto ret;
	}

	json = cJSON_CreateArray();

	cJSON * obj = 0;
	i = 0;
	cJSON_ArrayForEach(obj, arr){
		sds index = sdscatprintf(sdsempty(), "[%d]/%s", i, p);
        cJSON * col = cjson_(arr, index);
        if(!col){
        	cJSON_Delete(json);
        	json = 0;
        	break;
		}
		cJSON_AddItemToArray(json, cJSON_Duplicate(col, 0));
        sdsfree(index);
        ++i;
    }
ret:
	return json;
}

cJSON * cjson_parse(char const * fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	sds s = sdscatvprintf(sdsempty(), fmt, ap);
	va_end(ap);

	cJSON * json = cJSON_Parse(s);
	sdsfree(s);
	return json;
}

cJSON * cjson_parselen(char const * str, int len)
{
	return cjson_parse("%.*s", len, str);
}
/////////////////////////////////////////////////////////////////////////////////////////
#ifndef NDEBUG

int test_hp_cjson_main(int argc, char ** argv)
{
	int i, rc;
	assert(!cjson_(0, 0));

	/* cjson_parse */
	{
		cJSON * json = cjson_parse("{\"success\":false}");
		assert(json);
		cJSON_Delete(json);
	}
	/* cjson_in */
	{
		sds s;
		char * data[] = { "[]", "[\"1\"]", "[\"1\",\"2\"]"};
		char * result[] = { "('')", "(\"1\")", "(\"1\",\"2\")" };
		for(i = 0; i < 3; ++i){
			cJSON * json = cJSON_Parse(data[i]);
			assert(json);
			s = cjson_in(json);
			assert(strcmp(s, result[i]) == 0);
			sdsfree(s);
			cJSON_Delete(json);
		}
	}
	{
		sds s;
		cJSON * json = cJSON_Parse("{\"1\":\"1\"}"), * data[] = { json, cjson_(json, "1")};
		char * result[] = { "('')", "(\"1\")" };
		for(i = 0; i < 2; ++i){
			cJSON * json = (data[i]);
			assert(json);
			s = cjson_in(json);
			assert(strcmp(s, result[i]) == 0);
			sdsfree(s);
		}
		cJSON_Delete(json);
	}
	/* cjson_cmdline */
	{
		sds s;
		cJSON * args; 
		s = cjson_cmdline(0, 0); assert(strcmp(s, "") == 0); sdsfree(s);
		s = cjson_cmdline("hello", 0); assert(strcmp(s, "hello") == 0); sdsfree(s);
		args = cJSON_Parse("{\"a\":1}"); assert(args); s = cjson_cmdline("hello", args); assert(strcmp(s, "hello") == 0); sdsfree(s); cJSON_Delete(args);
		args = cJSON_Parse("[\"a\"]"); s = cjson_cmdline("hello", args); assert(strcmp(s, "hello a") == 0); sdsfree(s); cJSON_Delete(args);
		args = cJSON_Parse("[\"a\",\"bc\"]"); s = cjson_cmdline("hello", args); assert(strcmp(s, "hello a bc") == 0); sdsfree(s); cJSON_Delete(args);
	}
	{
		cJSON * json = cJSON_Parse("{\"success\":false}");
		assert(json && !cjson_ival(json, "success", 1));
		cJSON_Delete(json);
	}
	{
		cJSON * json = cJSON_Parse("{\"success\":true}");
		assert(json && cjson_ival(json, "success", 0));
		cJSON_Delete(json);
	}
	/* test for cJSON_AddItemToObject */
	{
		cJSON * json = cJSON_Parse("{\"params\":{\"id\":\"123412341\"}}")
			, * p1 = cJSON_Parse("[{\"OS\":\"Android\",\"OSVer\":\"5.1\"}]")
			;
		assert(json && p1 && cJSON_IsArray(p1));

//		cJSON_AddItemToObject(json, "params", cJSON_Duplicate(cJSON_GetArrayItem(p1, 0), 1));
		cJSON_ReplaceItemInObject(json, "params", cJSON_Duplicate(cJSON_GetArrayItem(p1, 0), 1));

		fprintf(stdout, "%s: %s\n", __FUNCTION__, cJSON_PrintUnformatted(json));
		assert(cjson_(json, "params/OS"));

		cJSON_Delete(p1);
		cJSON_Delete(json);
	}
	/* cjson_check */
	{
		cJSON * json = cJSON_Parse("{\"phonea\":\"  \"}");
		assert(json);
		rc = cjson_check(json, "phone", check_phone);
		assert(!rc);
		cJSON_Delete(json);
	}
	{
		cJSON * json = cJSON_Parse("{\"phone\":\"\"}");
		assert(json);
		rc = cjson_check(json, "phone", check_phone);
		assert(!rc);
		cJSON_Delete(json);
	}
	{
		cJSON * json = cJSON_Parse("{\"phone\":\"  \"}");
		assert(json);
		rc = cjson_check(json, "phone", check_phone);
		assert(!rc);
		cJSON_Delete(json);
	}
	{
		cJSON * json = cJSON_Parse("{\"phone\":\"1306696966\"}");
		assert(json);
		rc = cjson_check(json, "phone", check_phone);
		assert(!rc);
		cJSON_Delete(json);
	}
	{
		cJSON * json = cJSON_Parse("{\"phone\":\"13066969666666\"}");
		assert(json);
		rc = cjson_check(json, "phone", check_phone);
		assert(!rc);
		cJSON_Delete(json);
	}
	/* cjson_sval */
	{
		cJSON * json = cJSON_Parse("{\"params\":{\"hasChild\":11,\"deptCode\":\"999951688\"}}");
		assert(json);
		assert(cjson_exists(json, "params/deptCode", "params/hasChild", 0));
		assert(strcmp(cjson_sval(json, "params/deptCode", ""), "999951688") == 0);
		assert(strcmp(cjson_sval(json, "/params/deptCode", ""), "999951688") == 0);
		assert(strcmp(cjson_sval(json, "params/deptCode/", ""), "999951688") == 0);
		assert(strcmp(cjson_sval(json, "/params/deptCode/", ""), "999951688") == 0);
		assert(strcmp(cjson_sval(json, "/params/this_key_NOT_exist/", "hello"), "hello") == 0);
		assert(cjson_ival(json, "params/hasChild", 0) == 11);
		assert(cjson_ival(json, "params/this_key_NOT_exist", 12) == 12);
		cJSON_Delete(json);
	}
	/* cjson_sval with pure array */
	{
		cJSON * json = cJSON_Parse("{\"name\":\"jack\",\"apps\":[\"1\",\"2\",\"3\"]}");
		assert(json);
		assert(strcmp(cjson_sval(json, "name", ""), "jack") == 0);
		assert(strcmp(cjson_sval(json, "apps[0]", ""), "1") == 0);
		assert(strcmp(cjson_sval(json, "apps[1]", ""), "2") == 0);
		assert(strcmp(cjson_sval(json, "apps[2]", ""), "3") == 0);

		// also OK
		assert(strcmp(cjson_sval(json, "apps/[0]", ""), "1") == 0);
		assert(strcmp(cjson_sval(json, "apps/[1]", ""), "2") == 0);
		assert(strcmp(cjson_sval(json, "apps/[2]", ""), "3") == 0);
		cJSON_Delete(json);
	}
	/* cjson_ with regex: index */
	{
		cJSON * json = cJSON_Parse("[{\"flag\":true}]");
		assert(json);
		assert(cjson_(json, "[0]"));
		assert(cjson_ival(json, "[0]/flag", 0));
		cJSON_Delete(json);

	}
	/* cjson_ with syntax: or */
	{
		sds jsonstr = sdsnew("{\"data\":{\"params\":[{\"app_id\":\"appid\"}]}}");
		assert(jsonstr);
		cJSON * json = cJSON_Parse(jsonstr);
		assert(json);
		assert(!cjson_(json, "data/params["));
		assert(!cjson_(json, "data/params]"));
		assert(!cjson_(json, "data/params[]"));
		assert(!cjson_(json, "data/params[ ]"));
		assert(!cjson_(json, "data/params[a]"));
		assert(cjson_(json, "data/params[0]"));
		assert(cjson_(json, "data/params[-1]"));
		assert(cjson_(json, "data/params[1]"));
		assert(cjson_(json, "data/params[999]"));

		assert(!cjson_(json, "data/params[/app_id"));
		assert(!cjson_(json, "data/params]/app_id"));
		assert(!cjson_(json, "data/params[]/app_id"));
		assert(!cjson_(json, "data/params[ ]/app_id"));
		assert(!cjson_(json, "data/params[a]/app_id"));
		assert(cjson_(json, "data/params[0]/app_id"));
		assert(cjson_(json, "data/params[-1]/app_id"));
		assert(cjson_(json, "data/params[1]/app_id"));
		assert(cjson_(json, "data/params[999]/app_id"));
		assert(!cjson_(json, "data/params[999]/this_KEY_NOT_exist"));

		assert(cjson_sval(json, "data/params[0]/app_id", 0));
		cJSON_Delete(json);
		sdsfree(jsonstr);
	}
	/* cjson_sval with regex: range */
	{
		char val[64] = "";
		cJSON * json = cJSON_Parse(
				"{\"data\":{\"id\":\"123456789098765\"}}");
		assert(json);
		assert(strcmp(cjson_sval(json, "data/this_key_NOT_exist", "defvalue"), "defvalue") == 0);
		assert(strcmp(cjson_sval(json, "data/id", ""), "123456789098765") == 0);
		val[0] = '\0'; assert(strcmp(cjson_sval(json, "data/id[a,b]", val), "") == 0);

		val[0] = '\0'; assert(strcmp(cjson_sval(json, "data/id[0,13]", val), "12345678909876") == 0);
		val[0] = '\0'; assert(strcmp(cjson_sval(json, "data/id[-1,13]", val), "") == 0);
		val[0] = '\0'; assert(strcmp(cjson_sval(json, "data/id[1,13]", val), "2345678909876") == 0);
		val[0] = '\0'; assert(strcmp(cjson_sval(json, "data/id[13,13]", val), "6") == 0);
		val[0] = '\0'; assert(strcmp(cjson_sval(json, "data/id[14,13]", val), "") == 0);


		val[0] = '\0'; assert(strcmp(cjson_sval(json, "data/id[0,0]", val), "1") == 0);
		val[0] = '\0'; assert(strcmp(cjson_sval(json, "data/id[0,1]", val), "12") == 0);
		val[0] = '\0'; assert(strcmp(cjson_sval(json, "data/id[0,14]", val), "123456789098765") == 0);
		val[0] = '\0'; assert(strcmp(cjson_sval(json, "data/id[0,15]", val), "123456789098765") == 0);

		val[0] = '\0'; assert(strcmp(cjson_sval(json, "data/id[14,14]", val), "5") == 0);
		val[0] = '\0'; assert(strcmp(cjson_sval(json, "data/id[14,15]", val), "5") == 0);
		val[0] = '\0'; assert(strcmp(cjson_sval(json, "data/id[15,14]", val), "") == 0);
		val[0] = '\0'; assert(strcmp(cjson_sval(json, "data/id[15,15]", val), "") == 0);
		val[0] = '\0'; assert(strcmp(cjson_sval(json, "data/id[-1,-1]", val), "") == 0);

		cJSON_Delete(json);
	}
	/* cjson_ with syntax: or */
	{
		sds jsonstr = sdsnew("{\"data\":{\"id2\":\"123456789098765\"}}");
		assert(jsonstr);
		cJSON * json = cJSON_Parse(jsonstr);
		assert(json);
		assert(cjson_(json, "|") == json);
		assert(cjson_(json, "data"));
		assert(cjson_(json, "data|"));
		assert(cjson_(json, "|data"));
		assert(cjson_(json, "this_id_NOT_exist|data"));
		assert(cjson_(json, "|this_id_NOT_exist|data"));
		assert(cjson_(json, "this_id_NOT_exist|data|"));
		
		assert(cjson_(json, "data/this_id_NOT_exist|id2"));
		assert(cjson_(json, "data/this_id_NOT_exist|this_id_NOT_exist_too|id2"));

		cJSON_Delete(json);
		sdsfree(jsonstr);
	}
	{
		cJSON * json = cJSON_Parse("{\"phone\":\"13066969666   \"}");
		assert(json);
		rc = cjson_check(json, "phone", check_phone);
		assert(rc);
		assert(strcmp(cjson_sval(json, "phone", ""), "13066969666") == 0);
		cJSON_Delete(json);
	}
	{
		cJSON * json = cJSON_Parse("{\"phone\":\"13066969666\"}");
		assert(json);
		rc = cjson_check(json, "phone", check_phone);
		assert(rc);
		assert(strcmp(cjson_sval(json, "phone", ""), "13066969666") == 0);
		cJSON_Delete(json);
	}
	/* cjson_fmt */
	{
		cJSON * json = cJSON_Parse("{\"phone\":\"13066969666\"}");
		assert(json);
		sds s = cjson_fmt("phone=%s", json, "text");
		assert(s);
		assert(sdslen(s) == 0);
		sdsfree(s);
		cJSON_Delete(json);
	}
	{
		cJSON * json = cJSON_Parse("{\"age\":29}");
		assert(json);
		sds s = cjson_fmt("age=%d", json, "age");
		assert(s);
		assert(strcmp(s, "age=29") == 0);
		sdsfree(s);
		cJSON_Delete(json);
	}
	{
		cJSON * json = cJSON_Parse("{\"phone\":\"13066969666\"}");
		assert(json);
		sds s = cjson_fmt("phone=%s", json, "phone");
		assert(s);
		assert(strcmp(s, "phone=13066969666") == 0);
		sdsfree(s);
		cJSON_Delete(json);
	}
	/* cjson_exists */
	{
		cJSON * json = cJSON_Parse("{\"page\":1,\"line\":10,\"params\":{\"hasChild\":1,\"deptCode\":\"999951688\""
				",\"accessToken\":\"679cf9f8c5b04965b820a5b4103f2020\""
				",\"name\":\"name10\"}}");
		assert(json);
		assert(cjson_exists(json, "/page", "page/", "page", "params"
				, "/params/name", "params/name", "/params/name/", 0));
		assert(!cjson_exists(json, "/NOT_EXIST", 0));
		cJSON_Delete(json);
	}
	/* cjson_col */
	{
		cJSON * json = cJSON_Parse("{\"objs\":[{\"id\":\"2384\", \"name\":\"jack\"},{\"id\":\"2385\", \"name\":\"tom\"}]}");
		assert(json);
		assert(!cjson_col(json, "objs/id/a"));
		assert(!cjson_col(json, "objs/id/a/"));
		assert(!cjson_col(json, "/objs/id/a"));
		assert(!cjson_col(json, "objs/id/a/"));
		cJSON_Delete(json);
	}
	{
		cJSON * json = cJSON_Parse("{\"objs\":[{\"id\":\"2384\", \"name\":\"jack\"},{\"id\":\"2385\", \"name\":\"tom\"}]}");
		assert(json);
		assert(!cjson_col(json, "objs/phone"));

		cJSON * ids = cjson_col(json, "objs/id");
		assert(ids);

		fprintf(stdout, "%s: json='%s'\n", __FUNCTION__, cJSON_PrintUnformatted(ids));
		assert(strcmp(cJSON_PrintUnformatted(ids), "[\"2384\",\"2385\"]") == 0);

		cJSON_Delete(ids);
		cJSON_Delete(json);
	}
	{
		cJSON * json = cJSON_Parse("{\"line\":10,\"params\":{\"objs\":[{\"id\":\"2384\", \"name\":\"jack\"},{\"id\":\"2385\", \"name\":\"tom\"}]}}");
		assert(json);
		cJSON * ids = cjson_col(json, "params/objs/id");
		assert(ids);
		fprintf(stdout, "%s: json='%s'\n", __FUNCTION__, cJSON_PrintUnformatted(ids));
		assert(strcmp(cJSON_PrintUnformatted(ids), "[\"2384\",\"2385\"]") == 0);

		cJSON_Delete(ids);
		cJSON_Delete(json);
	}
	{
		cJSON * json = cJSON_Parse("{\"line\":10,\"params\":{\"objs\":[{\"id\":\"2384\", \"name\":\"jack\"},{\"id\":\"2385\", \"name\":\"tom\"}]}}");
		assert(json);
		cJSON * ids = cjson_col(json, "params/objs/id|name");
		assert(ids);
		fprintf(stdout, "%s: json='%s'\n", __FUNCTION__, cJSON_PrintUnformatted(ids));
		assert(strcmp(cJSON_PrintUnformatted(ids), "[\"2384\",\"2385\"]") == 0);

		cJSON_Delete(ids);
		cJSON_Delete(json);
	}
	{
		cJSON * json = cJSON_Parse("{\"line\":10,\"params\":{\"objs\":[{\"id\":\"2384\", \"name\":\"jack\"},{\"id\":\"2385\", \"name\":\"tom\"}]}}");
		assert(json);
		cJSON * ids = cjson_col(json, "params/objs/name|id");
		assert(ids);
		fprintf(stdout, "%s: json='%s'\n", __FUNCTION__, cJSON_PrintUnformatted(ids));
		assert(strcmp(cJSON_PrintUnformatted(ids), "[\"jack\",\"tom\"]") == 0);

		cJSON_Delete(ids);
		cJSON_Delete(json);
	}
	{
		cJSON * json = cJSON_Parse("{\"line\":10,\"params\":{\"objs\":[{\"id\":\"2384\", \"name1\":\"jack\"},{\"id\":\"2385\", \"name1\":\"tom\"}]}}");
		assert(json);
		cJSON * ids = cjson_col(json, "params/objs/name|id");
		assert(ids);
		fprintf(stdout, "%s: json='%s'\n", __FUNCTION__, cJSON_PrintUnformatted(ids));
		assert(strcmp(cJSON_PrintUnformatted(ids), "[\"2384\",\"2385\"]") == 0);

		cJSON_Delete(ids);
		cJSON_Delete(json);
	}
	{
		cJSON * json = cJSON_Parse("{\"page\":1,\"line\":10,\"params\":{\"hasChild\":1,\"deptCode\":\"999951688\""
				",\"accessToken\":\"679cf9f8c5b04965b820a5b4103f2020\""
				",\"name\":\"name10\"}}");
		assert(json);

		assert(cjson_is(json, "line", cJSON_IsNumber));
		assert(!cjson_is(json, "params/hasChild", cJSON_IsString));
		assert(cjson_is(json, "params/hasChild", cJSON_IsNumber));
		assert(cjson_is(json, "params/deptCode", cJSON_IsString));

		cJSON_Delete(json);
	}

	return 0;
}

#endif /* NDEBUG */
#endif //LIBHP_WITH_CJSON
