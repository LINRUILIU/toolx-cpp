cmake_minimum_required(VERSION 3.20)

project(toolx_install_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ToolX CONFIG REQUIRED)

add_executable(toolx_install_consumer main.cpp)
target_link_libraries(toolx_install_consumer PRIVATE toolx::logsys toolx::cfgx)
