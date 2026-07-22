cmake_minimum_required(VERSION 4.2)

project(RoadMapSim)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/CentralLibs")

message("-------------------------------------------")
message("-------------  Road Map Sim  --------------")
message("------------ UCF Senior Design ------------")
message("-------------- Summer 2026 ----------------")
message("-------------------------------------------")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Disable building the tools/benchmarks to save time
# set(GEOGRAPHICLIB_LIB_TYPE STATIC CACHE STRING "")
# set(BUILD_NETGEOGRAPHICLIB OFF CACHE BOOL "")
# add_subdirectory(external/geographiclib-2.7)
add_subdirectory(external/json)

add_subdirectory(common)
add_subdirectory(diagnostics)
add_subdirectory(spatial_logic)
add_subdirectory(physics)
add_subdirectory(road_network)
add_subdirectory(driver_logic)

# Unit tests (sim_tests target). On by default so a normal build keeps them
# compiling; turn off with -DROADMAP_BUILD_TESTS=OFF for a lean sim-only build.
option(ROADMAP_BUILD_TESTS "Build the simulation unit tests" ON)
if(ROADMAP_BUILD_TESTS)
    add_subdirectory(tests)
endif()

add_executable(sim main.cpp)

target_link_libraries(sim PRIVATE
    RoadMap::common::road_state
    RoadMap::common::vehicle_state
    RoadMap::common::heuristics
    RoadMap::driver_logic::pathfinding
    RoadMap::physics
    RoadMap::network
    RoadMap::tm_logger
    RoadMap::spatial_logic
)

# Install the simulation executable
install(TARGETS sim DESTINATION bin)

install(FILES $<TARGET_PDB_FILE:sim> DESTINATION bin OPTIONAL)

# Install the GeographicLib DLLs 
# (Assuming GeographicLib is a target in your project)
# install(TARGETS GeographicLib DESTINATION bin)

# Optional: Install headers if you need them for other sub-projects
# install(DIRECTORY ${GeographicLib_SOURCE_DIR}/include/ DESTINATION include)