add_library(lv_gridview STATIC lv_gridview.c)
target_include_directories(lv_gridview PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lv_gridview PUBLIC lvgl)

if (NOT CMAKE_TESTING_ENABLED)
    return()
endif ()

add_executable(lv_gridview_test test_gridview.c)
target_link_libraries(lv_gridview_test PRIVATE lv_gridview)
add_test(test_gridview lv_gridview_test)