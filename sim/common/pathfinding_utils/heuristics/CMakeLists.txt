add_library(heuristics STATIC)

target_include_directories(heuristics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(heuristics PUBLIC
    RoadMap::common::road_state
)

add_subdirectory(src)

add_library(RoadMap::common::heuristics ALIAS heuristics)

install(TARGETS heuristics ARCHIVE DESTINATION lib)

install(DIRECTORY include DESTINATION include)