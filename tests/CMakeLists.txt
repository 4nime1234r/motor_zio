set(ZIO_TESTS
    Physics3DTest
    UITest
    DockTest
    WidgetsTest
    GltfTest
    SpriteAnimationTest
    SkeletonTest
)

foreach(testName IN LISTS ZIO_TESTS)
    add_executable(${testName} ${testName}.cpp)
    target_link_libraries(${testName} PRIVATE zio::engine)
    target_compile_options(${testName} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    add_test(NAME ${testName} COMMAND ${testName})
endforeach()

add_executable(AssetFilesTest AssetFilesTest.cpp
    ../tools/zio_editor/src/core/AssetFiles.cpp
    ../tools/zio_editor/src/project/ProjectManager.cpp)
target_include_directories(AssetFilesTest PRIVATE ../tools/zio_editor/src ../src)
target_link_libraries(AssetFilesTest PRIVATE zio::engine)
target_compile_options(AssetFilesTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
add_test(NAME AssetFilesTest COMMAND AssetFilesTest)
