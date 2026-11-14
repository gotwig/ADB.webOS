add_library(commons-copyfile)
target_include_directories(commons-copyfile PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include(CheckSymbolExists)

check_symbol_exists(sendfile "sys/sendfile.h" HAVE_SENDFILE)

if (HAVE_SENDFILE)
    target_sources(commons-copyfile PRIVATE copyfile_sendfile.c)
else()
    target_sources(commons-copyfile PRIVATE copyfile_std.c)
endif ()