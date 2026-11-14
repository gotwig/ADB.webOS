add_library(commons-logging logging_common.c)

pkg_check_modules(PMLOG PmLogLib QUIET)
pkg_check_modules(SDL2 sdl2)

include(CheckSymbolExists)

check_symbol_exists(syslog "syslog.h" HAVE_SYSLOG)

option(COMMONS_LOGGING_SYSLOG "Use syslog for logging" OFF)

if (PMLOG_FOUND)
    target_include_directories(commons-logging SYSTEM PRIVATE ${PMLOG_INCLUDE_DIRS})
    target_link_libraries(commons-logging PRIVATE ${PMLOG_LIBRARIES})
    target_sources(commons-logging PRIVATE logging_pmlog.c)
elseif (HAVE_SYSLOG AND COMMONS_LOGGING_SYSLOG)
    target_sources(commons-logging PRIVATE logging_syslog.c)
elseif (SDL2_FOUND)
    target_sources(commons-logging PRIVATE logging_stdio.c)
    target_include_directories(commons-logging SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(commons-logging PRIVATE ${SDL2_LIBRARIES})
endif ()

if (COMMONS_LOGGING_SDL AND SDL2_FOUND)
    target_include_directories(commons-logging SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(commons-logging PRIVATE ${SDL2_LIBRARIES})
    target_sources(commons-logging PRIVATE logging_ext_sdl.c)
    target_compile_definitions(commons-logging PUBLIC COMMONS_LOGGING_SDL=1)
endif ()

if (COMMONS_LOGGING_SS4S)
    target_link_libraries(commons-logging PRIVATE ss4s)
    target_sources(commons-logging PRIVATE logging_ext_ss4s.c)
    target_compile_definitions(commons-logging PUBLIC COMMONS_LOGGING_SS4S=1)
endif ()

if (COMMONS_LOGGING_LVGL)
    target_sources(commons-logging PRIVATE logging_ext_lvgl.c)
    target_compile_definitions(commons-logging PUBLIC COMMONS_LOGGING_LVGL=1)
endif ()

target_include_directories(commons-logging PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})