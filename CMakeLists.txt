cmake_minimum_required(VERSION 3.3)

project(nsgod)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(wsrpc EXCLUDE_FROM_ALL)

add_executable(nsgod src/nsgod.cpp)
target_link_libraries(nsgod rpcws)