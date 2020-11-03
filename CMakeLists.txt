cmake_minimum_required(VERSION 3.5)

project(vs_to_cc CXX)

set(CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(vs_to_cc main.cpp)
set_property(TARGET vs_to_cc PROPERTY CXX_STANDARD 17)
