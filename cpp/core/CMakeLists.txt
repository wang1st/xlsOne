find_package(ZLIB REQUIRED)

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
    include/xlsone/core/license_manager.hpp
    include/xlsone/core/update_checker.hpp
)

target_include_directories(xlsone_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(xlsone_core
    PUBLIC
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Network
    PRIVATE
        ZLIB::ZLIB
)

target_compile_definitions(xlsone_core
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        XLSONE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        XLSONE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        XLSONE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)
