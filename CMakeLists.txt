cmake_minimum_required(VERSION 4.2)
project(Gaming C)

find_package(SDL3 REQUIRED)
find_package(SDL3_shadercross REQUIRED)

set(CMAKE_C_STANDARD 11)

add_executable(Gaming src/SDL_main.c)

target_link_libraries(Gaming SDL3::SDL3 SDL3_shadercross::SDL3_shadercross)

add_custom_command(TARGET Gaming POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:Gaming>/shaders
        COMMAND glslc ${CMAKE_CURRENT_SOURCE_DIR}/shaders/vertex.vert -o $<TARGET_FILE_DIR:Gaming>/shaders/vertex.spv
        COMMAND glslc ${CMAKE_CURRENT_SOURCE_DIR}/shaders/fragment.frag -o $<TARGET_FILE_DIR:Gaming>/shaders/fragment.spv
)