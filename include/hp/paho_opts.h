/*******************************************************************************
 * Copyright (c) 2012, 2018 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *   http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Ian Craggs - initial contribution
 *    Guilherme Maciel Ferreira - add keep alive option
 *******************************************************************************/

#if !defined(PUBSUB_OPTS_H)
#define PUBSUB_OPTS_H

#include "MQTTAsync.h"
#include "MQTTClientPersistence.h"

struct pubsub_opts
{
	/* debug app options */
	int publisher;  /* publisher app? */
	int quiet;
	int verbose;
	int tracelevel;
	char* delimiter;
	int maxdatalen;
	/* message options */
	char* message;
	char* filename;
	int stdin_lines;
	int stdlin_complete;
	int null_message;
	/* MQTT options */
	int MQTTVersion;
	char* clientid;
	int retained;
	char username[128];
	char password[128];
	char* host;
	char* port;
	char connection[256];
	int keepalive;
	/* will options */
	char* will_topic;
	char* will_payload;
	int will_qos;
	int will_retain;
	/* TLS options */
	int insecure;
	char* capath;
	char* cert;
	char* cafile;
	char* key;
	char* keypass;
	char* ciphers;
	char* psk_identity;
	char* psk;
	/* MQTT V5 options */
	int message_expiry;
	struct {
		char *name;
		char *value;
	} user_property;
};

#endif


