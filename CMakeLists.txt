cmake_minimum_required(VERSION 3.31)
project(untitled)

set(CMAKE_CXX_STANDARD 26)

file(
        DOWNLOAD
        https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.38.3/CPM.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
        EXPECTED_HASH SHA256=cc155ce02e7945e7b8967ddfaff0b050e958a723ef7aad3766d368940cb15494
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)


CPMAddPackage("gh:boostorg/winapi#boost-1.88.0")
CPMAddPackage("gh:boostorg/predef#boost-1.88.0")
CPMAddPackage("gh:boostorg/throw_exception#boost-1.88.0")
CPMAddPackage("gh:boostorg/static_assert#boost-1.88.0")
CPMAddPackage("gh:boostorg/assert#boost-1.88.0")
CPMAddPackage("gh:boostorg/core#boost-1.88.0")
CPMAddPackage("gh:boostorg/mp11#boost-1.88.0")
CPMAddPackage("gh:boostorg/describe#boost-1.88.0")
CPMAddPackage("gh:boostorg/container_hash#boost-1.88.0")
CPMAddPackage("gh:boostorg/config#boost-1.88.0")
CPMAddPackage("gh:boostorg/stacktrace#boost-1.88.0")


add_executable(untitled main.cpp)

target_link_libraries(untitled Boost::stacktrace)