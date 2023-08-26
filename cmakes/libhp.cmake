###########################################################################################
#
# This file is PART of libhp project
#
# author hongjun.liao <docici@126.com>
# date 2023/7/5

###########################################################################################
macro(hp_log)
    string(TIMESTAMP CURRENT_TIMESTAMP "%Y-%m-%d %H:%M:%S")
    get_property(_callee GLOBAL PROPERTY SB_CURRENT_FUNCTION)
    message("[${CURRENT_TIMESTAMP}]/${ARGV}")
#    hp_log("[${CURRENT_TIMESTAMP}]/${_callee}: ${ARGV}")
endmacro()
###########################################################################################
# libhp依赖查找

# 为数字的表示不受${withprefix}XXX选项开关的控制
#set(g_withs SSL ZLIB MYSQL BDB CURL MQTT CJSON 
#	OPTPARSE 1 1 DLFCN ZLOG HTTP 1 1 1 HTTP HTTP 1 1 1 
#	)
# 为.nullfilesub.h或.nullfilesrc.h的表示不要查找系统目录来定位库
#	.nullfilesub.h: 通过add_subdirectory()
#		在调用add_subdirectory（）前， 复制${cmakes}/${dep}.cmake文件到对应目录（如果CMakeLlists.txt不存在的话）
#	.nullfilesrc.h: 通过file(GLOB)
#set(g_hdrs openssl/ssl.h  zlib.h  mysql/mysql.h db.h curl/curl.h MQTTAsync.h cjson/cJSON.h 
#		optparse.h uuid/uuid.h uv.h dlfcn.h zlog.h http_parser.h .nullfilesub.h .nullfilesrc.h .nullfilesrc.h 
#		.nullfilesrc.h .nullfilesrc.h .nullfilesrc.h .nullfilesrc.h .nullfilesrc.h
#	)
# NOTE:如果include某个库的头文件时没有子目录，如果该库比较”大型“， 我们就手动添加一层子目录， 以避免编译工程时include冲突, 比如:
# #include <db.h>  //没有形如db/db.h子目录,我们手动添加一层，并复制对应文件：
# set(g_incs deps/db/include/）  

#set(g_incs deps/uv/include/  deps/uv/include/ deps/uv/include/ deps/uv/include/ deps/uv/include/ deps/uv/include/ deps/uv/include/
#		deps/uv/include/ deps/uv/include/ deps/uv/include/ deps/uv/include/ deps/uv/include/ deps/uv/include/ deps/uv/include/ deps/uv/include/ deps/uv/include/ 
#		deps/uv/include/ deps/uv/include/ deps/uv/include/ deps/uv/include/ deps/uv/include/
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
function(hp_cmake_copy_cmakefile cmakes depdir)
	string(REPLACE "/" ";" s_list ${depdir})
	list(GET s_list -1 dep)
#	hp_log("hp_cmake_find_deps: depdir=${depdir}, ${CMAKE_CURRENT_SOURCE_DIR}/${cmakes}/${dep}.cmake => ${CMAKE_CURRENT_SOURCE_DIR}/deps/${depdir}/CMakeLists.txt")
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${cmakes}/${dep}.cmake" AND
			NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/${depdir}/CMakeLists.txt")
	
		hp_log("hp_cmake_find_deps: copying ${CMAKE_CURRENT_SOURCE_DIR}/${cmakes}/${dep}.cmake ...")
	
		configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/${cmakes}/${dep}.cmake
			${CMAKE_CURRENT_SOURCE_DIR}/deps/${depdir}/CMakeLists.txt
				COPYONLY)
	endif()
	
endfunction()

function(hp_cmake_find_deps SRCS_ withprefix depdir cmakes withs hdrs incs deps libs)
	list(LENGTH ${hdrs} hdrs_len)
	math( EXPR hdrs_len "${hdrs_len} - 1")

	foreach(index RANGE ${hdrs_len} )

		list(GET ${withs} ${index} with)
		list(GET ${hdrs} ${index} hdr)
		list(GET ${incs} ${index} inc)
		list(GET ${deps} ${index} dep)
		list(GET ${libs} ${index} lib_)
	
		if(NOT ((${with} EQUAL 1) OR (${withprefix}${with})))
			continue()
		endif()
	
		# string "ssl crypto" to list "ssl;crypto"
		string(REPLACE " " ";" lib_ ${lib_})	
		string(REPLACE " " ";" inc ${inc})	
	
		if((${hdr} STREQUAL .nullfilesub.h ) OR (${hdr} STREQUAL .nullfilesrc.h ))
		
			if((NOT (${dep} STREQUAL .src)) AND (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${depdir}/${dep}") )
				hp_log(FATAL_ERROR "Dependency ${CMAKE_CURRENT_SOURCE_DIR}/${depdir}/${dep} NOT found")
			endif()

			# use add_subdirectory()
			if((${hdr} STREQUAL .nullfilesub.h ))

				# copy a default CMakeLists.txt if NOT exist
				hp_cmake_copy_cmakefile(${cmakes} ${dep})
				add_subdirectory(${depdir}/${dep})
				
				set(${dep}_INCLUDE_DIRS ${inc} PARENT_SCOPE)	
				set(${dep}_LIBRARIES ${lib_} PARENT_SCOPE)
				
				hp_log("hp_cmake_find_deps: added with add_subdirectory(${depdir}/${dep})")
			#use file(GLOB)
			elseif((${hdr} STREQUAL .nullfilesrc.h ))

				file(GLOB SRCS ${SRCS} ${lib_})	
				file(GLOB files ${lib_})
	
				set(${dep}_INCLUDE_DIRS ${inc} PARENT_SCOPE)	
				
				hp_log("hp_cmake_find_deps: added using file(GLOB), GLOB='${lib_}', files='${files}'")
			endif()	
		# search header file needed	
		else()
			find_path(${dep}_INCLUDE_DIRS ${hdr} )
			set(pathfound 1)
			# use add_subdirectory() instead if NOT found
			if(NOT ${dep}_INCLUDE_DIRS) 
				if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${depdir}/${dep}" )
					hp_log(FATAL_ERROR "Dependency ${CMAKE_CURRENT_SOURCE_DIR}/${depdir}/${dep} NOT found")
				endif()
				
				hp_cmake_copy_cmakefile(${cmakes} ${dep})
				add_subdirectory(${depdir}/${dep})

				unset(pathfound)
				set(${dep}_INCLUDE_DIRS ${inc} PARENT_SCOPE)	
			endif()
			
			set(${dep}_LIBRARIES ${lib_} PARENT_SCOPE)
				
			if(NOT pathfound) 
				hp_log("hp_cmake_find_deps: '${hdr}' NOT found, use add_subdirectory(${depdir}/${dep}) instead" )
			else()
				hp_log("hp_cmake_find_deps: Found '${hdr}'" )
			endif()
				
		endif()
		
	endforeach() 
	
	set(${SRCS_} ${SRCS} PARENT_SCOPE)
#	hp_log("hp_cmake_find_deps: SRCS='${${SRCS_}}'")

endfunction()
###########################################################################################

#测试 
#hp_cmake_find_deps(SRCS LIBHP_WITH_ deps cmakes g_withs g_hdrs g_incs g_deps g_libs)
#hp_log("hp_cmake_find_deps: SRCS='${SRCS}'")

