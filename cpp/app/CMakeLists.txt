add_executable(xlsone_app
    src/dialog_utils.cpp
    src/dialog_utils.hpp
    src/diagnostics_view.cpp
    src/diagnostics_view.hpp
    src/empty_workspace_view.cpp
    src/empty_workspace_view.hpp
    src/inspector_panel.cpp
    src/inspector_panel.hpp
    src/license_activation_dialog.cpp
    src/license_activation_dialog.hpp
    src/main.cpp
    src/main_window.cpp
    src/main_window.hpp
    src/merged_table_delegate.cpp
    src/merged_table_delegate.hpp
    src/merged_table_model.cpp
    src/merged_table_model.hpp
    src/schema_manager_dialog.cpp
    src/schema_manager_dialog.hpp
    src/sheet_strip.cpp
    src/sheet_strip.hpp
    src/symbol_icons.cpp
    src/symbol_icons.hpp
    src/ui_theme.cpp
    src/ui_theme.hpp
    src/update_dialog.cpp
    src/update_dialog.hpp
    src/workspace_chrome.cpp
    src/workspace_chrome.hpp
    xlsone.qrc
)

set_target_properties(xlsone_app PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    OUTPUT_NAME xlsOneQt
    MACOSX_BUNDLE_ICON_FILE xlsOne.icns
)

# Copy icon files into the macOS bundle Resources
if(APPLE)
    add_custom_command(TARGET xlsone_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:xlsone_app>/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/xlsOne.icns"
            "$<TARGET_BUNDLE_CONTENT_DIR:xlsone_app>/Resources/xlsOne.icns"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/xlsOne.png"
            "$<TARGET_BUNDLE_CONTENT_DIR:xlsone_app>/Resources/xlsOne.png"
    )
endif()

target_link_libraries(xlsone_app
    PRIVATE
        xlsone_core
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
)

# XLSONE_UPDATE_BASE_URL: override with -D for local dev (http) vs release (https)
if(NOT DEFINED XLSONE_UPDATE_BASE_URL)
    set(XLSONE_UPDATE_BASE_URL "https://z-pulse.cn")
endif()
target_compile_definitions(xlsone_app
    PRIVATE
        XLSONE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        XLSONE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        XLSONE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        XLSONE_UPDATE_BASE_URL="${XLSONE_UPDATE_BASE_URL}"
)

install(TARGETS xlsone_app
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
