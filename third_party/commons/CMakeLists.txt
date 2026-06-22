cmake_minimum_required(VERSION 3.16)

# To suppress warnings for ExternalProject DOWNLOAD_EXTRACT_TIMESTAMP
if (POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif ()

project(commons-c C CXX)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

find_package(PkgConfig REQUIRED)

include(CTest)

add_subdirectory(util)
add_subdirectory(platform)
add_subdirectory(os)
add_subdirectory(device)
add_subdirectory(media)
add_subdirectory(lvgl)
add_subdirectory(net)
add_subdirectory(io)