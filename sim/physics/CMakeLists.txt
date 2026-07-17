add_library(physics STATIC)

target_include_directories(physics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(physics PRIVATE
    RoadMap::common::vehicle_state
    RoadMap::common::road_state
    RoadMap::network
    RoadMap::spatial_logic
    RoadMap::driver_logic::pathfinding
)

add_subdirectory(src)

add_library(RoadMap::physics ALIAS physics)