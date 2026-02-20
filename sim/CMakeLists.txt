cmake_minimum_required(VERSION 4.2)

project(RoadMapSim)

message("-------------------------------------------")
message("-------------  Road Map Sim  --------------")
message("------------ UCF Senior Design ------------")
message("-------------- Summer 2026 ----------------")
message("-------------------------------------------")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(common)
add_subdirectory(physics)

add_executable(sim main.cpp)

target_link_libraries(sim PRIVATE
    RoadMap::common
    RoadMap::physics
)