cmake_minimum_required(VERSION 3.3)

project(nsgod)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(wsrpc EXCLUDE_FROM_ALL)

add_executable(nsgod src/nsgod.cpp src/process.cpp)
target_link_libraries(nsgod rpcws stdc++fs)
set_property(TARGET nsgod PROPERTY CXX_STANDARD 17)

add_executable(nsctl src/nsctl.cpp)
target_link_libraries(nsctl rpcws)
set_property(TARGET nsctl PROPERTY CXX_STANDARD 17)
