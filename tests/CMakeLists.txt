set(ZIO_TESTS
    Physics3DTest
    UITest
    DockTest
    WidgetsTest
    GltfTest
    SpriteAnimationTest
)

foreach(testName IN LISTS ZIO_TESTS)
    add_executable(${testName} ${testName}.cpp)
    target_link_libraries(${testName} PRIVATE zio::engine)
    target_compile_options(${testName} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    add_test(NAME ${testName} COMMAND ${testName})
endforeach()
