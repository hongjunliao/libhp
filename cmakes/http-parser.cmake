project(http-parser)
set(SOURCE_FILES http_parser.c)
add_library(http_parser STATIC ${SOURCE_FILES})
