find_package(CURL)

if (NOT CURL_FOUND)
    return()
endif ()

pkg_check_modules(SDL2 REQUIRED sdl2)

add_library(commons-gamecontrollerdb-updater gamecontrollerdb_updater.c)
target_link_libraries(commons-gamecontrollerdb-updater PUBLIC ${CURL_LIBRARIES} ${SDL2_LIBRARIES} commons-executor
        commons-logging)
target_include_directories(commons-gamecontrollerdb-updater SYSTEM PUBLIC ${CURL_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS}
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (NOT CMAKE_TESTING_ENABLED)
    return()
endif ()

add_executable(test_gcdb_update test_gcdb_update.c)
target_link_libraries(test_gcdb_update PRIVATE commons-gamecontrollerdb-updater commons-logging)
add_test(test_gcdb_update test_gcdb_update)