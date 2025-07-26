cmake_minimum_required(VERSION 3.31)
project(swm)

set(CMAKE_CXX_STANDARD 20)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED xcb xcb-keysyms xcb-icccm)
include_directories(${XCB_INCLUDE_DIR})
add_executable(swm main.cpp)
target_link_libraries(swm ${XCB_LIBRARIES})
