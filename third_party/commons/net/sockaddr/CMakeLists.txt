add_library(commons-sockaddr sockaddr.c)
target_include_directories(commons-sockaddr PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(tests_sockaddr sockaddr_tests.c)
target_link_libraries(tests_sockaddr PRIVATE commons-sockaddr)
add_test(tests_sockaddr tests_sockaddr)