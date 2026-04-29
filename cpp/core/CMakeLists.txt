find_package(ZLIB REQUIRED)

add_library(xlsone_core STATIC
    src/biff8_xls_parser.cpp
    src/compound_file_reader.cpp
    src/excel_parser.cpp
    src/exporter.cpp
    src/merger.cpp
    src/models.cpp
    src/schema_repository.cpp
    src/validator.cpp
    src/zip_archive.cpp
)

target_include_directories(xlsone_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(xlsone_core
    PUBLIC
        Qt6::Core
    PRIVATE
        ZLIB::ZLIB
)

target_compile_definitions(xlsone_core
    PRIVATE
        QT_NO_CAST_FROM_ASCII
)
