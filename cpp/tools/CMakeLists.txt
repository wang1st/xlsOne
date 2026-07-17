add_executable(xlsone_snapshot
    snapshot_main.cpp
)

target_link_libraries(xlsone_snapshot
    PRIVATE
        xlsone_core
        Qt${QT_VERSION_MAJOR}::Core
)

add_test(NAME xlsone_snapshot_help COMMAND xlsone_snapshot --help)

# ---- Gift Code Generator (admin tool) ----
add_executable(xlsone_gift_code_generator
    gift_code_generator_main.cpp
    gift_code_generator.hpp
    gift_code_generator.cpp
)

set_target_properties(xlsone_gift_code_generator PROPERTIES
    OUTPUT_NAME xlsone-gift-code-generator
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

target_link_libraries(xlsone_gift_code_generator
    PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Network
)

# No install() rule on purpose: this is an internal admin tool and must not
# ship in the customer MSI/ZIP. Build it explicitly when needed:
#   cmake --build <build-dir> --target xlsone_gift_code_generator
