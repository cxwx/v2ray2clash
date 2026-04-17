cmake_minimum_required(VERSION 3.16)
project(v2ray_to_clash LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(v2ray_to_clash
    src/main.cpp
)

if (MSVC)
    target_compile_options(v2ray_to_clash PRIVATE /W4)
else()
    target_compile_options(v2ray_to_clash PRIVATE -Wall -Wextra -Wpedantic)
endif()
