cmake_minimum_required(VERSION 3.16)
project(rx-compiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

find_path(ANTLR4_INCLUDE_DIR NAMES antlr4-runtime.h PATH_SUFFIXES antlr4-runtime)
find_library(ANTLR4_LIBRARY NAMES antlr4-runtime)
if(NOT ANTLR4_INCLUDE_DIR OR NOT ANTLR4_LIBRARY)
    message(FATAL_ERROR "antlr4-runtime not found (need /usr/include/antlr4-runtime and libantlr4-runtime)")
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/generated/RxLexer.cpp")
    message(FATAL_ERROR "ANTLR sources missing; run scripts/gen_parser.sh first")
endif()

file(GLOB_RECURSE RX_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(rxc ${RX_SOURCES})

target_include_directories(rxc PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/generated"
    "${ANTLR4_INCLUDE_DIR}")

target_link_libraries(rxc PRIVATE "${ANTLR4_LIBRARY}")
