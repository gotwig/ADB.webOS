pkg_check_modules(SDL2 REQUIRED sdl2)

add_library(commons-executor executor.c)
target_include_directories(commons-executor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} SYSTEM PUBLIC ${SDL2_INCLUDE_DIRS})
target_link_libraries(commons-executor PUBLIC ${SDL2_LIBRARIES} commons-logging commons-linked-list)

if (CMAKE_TESTING_ENABLED)
    add_subdirectory(tests)
    return()
endif ()