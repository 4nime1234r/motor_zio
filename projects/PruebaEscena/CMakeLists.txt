cmake_minimum_required(VERSION 3.20)
project(PruebaEscena LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. engine)

add_executable(PruebaEscena main.cpp)
zio_set_gui_app(PruebaEscena)
target_link_libraries(PruebaEscena PRIVATE zio::engine)
zio_enable_optimizations(PruebaEscena)
