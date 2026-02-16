add_library(physics STATIC)

target_include_directories(physics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

add_subdirectory(src)

add_library(RoadMap::physics ALIAS physics)