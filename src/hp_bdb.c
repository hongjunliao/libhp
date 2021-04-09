/*!
* This file is PART of libhp project
* @author hongjun.liao <docici@126.com>, @date 2019/3/7
*
* cache by bdb/Berkeley DB
* */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_WITH_BDB

#include "hp_bdb.h"
#include <string.h>
#include <assert.h>
/////////////////////////////////////////////////////////////////////////////////////

DBT * hp_bdb_get(DB * dbp, char const * keystr, DBT * data)
{
	if (!(dbp && keystr && data))
		return 0;

	DBT key = { 0 };
	key.data = (char *)keystr;
	key.size = strlen(keystr);

	dbp->get(dbp, NULL, &key, data, 0);

	return data;
}

int hp_bdb_put(DB * dbp, char const * keystr, DBT * data)
{
	DBT key = { 0 };
	key.data = (char *)keystr;
	key.size = strlen(keystr);

	int rc = dbp->put(dbp, NULL, &key, data, 0);

	return rc;
}
/////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG

#include <stdio.h>
#include <assert.h>
#include "db.h"

int test_hp_bdb_main(int argc, char ** argv)
{
	int rc;
	DB *dbp;

	rc = db_create(&dbp, 0, 0);
	assert(rc == 0);

	rc = dbp->open(dbp, NULL, "hp_bdb.db", NULL, DB_BTREE, DB_CREATE, 0);
	assert(rc == 0);

	DBT data = { 0 };
	if (!hp_bdb_get(dbp, "hello", &data)) {
		rc = hp_bdb_put(dbp, "world", &data);
		assert(rc == 0);
	}
	else {
		assert(memcmp(data.data, "world", data.size) == 0);
	}

	rc = dbp->close(dbp, rc);
	assert(rc == 0);

	return 0;
}

#endif /* NDEBUG */

#endif /* LIBHP_WITH_BDB */

