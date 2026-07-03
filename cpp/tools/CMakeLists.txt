add_executable(xlsone_snapshot
    snapshot_main.cpp
)

target_link_libraries(xlsone_snapshot
    PRIVATE
        xlsone_core
        Qt${QT_VERSION_MAJOR}::Core
)

add_test(NAME xlsone_snapshot_help COMMAND xlsone_snapshot --help)
