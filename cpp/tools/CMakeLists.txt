qt_add_executable(xlsone_snapshot
    snapshot_main.cpp
)

target_link_libraries(xlsone_snapshot
    PRIVATE
        xlsone_core
        Qt6::Core
)

add_test(NAME xlsone_snapshot_help COMMAND xlsone_snapshot --help)
