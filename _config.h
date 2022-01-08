/*!
 * This file is PART of rmqtt project
 * @author hongjun.liao <docici@126.com>, @date 2018/10/30
 *
 * config, this is tempplate file and does not participate in the compilation process
 * copy _config.h to config.h which is used by libhp
 * */

#ifndef HP_CONFIG_H
#define HP_CONFIG_H

#if !defined(__linux__) && !defined(_MSC_VER)
#define LIBHP_HAVE_POLL
#endif

#ifdef HAVE_CONFIG_H

/* define to enable GNU extension */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/* define to use zlib, hp_zc_XXX */
#define LIBHP_WITH_ZLIB
/* define to use libcurl, hp_curl_XXX */
#define LIBHP_WITH_CURL
/* define to use libssl, hp_ssl_XXX */
#define LIBHP_WITH_SSL

/* define to use zlog for hp_log */
#define LIBHP_WITH_ZLOG

#define LIBHP_WITH_JNI

/* define to use MQTT client, hp_mqtt_XXX
 * using paho.mqtt.c
 *  */
#define LIBHP_WITH_MQTT

/* define to use Redis client, hp_redis_XXX */
#define LIBHP_WITH_REDIS

/* mainly for hiredis, define if you want to use
 * https://github.com/microsoftarchive/redis.git
 * else use https://github.com/redis/hiredis.git
 */
#ifdef _MSC_VER
 //#define LIBHP_WITH_WIN32_INTERROP
#endif /* _MSC_VER */

/* define to use MySQL/MariaDB client, hp_mysql_XXX */
#define LIBHP_WITH_MYSQL

#define LIBHP_WITH_AMQP
#define LIBHP_WITH_PROC_SYSINFO

/* define to use libhp HTTP module ht_http_XXX,
 * using http-parser */
#define LIBHP_WITH_HTTP

/* define to use timer fd (current Linux only?), hp_timerfd_XXX */
#define LIBHP_WITH_TIMERFD

/* define to use Berkeley DB, hp_bdb_XXX */
#define LIBHP_WITH_BDB

/* define if you want using libhp DEPRECADTED API
 * Deprecated code is no longer actively maintained !
 * */
#define LIBHP_DEPRECADTED

#endif /* HAVE_CONFIG_H */

#endif /* HP_CONFIG_H */
