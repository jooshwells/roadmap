add_library(network STATIC)

target_include_directories(network PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(network PRIVATE
    RoadMap::common::road_state
    json
)

add_subdirectory(src)

add_library(RoadMap::network ALIAS network)