cmake_minimum_required(VERSION 3.10.0)
project(slug VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(GRAPHICS "GRAPHICS_API_OPENGL_43")
add_subdirectory(vendors/raylib)

add_executable(slug main.cpp)

target_link_libraries(slug raylib)
target_include_directories(slug PRIVATE vendors/stb)
