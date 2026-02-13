cmake_minimum_required(VERSION 4.2)

project(RoadMapSim)

message("-------------------------------------------")
message("-------------  Road Map Sim  --------------")
message("------------ UCF Senior Design ------------")
message("-------------- Summer 2026 ----------------")
message("-------------------------------------------")

add_subdirectory(common)

add_executable(sim main.cpp)

target_link_libraries(sim PRIVATE
    RoadMap::common
)