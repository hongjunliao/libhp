libhp--a common c library, depdency for rmqtt project
=============
libhp is a simple library for c, especially on networking I/O, Redis,MySQL client etc,
see rmqtt,a Redis-based MQTT broker for more use cases

# compile and use
* cmake is recommended

```code
# in your cmake file
set(LIBHP_WITH_HTTP 1 CACHE INTERNAL "LIBHP_WITH_HTTP")
add_subdirectory(deps/libhp)
```

```code

/* in your code */
#define LIBHP_WITH_HTTP
#include "hp/hp_log.h"
#include "hp/hp_http.h"
...
hp_log(stdout, "hello libhp\n");
hp_http http;
...

```

# demos and exmaples
* 1.samples in dir examples/, demos/
* 2.test program in tests/, or test functions in every hp_xxx.c/test_xxx_main()

	e.g.:
    include/hp/hp_io_t.h
    
```code
#ifndef NDEBUG
int test_hp_io_t_main(int argc, char ** argv);
#endif /* NDEBUG */

```

* 3.the rmqtt project

# API简要说明

* 1. hp_io_t, another networking I/O, using hp_iocp(IOCP) on Windows and hp_epoll(epoll) on Linux

```code

	/**
	 * In this simple example, we create a HTTP sever and a HTTP client,
	 * then the client send a HTTP request to the sever to get index.html
	 *
	 * see hp_io_t.c/test_hp_io_t_main for a complete example
	 * /
	struct httpclient {
		hp_io_t io;
		//...
	} ;
	struct httpserver {
		hp_io_t listenio;
		//...
	} ;

	/* a HTTP client I/O handle */
	hp_iohdl http_cli_hdl = {
			.on_new = 0/*http_cli_on_new*/,       // set to NULL is fine
			.on_parse = http_cli_on_parse,        // parse the HTTP response data
			.on_dispatch = http_cli_on_dispatch,  // process the HTTP message
			.on_loop = 0,                         // keep alive?
			.on_delete = 0/*http_cli_on_delete*/, // 
	#ifdef _MSC_VER
			.wm_user = 0 	/* WM_USER + N */        // Windows only
			.hwnd = 0    /* hwnd */
	#endif /* _MSC_VER */
	};
	
	/* the HTTP server I/O handle */
	hp_iohdl http_server_hdl = {
			.on_new = http_server_on_new,           // when a new HTTP client coming
			.on_parse = http_server_on_parse,       // parse HTTP data
			.on_dispatch = http_server_on_dispatch, // handle the HTTP message: reply/close/upgrade?
			.on_loop = 0,                           // key alive?
			.on_delete = http_server_on_delete,     // close the client
	#ifdef _MSC_VER
			.wm_user = 0 	/* WM_USER + N */
			.hwnd = 0    /* hwnd */
	#endif /* _MSC_VER */
	};

	// init needed
	httpclient cobj, *c = &cobj;
	httpserver sobj, *s = &sobj;
	hp_sock_t listen_fd, confd;

	/* the IO context */
	hp_io_ctx ioctxobj, *ioctx = &ioctxobj;
	rc = hp_io_init(ioctx); assert(rc == 0);

	/* HTTP server add listen socket  with handles */
	rc = hp_io_add(ioctx, &s->listenio, listen_fd, http_server_hdl); assert(rc == 0);
	/* add HTTP connect socket */
	rc = hp_io_add(ioctx, &c->io, confd, http_cli_hdl); assert(rc == 0);

	/* the client send HTTP request async, data is freed by hp_io_t system
	 */
	char * data = calloc("GET /index.html HTTP/1.1\r\nHost: %s:%d\r\n\r\n");
	rc = hp_io_write(ioctx, io, data, strlen(data), free, 0); assert(rc == 0);

	/* run event loop */
	for (;;) {
		hp_io_run(ioctx, 200, 0); 
	}

	/* clear */
	hp_io_uninit(ioctx);
	
```
* 2. hp_pub/sub, Redis Pub/Sub增强
  * 基于Redis Pub/Sub实现
  * 使用zset,hash数据结构等保存会话及发布过的消息,发布即不丢失
  * 消息需要确认,以支持离线消息及会话

# 第三方库说明及致谢

deps/目录为项目依赖的第三库,感谢原作者们!

* cJSON, https://github.com/DaveGamble/cJSON.git
* c-vector: https://github.com/eteran/c-vector
* gbk-utf8: https://github.com/lytsing/gbk-utf8.git
* http-parser: https://github.com/nodejs/http-parser.git
* optparse: https://github.com/skeeto/optparse.git
* dlfcn-win32: https://github.com/dlfcn-win32/dlfcn-win32.git
* hiredis: https://github.com/redis/hiredis.git
* Redis: https://github.com/antirez/redis.git
* sds: https://github.com/antirez/sds.git
* libyuarel: https://github.com/jacketizer/libyuarel.git
* zlog: https://github.com/HardySimpson/zlog.git

注意! cJSON,c-vector使用的是修改版本:

 * cJSON: https://gitee.com/docici/cJSON
 * c-vector:https://gitee.com/jun/c-vector
