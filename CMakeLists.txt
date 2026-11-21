cmake_minimum_required(VERSION 3.20)
project(motor_zio VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/zio_optimizations.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/zio_gui.cmake)

if(NOT DEFINED ENV{VULKAN_SDK} AND WIN32)
    file(GLOB zioVulkanSdks LIST_DIRECTORIES true "C:/VulkanSDK/*")
    list(SORT zioVulkanSdks COMPARE NATURAL ORDER DESCENDING)
    foreach(zioSdkDir IN LISTS zioVulkanSdks)
        if(EXISTS "${zioSdkDir}/Include/vulkan/vulkan.h")
            set(ENV{VULKAN_SDK} "${zioSdkDir}")
            message(STATUS "zio: VULKAN_SDK is not set; using ${zioSdkDir}")
            break()
        endif()
    endforeach()
endif()
if(WIN32 AND NOT DEFINED ENV{VULKAN_SDK})
    message(FATAL_ERROR "Zio needs the Vulkan SDK: install it "
                        "(winget install KhronosGroup.VulkanSDK) or set VULKAN_SDK.")
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/zio_spirv.cmake)
find_package(Vulkan REQUIRED)
if(NOT Vulkan_GLSLC_EXECUTABLE)
    find_program(Vulkan_GLSLC_EXECUTABLE glslc)
endif()

add_library(engine STATIC
    src/assets/Image.cpp
    src/audio/AudioManager.cpp
    src/audio/Clip.cpp
    src/audio/ClipCache.cpp
    src/core/Application.cpp
    src/events/EventBus.cpp
    src/input/InputManager.cpp
    src/jobs/JobSystem.cpp
    src/physics/Collision.cpp
    src/physics/World.cpp
    src/physics3d/Collision3D.cpp
    src/physics3d/World3D.cpp
    src/platform/Window.cpp
    src/prefs/Prefs.cpp
    src/render/DeviceVulkan.cpp
    src/renderer/BatchRenderer.cpp
    src/renderer/TextureCache.cpp
    src/scene/SceneStack.cpp
    src/scene2d/Label.cpp
    src/scene2d/Node.cpp
    src/scene2d/Scene2D.cpp
    src/scene2d/SceneIO.cpp
    src/scene3d/Gltf.cpp
    src/scripting/Script.cpp
    src/scripting/DotNet.cpp
    src/scene3d/Skeleton.cpp
    src/scene3d/Mesh.cpp
    src/scene3d/Node3D.cpp
    src/scene3d/Renderer3D.cpp
    src/scene3d/Scene3D.cpp
    src/scene3d/SceneIO3D.cpp
    src/scene2d/Sprite.cpp
    src/ui/BatchPainter.cpp
    src/ui/ColorPicker.cpp
    src/ui/Containers.cpp
    src/ui/Controls.cpp
    src/ui/Core.cpp
    src/ui/Dialog.cpp
    src/ui/Icon.cpp
    src/ui/Dock.cpp
    src/ui/EngineHost.cpp
    src/ui/Fields.cpp
    src/ui/Menus.cpp
    src/ui/GdiFont.cpp
    src/ui/IconGrid.cpp
    src/ui/Text.cpp
    src/ui/Tree.cpp
    src/ui/Viewport.cpp
)
add_library(zio::engine ALIAS engine)
set_target_properties(engine PROPERTIES OUTPUT_NAME zio_engine)

target_include_directories(engine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(engine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

find_package(Threads REQUIRED)
target_link_libraries(engine PUBLIC Threads::Threads)

target_link_libraries(engine PUBLIC Vulkan::Vulkan)
if(WIN32)
    target_link_libraries(engine PUBLIC
        user32 gdi32 winmm ole32 gdiplus xinput xaudio2 mfplat mfreadwrite)
endif()

zio_add_spirv_shader(engine
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/quad.vert quad_vert.spv.h zio_quad_vert_spirv)
zio_add_spirv_shader(engine
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/quad.frag quad_frag.spv.h zio_quad_frag_spirv)
zio_add_spirv_shader(engine
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/mesh3d.vert mesh3d_vert.spv.h zio_mesh3d_vert_spirv)
zio_add_spirv_shader(engine
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/mesh3d.frag mesh3d_frag.spv.h zio_mesh3d_frag_spirv)
zio_add_spirv_shader(engine
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/shadow.frag shadow_frag.spv.h zio_shadow_frag_spirv)

find_program(ZIO_DOTNET dotnet)
set(ZIO_BRIDGE_CPP ${CMAKE_CURRENT_BINARY_DIR}/ZioScriptingData.cpp)
if(ZIO_DOTNET AND WIN32)
    set(ZIO_BRIDGE_OUT ${CMAKE_CURRENT_BINARY_DIR}/zioscripting)
    file(GLOB ZIO_BRIDGE_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/dotnet/ZioScripting/*.cs
        ${CMAKE_CURRENT_SOURCE_DIR}/dotnet/ZioScripting/*.csproj)
    add_custom_command(
        OUTPUT ${ZIO_BRIDGE_CPP}
        COMMAND ${ZIO_DOTNET} build ${CMAKE_CURRENT_SOURCE_DIR}/dotnet/ZioScripting/ZioScripting.csproj
                -c Release -o ${ZIO_BRIDGE_OUT} --nologo -v q
        COMMAND ${CMAKE_COMMAND} -DDIR=${ZIO_BRIDGE_OUT} -DOUTPUT=${ZIO_BRIDGE_CPP}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/zio_embed_bridge.cmake
        DEPENDS ${ZIO_BRIDGE_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/zio_embed_bridge.cmake
        COMMENT "Compilando el puente de C# (ZioScripting)"
        VERBATIM)
else()
    message(STATUS "No se encontro dotnet: el motor se compila sin el puente de scripts de C#")
    file(WRITE ${ZIO_BRIDGE_CPP}.in "#include \"scripting/EmbeddedBridge.h\"\n\nnamespace zio::scripting\n{\n\nconst EmbeddedFile* embeddedBridgeFiles(int& count)\n{\n    static const unsigned char empty[] = {0};\n    static const EmbeddedFile files[] = {{\"ZioScripting.dll\", empty, 0}};\n    count = 1;\n    return files;\n}\n\n}\n")
    configure_file(${ZIO_BRIDGE_CPP}.in ${ZIO_BRIDGE_CPP} COPYONLY)
endif()
target_sources(engine PRIVATE ${ZIO_BRIDGE_CPP})
set_source_files_properties(${ZIO_BRIDGE_CPP} PROPERTIES GENERATED TRUE)

zio_enable_optimizations(engine)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(zioEditorDefault ON)
else()
    set(zioEditorDefault OFF)
endif()
option(ZIO_BUILD_EDITOR "Build the Zio editor (tools/zio_editor) so it can be run from the IDE" ${zioEditorDefault})
if(ZIO_BUILD_EDITOR AND WIN32)
    add_subdirectory(tools/zio_editor)
endif()

option(ZIO_BUILD_TESTS "Compila las pruebas automaticas del motor" OFF)
if(ZIO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS engine
    EXPORT zioTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT zioTargets
    FILE zioTargets.cmake
    NAMESPACE zio::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zio
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/zioConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/zioConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zio
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/zioConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/zioConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/zioConfigVersion.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/zio_optimizations.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/zio_gui.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zio
)
