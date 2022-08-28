cmake_minimum_required(VERSION 3.15)
project(redis-miniapps CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_definitions("-std=c++17")

include(${CMAKE_BINARY_DIR}/conan_paths.cmake)
find_package(hiredis)
find_package(Boost)

include_directories(${CMAKE_HOME_DIRECTORY}/include)

add_executable(pubsub-server src/pubsub-server/main.cpp)
target_link_libraries(pubsub-server hiredis::hiredis Boost::boost Boost::json)

add_executable(pubsub-client src/pubsub-client/main.cpp)
target_link_libraries(pubsub-client Boost::boost)
