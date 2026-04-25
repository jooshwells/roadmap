add_library(road_state STATIC)

target_include_directories(road_state PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(road_state PUBLIC
    # GeographicLib
)

add_subdirectory(src)

add_library(RoadMap::common::road_state ALIAS road_state)