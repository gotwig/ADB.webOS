# To suppress warnings for ExternalProject DOWNLOAD_EXTRACT_TIMESTAMP
if (POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif ()

if (NOT TARGET ext_inih)
    include(ExternalProject)

    set(EXT_INIH_CMAKELISTS_FILE "${CMAKE_CURRENT_BINARY_DIR}/ext_inih_cmake.txt")
    set(EXT_INIH_LIBRARY_TYPE SHARED)
    if (INIH_STATIC)
        set(EXT_INIH_LIBRARY_TYPE STATIC)
    endif ()
    file(WRITE "${EXT_INIH_CMAKELISTS_FILE}" "
cmake_minimum_required(VERSION 3.0)
project(inih LANGUAGES C)
add_library(inih ${EXT_INIH_LIBRARY_TYPE} ini.c)
set_target_properties(inih PROPERTIES SOVERSION 1)
install(FILES \"ini.h\" TYPE INCLUDE)
install(TARGETS inih LIBRARY DESTINATION \${CMAKE_INSTALL_LIBDIR} NAMELINK_SKIP)
")

    set(EXT_INIH_TOOLCHAIN_ARGS)
    if (CMAKE_TOOLCHAIN_FILE)
        list(APPEND EXT_INIH_TOOLCHAIN_ARGS "-DCMAKE_TOOLCHAIN_FILE:string=${CMAKE_TOOLCHAIN_FILE}")
    endif ()
    if (CMAKE_TOOLCHAIN_ARGS)
        list(APPEND EXT_INIH_TOOLCHAIN_ARGS "-DCMAKE_TOOLCHAIN_ARGS:string=${CMAKE_TOOLCHAIN_ARGS}")
    endif ()

    if (EXT_INIH_LIBRARY_TYPE STREQUAL "STATIC")
        set(LIBRARY_NAME "${CMAKE_STATIC_LIBRARY_PREFIX}inih${CMAKE_STATIC_LIBRARY_SUFFIX}")
    elseif (CMAKE_SHARED_LIBRARY_SUFFIX STREQUAL ".so")
        set(LIBRARY_NAME "${CMAKE_SHARED_LIBRARY_PREFIX}inih.so.1")
    else ()
        set(LIBRARY_NAME "${CMAKE_SHARED_LIBRARY_PREFIX}inih${CMAKE_SHARED_LIBRARY_SUFFIX}")
    endif ()

    ExternalProject_Add(ext_inih
            URL https://github.com/benhoyt/inih/archive/refs/tags/r58.tar.gz
            PATCH_COMMAND cmake -E copy "${EXT_INIH_CMAKELISTS_FILE}" CMakeLists.txt
            CMAKE_ARGS ${EXT_INIH_TOOLCHAIN_ARGS}
            -DCMAKE_BUILD_TYPE:string=${CMAKE_BUILD_TYPE}
            -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
            -DCMAKE_POLICY_VERSION_MINIMUM=3.5
            BUILD_BYPRODUCTS <INSTALL_DIR>/lib/${LIBRARY_NAME}
    )
    ExternalProject_Get_Property(ext_inih INSTALL_DIR)

    add_library(ext_inih_target ${EXT_INIH_LIBRARY_TYPE} IMPORTED)
    add_dependencies(ext_inih_target ext_inih)

    message(${INSTALL_DIR}/lib/${LIBRARY_NAME})
    set_target_properties(ext_inih_target PROPERTIES IMPORTED_LOCATION ${INSTALL_DIR}/lib/${LIBRARY_NAME})

    if (EXT_INIH_LIBRARY_TYPE STREQUAL "SHARED")
        if (NOT DEFINED CMAKE_INSTALL_LIBDIR)
            set(CMAKE_INSTALL_LIBDIR lib)
        endif ()

        install(DIRECTORY ${INSTALL_DIR}/lib/ DESTINATION ${CMAKE_INSTALL_LIBDIR})
    endif ()

endif ()

set(INIH_INCLUDE_DIRS ${INSTALL_DIR}/include)
set(INIH_INCLUDEDIR ${INSTALL_DIR}/include)
set(INIH_LIBRARIES ext_inih_target)
set(INIH_FOUND TRUE)
