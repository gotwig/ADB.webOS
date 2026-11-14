if (NOT TARGET ss4s-modules-list-ini)
    return()
endif ()

find_package(PkgConfig REQUIRED)

if (NOT INIH_FOUND)
    pkg_check_modules(INIH inih)
    if (NOT INIH_FOUND)
        include(ExternalINIH)
    endif ()
endif ()

add_library(commons-ss4s-modules-list ss4s_modules.c)
target_include_directories(commons-ss4s-modules-list PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
        PRIVATE SYSTEM ${INIH_INCLUDE_DIRS})
target_link_libraries(commons-ss4s-modules-list PUBLIC ss4s ss4s-modules-list-ini commons-os-info commons-array-list
        ${INIH_LIBRARIES})