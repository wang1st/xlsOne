if(TARGET SDL2::SDL2
    OR TARGET SDL2::SDL2-static
    OR TARGET PkgConfig::SDL2)
    include(FetchContent)
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Declare(
        cjson
        GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
        GIT_TAG acc76239bee01d8e9c858ae2cab296704e52d916
        GIT_SHALLOW FALSE
    )
    FetchContent_GetProperties(cjson)
    if(NOT cjson_POPULATED)
        FetchContent_Populate(cjson)
    endif()
    add_library(xlsone_cjson STATIC "${cjson_SOURCE_DIR}/cJSON.c")
    target_include_directories(
        xlsone_cjson SYSTEM PUBLIC "${cjson_SOURCE_DIR}"
    )
    add_library(
        xlsone_monocypher
        STATIC
            ../../cpp/core/src/ed25519/monocypher.c
            ../../cpp/core/src/ed25519/monocypher-ed25519.c
    )
    target_include_directories(
        xlsone_monocypher
        SYSTEM PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/../../cpp/core/src/ed25519"
    )

    set(XLSONE_NUKLEAR_DIR "${CMAKE_BINARY_DIR}/_deps/nuklear-v4.13.3")
    file(MAKE_DIRECTORY "${XLSONE_NUKLEAR_DIR}")
    if(NOT EXISTS "${XLSONE_NUKLEAR_DIR}/nuklear.h")
        file(DOWNLOAD
            "https://raw.githubusercontent.com/Immediate-Mode-UI/Nuklear/v4.13.3/nuklear.h"
            "${XLSONE_NUKLEAR_DIR}/nuklear.h"
            EXPECTED_HASH
                "SHA256=2e3b7c3f6528cd8e43aeae87e343fe9ca529b6f7380a99ebb63a41cf1d4a1552"
            TLS_VERIFY ON
            STATUS XLSONE_NUKLEAR_DOWNLOAD_STATUS
        )
        list(GET XLSONE_NUKLEAR_DOWNLOAD_STATUS 0 XLSONE_NUKLEAR_DOWNLOAD_CODE)
        if(NOT XLSONE_NUKLEAR_DOWNLOAD_CODE EQUAL 0)
            message(FATAL_ERROR "Unable to download the pinned Nuklear header")
        endif()
    endif()
    if(NOT EXISTS "${XLSONE_NUKLEAR_DIR}/nuklear_sdl_renderer.h")
        file(DOWNLOAD
            "https://raw.githubusercontent.com/Immediate-Mode-UI/Nuklear/v4.13.3/demo/sdl_renderer/nuklear_sdl_renderer.h"
            "${XLSONE_NUKLEAR_DIR}/nuklear_sdl_renderer.h"
            EXPECTED_HASH
                "SHA256=0aaf0cdc4c84170a6c3217e923ba6b4f5bbcd557916739d0a99a46aa61cc5901"
            TLS_VERIFY ON
            STATUS XLSONE_NUKLEAR_BACKEND_DOWNLOAD_STATUS
        )
        list(GET XLSONE_NUKLEAR_BACKEND_DOWNLOAD_STATUS 0
            XLSONE_NUKLEAR_BACKEND_DOWNLOAD_CODE)
        if(NOT XLSONE_NUKLEAR_BACKEND_DOWNLOAD_CODE EQUAL 0)
            message(FATAL_ERROR "Unable to download the pinned Nuklear SDL backend")
        endif()
    endif()
    if(NOT EXISTS "${XLSONE_NUKLEAR_DIR}/LICENSE")
        file(DOWNLOAD
            "https://raw.githubusercontent.com/Immediate-Mode-UI/Nuklear/v4.13.3/LICENSE"
            "${XLSONE_NUKLEAR_DIR}/LICENSE"
            EXPECTED_HASH
                "SHA256=e7315972a81b1883b33877e7c90b78e0e93ad67d1e5f9b723fd13c9ad3e2ec94"
            TLS_VERIFY ON
        )
    endif()

    add_executable(xlsone_app
        drop_paths.c
        license_manager.c
        main.c
        platform_dialog.c
        platform_drop.c
        platform_services.c
        sheet_tabs.c
    )
    target_include_directories(
        xlsone_app
        SYSTEM PRIVATE
            "${XLSONE_NUKLEAR_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../cpp/core/src/ed25519"
    )
    target_link_libraries(
        xlsone_app
        PRIVATE
            xlsone_core
            xlsone_i18n
            xlsone_cjson
            xlsone_monocypher
    )
    target_compile_definitions(
        xlsone_app
        PRIVATE
            XLSONE_VERSION="${PROJECT_VERSION}"
    )
    if(XLSONE_LICENSE_PUBLIC_KEY)
        target_compile_definitions(
            xlsone_app
            PRIVATE
                XLSONE_LICENSE_PUBLIC_KEY="${XLSONE_LICENSE_PUBLIC_KEY}"
        )
    else()
        message(WARNING
            "Building without XLSONE_LICENSE_PUBLIC_KEY; online and offline "
            "license signatures will be rejected.")
    endif()
    if(MSVC)
        target_compile_options(
            xlsone_app
            PRIVATE
                /W4
                # Nuklear 4.13.3 triggers MSVC's path-sensitive C4701
                # diagnostics in implementation code despite initializing
                # the values on every reachable path.
                /wd4701
        )
        if(XLSONE_C_WARNINGS_AS_ERRORS)
            target_compile_options(xlsone_app PRIVATE /WX)
        endif()
    else()
        target_compile_options(xlsone_app PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
        )
        if(XLSONE_C_WARNINGS_AS_ERRORS)
            target_compile_options(xlsone_app PRIVATE -Werror)
        endif()
    endif()
    if(WIN32)
        if(TARGET SDL2::SDL2main)
            target_link_libraries(xlsone_app PRIVATE SDL2::SDL2main)
        else()
            message(FATAL_ERROR
                "The Windows GUI build requires the SDL2main target.")
        endif()
    endif()
    if(TARGET SDL2::SDL2-static)
        target_link_libraries(xlsone_app PRIVATE SDL2::SDL2-static)
    elseif(TARGET SDL2::SDL2)
        target_link_libraries(xlsone_app PRIVATE SDL2::SDL2)
    else()
        target_link_libraries(xlsone_app PRIVATE PkgConfig::SDL2)
    endif()
    set_target_properties(xlsone_app PROPERTIES OUTPUT_NAME xlsOne)
    if(WIN32)
        set(XLSONE_WINDOWS_MANIFEST
            "${CMAKE_CURRENT_SOURCE_DIR}/../packaging/windows/xlsone.manifest"
        )
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/../packaging/windows/app-manifest.rc.in"
            "${CMAKE_CURRENT_BINARY_DIR}/xlsone-manifest.rc"
            @ONLY
        )
        target_sources(
            xlsone_app
            PRIVATE
                ../../cpp/app/resources/xlsone.rc
                "${CMAKE_CURRENT_BINARY_DIR}/xlsone-manifest.rc"
        )
        target_link_libraries(
            xlsone_app
            PRIVATE
                advapi32
                comdlg32
                ole32
                shell32
                winhttp
        )
        set_target_properties(xlsone_app PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
    if(APPLE)
        find_library(
            XLSONE_CORESERVICES_FRAMEWORK
            CoreServices
            REQUIRED
        )
        target_link_libraries(
            xlsone_app
            PRIVATE "${XLSONE_CORESERVICES_FRAMEWORK}"
        )
        set(XLSONE_MACOS_ICON
            "${CMAKE_CURRENT_SOURCE_DIR}/../../cpp/app/resources/xlsOne.icns"
        )
        target_sources(xlsone_app PRIVATE "${XLSONE_MACOS_ICON}")
        set_source_files_properties(
            "${XLSONE_MACOS_ICON}"
            PROPERTIES MACOSX_PACKAGE_LOCATION Resources
        )
        set_target_properties(xlsone_app PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME "xlsOne"
            MACOSX_BUNDLE_GUI_IDENTIFIER "cn.z-pulse.xlsone"
            MACOSX_BUNDLE_ICON_FILE "xlsOne.icns"
            MACOSX_BUNDLE_INFO_PLIST
                "${CMAKE_CURRENT_SOURCE_DIR}/../packaging/macos/Info.plist.in"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        )
    endif()
    if(WIN32)
        install(TARGETS xlsone_app RUNTIME DESTINATION .)
    else()
        install(TARGETS xlsone_app
            RUNTIME DESTINATION bin
            BUNDLE DESTINATION .
        )
    endif()
    if(NOT APPLE)
        install(
            FILES
                "${CMAKE_CURRENT_SOURCE_DIR}/../../cpp/packaging/windows/licenses/EULA_zh_CN.txt"
            DESTINATION licenses
        )
        install(FILES "${XLSONE_NUKLEAR_DIR}/LICENSE"
            DESTINATION licenses
            RENAME Nuklear-LICENSE.txt)
        install(FILES "${cjson_SOURCE_DIR}/LICENSE"
            DESTINATION licenses
            RENAME cJSON-LICENSE.txt)
        install(
            FILES
                "${CMAKE_CURRENT_SOURCE_DIR}/../licenses/EXPAT-LICENSE.txt"
                "${CMAKE_CURRENT_SOURCE_DIR}/../licenses/MONOCYPHER-LICENSE.txt"
                "${CMAKE_CURRENT_SOURCE_DIR}/../licenses/ZLIB-LICENSE.txt"
                "${CMAKE_CURRENT_SOURCE_DIR}/../THIRD_PARTY_NOTICES.md"
            DESTINATION licenses
        )
        if(XLSONE_SDL_LICENSE AND EXISTS "${XLSONE_SDL_LICENSE}")
            install(FILES "${XLSONE_SDL_LICENSE}"
                DESTINATION licenses
                RENAME SDL2-LICENSE.txt)
        endif()
    endif()
    if(APPLE)
        install(
            FILES
                "${CMAKE_CURRENT_SOURCE_DIR}/../packaging/macos/安装前必看.txt"
            DESTINATION "."
        )
        install(
            FILES
                "${CMAKE_CURRENT_SOURCE_DIR}/../../cpp/packaging/windows/licenses/EULA_zh_CN.txt"
            DESTINATION "xlsOne.app/Contents/Resources/licenses"
        )
        install(FILES "${XLSONE_NUKLEAR_DIR}/LICENSE"
            DESTINATION "xlsOne.app/Contents/Resources/licenses"
            RENAME Nuklear-LICENSE.txt)
        install(FILES "${cjson_SOURCE_DIR}/LICENSE"
            DESTINATION "xlsOne.app/Contents/Resources/licenses"
            RENAME cJSON-LICENSE.txt)
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../THIRD_PARTY_NOTICES.md"
            DESTINATION "xlsOne.app/Contents/Resources/licenses")
        install(
            FILES
                "${CMAKE_CURRENT_SOURCE_DIR}/../licenses/EXPAT-LICENSE.txt"
                "${CMAKE_CURRENT_SOURCE_DIR}/../licenses/MONOCYPHER-LICENSE.txt"
                "${CMAKE_CURRENT_SOURCE_DIR}/../licenses/ZLIB-LICENSE.txt"
            DESTINATION "xlsOne.app/Contents/Resources/licenses"
        )
        if(XLSONE_SDL_LICENSE AND EXISTS "${XLSONE_SDL_LICENSE}")
            install(FILES "${XLSONE_SDL_LICENSE}"
                DESTINATION "xlsOne.app/Contents/Resources/licenses"
                RENAME SDL2-LICENSE.txt)
        endif()
    endif()
    if(UNIX AND NOT APPLE)
        install(
            PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/../packaging/linux/xlsone.desktop"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
        )
        install(
            FILES "${CMAKE_CURRENT_SOURCE_DIR}/../../cpp/app/resources/xlsOne.png"
            DESTINATION
                "${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps"
            RENAME xlsone.png
        )
        install(
            FILES "${CMAKE_CURRENT_SOURCE_DIR}/../packaging/linux/xlsone-mime.xml"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/mime/packages"
        )
    endif()
    if(APPLE)
        set(XLSONE_BUNDLE_SEARCH_DIRS "")
        if(XLSONE_SDL_PREFIX)
            set(XLSONE_BUNDLE_SEARCH_DIRS "${XLSONE_SDL_PREFIX}/lib")
        endif()
        install(CODE "
            include(BundleUtilities)
            get_filename_component(
                XLSONE_INSTALLED_BUNDLE
                \"\${CMAKE_INSTALL_PREFIX}/xlsOne.app\"
                ABSOLUTE
            )
            fixup_bundle(
                \"\${XLSONE_INSTALLED_BUNDLE}\"
                \"\"
                \"${XLSONE_BUNDLE_SEARCH_DIRS}\"
            )
            execute_process(
                COMMAND codesign
                    --force
                    --deep
                    --sign -
                    \"\${XLSONE_INSTALLED_BUNDLE}\"
                RESULT_VARIABLE XLSONE_CODESIGN_RESULT
            )
            if(NOT XLSONE_CODESIGN_RESULT EQUAL 0)
                message(FATAL_ERROR
                    \"Unable to apply the ad-hoc signature to xlsOne.app\")
            endif()
        ")
    endif()
endif()
