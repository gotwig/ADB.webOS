add_library(commons-hostport hostport.c)
target_include_directories(commons-hostport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(commons-hostport PUBLIC commons-sockaddr)

add_executable(tests_hostport hostport_tests.c)
target_link_libraries(tests_hostport PRIVATE commons-hostport)
add_test(tests_hostport tests_hostport)