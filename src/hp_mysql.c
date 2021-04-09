
 /* This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2019/11/12
 *
 * mysql util
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_WITH_MYSQL

#include "hp_mysql.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>        	/* define NDEBUG to disable assertion */
#include <string.h>
#include <assert.h>        	/* define NDEBUG to disable assertion */
#include "sds/sds.h"        /* sds */
#include "cJSON/cJSON.h"	/* cJSON */
#include "mysql/mysql.h"
#include "mysql/errmsg.h"
#include "zlog.h"

extern int gloglevel;

static MYSQL * (* g_connect_cb)(MYSQL *mysql) = 0;

static sds mysql_ip = 0;
static sds mysql_user = 0;
static sds mysql_pwd = 0;
static sds mysql_db = 0;
static int mysql_port_ = 0;

#define sdscpnull(s, d) (s? sdscpy(s, d) : sdsnew(d))
/////////////////////////////////////////////////////////////////////////////////////////

static MYSQL * connect_mysql(MYSQL *mysql)
{
	if(gloglevel > 0){
	    zlog_info(zlog_get_category("main"), "connecting to MySql, --mysql='%s@%s:%d:%s', password='%s' ...\n",
	    		mysql_user, mysql_ip,
	    		mysql_port_, mysql_db, (strlen(mysql_pwd) > 0? "***" : ""));
	}

	mysql = mysql_real_connect(mysql, mysql_ip
    		, mysql_user, mysql_pwd, mysql_db, mysql_port_, NULL, CLIENT_FOUND_ROWS);
	if(!mysql){
		dzlog_error("%s: connect failed, --mysql='%s@%s:%d:%s'/'%s'\n",
				__FUNCTION__, mysql_user, mysql_ip,
				mysql_port_, mysql_db, mysql_pwd);
	}

	return mysql;
}

int hp_mysql_connect_addr(MYSQL ** mysql, char const * addr, char const * pwd, char const * initsql)
{
	if(!mysql)
		return -1;

	int rc = 0;
	g_connect_cb = connect_mysql;

	char ip[64] = "", user[64] = "", db[64] = "";

	if(!addr)
		addr = "";
	int n = sscanf(addr, "%[^@]@%[^:]:%d:%s", user, ip, &mysql_port_, db);

	mysql_ip = sdscpnull(mysql_ip, ip);
	mysql_user = sdscpnull(mysql_user, user);
	mysql_pwd = sdscpnull(mysql_pwd, pwd);
	mysql_db = sdscpnull(mysql_db, db);

    int reconnect = 1;
    mysql_options(*mysql, MYSQL_OPT_RECONNECT, &reconnect);
	if(!connect_mysql(*mysql))
		return -1;

	if(initsql)
		rc = mysql_query(*mysql, initsql);

	return rc;
}

int hp_mysql_connect(MYSQL ** mysql, MYSQL * (* fn)(MYSQL *mysql), char const * initsql)
{
	if(!fn)
		return -1;
	g_connect_cb = fn;

	int rc = fn((mysql? *mysql : 0))? 0 : -2;

	if(rc == 0 && initsql)
		rc = mysql_query(*mysql, initsql);

	return rc;
}
/*
 * @see:
 * 	https://dev.mysql.com/doc/refman/8.0/en/mysql-affected-rows.html
 *
 * query mysql and return cjson array
 * @return: 0 for OK, else error
 * */
int hp_mysql_query(MYSQL *mysql, char const *sql, cJSON ** data
		, int * rows, my_ulonglong * affected, hp_mysql_err_t err)
{
	if(!(mysql && sql))
		return -1;

	int i, rc;
	MYSQL_RES * result = 0;
	for(i = 0; i < 2; ++i){
		rc = mysql_query(mysql, sql);
		if(rc != 0){
			int myerrno = mysql_errno(mysql);
			sds myerrstr = sdsnew(mysql_error(mysql));

			if(err){
				sds errstr = sdscatprintf(sdsempty(), err, myerrno, myerrstr);
				strncpy(err, errstr, (sdslen(errstr) < 128? sdslen(errstr) : 128));
				sdsfree(errstr);
			}

			zlog_warn(zlog_get_category("MySQL"), "%s: failed, mysql_error=%d/'%s', sql='%s', retrying ...\n"
				, __FUNCTION__, myerrno, myerrstr, sql );

			if(myerrno == CR_COMMANDS_OUT_OF_SYNC)
				mysql_free_result(mysql_use_result(mysql));
			else if(g_connect_cb){
				mysql_close(mysql);
				g_connect_cb(mysql);
			}

			sdsfree(myerrstr);
			continue;
		}
		break;
	}
	if(rc != 0)
		return -3;

	result = mysql_store_result(mysql);
	if(rows)
		*rows = mysql_num_rows(result);
	if(affected)
		*affected = mysql_affected_rows(mysql);
	if(!data){
		rc = 0;
		goto ret;
	}

	*data = cJSON_CreateArray();

	if(result){
		MYSQL_ROW row;
		MYSQL_FIELD *fd;
		char field[102][102];
		int num_fields = mysql_num_fields(result);

		for (i = 0; (fd = mysql_fetch_field(result)); i++)
			strcpy(field[i], fd->name);

		MYSQL_FIELD *field_direct = 0;
		cJSON *dir1;
		while ((row = mysql_fetch_row(result))) {
			cJSON_AddItemToArray(*data, dir1 = cJSON_CreateObject());
			for (i = 0; i < num_fields; i++) {
				field_direct = mysql_fetch_field_direct(result, i);
				char const * val = row[i]? row[i] : "";
				if (IS_NUM(field_direct->type))
					cJSON_AddNumberToObject(dir1, field[i], atol(val));
				else
					cJSON_AddStringToObject(dir1, field[i], val);
			}
		}
	}

ret:
	if(result)
		mysql_free_result(result);
	return rc;
}

int hp_mysql_query_all(MYSQL *mysql, char const *sql[], int n_sql, cJSON * data[], char * err)
{
	if(!(mysql && sql && n_sql > 0))
		return -1;

	/* disable MySQL autocommit */
	int f = mysql_autocommit(mysql, 0);

	int i, j, r;

	r = 0;
	for(i = 0; i < n_sql; ++i){
		r = hp_mysql_query(mysql, sql[i], (data? &data[i] : 0), 0, 0, err);
		if(r != 0){
			mysql_rollback(mysql);

			if(data){
				for(j = 0; j < n_sql; ++j){
					cJSON_Delete(data[j]);
					data[j] = 0;
				}
			}

			break;
		}
	}

	/* reset autocommit */
	if(!f) f = 1;
	mysql_autocommit(mysql, f);

	if(r == 0 && err)
		err[0] = '\0';

	return r;
}

/////////////////////////////////////////////////////////////////////////////////////////

/**
 * MySQL query template
 */
cJSON * hp_mysql_tmpl(MYSQL * mysql, void * json
		, cJSON * (* check_cb)(MYSQL * mysql, void * json, sds * errstr)
		, int 	  (* sql_cb)(cJSON const * ijson, sds * sql_count, sds * sql_query, sds * errstr)
		, cJSON * (* result_cb)(int err, char const * errstr, cJSON * jrow_count, cJSON * jrows)
		, int flags)
{
	if(!(sql_cb && result_cb))
		return 0;

	int i, rc;
	cJSON * ojson = 0;
	sds errstr = sdsempty();
	cJSON * jrows = 0, * jrow_count = 0;
	sds sql_count = 0, sql_query = 0;

	cJSON * ijson = check_cb? check_cb(mysql, json, &errstr) : (cJSON *)json;
	if(!ijson){
		rc = -1;
		goto ret;
	}

	rc = sql_cb(ijson, &sql_count, &sql_query, &errstr);
	if(rc != 0){
		rc = -2;
		goto ret;
	}

	/* disable MySQL autocommit */
	int f = mysql_autocommit(mysql, 0);

	if(sql_query && sdslen(sql_query) > 0){
		int count = 0;
		sds * s = sdssplitlen(sql_query, sdslen(sql_query), ";", 1, &count);

		for(i = 0; i < count; ++i){
			if(sdslen(s[i]) == 0)
				continue;

			hp_mysql_err_t sqlerr = "mysql_error: %d/'%s'";
			rc = hp_mysql_query(mysql, s[i], (!jrows? (&jrows) : 0), 0, 0, sqlerr);
			if(rc != 0){
				mysql_rollback(mysql);
				errstr = sdscat(errstr, sqlerr);
				rc = -4;
				sdsfreesplitres(s, count);
				goto ret;
			}

		}
		sdsfreesplitres(s, count);
	}
	if(sql_count && sdslen(sql_count) > 0){
		int count = 0;
		sds * s = sdssplitlen(sql_count, sdslen(sql_count), ";", 1, &count);

		for(i = 0; i < count; ++i){
			if(sdslen(s[i]) == 0)
				continue;

			hp_mysql_err_t sqlerr = "mysql_error: %d/'%s'";
			rc = hp_mysql_query(mysql, s[i], (!jrow_count? (&jrow_count) : 0), 0, 0, sqlerr);
			if(rc != 0){
				mysql_rollback(mysql);
				errstr = sdscat(errstr, sqlerr);
				rc = -3;
				sdsfreesplitres(s, count);
				goto ret;
			}
		}
		sdsfreesplitres(s, count);
	}

	mysql_commit(mysql);

	/* reset autocommit */
	if(!f) f = 1;
	mysql_autocommit(mysql, f);

ret:
	ojson = result_cb(rc, errstr, jrow_count, jrows);
	assert(ojson);

	if(gloglevel > 0)
		zlog_debug(zlog_get_category("libxhmdm"), "%s: out_json='%s', in_json='%s'\n"
			, __FUNCTION__, cJSON_PrintUnformatted(ojson), (flags && json? cJSON_PrintUnformatted(json) : (char const *)json));

	if(ijson && !flags)
		cJSON_Delete(ijson);
	if(sql_query)
		sdsfree(sql_query);
	if(sql_count)
		sdsfree(sql_count);
	sdsfree(errstr);

	return ojson;
}

/////////////////////////////////////////////////////////////////////////////////////////
#ifndef NDEBUG
#include "hp_cjson.h"
#include "hp_config.h"
extern hp_config_t g_conf;

int test_hp_mysql_main(int argc, char ** argv)
{
	int i, j, rc;
	assert(g_conf);

	MYSQL mysqlopbj, * mysql = &mysqlopbj;

	mysql_server_init(argc, argv, 0);
	/* init MySQL */
	mysql = mysql_init(mysql);
	assert(mysql);

	fprintf(stdout, "%s: connecting to MySQL, --mysql='%s', password='%s' ...\n",
			__FUNCTION__, g_conf("mysql"), (strlen(g_conf("mysql.password")) > 0? "***" : ""));

	rc = hp_mysql_connect_addr(&mysql, g_conf("mysql"), g_conf("mysql.password"), "set names utf8");
	if(!(rc == 0 && mysql)){
		fprintf(stdout, "%s: connect to MySQL failed, skip this test\n", __FUNCTION__);
		return 0;
	}

	/* hp_mysql_query_all: SQL error */
	{
		char const * sql[] = { "select 1", "select1", "select 1" };
		cJSON * json[3] = { 0 };
		hp_mysql_err_t errstr = "%d/%s";
		rc = hp_mysql_query_all(mysql, sql, 3, json, errstr);
		assert(rc != 0);
		assert(!json[0] && !json[1] && !json[2]);
	}
	/* hp_mysql_query_all: OK */
	{
		char const * sql[] = { "select 1 a", "select 'hello' a", "select 1 a" };
		cJSON * json[3] = { 0 };
		hp_mysql_err_t errstr = "%d/%s";
		rc = hp_mysql_query_all(mysql, sql, 3, json, errstr);
		assert(rc == 0);
		assert(json[0] && json[1] && json[2]);
		assert(cjson_ival(json[0], "[0]/a", 0) == 1);
		assert(strcmp(cjson_sval(json[1], "[0]/a", ""), "hello") == 0);
		assert(cjson_ival(json[2], "[0]/a", 0) == 1);

		for(i = 0; i < 3;++i)
			cJSON_Delete(json[i]);
	}
	/* hp_mysql_query_all: update only */
	{
		char const * sql[] = { "CREATE TABLE test ( age int NULL )", "update test set age=1", "drop table test" };
		hp_mysql_err_t errstr = "%d/%s";
		rc = hp_mysql_query_all(mysql, sql, 3, 0, errstr);
		assert(rc == 0);
	}
	/* test mysql error */
	{
		hp_mysql_err_t err = { "" };
		assert(hp_mysql_query(mysql, "select 1", 0, 0, 0, err) == 0);
		assert(err[0] == '\0');
		assert(hp_mysql_query(mysql, "selec 1", 0, 0, 0, err) != 0);
		assert(err[0] == '\0');
		strcpy(err, "%d/%s");
		assert(hp_mysql_query(mysql, "selec a", 0, 0, 0, err) != 0);
		assert(strcmp(err, "%d/%s") != 0);

		/* sds */
		{
			sds err = sdsnewlen("%d/%s", 128);
			assert(hp_mysql_query(mysql, "selec a", 0, 0, 0, err) != 0);
			assert(strcmp(err, "%d/%s") != 0);
			sdsfree(err);
		}
	}
	/* test mysql reconnect */
	{
		 rc = mysql_query(mysql, "select 1");
		 assert(rc == 0);
		 rc = mysql_query(mysql, "select 1");
		 assert(rc != 0);
		 if(rc != 0) fprintf(stdout, "%s: MySQL error: %d/'%s'\n", __FUNCTION__, mysql_errno(mysql), mysql_error(mysql));

		//  rc = mysql_reset_connection(mysql);
		 if(rc != 0) fprintf(stdout, "%s: MySQL error: %d/'%s'\n", __FUNCTION__, mysql_errno(mysql), mysql_error(mysql));
		 rc = mysql_query(mysql, "select 1");
		 assert(rc != 0);

		 mysql_next_result(mysql);
		 rc = mysql_query(mysql, "select 1");
		 if(rc != 0) fprintf(stdout, "%s: MySQL error: %d/'%s'\n", __FUNCTION__, mysql_errno(mysql), mysql_error(mysql));
		 assert(rc != 0);

		 mysql_free_result(0);
		 mysql_free_result(mysql_use_result(mysql));
		 rc = mysql_query(mysql, "select 1");
		 if(rc != 0) fprintf(stdout, "%s: MySQL error: %d/'%s'\n", __FUNCTION__, mysql_errno(mysql), mysql_error(mysql));
		 assert(rc == 0);
		 mysql_free_result(mysql_use_result(mysql));

		//  rc = mysql_reset_connection(mysql);
		 if(rc != 0) fprintf(stdout, "%s: MySQL error: %d/'%s'\n", __FUNCTION__, mysql_errno(mysql), mysql_error(mysql));
		 rc = mysql_query(mysql, "select 1");
		 if(rc != 0) fprintf(stdout, "%s: MySQL error: %d/'%s'\n", __FUNCTION__, mysql_errno(mysql), mysql_error(mysql));
		 assert(rc == 0);

		 rc = hp_mysql_query(mysql, "select 1", 0, 0, 0, 0);
		 assert(rc == 0);
		 rc = hp_mysql_query(mysql, "select 1", 0, 0, 0, 0);
		 assert(rc == 0);
		 rc = hp_mysql_query(mysql, "select 1", 0, 0, 0, 0);
		 assert(rc == 0);
	}

	cJSON * check_cb(MYSQL * mysql, char const * jsonstr, sds * errstr)
	{
		cJSON * ijson = cJSON_Parse(jsonstr);
		if(!ijson){
			if(errstr)
				*errstr = sdscpy(*errstr, "invalid json");
		}
		return ijson;
	}


	cJSON * result_cb(int err, char const * errstr, cJSON * jrow_count, cJSON * jrows)
	{
		cJSON * ojson = cJSON_CreateObject();
		cJSON_AddNumberToObject(ojson, "result", err == 0? 1000 : 1100 + err);
		if(errstr)
			cJSON_AddStringToObject(ojson, "err", errstr);

		if(jrows)
			cJSON_Delete(jrows);
		if(jrow_count)
			cJSON_Delete(jrow_count);

		return ojson;
	}

	cJSON * json = 0;
	int rows = 0;
	my_ulonglong affected = 0;

	assert(hp_mysql_query(0, 0, 0, 0, 0, 0) != 0);
	assert(hp_mysql_query(mysql, 0, 0, 0, 0, 0) != 0);
	assert(hp_mysql_query(mysql, "select a from b", 0, 0, 0, 0) != 0);
	assert(hp_mysql_query(mysql, "select a from b", 0, 0, &affected, 0) != 0);
	assert(affected == 0);

	assert(hp_mysql_query(mysql, "select 1", 0, 0, 0, 0) == 0);
	assert(hp_mysql_query(mysql, "select 1", 0, 0, &affected, 0) == 0);
	assert(affected == 1);
	assert(hp_mysql_query(mysql, "select 1", 0, &rows, 0, 0) == 0);
	assert(rows == 1); rows = 0;

	assert(hp_mysql_query(mysql, "select 1 as a", &json, 0, 0, 0) == 0);
	assert(json && cJSON_GetArraySize(json) == 1 && cJSON_GetArrayItem(json, 0) && cJSON_GetObjectItem(cJSON_GetArrayItem(json, 0), "a")->valueint == 1);
	cJSON_Delete(json);

	assert(hp_mysql_query(mysql, "select 1 as a union select 2 as a", &json, &rows, 0, 0) == 0);
	assert(json && rows == 2 && cJSON_GetArraySize(json) == 2 && cJSON_GetArrayItem(json, 0) && cJSON_GetObjectItem(cJSON_GetArrayItem(json, 0), "a")->valueint == 1);
	cJSON_Delete(json);

	/* test transaction: sql failed */
	{
		int sql_cb(cJSON const * ijson, sds * sql_count, sds * sql_query, sds * errstr)
		{
			*sql_count = sdsnew("insert;"); *sql_query = sdsnew("select 1; select 2;"); return 0;
		}

		json = hp_mysql_tmpl(mysql
				, "{}"
				, check_cb, sql_cb, result_cb, 0);
		assert(json);
		assert(cjson_ival(json, "result", 0) != 1000);
		cJSON_Delete(json);
	}
	/* test transaction: OK */
	{
		int sql_cb(cJSON const * ijson, sds * sql_count, sds * sql_query, sds * errstr)
		{
			*sql_count = sdsnew("select 1;"); *sql_query = sdsnew("select 1; select 2;"); return 0;
		}

		json = hp_mysql_tmpl(mysql
				, "{}"
				, check_cb, sql_cb, result_cb, 0);
		assert(json);
		assert(cjson_ival(json, "result", 0) == 1000);
		cJSON_Delete(json);
	}

	/* query_tmpl: invalid arg */
	assert(!hp_mysql_tmpl(0, 0, 0, 0, 0, 0));
	/* query_tmpl: invalid arg */
	{
		int sql_cb(cJSON const * ijson, sds * sql_count, sds * sql_query, sds * errstr)
		{
			*sql_count = sdsnew("select 1 total"); *sql_query = sdsnew("select a from this_table_NOT_exist"); return 0;
		}

		json = hp_mysql_tmpl(mysql
				, "{\"page\":1,\"line\":10,\"params:{\"deviceName\":3}}"	/* invalid json syntax */
				, check_cb, sql_cb, result_cb, 0);
		assert(json);
		assert(cjson_ival(json, "result", 0) != 1000);
		cJSON_Delete(json);
	}
	/* query_tmpl: SQL failed */
	{
		int sql_cb(cJSON const * ijson, sds * sql_count, sds * sql_query, sds * errstr)
		{
			*sql_count = sdsnew("select 1 total"); *sql_query = sdsnew("select a from this_table_NOT_exist"); return 0;
		}

		json = hp_mysql_tmpl(mysql
				, "{\"page\":1,\"line\":10,\"params\":{\"deviceName\":3}}"
				, check_cb, sql_cb, result_cb, 0);
		assert(json);
		assert(cjson_ival(json, "result", 0) != 1000);
		cJSON_Delete(json);
	}
	/* query_tmpl: OK */
	{

		int sql_cb(cJSON const * ijson, sds * sql_count, sds * sql_query, sds * errstr)
		{
			*sql_count = sdsnew("select 1 total"); *sql_query = sdsnew("select 1"); return 0;
		}

		json = hp_mysql_tmpl(mysql
				, "{\"page\":1,\"line\":10,\"params\":{\"deviceName\":3}}"
				, check_cb, sql_cb, result_cb, 0);
		assert(json);
		assert(cjson_ival(json, "result", 0) == 1000);
		cJSON_Delete(json);
	}


	mysql_server_end();
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////
#endif /* NDEBUG */
#endif /* LIBHP_WITH_MYSQL */
