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

add_executable(xlsone_i18n_tests
    test_i18n.c
)
target_link_libraries(xlsone_i18n_tests PRIVATE xlsone_i18n)
if(MSVC)
    target_compile_options(xlsone_i18n_tests PRIVATE /W4)
    if(XLSONE_C_WARNINGS_AS_ERRORS)
        target_compile_options(xlsone_i18n_tests PRIVATE /WX)
    endif()
else()
    target_compile_options(xlsone_i18n_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )
    if(XLSONE_C_WARNINGS_AS_ERRORS)
        target_compile_options(xlsone_i18n_tests PRIVATE -Werror)
    endif()
endif()
add_test(NAME xlsone_i18n_tests COMMAND xlsone_i18n_tests)

add_executable(
    xlsone_drop_paths_tests
    test_drop_paths.c
    ../app/drop_paths.c
)
target_include_directories(
    xlsone_drop_paths_tests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../app"
)
if(MSVC)
    target_compile_options(xlsone_drop_paths_tests PRIVATE /W4)
    if(XLSONE_C_WARNINGS_AS_ERRORS)
        target_compile_options(xlsone_drop_paths_tests PRIVATE /WX)
    endif()
else()
    target_compile_options(xlsone_drop_paths_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )
    if(XLSONE_C_WARNINGS_AS_ERRORS)
        target_compile_options(xlsone_drop_paths_tests PRIVATE -Werror)
    endif()
endif()
add_test(NAME xlsone_drop_paths_tests COMMAND xlsone_drop_paths_tests)

if(TARGET xlsone_cjson
    AND TARGET xlsone_monocypher
    AND XLSONE_LICENSE_PUBLIC_KEY)
    add_executable(
        xlsone_license_tests
        test_license.c
        ../app/license_manager.c
    )
    target_include_directories(
        xlsone_license_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../app"
    )
    target_compile_definitions(
        xlsone_license_tests
        PRIVATE
            XLSONE_LICENSE_PUBLIC_KEY="${XLSONE_LICENSE_PUBLIC_KEY}"
    )
    target_link_libraries(
        xlsone_license_tests
        PRIVATE
            xlsone_cjson
            xlsone_monocypher
    )
    if(MSVC)
        target_compile_options(xlsone_license_tests PRIVATE /W4)
        if(XLSONE_C_WARNINGS_AS_ERRORS)
            target_compile_options(xlsone_license_tests PRIVATE /WX)
        endif()
    else()
        target_compile_options(
            xlsone_license_tests
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wshadow
        )
        target_link_libraries(xlsone_license_tests PRIVATE m)
        if(XLSONE_C_WARNINGS_AS_ERRORS)
            target_compile_options(xlsone_license_tests PRIVATE -Werror)
        endif()
    endif()
    add_test(NAME xlsone_license_tests COMMAND xlsone_license_tests)
endif()
