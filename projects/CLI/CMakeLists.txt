cmake_minimum_required(VERSION 3.20)
project(CLI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(zio CONFIG REQUIRED PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk" NO_DEFAULT_PATH)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../sdk/lib/cmake/zio/zio_optimizations.cmake")

add_executable(CLI main.cpp)
target_link_libraries(CLI PRIVATE zio::engine)
zio_enable_optimizations(CLI)
