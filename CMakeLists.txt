cmake_minimum_required(VERSION 3.25)
project(tellmeutf VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(TELLMEUTF_WERROR "Treat compiler warnings as errors" OFF)
option(TELLMEUTF_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(TELLMEUTF_SEMANTIC "Build the optional semantic search (needs scripts/fetch-semantic.sh)" OFF)
option(TELLMEUTF_BUILD_TESTS "Build the tests" ON)

# Warning and sanitizer flags shared by every target.
add_library(tellmeutf_options INTERFACE)
target_compile_options(tellmeutf_options INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
  -Wold-style-cast -Wnon-virtual-dtor -Woverloaded-virtual -Wcast-align
  -Wnull-dereference -Wdouble-promotion -Wformat=2)
if(TELLMEUTF_WERROR)
  target_compile_options(tellmeutf_options INTERFACE -Werror)
endif()
if(TELLMEUTF_SANITIZE)
  target_compile_options(tellmeutf_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(tellmeutf_options INTERFACE -fsanitize=address,undefined)
endif()

add_subdirectory(tools)
add_subdirectory(src)

if(TELLMEUTF_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
