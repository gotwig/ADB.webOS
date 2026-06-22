pkg_check_modules(PBNJSON_C REQUIRED pbnjson_c)

add_library(commons-webos-is-uhd is_uhd.c)
target_include_directories(commons-webos-is-uhd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
        SYSTEM PRIVATE ${PBNJSON_C_INCLUDE_DIRS})
target_link_libraries(commons-webos-is-uhd PUBLIC commons-luna-sync)