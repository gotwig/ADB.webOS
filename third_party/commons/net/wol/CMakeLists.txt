add_library(commons-wol wol.c)
target_include_directories(commons-wol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(commons-wol PUBLIC commons-logging)
