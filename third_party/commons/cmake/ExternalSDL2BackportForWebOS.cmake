# To suppress warnings for ExternalProject DOWNLOAD_EXTRACT_TIMESTAMP
if (POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif ()

include(ExternalProject)

set(EXT_SDL2_BACKPORT_TOOLCHAIN_ARGS)
if (CMAKE_TOOLCHAIN_FILE)
    list(APPEND EXT_SDL2_BACKPORT_TOOLCHAIN_ARGS "-DCMAKE_TOOLCHAIN_FILE:string=${CMAKE_TOOLCHAIN_FILE}")
endif ()
if (CMAKE_TOOLCHAIN_ARGS)
    list(APPEND EXT_SDL2_BACKPORT_TOOLCHAIN_ARGS "-DCMAKE_TOOLCHAIN_ARGS:string=${CMAKE_TOOLCHAIN_ARGS}")
endif ()

set(LIB_FILENAME "libSDL2-2.0.so.0")

if (DEFINED SDL2_BACKPORT_RELEASE)
    string(REGEX REPLACE "^release-([0-9]+\.[0-9]+\.[0-9]+)-webos\\.[0-9]+$" "SDL2-\\1-webos.tar.gz"
            _archive "${SDL2_BACKPORT_RELEASE}")
    ExternalProject_Add(ext_sdl2_backport
            URL "https://github.com/webosbrew/SDL-webOS/releases/download/${SDL2_BACKPORT_RELEASE}/${_archive}"
            CONFIGURE_COMMAND ""
            BUILD_COMMAND ""
            INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_directory <SOURCE_DIR> <INSTALL_DIR>
            BUILD_BYPRODUCTS <INSTALL_DIR>/lib/${LIB_FILENAME}
    )
elseif (DEFINED SDL2_BACKPORT_REVISION)
    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        set(EXT_SDL2_BACKPORT_BUILD_TYPE "Release")
    else ()
        set(EXT_SDL2_BACKPORT_BUILD_TYPE "RelWithDebInfo")
    endif ()

    ExternalProject_Add(ext_sdl2_backport
            GIT_REPOSITORY "https://github.com/webosbrew/SDL-webOS.git"
            GIT_TAG "${SDL2_BACKPORT_REVISION}"
            CMAKE_ARGS ${EXT_SDL2_BACKPORT_TOOLCHAIN_ARGS}
            -DCMAKE_BUILD_TYPE:string=${EXT_SDL2_BACKPORT_BUILD_TYPE}
            -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
            -DWEBOS=ON -DSDL_OFFSCREEN=OFF -DSDL_DISKAUDIO=OFF
            -DSDL_DUMMYAUDIO=OFF -DSDL_DUMMYVIDEO=OFF -DSDL_KMSDRM=OFF
            -DSDL_VENDOR_INFO=webOS\ Backport
            BUILD_BYPRODUCTS <INSTALL_DIR>/lib/${LIB_FILENAME}
    )
else ()
    message(FATAL_ERROR "SDL2_BACKPORT_REVISION is not defined")
endif ()

ExternalProject_Get_Property(ext_sdl2_backport INSTALL_DIR)

add_library(ext_sdl2_backport_target SHARED IMPORTED)
set_target_properties(ext_sdl2_backport_target PROPERTIES IMPORTED_LOCATION ${INSTALL_DIR}/lib/${LIB_FILENAME})
target_compile_definitions(ext_sdl2_backport_target INTERFACE __WEBOS__)

add_dependencies(ext_sdl2_backport_target ext_sdl2_backport)

set(SDL2_INCLUDE_DIRS ${INSTALL_DIR}/include/SDL2)
set(SDL2_LIBRARIES ext_sdl2_backport_target)
set(SDL2_FOUND TRUE)

if (NOT DEFINED CMAKE_INSTALL_LIBDIR)
    set(CMAKE_INSTALL_LIBDIR lib)
endif ()

install(DIRECTORY ${INSTALL_DIR}/lib/ DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PATTERN "include" EXCLUDE PATTERN "bin" EXCLUDE PATTERN "share" EXCLUDE
        PATTERN "pkgconfig" EXCLUDE PATTERN "cmake" EXCLUDE PATTERN "*.a" EXCLUDE)
