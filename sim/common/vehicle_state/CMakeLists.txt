add_library(vehicle_state STATIC)

target_include_directories(vehicle_state PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

add_subdirectory(src)

add_library(RoadMap::common::vehicle_state ALIAS vehicle_state)