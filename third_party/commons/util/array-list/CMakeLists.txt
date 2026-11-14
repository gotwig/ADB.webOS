add_library(commons-array-list array_list.c)
target_include_directories(commons-array-list PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(tests_array_list array_list_tests.c)
target_link_libraries(tests_array_list PRIVATE commons-array-list)
add_test(tests_array_list tests_array_list)
