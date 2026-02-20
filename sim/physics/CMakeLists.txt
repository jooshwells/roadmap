add_library(physics STATIC)

target_include_directories(physics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(physics PRIVATE
    RoadMap::common
)

add_subdirectory(src)

add_library(RoadMap::physics ALIAS physics)