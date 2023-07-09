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
#include "hp_assert.h"
#include "hp_config.h"
#include "inih/ini.h"		//ini_parse
#include "redis/src/dict.h" //dict
#include <string.h>
/////////////////////////////////////////////////////////////////////////////////////////
/*====================== Hash table type implementation  ==================== */
static int r_dictSdsKeyCompare(void *privdata, const void *key1,  const void *key2)
{
    int l1,l2;
    DICT_NOTUSED(privdata);

    l1 = sdslen((sds)key1);
    l2 = sdslen((sds)key2);
    if (l1 != l2) return 0;
    return memcmp(key1, key2, l1) == 0;
}

static void r_dictSdsDestructor(void *privdata, void *val)
{
    DICT_NOTUSED(privdata);

    sdsfree(val);
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
		dictAdd(cfg, sdsnew("mysql_ip"), sdsnew(mysql_ip));
		dictAdd(cfg, sdsnew("mysql_user"), sdsnew(mysql_user));
		dictAdd(cfg, sdsnew("mysql_port"), sdsfromlonglong(mysql_port));
		dictAdd(cfg, sdsnew("mysql_db"), sdsnew(mysql_db));
	}
	else if(strcmp(name, "mqtt.addr") == 0){
		/* mqtt.addr=0.0.0.0:7006 */
		char mqtt_bind[128] = "";
		int mqtt_port = 0;

		if(value && strlen(value) > 0){
			int n = sscanf(value, "%[^:]:%d", mqtt_bind, &mqtt_port);
			if(n != 2){
				return 0;
			}
		}
		/*
		 * NOTE:
		 * set mysql=
		 * will clear existing values */
		dictAdd(cfg, sdsnew("mqtt.bind"), sdsnew(mqtt_bind));
		dictAdd(cfg, sdsnew("mqtt.port"), sdsfromlonglong(mqtt_port));
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

		dictAdd(cfg, sdsnew("redis_ip"), sdsnew(redis_ip));
		dictAdd(cfg, sdsnew("redis_port"), sdsfromlonglong(redis_port));
	}

	dictAdd(cfg, sdsnew(name), sdsnew(value));

	return 1;
}

static char const * hp_config_load(char const * id)
{
	if(!id) return 0;
	char f = 0;
	static dict * s_config = 0;
	if(!s_config){
		s_config = dictCreate(&configTableDictType, 0);
		f = 1;
	}
	assert(s_config);

	if(strcmp(id, "hp_config_unload") == 0 && s_config){
		dictRelease(s_config);
		s_config = 0;
		return "0";
	}

	int f1 = (access("config.ini", F_OK) == 0 &&
				ini_parse("config.ini", inih_handler, s_config));
	int f2 = !f1 && (access("test/config.ini", F_OK) == 0 &&
				ini_parse("test/config.ini", inih_handler, s_config));
	if (!(f1 || f2)) {
		hp_log(stderr, "%s: ini_parse failed for  '%s'/'%s' \n", __FUNCTION__, "config.ini", "test/config.ini" );
		return "";
	}

	if(f && hp_log_level > 4){
		dictIterator * iter = dictGetIterator(s_config);
		dictEntry * ent;
		for(ent = 0; (ent = dictNext(iter));){
			printf("'%s'=>'%s'\n", (char *)ent->key, (char *)ent->v.val);
		}
		dictReleaseIterator(iter);
	}

	sds key = sdsnew(id);
	void * v = dictFetchValue(s_config, key);
	sdsfree(key);
	return v? (char *)v : "";
}

/////////////////////////////////////////////////////////////////////////////////////////////
hp_config_t hp_config_test = hp_config_load;

/////////////////////////////////////////////////////////////////////////////////////////

int test_hp_config_main(int argc, char ** argv)
{
	char const * loglevel = hp_config_test("loglevel");
	hp_assert(loglevel && strlen(loglevel) > 0, "loglevel='%s'", loglevel);
	assert(atoi(hp_config_test("hp_config_unload")) == 0);

	return 0;
}


#endif //NDEBUG

/////////////////////////////////////////////////////////////////////////////////////////
