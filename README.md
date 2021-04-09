libhp--a common c library, depdency for rmqtt project
=============
libhp is a simple library for c, especially on networking I/O, Redis,MySQL client etc,
see rmqtt,a Redis-based MQTT broker for more use cases

# compile and use
* 1.cd to libhp root dir
* 2.copy _config.h to config.h
* 3.edit config.h, uncomment LIBHP_WITH_XXX macros to turn on or off the libhp APIs you need, e.g.:
```code
/* enable MySQL APIs, hp_msyql_xxx */
#define LIBHP_WITH_MYSQL
```
* 4.untar and configure depencies
```code
cd deps && tar -xf deps.targ.gz && cd -
```

* 5.mkdir build && cd build
```code
# buld debug version, set CMAKE_BUILD_TYPE=Release to a release verison
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=1 .. && make -j 2
```
* 6.include in your project, link with -llibhp/liblibhp.lib
```code
//...
#inclde "hp/hp_log.h" /* hp_log */
//...
int main()
{
	hp_log(stdout, "hello libhp\n");
	return 0;
}
//...
```

# demos and exmaples
* 1.samples in dir examples/, demos/
* 2.test program in tests/, or test functions in every hp_xxx.c/test_xxx_main(), e.g.:
    include/hp/hp_cjson.h
```code
//...
#ifndef NDEBUG
int test_hp_cjson_main(int argc, char ** argv);
#endif /* NDEBUG */
```

* 3.the rmqtt project

# API简要说明

* 1. hp_epoll,hp_eti/eto, 事件驱动的I/O
  * hp_curl, 使用hp_epoll等的异步curl
  * 对比hp_uv_curl, 使用libuv的异步curl

* 2. hp_iocp, 结合WIN32 IOCP,消息队列,及select的I/O
  * select线程负责fd可读写通知
  * IOCP线程负责I/O完成通知
  * 用户线程调用message handle处理部分消息,发起异步读写操作,通知I/O完成等
```code
		/* the IOCP context */
		struct hp_iocp iocpcctxobj = { 0 }, *iocpctx = &iocpcctxobj;
		rc = hp_iocp_init(iocpctx, 2, WM_USER + 100, 200, 0/* user context */);
		assert(rc == 0);

		int tid = (int)GetCurrentThreadId();
		rc = hp_iocp_run(iocpctx, tid, 0);
		assert(rc == 0);

		/* prepare for I/O */
		int index = hp_iocp_add(iocpctx, 0, 0
			, 0, on_connect  /* connect to server and return the SOCKET */
			, on_data        /* called when data comming */
			, on_error       /* called when error occured */
			, 0);
		assert(index >= 0);

		/* write some thing async */
		char const * data = "GET /index.html HTTP/1.1\r\nHost: %s:%d\r\n\r\n";
		rc = hp_iocp_write(iocpctx, index, data, strlen(data), 0, 0);
		assert(rc == 0);

		/* handle message and process callbacks */
		for (rc = 0; ;) {
			MSG msgobj = { 0 }, *msg = &msgobj;
			if (PeekMessage((LPMSG)msg, (HWND)0, (UINT)0, (UINT)0, PM_REMOVE)) {
				rc = hp_iocp_handle_msg(iocpctx, msg->message, msg->wParam, msg->lParam);
			}
		}

		/* clear */
		hp_iocp_uninit(iocpctx);
```
* 3. hp_pu/sub, Redis Pub/Sub增强
  * 基于Redis Pub/Sub实现
  * 使用zset,hash数据结构等保存会话及发布过的消息,发布即不丢失
  * 消息需要确认,以支持离线消息及会话

# 第三方库说明及致谢

deps/目录为项目依赖的第三库,感谢原作者们! 为节约时间, 已将所有依赖打包为deps/deps.tar.gz

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

注意! cJSON,c-vector使用的是"升级"版本:
cJSON: https://gitee.com/docici/cJSON
c-vector:https://gitee.com/jun/c-vector
