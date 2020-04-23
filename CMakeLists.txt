project(bigLITTLE_Stimulator)
cmake_minimum_required(VERSION 3.4 FATAL_ERROR)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

add_compile_options("-Wall" "-O0")

include_directories(include)

file(GLOB SOURCES "src/*.c")

add_executable(${CMAKE_PROJECT_NAME} ${SOURCES})

set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES LINK_FLAGS -Wl,--copy-dt-needed-entries)

target_link_libraries(${CMAKE_PROJECT_NAME}
    rt
    m
    )

