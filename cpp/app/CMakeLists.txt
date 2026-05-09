add_executable(xlsone_app
    src/diagnostics_view.cpp
    src/diagnostics_view.hpp
    src/empty_workspace_view.cpp
    src/empty_workspace_view.hpp
    src/inspector_panel.cpp
    src/inspector_panel.hpp
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
    src/ui_theme.cpp
    src/ui_theme.hpp
    src/update_dialog.cpp
    src/update_dialog.hpp
    src/workspace_chrome.cpp
    src/workspace_chrome.hpp
)

set_target_properties(xlsone_app PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    OUTPUT_NAME xlsOneQt
)

target_link_libraries(xlsone_app
    PRIVATE
        xlsone_core
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
)

install(TARGETS xlsone_app
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
