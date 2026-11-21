cmake_minimum_required(VERSION 3.20)
project(zio_editor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(ZIO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
if(NOT TARGET engine)
    add_subdirectory(${ZIO_ROOT} engine)
endif()

enable_language(RC)
set_source_files_properties(zio_editor.rc PROPERTIES OBJECT_DEPENDS "${ZIO_ROOT}/assets/icon/zio.ico")

add_executable(zio_editor
    zio_editor.rc
    src/main.cpp
    src/EditorApp.cpp
    src/core/EditorContext.cpp
    src/core/Gizmo.cpp
    src/core/RecentProject.cpp
    src/core/Picking.cpp
    src/core/SelectionGizmo.cpp
    src/core/ViewportGrid.cpp
    src/project/ProjectManager.cpp
    src/panels/ContentBrowserModel.cpp
    src/ui/Panels.cpp
    src/ui/ToolButton.cpp
    src/ui/ViewportController.cpp
    src/update/Updater.cpp
)

target_include_directories(zio_editor PRIVATE
    src
    ${ZIO_ROOT}/src
)

option(ZIO_EDITOR_UPDATER "Activa la actualizacion automatica del editor" OFF)

file(STRINGS "${ZIO_ROOT}/CMakeLists.txt" zioVersionLine REGEX "^project\\(motor_zio VERSION")
string(REGEX MATCH "VERSION ([0-9]+\\.[0-9]+\\.[0-9]+)" zioVersionMatch "${zioVersionLine}")
set(ZIO_VERSION "${CMAKE_MATCH_1}")
if(NOT ZIO_VERSION)
    message(FATAL_ERROR "No se pudo leer la version de ${ZIO_ROOT}/CMakeLists.txt")
endif()

target_compile_definitions(zio_editor PRIVATE "ZIO_VERSION=\"${ZIO_VERSION}\"")
if(ZIO_EDITOR_UPDATER)
    target_compile_definitions(zio_editor PRIVATE ZIO_EDITOR_UPDATER=1)
endif()

target_link_libraries(zio_editor PRIVATE zio::engine psapi winhttp bcrypt)
target_compile_options(zio_editor PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
zio_enable_optimizations(zio_editor)
