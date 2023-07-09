#ifdef _WIN32
#include "redis/src\Win32_Interop\win32fixes.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>
#include <hiredis/adapters/ae.h>
#include "hp/hp_config.h"

/* Put event loop in the global scope, so it can be explicitly stopped */
static aeEventLoop *loop;
static int done = 0;

static void  setCallback(redisAsyncContext *c, void * r, void  *privdata) {
	redisReply *reply = r;
	if (!(reply && reply->type != REDIS_REPLY_ERROR)) {
		printf("%s: err/errstr=%d/'%s'\n", __FUNCTION__, c->err, (reply ? reply->str : c->errstr));
		return;
	}
	++done;
}

static void  getCallback(redisAsyncContext *c, void * r, void  *privdata) {
    redisReply *reply = r;
    if (reply == NULL) return;
    printf("%s: argv[%s]: %s\n", __FUNCTION__, (char*)privdata, reply->str);

	++done;
    /* Disconnect after receiving the reply to GET */
    redisAsyncDisconnect(c);
}

static void  connectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        printf("Error: %s\n", c->errstr);
        return;
    }

    printf("Connected...\n");
}

static void  disconnectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        printf("Error: %s\n", c->errstr);
        return;
    }

    printf("Disconnected...\n");
	
	if(done >= 2)
		aeStop(loop);
}

int hiredis_exmaple_ae_main(int argc, char **argv) 
{
	assert(hp_config_test);
#ifndef _WIN32
	signal(SIGPIPE, SIG_IGN);
#endif

#ifdef _WIN32
	/* For Win32_IOCP the event loop must be created before the async connect */
	loop = aeCreateEventLoop(1024 * 10);
#endif

	char host[64] = "";
	int port = 0;

	int n = sscanf(hp_config_test("redis"), "%[^:]:%d", host, &port);
	if (n != 2)
		return -2;
	redisAsyncContext *c = redisAsyncConnect(host, port);
    if (c->err) {
        /* Let *c leak for now... */
        printf("Error: %s\n", c->errstr);
        return 1;
    }

#ifndef _WIN32
    loop = aeCreateEventLoop(64);
#endif
    redisAeAttach(loop, c);
    redisAsyncSetConnectCallback(c,connectCallback);
    redisAsyncSetDisconnectCallback(c,disconnectCallback);
	redisAsyncCommand(c, setCallback, NULL, "HKEYS %s", "hello");
	redisAsyncCommand(c, setCallback, NULL, "SETs keys %b", argv[argc-1], strlen(argv[argc-1]));
    redisAsyncCommand(c, getCallback, (char*)"end-1", "GET key");
    aeMain(loop);
    return 0;
}

