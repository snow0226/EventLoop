cmake_minimum_required(Version 3.10)
project(event_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/inc)

add_subdirectory(src)