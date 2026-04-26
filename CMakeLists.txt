cmake_minimum_required(VERSION 3.16)
project(Gaming C)

set(CMAKE_C_STANDARD 11)

find_package(SDL3 REQUIRED)
find_package(SDL3_shadercross REQUIRED)
find_package(SDL3_image REQUIRED)

add_library(Game SHARED src/Game_main.c)
target_include_directories(Game PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(Game PROPERTIES
        PREFIX ""
        OUTPUT_NAME "game"
)

add_executable(Platform src/SDL_main.c)
target_link_libraries(Platform SDL3::SDL3 SDL3_shadercross::SDL3_shadercross SDL3_image::SDL3_image)
target_include_directories(Platform PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# building Platform automatically builds Game first
add_dependencies(Platform Game)

add_custom_command(TARGET Platform POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Game>
        $<TARGET_FILE_DIR:Platform>/$<TARGET_FILE_NAME:Game>

        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:Platform>/shaders
        COMMAND glslc ${CMAKE_CURRENT_SOURCE_DIR}/shaders/vertex.vert -o $<TARGET_FILE_DIR:Platform>/shaders/vertex.spv
        COMMAND glslc ${CMAKE_CURRENT_SOURCE_DIR}/shaders/fragment.frag -o $<TARGET_FILE_DIR:Platform>/shaders/fragment.spv
)