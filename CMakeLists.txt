cmake_minimum_required(VERSION 3.15)
project(redis-miniapps CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_definitions("-std=c++17")

include(${CMAKE_BINARY_DIR}/conan_paths.cmake)
find_package(hiredis)
find_package(Boost)

add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} hiredis::hiredis)

add_executable(redis-pubsub src/pubsub/main.cpp)
target_link_libraries(redis-pubsub hiredis::hiredis Boost::boost)