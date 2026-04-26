cmake_minimum_required(VERSION 3.16)
project(Gaming C)

set(CMAKE_C_STANDARD 11)

find_package(SDL3 REQUIRED)
find_package(SDL3_shadercross REQUIRED)
find_package(SDL3_image REQUIRED)
find_package(SDL3_ttf REQUIRED)

# this must have NO dependencies INCLUDING libc.
add_library(Game SHARED src/Game_main.c)
target_include_directories(Game PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(Game PROPERTIES
        PREFIX ""
        OUTPUT_NAME "game"
)
target_link_options(Game PRIVATE "-nostdlib")

add_executable(Platform src/SDL_main.c)
target_link_libraries(Platform SDL3::SDL3 SDL3_shadercross::SDL3_shadercross SDL3_image::SDL3_image SDL3_ttf::SDL3_ttf)
target_include_directories(Platform PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# building Platform automatically builds Game first
add_dependencies(Platform Game)

add_custom_command(TARGET Platform POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Game>
        $<TARGET_FILE_DIR:Platform>/$<TARGET_FILE_NAME:Game>

        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:Platform>/shaders
        COMMAND glslc ${CMAKE_CURRENT_SOURCE_DIR}/shaders/basic.vert -o $<TARGET_FILE_DIR:Platform>/shaders/basic.vert.spv
        COMMAND glslc ${CMAKE_CURRENT_SOURCE_DIR}/shaders/basic.frag -o $<TARGET_FILE_DIR:Platform>/shaders/basic.frag.spv
        COMMAND glslc ${CMAKE_CURRENT_SOURCE_DIR}/shaders/text.vert -o $<TARGET_FILE_DIR:Platform>/shaders/text.vert.spv
        COMMAND glslc ${CMAKE_CURRENT_SOURCE_DIR}/shaders/text.frag -o $<TARGET_FILE_DIR:Platform>/shaders/text.frag.spv
)