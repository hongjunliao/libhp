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
option(LIBHP_WITH_MQTT "Enable MQTT support" OFF)
option(LIBHP_WITH_HTTP "Enable HTTP client support" OFF)
option(LIBHP_WITH_CJSON "Enable cJSON support" OFF)
option(LIBHP_WITH_REDIS "Enable Redis support" OFF)

###########################################################################################
include(CheckIncludeFile)
check_include_file(unistd.h HAVE_UNISTD_H)
check_include_file(poll.h HAVE_POLL_H)
check_include_file(sys/epoll.h HAVE_SYS_EPOLL_H)
check_include_file(sys/uio.h HAVE_SYS_UIO_H)
check_include_file(sys/inotify.h HAVE_SYS_INOTIFY_H)
check_include_file(search.h HAVE_SEARCH_H)
check_include_file(sys/timerfd.h HAVE_SYS_TIMERFD_H)

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
	#ssl
	set(ssl_lib "libssl64MT libcrypto64MT")
else()
	set(sds_lib "deps/sds/*.c")
	set(uv_lib uv)
	set(path_mqtt_lib "paho-mqtt3a")
	set(ssl_lib "ssl crypto")
endif()

set(g_withs
		ZLIB MYSQL BDB CURL MQTT
		CJSON OPTPARSE REDIS REDIS
		ZLOG HTTP 1 1 1
		HTTP 1 1 1
	)
set(g_hdrs
		zlib.h  mysql/mysql.h db.h curl/curl.h MQTTAsync.h
		cjson/cJSON.h optparse.h uuid/uuid.h uv.h
		zlog.h http_parser.h .nullfilesub.h .nullfilesrc.h .nullfilesrc.h
		.nullfilesrc.h .nullfilesrc.h .nullfilesrc.h .nullfilesrc.h
	)
set(g_incs
		deps/zlib/include/ deps/ deps/db/include/ deps/ deps/paho.mqtt.c/include/
		deps/ deps/ deps/ deps/uv/include/
		deps/zlog/include/ deps/http-parser/ deps/ deps/ deps/
		deps deps/ deps/ "${PROJECT_BINARY_DIR}/ include/"
	)
	
set(g_deps
		zlib mysqlclient bdb curl paho.mqtt.c
		cjson optparse uuid uv
		zlog http-parser hiredis redis sds
		libyuarel c-vector inih .src
	)
set(g_libs
		z mysqlclient db curl ${path_mqtt_lib}
		cjson c uuid ${uv_lib}
		zlog http_parser hiredis "deps/redis/src/adlist.c deps/redis/src/dict.c deps/redis/src/siphash.c deps/redis/src/mt19937-64.c deps/redis/src/zmalloc.c" ${sds_lib}
		"deps/libyuarel/yuarel.c" "deps/c-vector/*.c deps/c-vector/*.cc" deps/inih/*.c "src/*.c src/*.cpp"
	)
###########################################################################################

hp_cmake_find_deps(SRCS LIBHP_WITH_ deps cmakes g_withs g_hdrs g_incs g_deps g_libs)
#build as library
add_library(libhp STATIC ${SRCS})

foreach(dep IN LISTS g_deps  )
	if(NOT ${dep}_INCLUDE_DIRS)
		continue()
	endif()

	hp_log("libhp: dep='${dep}', ${dep}_INCLUDE_DIRS='${${dep}_INCLUDE_DIRS}', ${dep}_LIBRARIES='${${dep}_LIBRARIES}'")
	target_include_directories(libhp PUBLIC ${${dep}_INCLUDE_DIRS} )
	target_link_libraries(libhp ${${dep}_LIBRARIES} ) 
endforeach() 

target_compile_options(libhp PUBLIC
    -DCMAKE_EXPORT_COMPILE_COMMANDS
    # for redis
    -DDICT_BENCHMARK_MAIN
	# for mg
	-DMG_ENABLE_DIRECTORY_LISTING
    # for cJSON
    -DcJSON_FREE_PRINT
)

#target_include_directories(libhp PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}> )
if(WIN32)
	target_link_directories(libhp PUBLIC deps/openssl/lib/VC/static/)
endif()

###########################################################################################
add_executable(test-libhp test/test.cpp )
target_link_libraries(test-libhp libhp )

###########################################################################################
configure_file(
    hp_config.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/hp_config.h"   # 明确写出完整路径
    @ONLY   # 可选，但建议加上
)

message(STATUS "  LIBHP_WITH_MQTT    = ${LIBHP_WITH_MQTT}")
message(STATUS "  LIBHP_WITH_HTTP    = ${LIBHP_WITH_HTTP}")
message(STATUS "  LIBHP_WITH_CJSON   = ${LIBHP_WITH_CJSON}")
message(STATUS "  LIBHP_WITH_REDIS   = ${LIBHP_WITH_REDIS}")
