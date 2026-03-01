 /*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2020/7/12
 *
 * 2024/5/3 update
 * simple, Redis's dict - based .ini configure file system
 * */
/////////////////////////////////////////////////////////////////////////////////////////

#ifdef HAVE_CONFIG_H
#include "hp_config.h"
#endif /* HAVE_CONFIG_H */

#include "hp/hp_ini.h" //hp_ini
#include "inih/ini.h"		//ini_parse
#include "hp/sdsinc.h" //sds
#include "hp/hp_log.h"
#include <assert.h>
#include <string.h>
/////////////////////////////////////////////////////////////////////////////////////////
/*====================== Hash table type implementation  ==================== */
static int r_dictSdsKeyCompare(dict *d, const void *key1, const void *key2)
{
    int l1,l2;
//    DICT_NOTUSED(privdata);

    l1 = sdslen((sds)key1);
    l2 = sdslen((sds)key2);
    if (l1 != l2) return 0;
    return memcmp(key1, key2, l1) == 0;
}

static void r_dictSdsDestructor(dict *d, void *key)
{
//    DICT_NOTUSED(privdata);

    sdsfree(key);
}

static uint64_t r_dictSdsHash(const void *key) {
    return dictGenHashFunction((unsigned char*)key, sdslen((char*)key));
}

/////////////////////////////////////////////////////////////////////////////////////////////

/* config table. char * => char * */
static dictType configTableDictType = {
	r_dictSdsHash,            /* hash function */
    NULL,                   /* key dup */
    NULL,                   /* val dup */
	r_dictSdsKeyCompare,      /* key compare */
    r_dictSdsDestructor,      /* key destructor */
	r_dictSdsDestructor       /* val destructor */
};

static int hp_config_ini_def_parser(void* user, const char* section, const char* name, const char* value)
{
	assert(user);
	hp_ini * ini = (hp_ini*)user;
	dict* d = ini->dict;
	assert(d);

	if(section && ini->section[0] == '\0') strncpy(ini->section, section, sizeof(ini->section) - 1);

	sds k = (section && section[0]? sdscatfmt(sdsempty(), "%s/%s", section, name) : sdsnew(name));
	dictReplace(d, k, sdsnew(value));
	return 1;
}

char const * hp_config_ini(hp_ini * ini, char const * k)
{
	if(!(ini && k)) return 0;
	if(!ini->parser) ini->parser = hp_config_ini_def_parser;
	if(!ini->dict)   ini->dict = dictCreate(&configTableDictType);

	int n;
	if(strcmp(k, "#unload") == 0 && ini->dict){
		if(ini->dict){
			dictRelease(ini->dict);
			ini->dict = 0;
		}
		ini->section[0] = '\0';
		return "0";
	}
	else if(strncmp(k, "#load", n = strlen("#load")) == 0 && strlen(k) >= (n + 2)){
		char const * f = k + n + 1;
		int line = 0;
		if ((line = ini_parse(f, ini->parser, ini)) != 0) {
			hp_log(stderr, "%s: ini_parse failed for '%s' at line %ini->dict\n", __FUNCTION__, f, line);
			return "-1";
		}
		return "0";
	}
	else if(strncmp(k, "#set", n = strlen("#set")) == 0 && strlen(k) >= (n + 4)){

		char buf[128]; strncpy(buf, k, sizeof(buf));
		char * k_ = buf + n + 1, * v = strchr(k_, ' ');
		if(!v) return "-1";

		*v='\0'; ++v;
		dictReplace(ini->dict, sdsnew(k_), sdsnew(v));
		return "0";
	}
	else if(strcmp(k, "#show") == 0){
		dictIterator * iter = dictGetIterator(ini->dict);
		dictEntry * ent;
		for(ent = 0; (ent = dictNext(iter));){
			printf("'%s'=>'%s'\n", (char *)dictGetKey(ent), (char *)dictGetVal(ent));
		}
		dictReleaseIterator(iter);
		return "0";
	}

	sds key;
	if(ini->section[0] != '\0' && !strchr(k, '/')){
		key = sdscatprintf(sdsempty(), "%s/%s", ini->section, k);
	}
	else key = sdsnew(k);

	void * v = dictFetchValue(ini->dict, key);
	sdsfree(key);
	return v? (char *)v : "";
}

/////////////////////////////////////////////////////////////////////////////////////////////
#ifndef NDEBUG
#include "hp/hp_assert.h"
#include <string.h>

/////////////////////////////////////////////////////////////////////////////////////////
static hp_ini definiobj = {.parser = 0}, * defini = &definiobj;
#define cfg(k) hp_config_ini(defini, (k))
#define cfgi(k) atoi(cfg(k))

/////////////////////////////////////////////////////////////////////////////////////////

int test_hp_config_main(int argc, char ** argv)
{
	FILE * f = fopen("test_hp_config_main.ini", "r");
	if(!f) f = fopen("test_hp_config_main.ini", "w");
	fclose(f);

	hp_assert_path("test_hp_config_main.ini", REG);
	{
		assert(cfgi("#set test.key.name 23") == 0 && cfgi("test.key.name") == 23);
		assert(cfgi("#set test.key.name 24") == 0 && cfgi("test.key.name") == 24);
		hp_assert(cfgi("#load test_hp_config_main.ini") == 0, "'#load test_hp_config_main.ini' failed");
		hp_assert(cfgi("#load this_file_not_exist.conf") != 0, "'#load this_file_not_exist.conf' OK?");

		assert(cfgi("#set loglevel 1") == 0 && cfgi("loglevel") == 1);
		assert(strlen(cfg("#show")) > 0);
		hp_assert(strlen(cfg("#show")) > 0, "#show failed");
		assert(cfgi("#unload") == 0);
	}
	return 0;
}


#endif //NDEBUG

/////////////////////////////////////////////////////////////////////////////////////////
