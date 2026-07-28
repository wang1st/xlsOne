add_executable(xlsone_core_tests
    test_core.c
)

target_link_libraries(xlsone_core_tests PRIVATE xlsone_core)
target_compile_definitions(xlsone_core_tests PRIVATE
    XLSONE_REPOSITORY_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../.."
    XLSONE_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)

if(MSVC)
    target_compile_options(xlsone_core_tests PRIVATE /W4)
    if(XLSONE_C_WARNINGS_AS_ERRORS)
        target_compile_options(xlsone_core_tests PRIVATE /WX)
    endif()
else()
    target_compile_options(xlsone_core_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )
    if(XLSONE_C_WARNINGS_AS_ERRORS)
        target_compile_options(xlsone_core_tests PRIVATE -Werror)
    endif()
endif()

add_test(NAME xlsone_core_tests COMMAND xlsone_core_tests)
