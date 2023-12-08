cmake_minimum_required(VERSION 3.1...3.28)

project(WorkPoolExample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

add_executable(WorkPoolExample WorkPoolExample.cpp)

target_include_directories(WorkPoolExample PUBLIC "${CMAKE_SOURCE_DIR}/../include")
