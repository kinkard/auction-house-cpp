cmake_minimum_required(VERSION 3.25.0 FATAL_ERROR)
project(auction-house LANGUAGES CXX C)

# Compiler configuration
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Dependencies
include(deps/asio.cmake)
add_subdirectory(deps/sqlite3)
add_subdirectory(deps/fmt)

# Server
add_executable(server src/server/main.cpp src/server/storage.cpp)
target_link_libraries(server asio sqlite3 fmt::fmt)

# Client
add_executable(client src/client/main.cpp)
target_link_libraries(client asio)

add_subdirectory(tests)
