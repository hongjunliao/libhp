/*!
 *  This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2019/11/27
 *
 * MySQL utils
 *
 * */
#ifndef LIBHP_MYSQL_H
#define LIBHP_MYSQL_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_WITH_MYSQL

#include "cjson/cJSON.h"	/* cJSON */
#include "hp/sdsinc.h"		/* sds */
#include "mysql/mysql.h"

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////////
/* cdmc@192.168.50.33:3306:cdmc */
int hp_mysql_connect_addr(MYSQL ** mysql, char const * addr, char const * pwd, char const * initsql);
int hp_mysql_connect(MYSQL ** mysql, MYSQL * (* fn)(MYSQL *mysql), char const * initsql);

typedef char (hp_mysql_err_t)[128];
int hp_mysql_query(MYSQL *mysql, char const *sql, cJSON ** data
		, int * rows, my_ulonglong * affected, hp_mysql_err_t err);

int hp_mysql_query_all(MYSQL *mysql, char const *sql[], int n_sql, cJSON * data[], char * err);

/*
 * NOTE: MySQL transaction based
 * */
cJSON * hp_mysql_tmpl(MYSQL * mysql, void * json
		, cJSON * (* check_cb)(MYSQL * mysql, void * json, sds * errstr)
		, int 	  (* sql_cb)(cJSON const * ijson, sds * sql_count, sds * sql_query, sds * errstr)
		, cJSON * (* result_cb)(int err, char const * errstr, cJSON * jrow_count, cJSON * jrows)
		, int flags);
/////////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
int test_hp_mysql_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif
#endif /* LIBHP_WITH_MYSQL */
#endif /* LIBHP_MYSQL_H */
