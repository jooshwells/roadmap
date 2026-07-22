# Unit tests for the simulation libraries. No external test framework: the
# tests self-register through test_harness.h, so a new test file only needs
# to be added to this source list.

add_executable(sim_tests
    test_main.cpp
    test_idm.cpp
    test_mobil.cpp
    test_driver_profiles.cpp
    test_intersections.cpp
)

target_include_directories(sim_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(sim_tests PRIVATE
    RoadMap::common::road_state
    RoadMap::common::vehicle_state
    RoadMap::common::idm_profiles
    RoadMap::physics
    RoadMap::network
    RoadMap::spatial_logic
)

# Not installed alongside sim: this binary is a development tool, not part of
# what the frontend ships.
