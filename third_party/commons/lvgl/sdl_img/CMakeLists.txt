option(COMMONS_LV_SDL_IMG_USE_IMAGE "Use SDL2_image API" ON)

pkg_check_modules(SDL2 REQUIRED sdl2)

add_library(lv_sdl_img STATIC lv_sdl_img.c)

target_include_directories(lv_sdl_img PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} PUBLIC SYSTEM ${SDL2_INCLUDE_DIRS})
target_link_libraries(lv_sdl_img PUBLIC lvgl ${SDL2_LIBRARIES})

get_target_property(LVGL_SOURCE_DIR lvgl SOURCE_DIR)
target_include_directories(lv_sdl_img PUBLIC "${LVGL_SOURCE_DIR}/src")

if (COMMONS_LV_SDL_IMG_USE_IMAGE)
    pkg_check_modules(SDL2_IMG REQUIRED SDL2_image)
    target_include_directories(lv_sdl_img PUBLIC SYSTEM ${SDL2_IMG_INCLUDE_DIRS})
    target_link_libraries(lv_sdl_img PUBLIC ${SDL2_IMG_LIBRARIES})
    target_compile_definitions(lv_sdl_img PRIVATE COMMONS_LV_SDL_IMG_USE_IMAGE=1)
endif()