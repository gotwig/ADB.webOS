find_package(PkgConfig REQUIRED)

pkg_check_modules(_COMMONS_CURL_SYS REQUIRED libcurl)

find_file(_COMMONS_CURL_SO libcurl.so.4 PATHS ${_COMMONS_CURL_SYS_LIBRARY_DIRS})
if (_COMMONS_CURL_SO)
    set(_PLACEHOLDER_SOVERSION 5)
    set(_ABI_FIX_SOVERSION 4)
else ()
    find_file(_COMMONS_CURL_SO libcurl.so.5 PATHS ${_COMMONS_CURL_SYS_LIBRARY_DIRS})
    if (_COMMONS_CURL_SO)
        set(_PLACEHOLDER_SOVERSION 4)
        set(_ABI_FIX_SOVERSION 5)
    else ()
        message(WARNING "No valid libcurl found in library paths")
        return()
    endif ()
endif ()

add_library(commons-curl-sys-placeholder SHARED lib.c)
set_target_properties(commons-curl-sys-placeholder PROPERTIES
        LIBRARY_OUTPUT_NAME curl
        SOVERSION ${_PLACEHOLDER_SOVERSION}
        ABI_FIX_SOVERSION ${_ABI_FIX_SOVERSION})
target_include_directories(commons-curl-sys-placeholder SYSTEM PUBLIC ${_COMMONS_CURL_SYS_INCLUDE_DIRS})
target_link_libraries(commons-curl-sys-placeholder PRIVATE ${_COMMONS_CURL_SYS_LIBRARIES})