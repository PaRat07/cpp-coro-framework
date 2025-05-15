cmake_minimum_required(VERSION 3.31)
project(coros-test)

set(CMAKE_CXX_STANDARD 26)

add_executable(coros-test main.cpp)
add_executable(coros-bench bench.cpp)

target_link_libraries(coros-test coros)
target_link_libraries(coros-bench coros)

