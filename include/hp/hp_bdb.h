/*!
* This file is PART of libhp project
* @author hongjun.liao <docici@126.com>, @date 2019/3/7
*
* bdb/Berkeley DB wrapper 
* */

#ifndef LIBHP_BDB_H___
#define LIBHP_BDB_H___

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_WITH_BDB

#include "db.h"              /* DB */
/////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif


DBT * hp_bdb_get(DB * dbp, char const * keystr, DBT * data);
int hp_bdb_put(DB * dbp, char const * keystr, DBT * data);

#define BDB_PUT_PTR(bdb, key, ptr) \
do{                                                                        \
	DBT BDB_PUT_PTR_data = { 0 };                                                  \
	BDB_PUT_PTR_data.data = &ptr;                                          \
	BDB_PUT_PTR_data.size = sizeof(ptr);                                   \
	int BDB_PUT_PTR_rc = hp_bdb_put((bdb), (key), &BDB_PUT_PTR_data);  \
	assert(BDB_PUT_PTR_rc == 0); \
}while(0)

#define BDB_GET_PTR(bdb, key, t, ptr) \
do{                                                                              \
	*(ptr) = 0; \
	DBT BDB_GET_PTR_data = { 0 };                                                        \
	BDB_GET_PTR_data.data = 0;                                                   \
	BDB_GET_PTR_data.size = 0;                                                   \
	hp_bdb_get((bdb), (key), &BDB_GET_PTR_data);                                 \
	if(BDB_GET_PTR_data.data)                       \
		*(ptr) = *(t **)BDB_GET_PTR_data.data;      \
}while(0)

#define BDB_PUT_PB(bdb, key, pb, pack_sz, pack) \
do{                                                                              \
	size_t hp_bdb_pb_put_pack_sz = pack_sz(pb);                                 \
	sds hp_bdb_pb_put_buf = sdsMakeRoomFor(sdsempty(), hp_bdb_pb_put_pack_sz);  \
	size_t hp_bdb_pb_put_packed = pack((pb), (uint8_t *)hp_bdb_pb_put_buf);     \
	assert(hp_bdb_pb_put_pack_sz == hp_bdb_pb_put_packed);                      \
	sdsIncrLen(hp_bdb_pb_put_buf, hp_bdb_pb_put_packed);                        \
	DBT hp_bdb_pb_put_data = { 0 };                                             \
	hp_bdb_pb_put_data.data = hp_bdb_pb_put_buf;                                \
	hp_bdb_pb_put_data.size = sdslen(hp_bdb_pb_put_buf);                        \
	int hp_bdb_pb_put_pack_sz_rc = hp_bdb_put((bdb), (key), &hp_bdb_pb_put_data);  \
	assert(hp_bdb_pb_put_pack_sz_rc == 0); \
	sdsfree(hp_bdb_pb_put_buf);            \
}while(0)

#define BDB_GET_PB(bdb, key, pb, unpack) \
do{                                                                              \
	*(pb) = 0;        \
	DBT BDB_GET_PB_data = { 0 };                                                 \
	BDB_GET_PB_data.data = 0;                                                   \
	hp_bdb_get((bdb), (key), &BDB_GET_PB_data);                                 \
	if(BDB_GET_PB_data.data){                                             \
		*(pb) = unpack(0, BDB_GET_PB_data.size, (uint8_t *)BDB_GET_PB_data.data); \
	}                 \
}while(0)

#ifndef NDEBUG
int test_hp_bdb_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif

#endif /* LIBHP_WITH_BDB */

#endif /* LIBHP_BDB_H___ */

