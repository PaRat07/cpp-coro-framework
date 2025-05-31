cmake_minimum_required(VERSION 3.31)
project(cpp-coro-framework)

set(CMAKE_CXX_STANDARD 26)

file(
        DOWNLOAD
        https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.38.3/CPM.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
        EXPECTED_HASH SHA256=cc155ce02e7945e7b8967ddfaff0b050e958a723ef7aad3766d368940cb15494
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

CPMAddPackage("gh:fmtlib/fmt#11.2.0")
CPMAddPackage("gh:getml/reflect-cpp#v0.19.0")

add_library(coros src/noop.cpp)

target_include_directories(coros PUBLIC include)

target_link_libraries(coros PUBLIC fmt::fmt reflectcpp)
target_link_options(coros PUBLIC -luring -lpq)

add_subdirectory(examples)