cmake_minimum_required(VERSION 3.17)
project(tevents)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-pthread")

add_executable(tevents main.cpp te.cpp te.h catch.hpp cmake-build-debug/mock_formatter.h)