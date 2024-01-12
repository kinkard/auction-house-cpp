cmake_minimum_required(VERSION 3.25.0 FATAL_ERROR)
project(auction-house LANGUAGES CXX C)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(server src/main.cpp)
