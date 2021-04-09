# author hongjun.liao <docici@126.com>
# date 2020/6

cmake_minimum_required(VERSION 2.4)
if(COMMAND cmake_policy)
  cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

# libhp
project(libhp)

# deps
file(GLOB SRCS deps/libyuarel/*.c deps/c-vector/*)
if(WIN32)
file(GLOB SRCS ${SRCS}
	deps/redis/src/sds.c
	deps/redis/src/zmalloc.c
	)
else()
file(GLOB SRCS ${SRCS}
	deps/sds/*.c
	)
endif()

file(GLOB SRCS ${SRCS}
	deps/redis/src/adlist.c
	deps/redis/src/dict.c
	deps/redis/src/zmalloc.c
    deps/redis/src/siphash.c
	deps/http-parser/http_parser.c
	deps/gbk-utf8/utf8.c
	)
file(GLOB_RECURSE SRCS ${SRCS} src/*.c include/*)
file(GLOB SRCS ${SRCS} README.md)

add_library(libhp STATIC ${SRCS})
target_compile_options(libhp PUBLIC
    -DHAVE_CONFIG_H -D_GNU_SOURCE -O0 -g3 -Wall -DCMAKE_EXPORT_COMPILE_COMMANDS
    # for redis
    -DDICT_BENCHMARK_MAIN
    # for cJSON
    -DcJSON_FREE_PRINT
	)
add_compile_options(-DcJSON_FREE_PRINT)
if(NOT WIN32)
add_compile_options(-g3 -Wall)
else()
target_compile_options(-D_CRT_SECURE_NO_WARNINGS)
endif()

add_subdirectory(deps/cJSON)
add_subdirectory(deps/dlfcn-win32)

target_include_directories(libhp PRIVATE include/hp/ include/ deps/ ./)
target_link_libraries(libhp dl cjson)
# message(STATUS ${CMAKE_C_FLAGS})

# test-libhp
project(test-libhp)
file(GLOB SRCS test/*.c deps/inih/*)
add_executable(test-libhp ${SRCS} )
target_include_directories(test-libhp PRIVATE include/ deps/ ./)
target_link_directories(test-libhp PRIVATE lib/)
target_link_libraries(test-libhp libhp ssl crypto dl cjson hiredis uv zlog uuid pthread db z paho-mqtt3a curl mysqlclient)
