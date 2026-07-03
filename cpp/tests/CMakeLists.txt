enable_testing()

add_executable(xlsone_core_tests
    core_tests.cpp
)

target_link_libraries(xlsone_core_tests
    PRIVATE
        xlsone_core
        Qt${QT_VERSION_MAJOR}::Test
        Qt${QT_VERSION_MAJOR}::Network
)

target_compile_definitions(xlsone_core_tests
    PRIVATE
        XLSONE_REPO_ROOT="${CMAKE_SOURCE_DIR}/.."
)

add_test(NAME xlsone_core_tests COMMAND xlsone_core_tests)
