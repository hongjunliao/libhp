 /*!
 * This file is PART of xhhp project
 * @author hongjun.liao <docici@126.com>, @date 2020/7/9
 *
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "Win32_Interop.h"
#include "redis/src/dict.h"
#ifdef LIBHP_WITH_WIN32_INTERROP
#include "redis/src/Win32_Interop/Win32_QFork.h"
#endif /* LIBHP_WITH_WIN32_INTERROP */

#include <assert.h>
#include <sys/time.h>
#include "hphdrs.h"
#include "c-vector/cvector.h"
#include "inih/ini.h"
#ifdef LIBHP_WITH_ZLOG
#include "zlog.h"
#endif
#include "hp/libhp.h"

static int gloglevel = 0;

static dict * config = 0;
static char const * cfg(char const * id) {
	sds key = sdsnew(id);
	void * v = dictFetchValue(config, key);
	sdsfree(key);
	return v? (char *)v : "";
}
static hp_config_t g_conf = cfg;

/////////////////////////////////////////////////////////////////////////////////////////
/*====================== Hash table type implementation  ==================== */
static int dictSdsKeyCompare(void *privdata, const void *key1,  const void *key2)
{
    int l1,l2;
    DICT_NOTUSED(privdata);

    l1 = sdslen((sds)key1);
    l2 = sdslen((sds)key2);
    if (l1 != l2) return 0;
    return memcmp(key1, key2, l1) == 0;
}

/* A case insensitive version used for the command lookup table and other
 * places where case insensitive non binary-safe comparison is needed. */
static int dictSdsKeyCaseCompare(void *privdata, const void *key1,
        const void *key2)
{
    DICT_NOTUSED(privdata);

    return strcasecmp(key1, key2) == 0;
}

static void dictSdsDestructor(void *privdata, void *val)
{
    DICT_NOTUSED(privdata);

    sdsfree(val);
}

static uint64_t dictSdsHash(const void *key) {
    return dictGenHashFunction((unsigned char*)key, sdslen((char*)key));
}

/* config table. char * => char * */
static dictType configTableDictType = {
	dictSdsHash,            /* hash function */
    NULL,                   /* key dup */
    NULL,                   /* val dup */
	dictSdsKeyCompare,      /* key compare */
    dictSdsDestructor,      /* key destructor */
	dictSdsDestructor       /* val destructor */
};
/////////////////////////////////////////////////////////////////////////////////////////

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

/////////////////////////////////////////////////////////////////////////////////////////

int libhp_all_tests_main(int argc, char ** argv)
{
	int rc;

	/* config */
	gloglevel = 9;
	config = dictCreate(&configTableDictType, 0);
	sds conf = sdsnew("config.ini");
#ifdef LIBHP_WITH_ZLOG
	rc = zlog_init("zlog.conf"); assert(rc == 0);
	rc = dzlog_set_category("libhp"); assert(rc == 0);
#endif

	fprintf(stdout, "%s: using %s as config for libhp tests\n", __FUNCTION__, conf);

	FILE * f = fopen(conf, "r");
	if(f) {
		fclose(f);
		if (ini_parse(conf, inih_handler, config) < 0) {
			fprintf(stderr, "%s: ini_parse '%s' \n", __FUNCTION__, conf);
			return -3;
		}
	}
	else return -1;

	rc = test_hp_io_t_main(argc, argv); assert(rc == 0);
	rc = test_cvector_main(argc, argv); assert(rc == 0);
	rc = test_cvector_cpp_main(argc, argv); assert(rc == 0);
	rc = test_hp_libc_main(argc, argv);
	rc = test_hp_cache_main(argc, argv); assert(rc == 0);
	rc = test_hp_err_main(argc, argv); assert(rc == 0);
	rc = test_hp_log_main(argc, argv);
	rc = test_hp_msg_main(argc, argv); assert(rc == 0);
	rc = test_hp_net_main(argc, argv); assert(rc == 0);
	rc = test_hp_str_main(argc, argv); assert(rc == 0);
	rc = test_hp_opt_main(argc, argv); assert(rc == 0);

#ifdef LIBHP_WITH_MYSQL
	rc = test_hp_mysql_main(argc, argv);
#endif

#if (defined LIBHP_WITH_CURL) && (defined LIBHP_WITH_HTTP)
	rc = test_hp_http_main(argc, argv); assert(rc == 0);
#endif

#ifdef LIBHP_WITH_MQTT
	rc = test_hp_mqtt_main(argc, argv);; assert(rc == 0);
#endif

#ifdef LIBHP_WITH_BDB
	rc = test_hp_bdb_main(argc, argv);; assert(rc == 0);
#endif

#ifdef LIBHP_WITH_ZLIB
	rc = test_hp_z_main(argc, argv); assert(rc == 0);
#endif

#ifdef LIBHP_WITH_SSL
	rc = test_hp_ssl_main(argc, argv); assert(rc == 0);
#endif

#ifndef _MSC_VER
	rc = test_hp_stat_main(argc, argv); assert(rc == 0);
	rc = test_hp_io_main(argc, argv); assert(rc == 0);
#ifdef LIBHP_WITH_CJSON
	rc = test_hp_cjson_main(argc, argv); assert(rc == 0);
	rc = test_hp_var_main(argc, argv);
#endif
#if defined(__linux__)
	rc = test_hp_inotify_main(argc, argv);
	rc = test_hp_epoll_main(argc, argv); assert(rc == 0);
#endif
	rc = test_hp_io_main(argc, argv); assert(rc == 0);
#ifdef LIBHP_WITH_CURL
	rc = test_hp_curl_main(argc, argv); assert(rc == 0);
#endif
#else
	rc = test_hp_iocp_main(argc, argv); assert(rc == 0);
#endif /* _MSC_VER */

#ifdef LIBHP_WITH_CURL
	rc = test_hp_uv_curl_main(argc, argv); assert(rc == 0);
#endif

#ifdef LIBHP_DEPRECADTED
	rc = test_hp_dict_main(argc, argv);
#ifdef LIBHP_WITH_TIMERFD
	rc = test_hp_expire_main(argc, argv); assert(rc == 0);
#endif
#endif

#ifdef LIBHP_WITH_TIMERFD
	rc = test_hp_timerfd_main(argc, argv); assert(rc == 0);
#endif

#ifdef LIBHP_WITH_REDIS
#ifdef LIBHP_WITH_WIN32_INTERROP
	int hiredis_exmaple_ae_main(int argc, char **argv);
	rc = hiredis_exmaple_ae_main(argc, argv); assert(rc == 0);
#endif

	rc = test_hp_redis_main(argc, argv); assert(rc == 0);
	rc = test_hp_pub_main(argc, argv); assert(rc == 0);
#endif

	/* clear */
	dictRelease(config);
	sdsfree(conf);

	return rc;
}

/////////////////////////////////////////////////////////////////////////////////////////
