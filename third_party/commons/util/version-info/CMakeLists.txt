add_library(commons-version-info version_info.c)
target_include_directories(commons-version-info PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (NOT CMAKE_TESTING_ENABLED)
    return()
endif ()

add_executable(version_info_test version_info_test.c)
target_link_libraries(version_info_test PRIVATE commons-version-info)
add_test(version_info_test version_info_test)