
add_library(tm_logger INTERFACE)

target_include_directories(tm_logger INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(tm_logger INTERFACE
    RoadMap::common::vehicle_state
)

# 4. Create an alias (Best Practice)
# This allows you to link it using the namespace format, e.g., target_link_libraries(my_exe RoadMap::tm_logger)
add_library(RoadMap::tm_logger ALIAS tm_logger)