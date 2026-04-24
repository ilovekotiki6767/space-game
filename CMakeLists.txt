cmake_minimum_required(VERSION 4.2)
project(Gaming C)

find_package(SDL3)

set(CMAKE_C_STANDARD 11)

add_executable(Gaming
        main.c)

target_link_libraries(Gaming SDL3::SDL3)