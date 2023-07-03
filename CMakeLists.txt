###########################################################################################
#
# This file is PART of libhp project
#
# author hongjun.liao <docici@126.com>
# date 2020/6

###########################################################################################

cmake_minimum_required(VERSION 2.8.12)
if(COMMAND cmake_policy)
cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)
set(CMAKE_LEGACY_CYGWIN_WIN32 1)

# libhp
project(libhp)

###########################################################################################

if(NOT CMAKE_BUILD_TYPE)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Options are: None, Debug, Release, RelWithDebInfo, MinSizeRel." FORCE)
endif()
# -DCMAKE_EXPORT_COMPILE_COMMANDS is for lsp/clangd, see
# https://clangd.llvm.org/installation.html
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# show build type
message("CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} Options are: Debug|Release")

###########################################################################################
configure_file("config.h.in" "config.h")

###########################################################################################
# deps from deps/ for from pkg-config
if(NOT MSVC)
	find_package(PkgConfig REQUIRED)
	if(LIBHP_WITH_SSL) 
		pkg_check_modules(openssl REQUIRED openssl) 
	endif()
	if(LIBHP_WITH_ZLIB) 
		pkg_check_modules(zlib REQUIRED zlib)	   
	endif()
	if(LIBHP_WITH_BDB)
		#pkg_check_modules(LIBHP_WITH_BDB REQUIRED bdb)
		find_path(bdb db.h )
		if(NOT bdb AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/bdb") 
			message(FATAL_ERROR "LIBHP_WITH_BDB defined but ${CMAKE_CURRENT_SOURCE_DIR}/deps/bdb NOT found")
		endif()
	endif()
	if(LIBHP_WITH_CURL)
		pkg_check_modules(libcurl REQUIRED libcurl)
	endif()
	if(LIBHP_WITH_REDIS)
		#fixme:
		# cause pkg-config/hiredis has no redisAsyncContext::dataCleanup member, using deps/hiredis instead
		# pkg_check_modules(hiredis REQUIRED hiredis)

		if( NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/hiredis") 
			message(FATAL_ERROR "LIBHP_WITH_REDIS defined but ${CMAKE_CURRENT_SOURCE_DIR}/deps/hiredis NOT found")
		else()
			file(GLOB SRCS ${SRCS} deps/hiredis/alloc.c)
		endif()

	endif()
	if(LIBHP_WITH_MYSQL)
		pkg_check_modules(mysqlclient REQUIRED mysqlclient)
	endif()
	if(LIBHP_WITH_MQTT) 
		#pkg_check_modules(LIBHP_WITH_MQTT REQUIRED paho-mqtt)
		#find_package(paho-mqtt REQUIRED paho-mqtt)
		find_path(paho_mqtt_c MQTTAsync.h )
		if(NOT paho_mqtt_c AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/paho.mqtt.c") 
			message(FATAL_ERROR "LIBHP_WITH_MQTT defined but ${CMAKE_CURRENT_SOURCE_DIR}/deps/paho.mqtt.c NOT found")
		endif()
	endif()
	if(LIBHP_WITH_CJSON) 
		pkg_check_modules(libcjson REQUIRED libcjson) 
	endif()	
	if(LIBHP_WITH_OPTPARSE AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/optparse")
		message(FATAL_ERROR "LIBHP_WITH_OPTPARSE defined but ${CMAKE_CURRENT_SOURCE_DIR}/deps/optparse NOT found")
	endif()
	if(LIBHP_WITH_DLFCN)	#nothing need to do 
	endif()
	#pkg_check_modules(LIBHP_WITH_PROC_SYSINFO REQUIRED openssl)
	#pkg_check_modules(LIBHP_WITH_AMQP REQUIRED openssl)
	#pkg_check_modules(LIBHP_WITH_TIMERFD REQUIRED openssl)
	#pkg_check_modules(LIBHP_WITH_JNI REQUIRED openssl)
	#pkg_check_modules(LIBHP_DEPRECADTED REQUIRED openssl)
	
	pkg_check_modules(uuid REQUIRED uuid)	   

else() # Windows/msvc specific
	if(LIBHP_WITH_DLFCN)
		add_subdirectory(deps/dlfcn-win32)	
	endif()

endif()

if(LIBHP_WITH_ZLOG)
	find_path(zlog zlog.h )
	if(NOT zlog AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/zlog") 
		message(FATAL_ERROR "LIBHP_WITH_ZLOG defined but
			${CMAKE_CURRENT_SOURCE_DIR}/deps/zlog NOT found")
	elseif(NOT zlog)
		add_subdirectory(deps/zlog)
	endif()
	
endif()
pkg_check_modules(libuv REQUIRED libuv) 

###########################################################################################
# 3thrd party sources

#deps/redis
#pkg_check_modules(LIBHP_WITH_REDIS REQUIRED LIBHP_WITH_REDIS)
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/redis")
	message(FATAL_ERROR "Redis source dir ${CMAKE_CURRENT_SOURCE_DIR}/deps/redis NOT found")
else()
	file(GLOB SRCS ${SRCS} deps/redis/src/adlist.c deps/redis/src/zmalloc.c 
		deps/redis/src/dict.c deps/redis/src/siphash.c deps/redis/src/mt19937-64.c)
endif()

# Redis/sds
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/sds")
	message(FATAL_ERROR "Redis/sds source ${CMAKE_CURRENT_SOURCE_DIR}/deps/sds NOT found")
else()
	file(GLOB SRCS ${SRCS} deps/sds/*.c )
endif()

# deps/http-parser ...
#pkg_check_modules(LIBHP_WITH_HTTP REQUIRED openssl)
if(LIBHP_WITH_HTTP)
	#pkg_check_modules(http-parser REQUIRED http-parser)
	find_path(http-parser http_parser.h )
	if(NOT http-parser AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/http-parser") 
		message(FATAL_ERROR "LIBHP_WITH_HTTP defined but ${CMAKE_CURRENT_SOURCE_DIR}/deps/http-parser NOT found")
	endif()

	if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/gbk-utf8" OR
			NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/libyuarel")
		message(FATAL_ERROR "LIBHP_WITH_HTTP defined buf source dirs ${CMAKE_CURRENT_SOURCE_DIR}/deps/gbk-utf8,
			${CMAKE_CURRENT_SOURCE_DIR}/deps/libyuarel	NOT found")
	else()
		file(GLOB SRCS ${SRCS} deps/gbk-utf8/utf8.c deps/libyuarel/*.c) 
	endif()
   	
endif()

#deps/inih
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/inih")
	message(FATAL_ERROR "source dir ${CMAKE_CURRENT_SOURCE_DIR}/deps/inih NOT found")
else()
	file(GLOB SRCS ${SRCS} deps/inih/*.c)
endif()

#deps/c-vector
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	file(GLOB SRCS ${SRCS} deps/c-vector/*) 
endif()
###########################################################################################
# souces
file(GLOB_RECURSE SRCS ${SRCS} src/*.c src/*.cpp)

#build as library
add_library(libhp STATIC ${SRCS})

###########################################################################################

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
if(MSVC)
	target_compile_options(libhp PUBLIC -D_CRT_SECURE_NO_WARNINGS /MTd)
endif()

###########################################################################################

target_include_directories(libhp PRIVATE "${PROJECT_BINARY_DIR}/" include/hp/ include/ deps/
	deps/http-parser/)
if(MSVC AND LIBHP_WITH_DLFCN)
	target_include_directories(libhp PRIVATE deps/dlfcn-win32/)
endif()

if(LIBHP_WITH_ZLOG)
	target_include_directories(libhp PRIVATE ${zlog_INCLUDE_DIRS})
endif()

##########################################################################################

target_link_libraries(libhp ${libuv_LIBRARIES} ${uuid_LIBRARIES}) 
if(LIBHP_WITH_SSL) 
	target_link_libraries(libhp ${openssl_LIBRARIES}) 
endif()
if(LIBHP_WITH_ZLIB) 
	target_link_libraries(libhp ${zlib_LIBRARIES}) 
endif()
if(LIBHP_WITH_CURL)
	target_link_libraries(libhp ${libcurl_LIBRARIES}) 
endif()
if(LIBHP_WITH_REDIS)
endif()
if(LIBHP_WITH_MYSQL)
	target_link_libraries(libhp ${mysqlclient_LIBRARIES}) 
endif()
if(LIBHP_WITH_MQTT) 
	target_link_libraries(libhp paho-mqtt3a) 
endif()
if(LIBHP_WITH_CJSON) 
	target_link_libraries(libhp ${libcjson_LIBRARIES} ) 
endif()	
if(LIBHP_WITH_HTTP) 
	target_link_libraries(libhp http_parser ) 
endif()	
if(LIBHP_WITH_DLFCN)
	target_link_libraries(libhp dl ) 
endif()
if(LIBHP_WITH_ZLOG)
	target_link_libraries(libhp zlog ) 
endif()

if(LIBHP_WITH_REDIS)
	target_link_libraries(libhp hiredis) 
endif()
##########################################################################################
# test executable: test-libhp 

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	project(test-libhp)

	file(GLOB SRCS ${SRCS} deps/sds/*.c )
	file(GLOB SRCS ${SRCS} test/test.c test/example-libuv.c deps/inih/*.c deps/c-vector/*.c deps/c-vector/*.cc)

	if(LIBHP_WITH_WIN32_INTERROP)
		file(GLOB SRCS ${SRCS} test/example-ae.c)
	endif()

	########################################################################################## 

	add_executable(test-libhp ${SRCS} )

	##########################################################################################

	target_compile_options(test-libhp PUBLIC
		-DHAVE_CONFIG_H
		-DCMAKE_EXPORT_COMPILE_COMMANDS
		# for mongoose
		-DMG_ENABLE_LINES -DMG_ENABLE_DIRECTORY_LISTING
		# for redis
		-DDICT_BENCHMARK_MAIN
		# for cJSON
		-DcJSON_FREE_PRINT
	)
	if(MSVC)
		target_compile_options(libhp PUBLIC -D_CRT_SECURE_NO_WARNINGS /MTd)
	endif()

	##########################################################################################

	target_include_directories(test-libhp PRIVATE "${PROJECT_BINARY_DIR}/" include/ deps/ include/hp/)
	target_link_libraries(test-libhp ${libuv_LIBRARIES} ${uuid_LIBRARIES}
		${openssl_LIBRARIES}  ${zlib_LIBRARIES} ${libcurl_LIBRARIES}
		${mysqlclient_LIBRARIES} paho-mqtt3a ${libcjson_LIBRARIES} http_parser dl zlog
		hiredis db)
endif()

