cmake_minimum_required(VERSION 4.2)

project(RoadMapSim)

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
add_subdirectory(physics)
add_subdirectory(road_network)
add_subdirectory(driver_logic)

add_executable(sim main.cpp)

target_link_libraries(sim PRIVATE
    RoadMap::common::road_state
    RoadMap::common::vehicle_state
    RoadMap::common::heuristics
    RoadMap::driver_logic::pathfinding
    RoadMap::physics
    RoadMap::network
)

# Install the simulation executable
install(TARGETS sim DESTINATION bin)

# Install the GeographicLib DLLs 
# (Assuming GeographicLib is a target in your project)
# install(TARGETS GeographicLib DESTINATION bin)

# Optional: Install headers if you need them for other sub-projects
# install(DIRECTORY ${GeographicLib_SOURCE_DIR}/include/ DESTINATION include)