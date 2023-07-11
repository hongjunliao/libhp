###########################################################################################
#
# This file is PART of libhp project
#
# author hongjun.liao <docici@126.com>
# date 2023/7/5

###########################################################################################
# libhp依赖查找

# 为数字的表示不受${withprefix}XXX选项开关的控制
#set(g_withs SSL ZLIB MYSQL BDB CURL MQTT CJSON 
#	OPTPARSE 1 1 DLFCN ZLOG HTTP 1 1 1 HTTP HTTP 1 1 1 
#	)
# 为.nullfilesub.h或.nullfilesrc.h的表示不要查找系统目录来定位库
#	.nullfilesub.h: 通过add_subdirectory()
#	.nullfilesrc.h: 通过file(GLOB)
#set(g_hdrs openssl/ssl.h  zlib.h  mysql/mysql.h db.h curl/curl.h MQTTAsync.h cjson/cJSON.h 
#		optparse.h uuid/uuid.h uv.h dlfcn.h zlog.h http_parser.h .nullfilesub.h .nullfilesrc.h .nullfilesrc.h 
#		.nullfilesrc.h .nullfilesrc.h .nullfilesrc.h .nullfilesrc.h .nullfilesrc.h
#	)
# 为.find_path的表示使用之前查找返回的目录
#set(g_incs .find_path  .find_path .find_path .find_path .find_path .find_path .find_path
#		.find_path .find_path .find_path .find_path .find_path .find_path .find_path .find_path .find_path 
#		.find_path .find_path .find_path .find_path .find_path
#	)
#.src表示项目源目录,不需要查检deps/下相应目录是否存在
#set(g_deps openssl zlib mysqlclient bdb curl paho.mqtt.c cjson 
#		optparse uuid uv dlfcn-win32 zlog http-parser hiredis redis sds 
#		gbk-utf8 libyuarel c-vector inih .src
#	)
# 空格隔开的表示实际为数组
#set(g_libs "ssl crypto" z mysqlclient db curl paho-mqtt3a cjson 
#			c uuid uv dl zlog http_parser hiredis 
#			"deps/redis/src/adlist.c deps/redis/src/zmalloc.c deps/redis/src/dict.c deps/redis/src/siphash.c deps/redis/src/mt19937-64.c"
#			deps/sds/*.c
#			deps/gbk-utf8/utf8.c 
#			deps/libyuarel/*.c
#			"deps/c-vector/*.c deps/c-vector/*.cc"
#			deps/inih/*.c
#			src/*.c
#	)

# don't overwrite the 3rd party's own CMakeLists.txt file
function(hp_cmake_copy_cmakefile dep)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmakefiles/${dep}.cmake" AND
			NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/${dep}/CMakeLists.txt")
	
		message("hp_cmake_find_deps: copying ${CMAKE_CURRENT_SOURCE_DIR}/cmakefiles/${dep}.cmake ...")
	
		configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/cmakefiles/${dep}.cmake
			${CMAKE_CURRENT_SOURCE_DIR}/deps/${dep}/CMakeLists.txt
				COPYONLY)
	endif()
	
endfunction()

function(hp_cmake_find_deps SRCS_ withprefix withs hdrs incs deps libs)
	list(LENGTH ${hdrs} hdrs_len)
	math( EXPR hdrs_len "${hdrs_len} - 1")

	foreach(index RANGE ${hdrs_len} )

		list(GET ${withs} ${index} with)
		list(GET ${hdrs} ${index} hdr)
		list(GET ${incs} ${index} inc)
		list(GET ${deps} ${index} dep)
		list(GET ${libs} ${index} lib_)
	
		# string "ssl crypto" to list "ssl;crypto"
		string(REPLACE " " ";" lib_ ${lib_})	
		if((${inc} STREQUAL .find_path))
			unset(inc)
		else()
			string(REPLACE " " ";" inc ${inc})	
		endif()
	
		if((${with} EQUAL 1 ) AND (${hdr} STREQUAL .nullfilesub.h ))
			if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/${dep}" )
				message(FATAL_ERROR "Dependency ${CMAKE_CURRENT_SOURCE_DIR}/deps/${dep} NOT found")
			endif()
			
			hp_cmake_copy_cmakefile(${dep})

			add_subdirectory(deps/${dep})
			set(${dep}_INCLUDE_DIRS "deps/${dep}" ${inc} PARENT_SCOPE)	
			set(${dep}_LIBRARIES ${lib_} PARENT_SCOPE)
			message("hp_cmake_find_deps: lib added, add_subdirectory(deps/${dep}),${dep}_INCLUDE_DIRS='${${dep}_INCLUDE_DIRS}', ${dep}_LIBRARIES='${${dep}_LIBRARIES}'")
			continue()
		endif()	
	
		if((${with} EQUAL 1 ) AND (${hdr} STREQUAL .nullfilesrc.h ))
			if(${dep} STREQUAL .src)
				set(${dep}_INCLUDE_DIRS ${inc} PARENT_SCOPE)	
			elseif(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/${dep}" )
				message(FATAL_ERROR "Dependency ${CMAKE_CURRENT_SOURCE_DIR}/deps/${dep} NOT found")
			endif()
			
			file(GLOB SRCS ${SRCS} ${lib_})
	
			file(GLOB files ${lib_})
			message("hp_cmake_find_deps: lib added using file(GLOB), GLOB='${lib_}', files='${files}'")
			continue()
		endif()	

		if(${withprefix}${with} AND (${hdr} STREQUAL .nullfilesrc.h ))
			if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/${dep}" )
				message(FATAL_ERROR "Dependency ${CMAKE_CURRENT_SOURCE_DIR}/deps/${dep} NOT found")
			endif()
			
			file(GLOB SRCS ${SRCS} ${lib_})
	
			file(GLOB files ${lib_})
			message("hp_cmake_find_deps: lib enabled by ${withprefix}${with}, using file(GLOB), GLOB='${lib_}', files='${files}'")
			continue()
		endif()
	
		if((${with} EQUAL 1) OR (${withprefix}${with}))
			find_path(${dep}_INCLUDE_DIRS ${hdr} )
		
			if(NOT ${dep}_INCLUDE_DIRS) 
				if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/${dep}" )
					message(FATAL_ERROR "Dependency ${CMAKE_CURRENT_SOURCE_DIR}/deps/${dep} NOT found")
				endif()
				
				hp_cmake_copy_cmakefile(${dep})
				
				add_subdirectory(deps/${dep})
				set(${dep}_INCLUDE_DIRS "deps/${dep}" ${inc} PARENT_SCOPE)	
				message("hp_cmake_find_deps: lib enabled by ${withprefix}${with}, add_subdirectory(deps/${dep})" )
				continue()
			endif()
			
			set(${dep}_LIBRARIES ${lib_} PARENT_SCOPE)	
			message("hp_cmake_find_deps: lib added by searching, ${dep}_INCLUDE_DIRS='${${dep}_INCLUDE_DIRS}', ${dep}_LIBRARIES='${${dep}_LIBRARIES}'" )
		endif()

	endforeach() 
	
	set(${SRCS_} ${SRCS} PARENT_SCOPE)
#	message("hp_cmake_find_deps: SRCS='${${SRCS_}}'")

endfunction()
###########################################################################################

#测试 
#hp_cmake_find_deps(SRCS LIBHP_WITH_ g_withs g_hdrs g_incs g_deps g_libs)
#message("hp_cmake_find_deps: SRCS='${SRCS}'")

