#############################################################################################
# https://gitee.com/opensource168/libuuid.git
# git checkout win64-2.11.11001
#
#
#
#
cmake_minimum_required(VERSION 3.6)

project(uuid C)

set(SOURCE_FILES libuuid/clear.c libuuid/compare.c libuuid/copy.c libuuid/dllmain.cpp libuuid/gen_uuid.c libuuid/isnull.c libuuid/libuuid.cpp 
	libuuid/pack.c libuuid/parse.c libuuid/randutils.c libuuid/stdafx.cpp libuuid/unpack.c libuuid/unparse.c libuuid/uuid_time.c)

add_library(uuid STATIC ${SOURCE_FILES})


