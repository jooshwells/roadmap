add_library(spatial_logic STATIC)

target_include_directories(spatial_logic PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(spatial_logic PRIVATE
    RoadMap::common::vehicle_state
    RoadMap::common::road_state
    RoadMap::network
)

add_subdirectory(src)

add_library(RoadMap::spatial_logic ALIAS spatial_logic)

install(TARGETS spatial_logic ARCHIVE DESTINATION lib)

install(DIRECTORY include DESTINATION include)