add_library(pathfinding STATIC)

target_include_directories(pathfinding PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

add_subdirectory(src)

target_link_libraries(pathfinding PUBLIC
    RoadMap::common::heuristics
    RoadMap::network
)

add_library(RoadMap::driver_logic::pathfinding ALIAS pathfinding)