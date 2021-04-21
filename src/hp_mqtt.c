
/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2020/1/16
 *
 *
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_WITH_MQTT
#include "sdsinc.h"
#include "hp_mqtt.h"
#include <search.h>  /* lfind */
#include "MQTTAsync.h"
#include "MQTTClientPersistence.h"
#include <stdio.h>
#include <limits.h>
#include <assert.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include "c-vector/cvector.h"
#include "paho_opts.h"
#include "string_util.h"

#if defined(WIN32)
#include <windows.h>
#define sleep Sleep
#else
#include <sys/time.h>
#include <unistd.h>
#endif

#if defined(_WRS_KERNEL)
#include <OsWrapper.h>
#endif

/////////////////////////////////////////////////////////////////////////////////////

static int l_loglevel = 0, * g_loglevel = &l_loglevel;

static MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;

static struct pubsub_opts opts =
{
	0, 0, 0, MQTTASYNC_TRACE_ERROR, "\n", 100,  	/* debug/app options */
	NULL, NULL, 1, 0, 0, /* message options */
	MQTTVERSION_DEFAULT, 0, 0, "dev", "dev123", 0, 0, "ws://218.17.105.178:20024", 10, /* MQTT options */
	NULL, NULL, 0, 0, /* will options */
	0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, /* TLS options */
	0, {NULL, NULL}, /* MQTT V5 options */
};

/////////////////////////////////////////////////////////////////////////////////////

static int handle_cmd(char const * s){
	if(!(s && strlen(s) > 0))
		return -1;
	if(s[0] == '-'){
		int i = 0;
		int argc = 0;
		sds * argv = sdssplitlen(s, strlen(s), " ", 1, &argc);
		for(i = 0; i < argc; ++i){
			if (strcmp(argv[i], "--trace") == 0 && i + 1 < argc) {
				int level = atoi(argv[i + 1]);
				if(!(level >= MQTTASYNC_TRACE_MAXIMUM && level <= MQTTASYNC_TRACE_FATAL))
					level = opts.tracelevel;
				opts.tracelevel = level;
				MQTTAsync_setTraceLevel(opts.tracelevel);
				++i;

				printf("%s: set --trace %d\n", __FUNCTION__, opts.tracelevel);
			}
		}
		sdsfreesplitres(argv, argc);
	}
	return 0;
}


static int hp_mqtt__messageArrived(void *context, char *topicName, int topicLen, MQTTAsync_message *message)
{
	assert(context);
	hp_mqtt * cli = (hp_mqtt *)context;

	sds topic = sdsnewlen(topicName, topicLen)
			, msg = sdsnewlen(message->payload, message->payloadlen);

	if(*g_loglevel > 8){

		printf("%s: topic='%s', payload=%d/'%s'\n", __FUNCTION__
			, topic, (int)sdslen(msg), msg);
	}

	if(sdslen(msg) > 0 && msg[0] == '$')
		handle_cmd(msg + 1);

	if(cli->on_message)
		cli->on_message(cli, topic, msg, sdslen(msg), cli->on_arg);

	fflush(stdout);

	sdsfree(topic);
	sdsfree(msg);
	MQTTAsync_freeMessage(&message);
	MQTTAsync_free(topicName);

	return 1;
}


static void hp_mqtt__onDisconnect(void* context, MQTTAsync_successData* response)
{
	assert(context);
	hp_mqtt * cli = (hp_mqtt *)context;
	cli->state = 0;

	printf("%s: disconnected, broker='%s'\n", __FUNCTION__, opts.connection);

	if(cli->on_disconnect)
		cli->on_disconnect(cli, cli->on_arg);
}


static void hp_mqtt__onSubscribe5(void* context, MQTTAsync_successData5* response)
{
	assert(context);
	hp_mqtt * cli = (hp_mqtt *)context;
	cli->state = XHMDM_SUB_OK;

	if(cli->on_sub)
		cli->on_sub(cli, cli->on_arg);
}

static void hp_mqtt__onSubscribe(void* context, MQTTAsync_successData* response)
{
	assert(context);
	hp_mqtt * cli = (hp_mqtt *)context;
	cli->state = XHMDM_SUB_OK;

	if(cli->on_sub)
		cli->on_sub(cli, cli->on_arg);
}

static void hp_mqtt__onSubscribeFailure5(void* context, MQTTAsync_failureData5* response)
{
	assert(context);
	hp_mqtt * cli = (hp_mqtt *)context;

	if (!opts.quiet)
		fprintf(stderr, "%s: Subscribe failed, rc %s reason code %s\n", __FUNCTION__,
				MQTTAsync_strerror(response->code),
				MQTTReasonCode_toString(response->reasonCode));

	hp_mqtt_sub(cli, cvector_size(cli->topics), cli->topics, cli->qoses, 0);
}


static void hp_mqtt__onSubscribeFailure(void* context, MQTTAsync_failureData* response)
{
	assert(context);
	hp_mqtt * cli = (hp_mqtt *)context;

	if (!opts.quiet)
		fprintf(stderr, "%s: Subscribe failed, rc %s\n", __FUNCTION__,
			MQTTAsync_strerror(response->code));

	hp_mqtt_sub(cli, cvector_size(cli->topics), cli->topics, cli->qoses, 0);
}


static void hp_mqtt__onConnectFailure5(void* context, MQTTAsync_failureData5* response)
{
	assert(context);
	hp_mqtt * cli = (hp_mqtt *)context;

	if (!opts.quiet)
		fprintf(stderr, "%s: Connect failed, rc %s reason code %s\n", __FUNCTION__,
			MQTTAsync_strerror(response->code),
			MQTTReasonCode_toString(response->reasonCode));

	if(cli->on_connect)
		cli->on_connect(cli, response->code, MQTTAsync_strerror(response->code), cli->on_arg);
}

static void hp_mqtt__onConnectFailure(void* context, MQTTAsync_failureData* response)
{
	assert(context);
	hp_mqtt * cli = (hp_mqtt *)context;

	if (!opts.quiet)
		fprintf(stderr, "%s: Connect failed, rc %s\n", __FUNCTION__, response ? MQTTAsync_strerror(response->code) : "none");
	if(cli->on_connect)
		cli->on_connect(cli, response->code, MQTTAsync_strerror(response->code), cli->on_arg);
}

static void hp_mqtt__onConnect5(void* context, MQTTAsync_successData5* response)
{
	hp_mqtt * cli = (hp_mqtt *)context;
	cli->state = XHMDM_CON_OK;

	fprintf(stdout, "%s: connected, broker='%s'\n", __FUNCTION__, opts.connection);

	if(cli->on_connect)
		cli->on_connect(cli, 0, "", cli->on_arg);
}

static void hp_mqtt__onConnect(void* context, MQTTAsync_successData* response)
{
	hp_mqtt * cli = (hp_mqtt *)context;
	cli->state = XHMDM_CON_OK;

	fprintf(stdout, "%s: connected, broker='%s'\n", __FUNCTION__, opts.connection);

	if(cli->on_connect)
		cli->on_connect(cli, 0, "", cli->on_arg);
}

static void trace_callback(enum MQTTASYNC_TRACE_LEVELS level, char* message)
{
//	fprintf(stderr, "%s: %d/%s: Trace : %d, %s\n", getpid(), __FUNCTION__, level, message);
}

void hp_mqtt__onConnected(void* context, char* cause)
{
	assert(context);
	hp_mqtt * cli = (hp_mqtt *)context;

	fprintf(stdout, "%s: connected, broker='%s', cause='%s'\n", __FUNCTION__, opts.connection, cause);

	if(cause && strstr(cause, "onSuccess"))
		return;
	if(cli->on_connect)
		cli->on_connect(cli, 0, cause, cli->on_arg);
}

/////////////////////////////////////////////////////////////////////////////////////

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
		)
{
	if(!(cli))
		return -1;

	/* @see MQTTAsync_connectOptions::cleansession */
	if(opts.clientid)
		free(opts.clientid);
	if(clientid && strlen(clientid) > 0){
		opts.clientid = strdup(clientid);
		conn_opts.cleansession = 0;
	}
	else {
		opts.clientid = strdup("");
		conn_opts.cleansession = 1;
	}

	memset(cli, 0, sizeof(hp_mqtt));
	cli->state = 0;
	cvector_init(cli->topics, 2);
	cvector_init(cli->qoses, 2);
	cli->on_connect = conect_cb;
	cli->on_message = message_cb;
	cli->on_disconnect = disconnect_cb;
	cli->on_sub = sub_cb;
	cli->on_arg = arg;
	if(loglevel)
		g_loglevel = loglevel;

	MQTTAsync_createOptions create_opts = MQTTAsync_createOptions_initializer;
	int rc = 0;
	char str[128], *url = NULL;

	if(mqtt_addr) strncpy(opts.connection, mqtt_addr, sizeof(opts.connection));
	if(mqtt_user) strncpy(opts.username, mqtt_user, sizeof(opts.username));
	if(mqtt_pwd) strncpy(opts.password, mqtt_pwd, sizeof(opts.password));

	if (opts.connection)
		url = opts.connection;
	else
	{
		url = str;
		sprintf(url, "%s:%s", opts.host, opts.port);
	}

	if (opts.tracelevel > 0)
	{
		MQTTAsync_setTraceCallback(trace_callback);
		MQTTAsync_setTraceLevel(opts.tracelevel);
	}

	if (opts.MQTTVersion >= MQTTVERSION_5)
		create_opts.MQTTVersion = MQTTVERSION_5;

	create_opts.sendWhileDisconnected = 1;
	create_opts.maxBufferedMessages = INT_MAX;

	int persistence_type = ((flags & 1)? MQTTCLIENT_PERSISTENCE_DEFAULT : MQTTCLIENT_PERSISTENCE_NONE);
	rc = MQTTAsync_createWithOptions(&cli->context, url, opts.clientid, persistence_type,
			NULL, &create_opts);
	if (rc != MQTTASYNC_SUCCESS)
	{
		if (!opts.quiet)
			fprintf(stderr, "%s: Failed to create client '%s', return code: %s, url='%s'\n"
					, __FUNCTION__, opts.clientid, MQTTAsync_strerror(rc)
					, url);
		return -1;
	}

	rc = MQTTAsync_setCallbacks(cli->context, cli, NULL, hp_mqtt__messageArrived, NULL);
	if (rc != MQTTASYNC_SUCCESS) {
		if (!opts.quiet)
			fprintf(stderr, "%s: Failed to set callbacks, return code: %s\n", __FUNCTION__, MQTTAsync_strerror(rc));
		return (-2);
	}

	return 0;
}

int hp_mqtt_connect(hp_mqtt * cli)
{
	if(!cli)
		return -1;

	int rc;
	MQTTAsync_willOptions will_opts = MQTTAsync_willOptions_initializer;
	MQTTAsync_SSLOptions ssl_opts = MQTTAsync_SSLOptions_initializer;

	if (opts.MQTTVersion == MQTTVERSION_5)
	{
		MQTTAsync_connectOptions conn_opts5 = MQTTAsync_connectOptions_initializer5;
		conn_opts = conn_opts5;
		conn_opts.onSuccess5 = hp_mqtt__onConnect5;
		conn_opts.onFailure5 = hp_mqtt__onConnectFailure5;
		conn_opts.cleanstart = 0;
	}
	else
	{
		conn_opts.onSuccess = hp_mqtt__onConnect;
		conn_opts.onFailure = hp_mqtt__onConnectFailure;
	}

	conn_opts.keepAliveInterval = opts.keepalive;
	conn_opts.username = opts.username;
	conn_opts.password = opts.password;
	conn_opts.MQTTVersion = opts.MQTTVersion;
	conn_opts.context = cli;
	conn_opts.automaticReconnect = 1;
	conn_opts.connectTimeout = 20;

	if (opts.will_topic) 	/* will options */
	{
		will_opts.message = opts.will_payload;
		will_opts.topicName = opts.will_topic;
		will_opts.qos = opts.will_qos;
		will_opts.retained = opts.will_retain;
		conn_opts.will = &will_opts;
	}

	if (opts.connection && (strncmp(opts.connection, "ssl://", 6) == 0 ||
			strncmp(opts.connection, "wss://", 6) == 0))
	{
		ssl_opts.verify = (opts.insecure) ? 0 : 1;
		ssl_opts.CApath = opts.capath;
		ssl_opts.keyStore = opts.cert;
		ssl_opts.trustStore = opts.cafile;
		ssl_opts.privateKey = opts.key;
		ssl_opts.privateKeyPassword = opts.keypass;
		ssl_opts.enabledCipherSuites = opts.ciphers;
		conn_opts.ssl = &ssl_opts;
	}
	rc = MQTTAsync_setConnected(cli->context, cli, hp_mqtt__onConnected);
	if (rc != MQTTASYNC_SUCCESS) {
		if (!opts.quiet)
			fprintf(stderr, "%s: Failed MQTTAsync_setConnected, return code: %s\n", __FUNCTION__, MQTTAsync_strerror(rc));
		return (-2);
	}

	if (!opts.quiet)
		printf("%s: connecting to broker='%s', username/password='%s'/'%s' ...\n"
			, __FUNCTION__
			, opts.connection, opts.username, (strlen(opts.password) > 0? "***" : ""));

	if ((rc = MQTTAsync_connect(cli->context, &conn_opts)) != MQTTASYNC_SUCCESS)
	{
		if (!opts.quiet)
			fprintf(stderr, "%s: Failed to start connect, return code %s\n", __FUNCTION__, MQTTAsync_strerror(rc));
		return(-3);
	}

	cli->state = XHMDM_CON_ING;
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////

static int hp_mqtt_do_sub(hp_mqtt * cli, MQTTAsync_token * token)
{
	assert(cli);
	int rc;

	MQTTAsync client = (MQTTAsync)cli->context;
	MQTTAsync_responseOptions ropts = MQTTAsync_responseOptions_initializer;
	ropts.context = cli;

	if(opts.MQTTVersion == MQTTVERSION_5){
		ropts.onSuccess5 = hp_mqtt__onSubscribe5;
		ropts.onFailure5 = hp_mqtt__onSubscribeFailure5;
	}
	else{
		ropts.onSuccess = hp_mqtt__onSubscribe;
		ropts.onFailure = hp_mqtt__onSubscribeFailure;
	}

	rc = MQTTAsync_subscribeMany(client, cvector_size(cli->topics), cli->topics, cli->qoses, &ropts);

	if (rc != MQTTASYNC_SUCCESS) {
		if (!opts.quiet)
			fprintf(stderr, "%s: Failed to start subscribe, return code %s\n", __FUNCTION__, MQTTAsync_strerror(rc));
		return -1;
	}

	if(token)
		*token = ropts.token;

	cli->state = XHMDM_SUB_ING;

	printf("%s: subscribing topic='%s' ..., count=%d\n", __FUNCTION__, cli->topics[0], (int)cvector_size(cli->topics));

	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////

static int cvector_cmp(int n_vec1, char *const* vec1
		, int n_vec2, char *const* vec2)
{
	assert(vec1 && vec2);
	int i;
	int r = 0;
	if(n_vec1 != n_vec2)
		return n_vec2 > n_vec1? 1 : -1;

	for(i = 0; i < n_vec2; ++i){
		sds key = sdsnew(vec2[i]), * val = (char **)cvector_lfind(vec1, lfind, &key, hp_strcasecmp);
		sdsfree(key);
		if(!val)
			return -1;
	}
	return r;
}

int hp_mqtt_sub(hp_mqtt * cli, int n_topic, char* const* topic, int * qos, MQTTAsync_token * token)
{
	int rc;
	int i;
	if(!(cli && topic && qos))
		return -1;
	for(i = 0; i < n_topic; ++i){
		if(!topic[i])
			return -2;
		if(!(qos[i] >= 0 && qos[i] <= 2))
			qos[i] = 2;
	}

	rc = cvector_cmp(cvector_size(cli->topics), cli->topics, n_topic, topic);
	if(rc == 0)
		return 0; /* topic NOT changed, just return OK */

	/* clear ole topics */
	for(i = 0; i < cvector_size(cli->topics); ++i)
		sdsfree(cli->topics[i]);
	cvector_set_size(cli->topics, 0);

	/* update topics */
	for(i = 0; i < n_topic; ++i){
		cvector_push_back(cli->topics, sdsnew(topic[i]));
		cvector_push_back(cli->qoses, qos[i]);
	}

	return hp_mqtt_do_sub(cli, token);
}

/////////////////////////////////////////////////////////////////////////////////////

int hp_mqtt_pub(hp_mqtt * cli, char const * topic, int qos, char const * data, int len, MQTTAsync_token * token)
{
	if(!(cli && topic && data))
		return -1;

	int rc;
	MQTTAsync_responseOptions pub_opts = MQTTAsync_responseOptions_initializer;
	if(len == 0) { len = strlen(data); }

	rc = MQTTAsync_send(cli->context, topic, len, data, qos, 0, &pub_opts);
	if (!(rc == MQTTASYNC_SUCCESS ))
		fprintf(stderr, "%s: Error from MQTTAsync_send: %d/%s, topic='%s'\n", __FUNCTION__
			, rc, MQTTAsync_strerror(rc), topic);

	if(token)
		*token = pub_opts.token;

	return rc;
}

int hp_mqtt_disconnect(hp_mqtt * cli)
{
	int rc;
	if(!cli)
		return -1;

	MQTTAsync_disconnectOptions disc_opts = MQTTAsync_disconnectOptions_initializer;

	disc_opts.onSuccess = hp_mqtt__onDisconnect;
	disc_opts.context = cli;
	if ((rc = MQTTAsync_disconnect(cli->context, &disc_opts)) != MQTTASYNC_SUCCESS)
	{
		cli->state = 0;

		if (!opts.quiet)
			fprintf(stderr, "%s: Failed to start disconnect, return code: %s\n", __FUNCTION__, MQTTAsync_strerror(rc));
		return 0;
	}

	cli->state = XHMDM_DISCON_ING;

	return 0;
}

void hp_mqtt_uninit(hp_mqtt * cli)
{
	int i;

	if(!cli)
		return;

	MQTTAsync_destroy(&cli->context);

	for(i = 0; i < cvector_size(cli->topics); ++i)
		sdsfree(cli->topics[i]);
	cvector_free(cli->topics);
	cvector_free(cli->qoses);
	memset(cli, 0, sizeof(hp_mqtt));
}

/////////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG

#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "MQTTClient.h"
#include "hp_config.h"
extern hp_config_t g_conf;

struct basic_test {
	char * topics[1];
	int qoses[1];
	char const * message;
	int done;
};

struct resub_test {
	char * topics[1], * topics2[2], * topics3[2], * topics4[1];
	int qoses[4];
	char const * message;
	int n_msg;
	int done;
};

static struct basic_test t1 = {
		{ "hp_sdfsdfs/0/dev1"}
		,{2}
		, "hello#123"
		,0
};

static struct resub_test t2 = {
		  {"hp_sdfsdfs/1/dev1" }
		, {"hp_sdfsdfs/1/dev1", "hp_sdfsdfs/apps/apps1" }
		, {"hp_sdfsdfs/1/dev1", "hp_sdfsdfs/apps/apps2" }
		, {"hp_sdfsdfs/1/dev1" }
		,{2,2,2,2}
		, "hello#123"
		,0
		,0
};

static void test__t1_hp_mqtt_message_cb(
		hp_mqtt * cli, char const * topic, char const * msg, int len, void * arg)
{
	assert(cli);
	assert(strcmp(topic, t1.topics[0]) == 0);
	assert(strncmp(msg, t1.message, len) == 0);
	t1.done = 1;
}

static void test__t2_hp_mqtt_message_cb(
		hp_mqtt * cli, char const * topic, char const * msg, int len, void * arg)
{
	assert(cli);
	printf("%s: topic='%s', payload=%d/'%.*s'\n", __FUNCTION__
		, topic, len, len, msg);

//	assert(strncmp(msg, "1", 1) != 0 && strncmp(msg, "2", 1) != 0 && strncmp(msg, "3", 1) != 0);

	++t2.n_msg;
	if(strncmp(msg, "done", 4) == 0)
		t2.done = 1;
}

static int s_conn_failed = 0;

static void on_connect(hp_mqtt * cli, int err, char const * errstr, void * arg)
{
	assert(cli);
	if(err != 0)
		s_conn_failed = 1;
}

/////////////////////////////////////////////////////////////////////////////////////////

int test_hp_mqtt_main(int argc, char ** argv)
{
	assert(g_conf);

	char * mqtt_addr=g_conf("mqtt.addr");
	char * mqtt_user=g_conf("mqtt.user");;
	char * mqtt_pwd=g_conf("mqtt.pwd");;

	/* check if connect OK */
	{

		int rc;
		hp_mqtt mqttcliobj, * mqttcli = &mqttcliobj;

		rc = hp_mqtt_init(mqttcli, 0, on_connect, test__t1_hp_mqtt_message_cb, 0, 0
				, mqtt_addr, mqtt_user, mqtt_pwd
				, 0, 0, 0);
		assert(rc == 0);

		rc = hp_mqtt_connect(mqttcli);
		assert(rc == 0);

		while(mqttcli->state != XHMDM_CON_OK){
			usleep(200 * 1000);
			if(s_conn_failed)
				break;
		}

		hp_mqtt_disconnect(mqttcli);

		hp_mqtt_uninit(mqttcli);
		if(s_conn_failed){
			fprintf(stdout, "%s: connect to MQTT failed, skip this test\n", __FUNCTION__);
			return 0;
		}
	}

	/* basic tests */
	{
		int rc;
		MQTTAsync_token token;
		hp_mqtt mqttcliobj, * mqttcli = &mqttcliobj;

		rc = hp_mqtt_init(mqttcli, 0, 0, test__t1_hp_mqtt_message_cb, 0, 0
				, mqtt_addr, mqtt_user, mqtt_pwd
				, 0, 0, 0);
		assert(rc == 0);

		rc = hp_mqtt_connect(mqttcli);
		assert(rc == 0);

		while(mqttcli->state != XHMDM_CON_OK)
			usleep(200 * 1000);

		rc = hp_mqtt_sub(mqttcli, 1, t1.topics, t1.qoses, &token);
		assert(rc == 0);

		while(mqttcli->state != XHMDM_SUB_OK)
			usleep(200 * 1000);

		rc = hp_mqtt_pub(mqttcli, t1.topics[0], t1.qoses[0], t1.message, 0, &token);
		assert(rc == 0);

		rc = MQTTAsync_waitForCompletion(mqttcli->context, token, 8000);
		assert(rc == MQTTASYNC_SUCCESS);

		while(!t1.done)
			usleep(200 * 1000);

		hp_mqtt_disconnect(mqttcli);

		hp_mqtt_uninit(mqttcli);
	}

	/* resub tests */
	{
		int rc;
		MQTTAsync_token token;
		hp_mqtt mqttcliobj, * mqttcli = &mqttcliobj;

		rc = hp_mqtt_init(mqttcli, 0, 0, test__t2_hp_mqtt_message_cb, 0, 0
				, mqtt_addr, mqtt_user, mqtt_pwd
				, 0, 0, 0);
		assert(rc == 0);

		rc = hp_mqtt_connect(mqttcli);
		assert(rc == 0);

		while(mqttcli->state != XHMDM_CON_OK)
			usleep(200 * 1000);

		/* 1 topic */
		rc = hp_mqtt_sub(mqttcli, 1, t2.topics, t2.qoses, &token);
		assert(rc == 0);
		while(mqttcli->state != XHMDM_SUB_OK)
			usleep(200 * 1000);

		/* 2 topic, add a new one */
		rc = hp_mqtt_sub(mqttcli, 2, t2.topics2, t2.qoses, &token);
		assert(rc == 0);
		while(mqttcli->state != XHMDM_SUB_OK)
			usleep(200 * 1000);

		/* 2 topic: send message OK */
		rc = hp_mqtt_pub(mqttcli, t2.topics2[1], t2.qoses[1], t2.message, 0, &token);
		assert(rc == 0);
		rc = MQTTAsync_waitForCompletion(mqttcli->context, token, 8000);
		assert(rc == MQTTASYNC_SUCCESS);

		/* 2 topic, changed */
		rc = hp_mqtt_sub(mqttcli, 2, t2.topics3, t2.qoses, &token);
		assert(rc == 0);
		while(mqttcli->state != XHMDM_SUB_OK)
			usleep(200 * 1000);

		/* 2 topic: changed, send message1 failed, message2 OK */
		rc = hp_mqtt_pub(mqttcli, t2.topics2[1], t2.qoses[1], "1", 0, &token);
		assert(rc == 0);
		rc = MQTTAsync_waitForCompletion(mqttcli->context, token, 8000);
		assert(rc == MQTTASYNC_SUCCESS);

		rc = hp_mqtt_pub(mqttcli, t2.topics3[1], t2.qoses[0], t2.message, 0, &token);
		assert(rc == 0);
		rc = MQTTAsync_waitForCompletion(mqttcli->context, token, 8000);
		assert(rc == MQTTASYNC_SUCCESS);

		/* back to 1 topic, both send failed */
		rc = hp_mqtt_sub(mqttcli, 1, t2.topics4, t2.qoses, &token);
		assert(rc == 0);
		while(mqttcli->state != XHMDM_SUB_OK)
			usleep(200 * 1000);

		rc = hp_mqtt_pub(mqttcli, t2.topics2[1], t2.qoses[0], "2", 0, &token);
		assert(rc == 0);
		rc = MQTTAsync_waitForCompletion(mqttcli->context, token, 8000);
		assert(rc == MQTTASYNC_SUCCESS);

		rc = hp_mqtt_pub(mqttcli, t2.topics3[1], t2.qoses[0], "3", 0, &token);
		assert(rc == 0);
		rc = MQTTAsync_waitForCompletion(mqttcli->context, token, 8000);
		assert(rc == MQTTASYNC_SUCCESS);

		/* exit message, OK */
		rc = hp_mqtt_pub(mqttcli, t2.topics4[0], t2.qoses[0], "done", 0, &token);
		assert(rc == 0);
		rc = MQTTAsync_waitForCompletion(mqttcli->context, token, 8000);
		assert(rc == MQTTASYNC_SUCCESS);

		while(!t2.done)
			usleep(200 * 1000);

		hp_mqtt_disconnect(mqttcli);

		hp_mqtt_uninit(mqttcli);
	}
	return 0;
}

#endif /* NDEBUG */
#endif /* LIBHP_WITH_MQTT */


