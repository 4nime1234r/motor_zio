cmake_minimum_required(VERSION 3.20)
project(PruebaUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. engine)

add_executable(PruebaUI main.cpp)
zio_set_gui_app(PruebaUI)
target_link_libraries(PruebaUI PRIVATE zio::engine)
target_compile_options(PruebaUI PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
zio_enable_optimizations(PruebaUI)
