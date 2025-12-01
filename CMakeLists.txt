cmake_minimum_required(VERSION 3.22)
project(rs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(rs
  src/main.cc
)
