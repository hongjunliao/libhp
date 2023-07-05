###########################################################################################
#
# This file is PART of libhp project
#
# author hongjun.liao <docici@126.com>
# date 2020/6

###########################################################################################

cmake_minimum_required(VERSION 2.8.12)

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
# deps from deps/ 
# pkg_check_modules(openssl REQUIRED openssl)

# FIXME:
# cause pkg-config/hiredis has no redisAsyncContext::dataCleanup member, using deps/hiredis instead
# pkg_check_modules(hiredis REQUIRED hiredis)

# 为数字的表示不受LIBHP_WITH_XXX选项开关的控制
set(withs SSL ZLIB MYSQL BDB CURL MQTT CJSON 
	OPTPARSE 1 1 DLFCN ZLOG HTTP 1 1 1 HTTP HTTP 1 1 1 
	)
# 为.nullfilesub.h或.nullfilesrc.h的表示不要查找系统目录来定位库
#	.nullfilesub.h: 通过add_subdirectory()
#	.nullfilesrc.h: 通过file(GLOB)
set(hdrs openssl/ssl.h  zlib.h  mysql/mysql.h db.h curl/curl.h MQTTAsync.h cjson/cJSON.h 
		optparse.h uuid/uuid.h uv.h dlfcn.h zlog.h http_parser.h .nullfilesub.h .nullfilesrc.h .nullfilesrc.h 
		.nullfilesrc.h .nullfilesrc.h .nullfilesrc.h .nullfilesrc.h .nullfilesrc.h
	)
#.src表示项目源目录,不需要查检deps/下相应目录是否存在
set(deps openssl zlib mysqlclient bdb curl paho.mqtt.c cjson 
		optparse uuid uv dlfcn-win32 zlog http-parser hiredis redis sds 
		gbk-utf8 libyuarel c-vector inih .src
	)
# 空格隔开的表示实际为数组
set(libs "ssl crypto" z mysqlclient db curl paho-mqtt3a cjson 
			c uuid uv dl zlog http_parser hiredis 
			"deps/redis/src/adlist.c deps/redis/src/zmalloc.c deps/redis/src/dict.c deps/redis/src/siphash.c deps/redis/src/mt19937-64.c"
			deps/sds/*.c
			deps/gbk-utf8/utf8.c 
			deps/libyuarel/*.c
			"deps/c-vector/*.c deps/c-vector/*.cc"
			deps/inih/*.c
			src/*.c
	)


#function(hp_find_deps SRCS withs hdrs deps libs)
	list(LENGTH hdrs hdrs_len)
	math( EXPR hdrs_len "${hdrs_len} - 1")

foreach(index RANGE ${hdrs_len} )

	list(GET withs ${index} with)
	list(GET hdrs ${index} hdr)
	list(GET deps ${index} dep)
	list(GET libs ${index} lib_)

	# string "ssl crypto" to list "ssl;crypto"
	string(REPLACE " " ";" lib_ ${lib_})	

	if((${with} EQUAL 1 ) AND (${hdr} STREQUAL .nullfilesub.h ))
		if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/${dep}" )
			message(FATAL_ERROR "Dependency ${CMAKE_CURRENT_SOURCE_DIR}/deps/${dep} NOT found")
		endif()
		
		add_subdirectory(deps/${dep})
		set(${dep}_INCLUDE_DIRS "deps/${dep}")	
		set(${dep}_LIBRARIES ${lib_})
		message("libhp: lib added, add_subdirectory(deps/${dep}),${dep}_INCLUDE_DIRS='${${dep}_INCLUDE_DIRS}', ${dep}_LIBRARIES='${${dep}_LIBRARIES}'")
		continue()
	endif()	

	if((${with} EQUAL 1 ) AND (${hdr} STREQUAL .nullfilesrc.h ))
		if((NOT (${dep} STREQUAL .src) ) AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/${dep}" )
			message(FATAL_ERROR "Dependency ${CMAKE_CURRENT_SOURCE_DIR}/deps/${dep} NOT found")
		endif()
		
		file(GLOB SRCS ${SRCS} ${lib_})

		file(GLOB files ${lib_})
		#		message("libhp: lib added using file(GLOB), GLOB='${lib_}', files='${files}'")
		continue()
	endif()	

	if(LIBHP_WITH_${with} AND (${hdr} STREQUAL .nullfilesrc.h ))
		if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/${dep}" )
			message(FATAL_ERROR "Dependency ${CMAKE_CURRENT_SOURCE_DIR}/deps/${dep} NOT found")
		endif()
		
		file(GLOB SRCS ${SRCS} ${lib_})

		file(GLOB files ${lib_})
		message("libhp: lib enabled by LIBHP_WITH_${with}, using file(GLOB), GLOB='${lib_}', files='${files}'")
		continue()
	endif()

	if((${with} EQUAL 1) OR LIBHP_WITH_${with})
		find_path(${dep}_INCLUDE_DIRS ${hdr} )
	
		if(NOT ${dep}_INCLUDE_DIRS) 
			if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/${dep}" )
				message(FATAL_ERROR "Dependency ${CMAKE_CURRENT_SOURCE_DIR}/deps/${dep} NOT found")
			endif()
			
			add_subdirectory(deps/${dep})
			set(${dep}_INCLUDE_DIRS "deps/${dep}")	
			message("libhp: lib enabled by LIBHP_WITH_${with}, add_subdirectory(deps/${dep})" )
			continue()
		endif()
		
		set(${dep}_LIBRARIES ${lib_})	
		message("libhp: lib added by searching, ${dep}_INCLUDE_DIRS='${${dep}_INCLUDE_DIRS}', ${dep}_LIBRARIES='${${dep}_LIBRARIES}'" )
	endif()
	

endforeach() 
#	message("libhp: SRCS='${SRCS}'")
	set(${sources} ${SRCS} PARENT_SCOPE)
#endfunction()
###########################################################################################

#build as library
#message("libhp: SRCS='${SRCS}'")
add_library(libhp ${SRCS})

foreach(dep IN LISTS deps  )
	message("libhp: dep='${dep}', ${dep}_INCLUDE_DIRS='${${dep}_INCLUDE_DIRS}', ${dep}_LIBRARIES='${${dep}_LIBRARIES}'")
	if(NOT (${dep} STREQUAL .src))
		target_include_directories(libhp PRIVATE ${${dep}_INCLUDE_DIRS} )
		target_link_libraries(libhp ${${dep}_LIBRARIES} ) 
	
		
	endif()
endforeach() 
target_include_directories(libhp PRIVATE "${PROJECT_BINARY_DIR}/" include/hp/ include/ deps/ )

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
add_executable(test-libhp src/test.c test/test.c )
target_include_directories(test-libhp PRIVATE "${PROJECT_BINARY_DIR}/" include/hp/ include/ deps/ )
target_link_libraries(test-libhp libhp ) 

###########################################################################################
configure_file("config.h.in" "config.h")


