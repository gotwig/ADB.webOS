add_library(commons-sps-parser sps_parser.c)
target_include_directories(commons-sps-parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (NOT CMAKE_TESTING_ENABLED)
    return()
endif ()

add_executable(tests_sps_parser sps_parser_tests.c)
target_link_libraries(tests_sps_parser PRIVATE commons-sps-parser)
add_test(tests_sps_parser tests_sps_parser)