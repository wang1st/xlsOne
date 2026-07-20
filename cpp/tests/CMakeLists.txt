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
        XLSONE_BUILD_TESTS
)

# Keep the test's platform expectations aligned with the library configuration.
# Q_OS_LINUX alone is not sufficient: ordinary Linux builds use online
# activation unless this explicit option is enabled.
if(XLSONE_LINUX_BUILTIN_LICENSE)
    target_compile_definitions(xlsone_core_tests PRIVATE XLSONE_LINUX_BUILTIN_LICENSE)
endif()

add_test(NAME xlsone_core_tests COMMAND xlsone_core_tests)
