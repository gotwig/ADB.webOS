include(ExternalProject)

set(EXT_MBEDTLS_TOOLCHAIN_ARGS)
if (CMAKE_TOOLCHAIN_FILE)
    list(APPEND EXT_MBEDTLS_TOOLCHAIN_ARGS "-DCMAKE_TOOLCHAIN_FILE:string=${CMAKE_TOOLCHAIN_FILE}")
endif ()
if (CMAKE_TOOLCHAIN_ARGS)
    list(APPEND EXT_MBEDTLS_TOOLCHAIN_ARGS "-DCMAKE_TOOLCHAIN_ARGS:string=${CMAKE_TOOLCHAIN_ARGS}")
endif ()

ExternalProject_Add(ext_mbedtls
        URL https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/v3.4.0.tar.gz
        CMAKE_ARGS ${EXT_MBEDTLS_TOOLCHAIN_ARGS}
        -DCMAKE_BUILD_TYPE:string=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
        -DENABLE_PROGRAMS=OFF
        -DENABLE_TESTING=OFF
        -DDISABLE_PACKAGE_CONFIG_AND_INSTALL=ON
        -DUSE_SHARED_MBEDTLS_LIBRARY=ON
        -DUSE_STATIC_MBEDTLS_LIBRARY=OFF
        )
ExternalProject_Get_Property(ext_mbedtls INSTALL_DIR)


macro(add_mbedtls_library)
    cmake_parse_arguments(_ADD "" "PREFIX;NAME" "INCLUDES" ${ARGN})
    string(TOLOWER "${_ADD_PREFIX}" _ADD_PREFIX_LOWER)
    set(_ADD_TARGET "${_ADD_PREFIX_LOWER}_target")
    if (TARGET ${_ADD_TARGET})
        return()
    endif ()
    add_library(${_ADD_TARGET} SHARED IMPORTED)
    set(LIB_FILENAME "${CMAKE_SHARED_LIBRARY_PREFIX}${_ADD_NAME}${CMAKE_SHARED_LIBRARY_SUFFIX}")
    set_target_properties(${_ADD_TARGET} PROPERTIES IMPORTED_LOCATION "${INSTALL_DIR}/lib/${LIB_FILENAME}")

    ExternalProject_Add_Step(ext_mbedtls lib_${_ADD_PREFIX_LOWER} BYPRODUCTS "${INSTALL_DIR}/lib/${LIB_FILENAME}")

    add_dependencies(${_ADD_TARGET} ext_mbedtls)

    if (_ADD_INCLUDES)
        list(TRANSFORM _ADD_INCLUDES PREPEND "${INSTALL_DIR}/include/")
        set(${_ADD_PREFIX}_INCLUDE_DIRS ${_ADD_INCLUDES})
    else ()
        set(${_ADD_PREFIX}_INCLUDE_DIRS ${INSTALL_DIR}/include)
    endif ()
    set(${_ADD_PREFIX}_INCLUDEDIR ${INSTALL_DIR}/include)
    set(${_ADD_PREFIX}_LIBRARY ${_ADD_TARGET})
    set(${_ADD_PREFIX}_LIBRARIES ${_ADD_TARGET})
endmacro()


add_mbedtls_library(PREFIX MBEDCRYPTO NAME mbedcrypto)
add_mbedtls_library(PREFIX MBEDX509 NAME mbedx509)
add_mbedtls_library(PREFIX MBEDTLS NAME mbedtls)

set(MBEDTLS_FOUND TRUE)

if (NOT DEFINED CMAKE_INSTALL_LIBDIR)
    set(CMAKE_INSTALL_LIBDIR lib)
endif ()

install(DIRECTORY ${INSTALL_DIR}/lib/ DESTINATION ${CMAKE_INSTALL_LIBDIR})
