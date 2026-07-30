cmake_minimum_required(VERSION 3.22)

project(xlsOneC
    VERSION 1.1.3
    DESCRIPTION "Pure C desktop edition of xlsOne"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(GNUInstallDirs)

if(MSVC)
    set(
        CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

option(XLSONE_C_BUILD_APP "Build the SDL desktop application" ON)
option(XLSONE_C_BUILD_TESTS "Build the pure C tests" ON)
option(XLSONE_C_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(XLSONE_C_FETCH_SDL2 "Build a pinned static SDL2 from source" OFF)
set(
    XLSONE_LICENSE_PUBLIC_KEY
    ""
    CACHE STRING
    "Ed25519 public key used to verify signed xlsOne licenses (64 hex chars)"
)
option(
    XLSONE_C_INSTALL_DEVELOPMENT
    "Install the pure C static library, headers, and developer documentation"
    OFF
)

find_package(ZLIB REQUIRED)
find_package(EXPAT REQUIRED)

if(NOT XLSONE_LICENSE_PUBLIC_KEY
    AND DEFINED ENV{XLSONE_LICENSE_PUBLIC_KEY})
    set(XLSONE_LICENSE_PUBLIC_KEY "$ENV{XLSONE_LICENSE_PUBLIC_KEY}")
endif()
if(XLSONE_LICENSE_PUBLIC_KEY)
    string(LENGTH "${XLSONE_LICENSE_PUBLIC_KEY}" XLSONE_LICENSE_KEY_LENGTH)
    if(NOT XLSONE_LICENSE_KEY_LENGTH EQUAL 64
        OR XLSONE_LICENSE_PUBLIC_KEY MATCHES "[^0-9A-Fa-f]")
        message(FATAL_ERROR
            "XLSONE_LICENSE_PUBLIC_KEY must contain exactly 64 hexadecimal characters.")
    endif()
endif()

if(XLSONE_C_BUILD_APP)
    if(XLSONE_C_FETCH_SDL2)
        include(FetchContent)
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST OFF CACHE BOOL "" FORCE)
        set(SDL_TESTS OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            sdl2
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG 5d249570393f7a37e037abf22cd6012a4cc56a71
            GIT_SHALLOW FALSE
            PATCH_COMMAND
                "${CMAKE_COMMAND}"
                "-DPATCH_WORKING_DIRECTORY=<SOURCE_DIR>"
                "-DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/patches/sdl2-drop-hover.patch"
                -P
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/apply_git_patch.cmake"
        )
        FetchContent_MakeAvailable(sdl2)
        set(XLSONE_SDL_LICENSE "${sdl2_SOURCE_DIR}/LICENSE.txt")
    else()
        find_package(SDL2 2.0.18 QUIET CONFIG)
        if(NOT TARGET SDL2::SDL2
            AND NOT TARGET SDL2::SDL2-static)
            find_package(PkgConfig QUIET)
            if(PkgConfig_FOUND)
                pkg_check_modules(SDL2 IMPORTED_TARGET "sdl2>=2.0.18")
            endif()
        endif()
        if(SDL2_DIR)
            if(EXISTS "${SDL2_DIR}/copyright")
                set(XLSONE_SDL_LICENSE "${SDL2_DIR}/copyright")
            endif()
            get_filename_component(
                XLSONE_SDL_PREFIX
                "${SDL2_DIR}/../../.."
                REALPATH
            )
            if(NOT XLSONE_SDL_LICENSE
                AND EXISTS "${XLSONE_SDL_PREFIX}/LICENSE.txt")
                set(XLSONE_SDL_LICENSE
                    "${XLSONE_SDL_PREFIX}/LICENSE.txt")
            elseif(NOT XLSONE_SDL_LICENSE AND EXISTS
                "${XLSONE_SDL_PREFIX}/share/sdl2/copyright")
                set(XLSONE_SDL_LICENSE
                    "${XLSONE_SDL_PREFIX}/share/sdl2/copyright")
            endif()
        endif()
    endif()
    if(NOT TARGET SDL2::SDL2
        AND NOT TARGET SDL2::SDL2-static
        AND NOT TARGET PkgConfig::SDL2)
        message(FATAL_ERROR
            "SDL2 2.0.18 or newer was not found. Install a compatible SDL2, use "
            "-DXLSONE_C_FETCH_SDL2=ON, or configure with "
            "-DXLSONE_C_BUILD_APP=OFF.")
    endif()
endif()

add_library(xlsone_core STATIC
    src/memory.c
    src/models.c
    src/zip_archive.c
    src/compound_file.c
    src/excel_parser.c
    src/biff8_parser.c
    src/merger.c
    src/validator.c
    src/exporter.c
)

add_library(xlsone_i18n STATIC
    app/i18n.c
)

target_include_directories(xlsone_i18n
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/app"
)

target_include_directories(xlsone_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(MSVC)
    target_compile_options(xlsone_i18n PRIVATE /W4)
    target_compile_options(xlsone_core PRIVATE /W4)
    if(XLSONE_C_WARNINGS_AS_ERRORS)
        target_compile_options(xlsone_i18n PRIVATE /WX)
        target_compile_options(xlsone_core PRIVATE /WX)
    endif()
else()
    target_compile_options(xlsone_i18n PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )
    target_compile_options(xlsone_core PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )
    if(XLSONE_C_WARNINGS_AS_ERRORS)
        target_compile_options(xlsone_i18n PRIVATE -Werror)
        target_compile_options(xlsone_core PRIVATE -Werror)
    endif()
endif()

if(NOT MSVC)
    target_link_libraries(xlsone_core PUBLIC m)
endif()
target_link_libraries(xlsone_core PRIVATE ZLIB::ZLIB EXPAT::EXPAT)

if(XLSONE_C_INSTALL_DEVELOPMENT)
    install(TARGETS xlsone_core
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    )
    install(
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )
    install(
        FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
            "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}"
    )
endif()

if(XLSONE_C_BUILD_APP)
    add_subdirectory(app)
endif()

if(XLSONE_C_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
