add_executable(zio_player main.cpp PlayerApp.cpp)
target_include_directories(zio_player PRIVATE .)
target_link_libraries(zio_player PRIVATE zio::engine)
