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

* 1. hp_io_t, another networking I/O, using hp_iocp on Windows and hp_epoll on Linux
```code
		/* the IO context */
		hp_io_ctx ioctxobj, * ioctx = &ioctxobj;
		/* IO init options */
		hp_ioopt ioopt = {
				  listen_fd   /* listen fd */
				, on_accept   /* called when new connection coming */
				, { 0 } };
		rc = hp_io_init(ioctx, &ioopt);
		assert(rc == 0);

		/* prepare for I/O */
		hp_io_t ioobj, * io = &ioobj;
		rc = hp_io_add(ioctx, io, fd
		, on_data     /* called when data comming */
		, on_error);  /* called when error occured */
		assert(rc == 0);

		/* write some thing async */
		char const * data = "GET /index.html HTTP/1.1\r\nHost: %s:%d\r\n\r\n";
		rc = hp_io_write(ioctx, io, data, strlen(data), 0, 0);
		assert(rc == 0);

		/* run event loop */
		for (; !quit;) {
			hp_io_run(ioctx, 200, 0);
		}

		/* clear */
		hp_io_uninit(ioctx);
```
* 2. hp_pu/sub, Redis Pub/Sub增强
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

注意! cJSON,c-vector使用的是修改版本:
cJSON: https://gitee.com/docici/cJSON
c-vector:https://gitee.com/jun/c-vector
# build
mkdir build
cd build
rm -rf * && cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DLIBHP_WITH_CJSON=1 .. && make -j 2
