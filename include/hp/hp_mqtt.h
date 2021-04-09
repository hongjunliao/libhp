 /* This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2020/1/17
 *
 * MQTT util using paho.mqtt.c
 * */
#ifndef LIBHP_MQTT_H
#define LIBHP_MQTT_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifndef LIBHP_WITHOUT_MQTT

#ifndef _MSC_VER
#include "sds/sds.h"
#include "MQTTAsync.h"

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////////
typedef struct hp_mqtt hp_mqtt;

typedef void (* hp_mqtt_connect_cb)
		(hp_mqtt * cli, int err, char const * errstr, void * arg);
typedef void (* hp_mqtt_message_cb)
		(hp_mqtt * cli, char const * topic, char const * msg, int len, void * arg);
typedef void (* hp_mqtt_disconnect_cb)
		(hp_mqtt * cli, void * arg);
typedef void (* hp_mqtt_sub_cb)
		(hp_mqtt * cli, void * arg);

//#define XHMDM_INIT		0
#define XHMDM_CON_ING 		1
#define XHMDM_CON_OK  		2
#define XHMDM_SUB_ING 		4
#define XHMDM_SUB_OK  		8
#define XHMDM_DISCON_ING 	16

struct hp_mqtt {
	int 		state;
	sds *       topics;
	int *		qoses;

	MQTTAsync 	context;
	hp_mqtt_connect_cb on_connect;
	hp_mqtt_message_cb on_message;
	hp_mqtt_disconnect_cb on_disconnect;
	hp_mqtt_sub_cb on_sub;
	void * 		on_arg;
};

int hp_mqtt_init(hp_mqtt * cli
		, char const * clientid
		, hp_mqtt_connect_cb conect_cb
		, hp_mqtt_message_cb message_cb
		, hp_mqtt_disconnect_cb disconnect_cb
		, hp_mqtt_sub_cb sub_cb
		, char const * mqtt_addr
		, char const * mqtt_user
		, char const * mqtt_pwd
		, int * loglevel
		, void * arg
		, int flags
		);
int hp_mqtt_connect(hp_mqtt * cli);
int hp_mqtt_sub(hp_mqtt * cli, int n_topic, char * const* topic, int * qos, MQTTAsync_token * token);
int hp_mqtt_pub(hp_mqtt * cli, char const * topic, int qos, char const * data, MQTTAsync_token * token);
int hp_mqtt_disconnect(hp_mqtt * cli);
void hp_mqtt_uninit(hp_mqtt * cli);

/////////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
int test_hp_mqtt_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif
#endif /* _MSC_VER */

#endif /* LIBHP_WITH_MQTT */
#endif /* LIBHP_MQTT_H */
