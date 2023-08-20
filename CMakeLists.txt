###########################################################################################
#
# This file is PART of libhp project
#
# author hongjun.liao <docici@126.com>
# date 2020/6

###########################################################################################

cmake_minimum_required(VERSION 2.8.12)

include(cmakes/libhp.cmake) # hp_cmake_find_deps
# libhp
project(libhp)

###########################################################################################

if(NOT CMAKE_BUILD_TYPE)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Options are: None, Debug, Release, RelWithDebInfo, MinSizeRel." FORCE)
endif()
# -DCMAKE_EXPORT_COMPILE_COMMANDS is for lsp/clangd, see
# https://clangd.llvm.org/installation.html
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

###########################################################################################
include(CheckIncludeFile)
check_include_file(poll.h HAVE_UNISTD_H)
check_include_file(poll.h HAVE_POLL_H)
check_include_file(sys/epoll.h HAVE_SYS_EPOLL_H)
check_include_file(sys/uio.h HAVE_SYS_UIO_H)
check_include_file(sys/inotify.h HAVE_SYS_INOTIFY_H)
check_include_file(search.h HAVE_SEARCH_H)

###########################################################################################

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
# hp_io_t test
set(LIBHP_WITH_CJSON 1 CACHE INTERNAL "LIBHP_WITH_CJSON")

if(WIN32)
	# hp_iocp test
	set(LIBHP_WITH_HTTP 1 CACHE INTERNAL "LIBHP_WITH_HTTP")	
endif()
endif()

# FIXME
if(WIN32)
	# hp_io_t test
	set(LIBHP_WITH_CJSON 1 CACHE INTERNAL "LIBHP_WITH_CJSON")
	# hp_iocp test
	set(LIBHP_WITH_HTTP 1 CACHE INTERNAL "LIBHP_WITH_HTTP")	
endif()
###########################################################################################

# deps from deps/ 
# pkg_check_modules(openssl REQUIRED openssl)

# FIXME:
# cause pkg-config/hiredis has no redisAsyncContext::dataCleanup member, using deps/hiredis instead
# pkg_check_modules(hiredis REQUIRED hiredis)

if(WIN32)
	# paho.mqtt.c
	set(PAHO_BUILD_STATIC 1 CACHE INTERNAL "PAHO_BUILD_STATIC")	
	set(path_mqtt_lib "paho-mqtt3a-static")
	# sds
	set(sds_lib "deps/hiredis/sds.c")
	set(uv_lib uv_a)
else()
	set(sds_lib "deps/sds/*.c")
	set(uv_lib uv)
	set(path_mqtt_lib "paho-mqtt3a")
endif()

set(g_withs SSL ZLIB MYSQL BDB CURL MQTT CJSON 
	OPTPARSE 1 1 DLFCN ZLOG HTTP 1 1 1 HTTP HTTP 1 1 1 
	)
set(g_hdrs openssl/ssl.h  zlib.h  mysql/mysql.h db.h curl/curl.h MQTTAsync.h cjson/cJSON.h
		optparse.h uuid/uuid.h uv.h dlfcn.h zlog.h http_parser.h .nullfilesub.h .nullfilesrc.h .nullfilesrc.h 
		.nullfilesrc.h .nullfilesrc.h .nullfilesrc.h .nullfilesrc.h .nullfilesrc.h
	)
set(g_incs openssl/include deps/zlib/include/ deps/ deps/db/include/ deps/ deps/paho.mqtt.c/include/ deps/
		deps/ deps/ deps/uv/include/ deps/dlfcn-win32/include/ deps/zlog/include/ deps/http-parser/ deps/ deps/ deps/ 
		deps/ deps/ deps/ deps/ "${PROJECT_BINARY_DIR}/ include/"
	)
	
set(g_deps openssl zlib mysqlclient bdb curl paho.mqtt.c cjson 
		optparse uuid uv dlfcn-win32 zlog http-parser hiredis redis sds 
		gbk-utf8 libyuarel c-vector inih .src
	)
set(g_libs "ssl crypto" z mysqlclient db curl ${path_mqtt_lib} cjson 
			c uuid ${uv_lib} dl zlog http_parser hiredis 
			"deps/redis/src/adlist.c deps/redis/src/dict.c deps/redis/src/siphash.c deps/redis/src/mt19937-64.c deps/redis/src/zmalloc.c"
			${sds_lib}
			deps/gbk-utf8/utf8.c 
			deps/libyuarel/*.c
			"deps/c-vector/*.c deps/c-vector/*.cc"
			deps/inih/*.c
			"src/*.c src/*.cpp"
	)
###########################################################################################

hp_cmake_find_deps(SRCS LIBHP_WITH_ deps cmakes g_withs g_hdrs g_incs g_deps g_libs)
#build as library
add_library(libhp ${SRCS})

foreach(dep IN LISTS g_deps  )
	if(NOT ${dep}_INCLUDE_DIRS)
		continue()
	endif()

	hp_log("libhp: dep='${dep}', ${dep}_INCLUDE_DIRS='${${dep}_INCLUDE_DIRS}', ${dep}_LIBRARIES='${${dep}_LIBRARIES}'")
	target_include_directories(libhp PRIVATE ${${dep}_INCLUDE_DIRS} )
	target_link_libraries(libhp ${${dep}_LIBRARIES} ) 
endforeach() 

target_compile_options(libhp PUBLIC
	-DHAVE_CONFIG_H
    -DCMAKE_EXPORT_COMPILE_COMMANDS
    # for redis
    -DDICT_BENCHMARK_MAIN
	# for mg
	-DMG_ENABLE_DIRECTORY_LISTING
    # for cJSON
    -DcJSON_FREE_PRINT
)

###########################################################################################
add_executable(test-libhp test/test.cpp )
target_include_directories(test-libhp PRIVATE "${PROJECT_BINARY_DIR}/" include/ deps/ )
target_link_libraries(test-libhp libhp ) 

###########################################################################################
configure_file("config.h.in" "config.h")

