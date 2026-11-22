set(ZIO_TESTS
    Physics3DTest
    Physics2DTest
    UITest
    DockTest
    WidgetsTest
    GltfTest
    SpriteAnimationTest
    SkeletonTest
    ScriptTest
    CSharpScriptTest
    PackageTest
    LightTest
    PrefabTest
    AudioTest
)

foreach(testName IN LISTS ZIO_TESTS)
    add_executable(${testName} ${testName}.cpp)
    target_link_libraries(${testName} PRIVATE zio::engine)
    target_compile_options(${testName} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    add_test(NAME ${testName} COMMAND ${testName})
endforeach()

add_executable(WindowSmokeTest WindowSmokeTest.cpp)
target_link_libraries(WindowSmokeTest PRIVATE zio::engine)
target_compile_definitions(WindowSmokeTest PRIVATE ZIO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_options(WindowSmokeTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
add_test(NAME WindowSmokeTest COMMAND WindowSmokeTest)
set_tests_properties(WindowSmokeTest PROPERTIES SKIP_RETURN_CODE 77)

add_executable(AssetFilesTest AssetFilesTest.cpp
    ../tools/zio_editor/src/core/AssetFiles.cpp
    ../tools/zio_editor/src/project/ProjectManager.cpp)
target_include_directories(AssetFilesTest PRIVATE ../tools/zio_editor/src ../src)
target_link_libraries(AssetFilesTest PRIVATE zio::engine)
target_compile_options(AssetFilesTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
add_test(NAME AssetFilesTest COMMAND AssetFilesTest)

add_executable(ProjectInfoTest ProjectInfoTest.cpp
    ../tools/zio_editor/src/project/ProjectManager.cpp)
target_include_directories(ProjectInfoTest PRIVATE ../tools/zio_editor/src ../src)
target_link_libraries(ProjectInfoTest PRIVATE zio::engine)
target_compile_options(ProjectInfoTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
add_test(NAME ProjectInfoTest COMMAND ProjectInfoTest)

add_executable(ReleaseNotesTest ReleaseNotesTest.cpp
    ../tools/zio_editor/src/update/ReleaseNotes.cpp)
target_include_directories(ReleaseNotesTest PRIVATE ../tools/zio_editor/src ../src)
target_link_libraries(ReleaseNotesTest PRIVATE zio::engine)
target_compile_options(ReleaseNotesTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
add_test(NAME ReleaseNotesTest COMMAND ReleaseNotesTest)
