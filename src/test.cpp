 /*!
 * This file is PART of xhhp project
 * @author hongjun.liao <docici@126.com>, @date 2020/7/9
 *
 * */
#ifdef HAVE_CONFIG_H
#include "hp_config.h"
#endif /* HAVE_CONFIG_H */

#ifndef NDEBUG

#ifdef LIBHP_WITH_ZLOG
#include "zlog.h"
#endif
#include <iostream>
#include <cstring>
#include "hp/hphdrs.h"
/////////////////////////////////////////////////////////////////////////////////////////
//deps/c-vector/example.c
//deps/c-vector/example.cc
extern "C" {
int test_cvector_main(int argc, char *argv[]);
int test_cvector_cpp_main(int argc, char *argv[]);
int hiredis_exmaple_ae_main(int argc, char **argv);
}
#define run_test(func) do {                 \
	hp_log(stdout, "begin test: %s ...\n", #func);	\
	rc = func(argc, argv); assert(rc == 0); \
	hp_log(stdout, "test %s done\n", #func);	    \
} while(0)
/////////////////////////////////////////////////////////////////////////////////////////

static int test_inih_handler(void* user, const char* section, const char* name,
                   const char* value)
{
	assert(user);
	hp_ini * ini = (hp_ini*)user;
	dict* d = ini->dict;
	assert(d);

	if(section && ini->section[0] == '\0') strncpy(ini->section, section, sizeof(ini->section) - 1);

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
		dictReplace(d, sdsnew("mysql_ip"), sdsnew(mysql_ip));
		dictReplace(d, sdsnew("mysql_user"), sdsnew(mysql_user));
		dictReplace(d, sdsnew("mysql_port"), sdsfromlonglong(mysql_port));
		dictReplace(d, sdsnew("mysql_db"), sdsnew(mysql_db));
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
		dictReplace(d, sdsnew("mqtt.bind"), sdsnew(mqtt_bind));
		dictReplace(d, sdsnew("mqtt.port"), sdsfromlonglong(mqtt_port));
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

		dictReplace(d, sdsnew("redis_ip"), sdsnew(redis_ip));
		dictReplace(d, sdsnew("redis_port"), sdsfromlonglong(redis_port));
	}

	sds k = (section && section[0]? sdscatfmt(sdsempty(), "%s/%s", section, name) : sdsnew(name));
	dictReplace(d, k, sdsnew(value));

	return 1;
}

/////////////////////////////////////////////////////////////////////////////////////////
//global configure for all tests
static hp_ini definiobj = {.parser = test_inih_handler};
hp_ini * hp_config_test = &definiobj;
#define cfg(k) hp_ini_exec(hp_config_test, (k))
#define cfgi(k) atoi(cfg(k))

/////////////////////////////////////////////////////////////////////////////////////////

int libhp_all_tests_main(int argc, char ** argv)
{
	int rc;

	hp_log_level = 9;
	if(!(cfgi("#load config.ini") == 0 || cfgi("#load test/config.ini") == 0)){
		fprintf(stderr, "%s: cannot find config.ini needed by these tests\n", __FUNCTION__);
		return -1;
	}
	cfg("#show");
	assert(strlen(cfg("test/web_root")) > 0 && strcmp(cfg("test/web_root"), cfg("web_root")) == 0);
	// hp_log()
	{
		hp_log(stdout, "0 s, 0 char *\n");
		hp_log(stdout, "0 s, 1 char *\n", __FUNCTION__);
		hp_log(stdout, "1 s, 0 char *: '%s'\n");
		hp_log(stdout, "1 s, 1 char *: '%s'\n", __FUNCTION__);

		hp_log(stdout, "0 s, 0 string\n");
		hp_log(stdout, "0 s, 1 string\n", std::string("hello"));
		hp_log(stdout, "1 s, 0 string: '%s'\n");
		hp_log(stdout, "1 s, 1 string: '%s'\n", std::string("hello"));


		hp_log(stdout, "2 s, 0 string: '%s' '%s'\n");
		hp_log(stdout, "2 s, 1 string: '%s' '%s'\n", std::string("hello"));
		hp_log(stdout, "0 s, 2 string\n", std::string("hello"), std::string("world"));
		hp_log(stdout, "1 s, 2 string: '%s'\n", std::string("hello"), std::string("world"));
		hp_log(stdout, "2 s, 2 string: '%s' '%s'\n", std::string("hello"), std::string("world"));

		hp_log(stdout, "2 s, 1 string, 1 int: '%s' '%s'\n", std::string("hello"), (int)5);
		hp_log(stdout, "2 d, 1 string, 1 int: '%d' '%d'\n", std::string("hello"), (int)5);

		hp_log(stdout, "");
		hp_log(stdout, "%");
		hp_log(stdout, "%%");
		hp_log(stdout, "%%%");
		hp_log(stdout, "%%%%");
		hp_log(stdout, "\n");

		hp_log(stdout, "%%'%s'%%'%s'%%\n", "hello", std::string("world") );
		hp_log(stdout, "'%s'%%%'%s'\n", "hello", std::string("world") );

		hp_log(stdout, "%%p=%p\n", &rc);
	}
	run_test(test_hp_config_main);
	run_test(test_cvector_main);
	run_test(test_cvector_cpp_main);
	run_test(test_hp_stdlib_main);
	run_test(test_hp_err_main);
	run_test(test_hp_log_main);
	run_test(test_hp_msg_main);
	run_test(test_hp_net_main);
	run_test(test_hp_str_main);
	run_test(test_hp_dict_main);
#ifdef LIBHP_WITH_HTTP
	run_test(test_hp_url_main);
#endif
#ifdef LIBHP_WITH_SSL
	run_test(test_hp_ssl_main);
#endif

#ifdef HAVE_UNISTD_H
	run_test(test_hp_io_main);
#endif //HAVE_POLL_H
#ifdef HAVE_POLL_H
	run_test(test_hp_poll_main);
#endif //HAVE_POLL_H
#ifdef HAVE_SYS_EPOLL_H
	run_test(test_hp_epoll_main);
#endif //#ifdef HAVE_SYS_EPOLL_H

#ifdef LIBHP_WITH_OPTPARSE
	run_test(test_hp_opt_main);
#endif

#ifdef LIBHP_WITH_BDB
	run_test(test_hp_bdb_main);
#endif

#ifdef LIBHP_WITH_ZLIB
	run_test(test_hp_z_main);
#endif

#ifndef _MSC_VER
	run_test(test_hp_stat_main);
#ifdef LIBHP_WITH_CJSON
	run_test(test_hp_cjson_main);
#ifdef LIBHP_DEPRECADTED
	run_test(test_hp_var_main);
#endif
#endif
#else
	run_test(test_hp_iocp_main);
#endif /* _MSC_VER */
	run_test(test_hp_io_t_main);
#if (defined LIBHP_WITH_CURL) && (defined LIBHP_WITH_HTTP)
	run_test(test_hp_http_main);
#endif

#ifdef LIBHP_WITH_CURL
	run_test(test_hp_curl_main);
#endif

#ifdef LIBHP_WITH_TIMERFD
	run_test(test_hp_timerfd_main);
#endif

#ifdef LIBHP_WITH_REDIS
//	rc = hiredis_exmaple_ae_main(argc, argv); assert(rc == 0);

#ifdef LIBHP_WITH_MYSQL
	run_test(test_hp_mysql_main);
#endif

#ifdef LIBHP_WITH_MQTT
	run_test(test_hp_mqtt_main);
#endif
	run_test(test_hp_redis_main);
	run_test(test_hp_pub_main);
#endif
#if defined(__linux__)
	run_test(test_hp_inotify_main);
#endif

#ifdef LIBHP_DEPRECADTED
	run_test(test_hp_cache_main);
#ifdef LIBHP_WITH_TIMERFD
	run_test(test_hp_expire_main);
#endif
#endif

	cfg("#unload");
	return rc;
}

#endif //NDEBUG
/////////////////////////////////////////////////////////////////////////////////////////
