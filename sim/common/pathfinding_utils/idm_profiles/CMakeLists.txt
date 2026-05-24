
add_library(idm_profiles INTERFACE)

target_include_directories(idm_profiles INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(idm_profiles INTERFACE
    RoadMap::common::vehicle_state
)

add_library(RoadMap::common::idm_profiles ALIAS idm_profiles)

install(DIRECTORY include DESTINATION include)