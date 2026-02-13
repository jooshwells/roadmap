cmake_minimum_required(VERSION 4.2)

project(ExampleProject)

add_executable(Hello main.cpp)

target_link_libraries(Hello PRIVATE
    example_library
)

add_subdirectory(example_library)