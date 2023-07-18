 /*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2023/7/8
 *
 * */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifndef NDEBUG

#include <unistd.h>
#include "hp/hp_assert.h"
#include "hp/hp_config.h"
#include "inih/ini.h"		//ini_parse
#include "redis/src/dict.h" //dict
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


static int inih_handler(void* user, const char* section, const char* name,
                   const char* value)
{
	dict* cfg = (dict*)user;
	assert(cfg);

	if(strcmp(name, "mysql") == 0){
		/* cdmc@192.168.50.33:3306:cdmc */
		char mysql_ip[64] = "", mysql_user[64] = "", mysql_db[64] = "";
		int mysql_port = 0;

		if(value && strlen(value) > 0){
			int n = sscanf(value, "%[^@]@%[^:]:%d:%s", mysql_user, mysql_ip, &mysql_port, mysql_db);
			if(n != 4){
				return 0;
			}
		}
		/*
		 * NOTE:
		 * set mysql=
		 * will clear existing values */
		dictReplace(cfg, sdsnew("mysql_ip"), sdsnew(mysql_ip));
		dictReplace(cfg, sdsnew("mysql_user"), sdsnew(mysql_user));
		dictReplace(cfg, sdsnew("mysql_port"), sdsfromlonglong(mysql_port));
		dictReplace(cfg, sdsnew("mysql_db"), sdsnew(mysql_db));
	}
	else if(strcmp(name, "mqtt.addr") == 0){
		/* mqtt.addr=0.0.0.0:7006 */
		char mqtt_bind[128] = "";
		int mqtt_port = 0;

		if(value && strlen(value) > 0){
			char const * pp = strstr(value, "://");
			int n = sscanf((pp? value + (pp - value + 3) : value), "%[^:]:%d", mqtt_bind, &mqtt_port);
			if(n != 2){
				return 0;
			}
		}
		/*
		 * NOTE:
		 * set mysql=
		 * will clear existing values */
		dictReplace(cfg, sdsnew("mqtt.bind"), sdsnew(mqtt_bind));
		dictReplace(cfg, sdsnew("mqtt.port"), sdsfromlonglong(mqtt_port));
	}
	else if(strcmp(name, "redis") == 0){

		char redis_ip[64] = "";
		int redis_port = 0;

		/* NOTE:
		 * set redis=
		 * will clear existing values */
		if(value && strlen(value) > 0){
			int n = sscanf(value, "%[^:]:%d", redis_ip, &redis_port);
			if(n != 2){
				return 0;
			}
		}

		dictReplace(cfg, sdsnew("redis_ip"), sdsnew(redis_ip));
		dictReplace(cfg, sdsnew("redis_port"), sdsfromlonglong(redis_port));
	}

	dictReplace(cfg, sdsnew(name), sdsnew(value));

	return 1;
}

static char const * hp_config_load(char const * id)
{
	if(!id) return 0;

	int n;
	static dict * s_config = 0;
	if(!s_config){
		s_config = dictCreate(&configTableDictType);
	}
	assert(s_config);

	if(strcmp(id, "#unload") == 0 && s_config){
		dictRelease(s_config);
		s_config = 0;
		return "0";
	}
	else if(strncmp(id, "#load", n = strlen("#load")) == 0 && strlen(id) >= (n + 2)){
		char const * f = id + n + 1;
		int line = 0;
		if ((line = ini_parse(f, inih_handler, s_config)) != 0) {
			hp_log(stderr, "%s: ini_parse failed for '%s' at line %d\n", __FUNCTION__, f, line);
			return "-1";
		}
		return "0";
	}
	else if(strncmp(id, "#set", n = strlen("#set")) == 0 && strlen(id) >= (n + 4)){

		char buf[128]; strncpy(buf, id, sizeof(buf));
		char * k = buf + n + 1, * v = strchr(k, ' ');
		if(!v) return "-1";

		*v='\0'; ++v;
		dictReplace(s_config, sdsnew(k), sdsnew(v));
		return "0";
	}
	else if(strcmp(id, "#show") == 0){
		dictIterator * iter = dictGetIterator(s_config);
		dictEntry * ent;
		for(ent = 0; (ent = dictNext(iter));){
			printf("'%s'=>'%s'\n", (char *)dictGetKey(ent), (char *)dictGetVal(ent));
		}
		dictReleaseIterator(iter);
		return "0";
	}

	sds key = sdsnew(id);
	void * v = dictFetchValue(s_config, key);
	sdsfree(key);
	return v? (char *)v : "";
}

/////////////////////////////////////////////////////////////////////////////////////////////
hp_config_t hp_config_test = hp_config_load;
#define cfg hp_config_test
#define cfgi(k) atoi(cfg(k))

/////////////////////////////////////////////////////////////////////////////////////////

int test_hp_config_main(int argc, char ** argv)
{
	assert(cfgi("#set test.key.name 23") == 0 && cfgi("test.key.name") == 23);
	assert(cfgi("#set test.key.name 24") == 0 && cfgi("test.key.name") == 24);
	hp_assert(cfgi("#load bitcoin.conf") == 0, "'#load bitcoin.conf' failed");
	hp_assert(cfgi("#load this_file_not_exist.conf") != 0, "'#load this_file_not_exist.conf' OK?");
	hp_assert(strlen(cfg("loglevel")) > 0, "loglevel NOT found");
	hp_assert(strlen(cfg("#show")) > 0, "#show failed");
	return 0;
}


#endif //NDEBUG

/////////////////////////////////////////////////////////////////////////////////////////
