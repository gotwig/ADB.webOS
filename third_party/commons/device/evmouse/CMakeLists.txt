find_path(LINUX_INPUT_HEADER_DIR linux/input.h)

if (NOT LINUX_INPUT_HEADER_DIR)
    return()
endif ()

pkg_check_modules(SDL2 REQUIRED sdl2)

add_library(commons-evmouse evmouse.c)
target_include_directories(commons-evmouse PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(commons-evmouse SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(commons-evmouse PUBLIC commons-logging ${SDL2_LIBRARIES})