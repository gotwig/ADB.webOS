add_subdirectory(sys-placeholder)

if (NOT TARGET commons-curl-sys-placeholder)
    message(WARNING "Disable commons-curl-abi-fix as no valid libcurl found")
    return()
endif()

add_library(commons-curl-abi-fix SHARED lib.c)

get_target_property(_ABI_FIX_SOVERSION commons-curl-sys-placeholder ABI_FIX_SOVERSION)

target_link_libraries(commons-curl-abi-fix PUBLIC commons-curl-sys-placeholder)
set_target_properties(commons-curl-abi-fix PROPERTIES LIBRARY_OUTPUT_NAME curl SOVERSION ${_ABI_FIX_SOVERSION})
