# author hongjun.liao <docici@126.com>
# date 2020/6

cmake_minimum_required(VERSION 2.4)
if(COMMAND cmake_policy)
cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)
set(CMAKE_LEGACY_CYGWIN_WIN32 1)

if(MSVC)
set(BUILD_SHARED_LIBS OFF)
add_compile_options(/MTd)
#set(LIBHP_WITH_WIN32_INTERROP 0 cache bool "using microsoftarchive/redis.git")
endif()

# libhp
project(libhp)

# deps: sds etc
file(GLOB SRCS deps/libyuarel/*.c deps/c-vector/*.c deps/c-vector/*.cc)
file(GLOB SRCS ${SRCS} deps/redis/src/adlist.c deps/redis/src/zmalloc.c 
	deps/redis/src/dict.c deps/redis/src/siphash.c)
if(WIN32 AND NOT MSVC)
file(GLOB SRCS ${SRCS} deps/redis/src/mt19937-64.c)
endif()
	
if(WIN32)
file(GLOB SRCS ${SRCS} deps/redis/src/sds.c )
else()
file(GLOB SRCS ${SRCS} deps/sds/*.c)
endif()

file(GLOB SRCS ${SRCS}
	deps/http-parser/http_parser.c deps/gbk-utf8/utf8.c )
file(GLOB_RECURSE SRCS ${SRCS} src/*.c src/*.cpp)
file(GLOB SRCS ${SRCS} README.md)

add_library(libhp STATIC ${SRCS})
target_compile_options(libhp PUBLIC
    -DHAVE_CONFIG_H -D_GNU_SOURCE -DCMAKE_EXPORT_COMPILE_COMMANDS
    # for redis
    -DDICT_BENCHMARK_MAIN
    # for cJSON
    -DcJSON_FREE_PRINT
	)
add_compile_options(-DcJSON_FREE_PRINT)
if(NOT MSVC)
	target_compile_options(libhp PUBLIC  -O0 -g3  -Wall)
else()
	target_compile_options(libhp PUBLIC -D_CRT_SECURE_NO_WARNINGS /MTd)
endif()

# deps using cmake build system
if(NOT LIBHP_WITHOUT_CJSON)
add_subdirectory(deps/cJSON EXCLUDE_FROM_ALL)
endif()
if(WIN32 AND MSVC)
add_subdirectory(deps/dlfcn-win32 EXCLUDE_FROM_ALL)
endif()
if(MSVC)
add_subdirectory(deps/hiredis) 
endif()

if(WIN32 AND MSVC)
add_subdirectory(deps/paho.mqtt.c EXCLUDE_FROM_ALL)
add_subdirectory(deps/libuv EXCLUDE_FROM_ALL)
endif()

target_include_directories(libhp PRIVATE include/hp/ include/ deps/ deps/libuv/include/ deps/libc/ ./)
if(MSVC)
target_include_directories(libhp PRIVATE include/win/ )
endif()
if(LIBHP_WITH_WIN32_INTERROP)
target_include_directories(libhp PRIVATE deps/redis/deps/ )
endif()
target_link_libraries(libhp dl)
if(NOT LIBHP_WITHOUT_CJSON)
target_link_libraries(libhp cjson)
endif()
# message(STATUS ${CMAKE_C_FLAGS})

# test-libhp 
project(test-libhp)
file(GLOB SRCS test/test.c test/example-libuv.c deps/inih/*.c)
if(LIBHP_WITH_WIN32_INTERROP)
file(GLOB SRCS ${SRCS} test/example-ae.c)
endif()

add_executable(test-libhp ${SRCS} )
target_compile_options(test-libhp PUBLIC
    -DHAVE_CONFIG_H -D_GNU_SOURCE -DCMAKE_EXPORT_COMPILE_COMMANDS
    # for mongoose
    -DMG_ENABLE_LINES -DMG_ENABLE_DIRECTORY_LISTING
    # for redis
    -DDICT_BENCHMARK_MAIN
    # for cJSON
    -DcJSON_FREE_PRINT
)
if(NOT MSVC)
	target_compile_options(test-libhp PUBLIC  -O0 -g3  -Wall)
else()
	target_compile_options(test-libhp PUBLIC -D_CRT_SECURE_NO_WARNINGS  /MTd)
endif()

target_include_directories(test-libhp PRIVATE include/ deps/ ./)
if(LIBHP_WITH_WIN32_INTERROP)
target_include_directories(test-libhp PRIVATE deps/redis/deps/ deps/libuv/include/)
endif()

if(MSVC)
target_link_directories(test-libhp PRIVATE lib/)
endif()
target_link_libraries(test-libhp libhp)
if(NOT LIBHP_WITHOUT_CJSON)
target_link_libraries(test-libhp cjson)
endif()
if(MSVC OR (NOT WIN32))
target_link_libraries(test-libhp libhp dl zlog)
target_link_libraries(test-libhp paho-mqtt3a)
endif()
if(NOT WIN32)
target_link_libraries(test-libhp uuid uv curl hiredis z ssl crypto pthread mysqlclient db)
elseif(LIBHP_WITH_WIN32_INTERROP)
target_link_libraries(test-libhp Win32_Interop lua jemalloc redis-server uv_a)
elseif(WIN32 AND NOT MSVC)
target_link_libraries(test-libhp uv libhp dl)
else()
target_link_libraries(test-libhp uv_a libhp dl hiredis_static)
endif()
