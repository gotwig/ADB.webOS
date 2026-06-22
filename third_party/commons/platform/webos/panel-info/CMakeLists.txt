pkg_check_modules(SDL2 REQUIRED sdl2)

add_library(commons-webos-panel-info STATIC panel_info.c)
target_include_directories(commons-webos-panel-info PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(commons-webos-panel-info PUBLIC ${SDL2_LIBRARIES})