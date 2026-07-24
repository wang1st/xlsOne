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

add_executable(xlsone_inspector_tests
    inspector_panel_tests.cpp
    ../app/src/dialog_utils.cpp
    ../app/src/inspector_panel.cpp
    ../app/src/ui_theme.cpp
)

target_include_directories(xlsone_inspector_tests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../app/src"
)

target_link_libraries(xlsone_inspector_tests
    PRIVATE
        xlsone_core
        Qt${QT_VERSION_MAJOR}::Test
        Qt${QT_VERSION_MAJOR}::Widgets
)

add_test(NAME xlsone_inspector_tests COMMAND xlsone_inspector_tests)
set_tests_properties(xlsone_inspector_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
