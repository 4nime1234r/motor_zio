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

set(IMGUI_DIR "${ZIO_ROOT}/third_party/imgui")

set(IMGUI_RENDERER_BACKEND ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp)

add_executable(zio_editor
    src/main.cpp
    src/EditorWindow.cpp
    src/EditorApp.cpp
    src/core/EditorContext.cpp
    src/core/Gizmo.cpp
    src/core/RecentProject.cpp
    src/core/Picking.cpp
    src/core/SelectionGizmo.cpp
    src/core/ViewportGrid.cpp
    src/project/ProjectManager.cpp
    src/panels/AddObjectMenu.cpp
    src/panels/ContentBrowserModel.cpp
    src/panels/ContentBrowserPanel.cpp
    src/panels/SceneHierarchyPanel.cpp
    src/panels/InspectorPanel.cpp
    src/panels/DiagnosticsPanel.cpp

    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_RENDERER_BACKEND}
)

target_include_directories(zio_editor PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    src
    ${ZIO_ROOT}/src
)

target_link_libraries(zio_editor PRIVATE zio::engine psapi)
target_compile_options(zio_editor PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
zio_enable_optimizations(zio_editor)
