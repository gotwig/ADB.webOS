if (NOT HELPERS_FOUND)
    pkg_check_modules(HELPERS helpers)
endif ()
if (NOT HELPERS_FOUND)
    include(ExternalWebOSUserland)
endif ()

pkg_check_modules(PBNJSON_C REQUIRED pbnjson_c)

add_library(commons-luna-sync lunasynccall.c)
target_include_directories(commons-luna-sync PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
        SYSTEM PRIVATE ${PBNJSON_C_INCLUDE_DIRS} ${HELPERS_INCLUDE_DIRS})
target_link_libraries(commons-luna-sync PUBLIC pthread ${PBNJSON_C_LIBRARIES} ${HELPERS_LIBRARIES})