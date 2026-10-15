cmake_minimum_required(VERSION 3.16)
project(geodb CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PQXX REQUIRED IMPORTED_TARGET libpqxx)

add_library(geodb geodb.cpp)
target_include_directories(geodb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(geodb PUBLIC PkgConfig::PQXX)

add_executable(geodb_demo main.cpp)
target_link_libraries(geodb_demo PRIVATE geodb)

add_executable(track_sim track_sim.cpp)
target_link_libraries(track_sim PRIVATE geodb)