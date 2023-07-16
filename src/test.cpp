 /*!
 * This file is PART of xhhp project
 * @author hongjun.liao <docici@126.com>, @date 2020/7/9
 *
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifndef NDEBUG

#ifdef LIBHP_WITH_ZLOG
#include "zlog.h"
#endif
#include "hphdrs.h"
#include <iostream>
#include "libhp.h"
/////////////////////////////////////////////////////////////////////////////////////////
//deps/c-vector/example.c
//deps/c-vector/example.cc
extern "C" {
int test_cvector_main(int argc, char *argv[]);
int test_cvector_cpp_main(int argc, char *argv[]);
}
#define run_test(func) do {                 \
	hp_log(stdout, "begin test: %s ...\n", #func);	\
	rc = func(argc, argv); assert(rc == 0); \
	hp_log(stdout, "test %s done\n", #func);	    \
} while(0)
/////////////////////////////////////////////////////////////////////////////////////////

int libhp_all_tests_main(int argc, char ** argv)
{
	int rc;

	hp_log_level = 9;
#ifdef LIBHP_WITH_ZLOG
	rc = zlog_init("zlog.conf"); assert(rc == 0);
	rc = dzlog_set_category("libhp"); assert(rc == 0);
#endif

	run_test(test_hp_config_main);
	run_test(test_cvector_main);
	run_test(test_cvector_cpp_main);
	run_test(test_hp_libc_main);
	run_test(test_hp_err_main);

	// hp_log()
	{
		hp_log(std::cout, "0 s, 0 char *\n");
		hp_log(std::cout, "0 s, 1 char *\n", __FUNCTION__);
		hp_log(std::cout, "1 s, 0 char *: '%s'\n");
		hp_log(std::cout, "1 s, 1 char *: '%s'\n", __FUNCTION__);

		hp_log(std::cout, "0 s, 0 string\n");
		hp_log(std::cout, "0 s, 1 string\n", std::string("hello"));
		hp_log(std::cout, "1 s, 0 string: '%s'\n");
		hp_log(std::cout, "1 s, 1 string: '%s'\n", std::string("hello"));


		hp_log(std::cout, "2 s, 0 string: '%s' '%s'\n");
		hp_log(std::cout, "2 s, 1 string: '%s' '%s'\n", std::string("hello"));
		hp_log(std::cout, "0 s, 2 string\n", std::string("hello"), std::string("world"));
		hp_log(std::cout, "1 s, 2 string: '%s'\n", std::string("hello"), std::string("world"));
		hp_log(std::cout, "2 s, 2 string: '%s' '%s'\n", std::string("hello"), std::string("world"));

		hp_log(std::cout, "2 s, 1 string, 1 int: '%s' '%s'\n", std::string("hello"), (int)5);
		hp_log(std::cout, "2 d, 1 string, 1 int: '%d' '%d'\n", std::string("hello"), (int)5);

		hp_log(std::cout, "");
		hp_log(std::cout, "%");
		hp_log(std::cout, "%%");
		hp_log(std::cout, "%%%");
		hp_log(std::cout, "%%%%");
		hp_log(std::cout, "\n");

		hp_log(std::cout, "%%'%s'%%'%s'%%\n", "hello", std::string("world") );
		hp_log(std::cout, "'%s'%%%'%s'\n", "hello", std::string("world") );
	}

	run_test(test_hp_log_main);

	run_test(test_hp_msg_main);
	run_test(test_hp_net_main);
	run_test(test_hp_str_main);
#ifdef LIBHP_WITH_OPTPARSE
	run_test(test_hp_opt_main);
#endif

#ifdef LIBHP_WITH_BDB
	run_test(test_hp_bdb_main);
#endif

#ifdef LIBHP_WITH_ZLIB
	run_test(test_hp_z_main);
#endif

#ifdef LIBHP_WITH_SSL
	run_test(test_hp_ssl_main);
#endif

#ifndef _MSC_VER
	run_test(test_hp_stat_main);
#ifdef LIBHP_WITH_CJSON
	run_test(test_hp_cjson_main);
	run_test(test_hp_var_main);
#endif
#else
	run_test(test_hp_iocp_main);
#endif /* _MSC_VER */

	run_test(test_hp_io_t_main);
	run_test(test_hp_epoll_main);
	run_test(test_hp_io_main);
#if (defined LIBHP_WITH_CURL) && (defined LIBHP_WITH_HTTP)
	run_test(test_hp_http_main);
#endif

#ifdef LIBHP_WITH_CURL
//	run_test(test_hp_uv_curl_main);
//	run_test(test_hp_curl_main);
#endif

#ifdef LIBHP_WITH_TIMERFD
//	run_test(test_hp_timerfd_main);
#endif

#ifdef LIBHP_WITH_REDIS
#ifdef LIBHP_WITH_WIN32_INTERROP
	int hiredis_exmaple_ae_main(int argc, char **argv);
	rc = hiredis_exmaple_ae_main(argc, argv); assert(rc == 0);
#endif

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
	run_test(test_hp_dict_main);
	run_test(test_hp_cache_main);
#ifdef LIBHP_WITH_TIMERFD
	run_test(test_hp_expire_main);
#endif
#endif

	assert(atoi(hp_config_test("hp_config_unload")) == 0);

	return rc;
}

#else
int libhp_all_tests_main(int argc, char ** argv) { return 0; }
#endif //NDEBUG
/////////////////////////////////////////////////////////////////////////////////////////
