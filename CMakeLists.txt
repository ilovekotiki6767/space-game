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

set(SHADER_SOURCES
        shaders/basic.vert
        shaders/basic.frag
        shaders/text.vert
        shaders/text.frag
)

set(SHADER_OUTPUTS "")
foreach(SHADER ${SHADER_SOURCES})
    set(INPUT  ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER})
    set(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${SHADER}.spv)
    cmake_path(GET OUTPUT PARENT_PATH OUTPUT_DIR)
    add_custom_command(
            OUTPUT  ${OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_DIR}
            COMMAND glslc ${INPUT} -o ${OUTPUT}
            DEPENDS ${INPUT}
    )
    list(APPEND SHADER_OUTPUTS ${OUTPUT})
endforeach()

add_custom_target(Shaders ALL DEPENDS ${SHADER_OUTPUTS})
add_dependencies(Platform Shaders)

add_custom_command(TARGET Platform POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Game>
        $<TARGET_FILE_DIR:Platform>/$<TARGET_FILE_NAME:Game>
        COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:Platform>/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SHADER_OUTPUTS}
        $<TARGET_FILE_DIR:Platform>/shaders
)