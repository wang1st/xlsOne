add_library(xlsone_core STATIC
    src/algorithm_keywords.cpp
    src/biff8_xls_parser.cpp
    src/compound_file_reader.cpp
    src/ed25519/monocypher.c
    src/ed25519/monocypher-ed25519.c
    src/excel_parser.cpp
    src/exporter.cpp
    src/license_manager.cpp
    src/merger.cpp
    src/models.cpp
    src/schema_repository.cpp
    src/update_checker.cpp
    src/validator.cpp
    src/zip_archive.cpp
    ${XLSONE_OBFUSCATED_SECRETS}
    include/xlsone/core/license_manager.hpp
    include/xlsone/core/update_checker.hpp
)

target_include_directories(xlsone_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# zlib: prefer Qt6::ZlibPrivate when available; fall back to system ZLIB for
# Qt distributions that do not ship private package configs.
if(QT_VERSION_MAJOR EQUAL 6)
    find_package(Qt6 COMPONENTS ZlibPrivate QUIET)
    if(TARGET Qt6::ZlibPrivate)
        set(_xlsone_zlib_target Qt6::ZlibPrivate)
    else()
        find_package(ZLIB REQUIRED)
        set(_xlsone_zlib_target ZLIB::ZLIB)
    endif()
else()
    find_package(ZLIB REQUIRED)
    set(_xlsone_zlib_target ZLIB::ZLIB)
endif()

target_link_libraries(xlsone_core
    PUBLIC
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Network
    PRIVATE
        ${_xlsone_zlib_target}
)

# XLSONE_ACTIVATION_BASE_URL is defined as a CMake variable in the top-level
# CMakeLists.txt so it can be passed to the obfuscation generator.  When
# obfuscation is disabled, the URL is passed through as a compile definition
# exactly as before so dev builds with custom endpoints keep working.

target_compile_definitions(xlsone_core
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        XLSONE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        XLSONE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        XLSONE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

if(XLSONE_OBFUSCATE)
    target_compile_definitions(xlsone_core PRIVATE XLSONE_OBFUSCATE)
    if(TARGET xlsone_obf_secrets)
        add_dependencies(xlsone_core xlsone_obf_secrets)
    endif()
else()
    target_compile_definitions(xlsone_core
        PRIVATE
            XLSONE_ACTIVATION_BASE_URL="${XLSONE_ACTIVATION_BASE_URL}"
    )
endif()

if(XLSONE_LINUX_BUILTIN_LICENSE)
    target_compile_definitions(xlsone_core PRIVATE XLSONE_LINUX_BUILTIN_LICENSE)
endif()
