cmake_minimum_required(VERSION 3.22)

project(xlsOne
    VERSION 1.1.1
    DESCRIPTION "Commercial-friendly pure C Excel consolidation desktop app"
    LANGUAGES C
)

enable_testing()
add_subdirectory(c)
if(NOT APPLE)
    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/releases/v1.1.1.md"
        DESTINATION "share/doc/xlsOne"
    )
endif()

set(CPACK_PACKAGE_NAME "xlsOne")
set(CPACK_PACKAGE_VENDOR "Z-Pulse")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "support@z-pulse.cn")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Pure C Excel workbook consolidation desktop application"
)
set(CPACK_WIX_UPGRADE_GUID
    "3D6CF1D8-4A9C-4E7B-B1D2-85162F0C7A11"
)
set(CPACK_PACKAGE_INSTALL_DIRECTORY "xlsOne")
set(CPACK_PACKAGE_EXECUTABLES "xlsOne" "xlsOne")
if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_PACKAGE_FILE_NAME
        "xlsOne-${PROJECT_VERSION}-macos-universal"
    )
elseif(WIN32)
    set(CPACK_GENERATOR "ZIP")
    set(CPACK_PACKAGE_FILE_NAME
        "xlsone-${PROJECT_VERSION}-windows-amd64"
    )
    set(CPACK_RESOURCE_FILE_LICENSE
        "${CMAKE_CURRENT_SOURCE_DIR}/cpp/packaging/windows/licenses/EULA_zh_CN.txt"
    )
    set(CPACK_WIX_INSTALL_SCOPE "perMachine")
    set(CPACK_WIX_CULTURES "zh-CN")
    set(CPACK_WIX_LICENSE_RTF
        "${CMAKE_CURRENT_SOURCE_DIR}/cpp/packaging/windows/licenses/EULA_zh_CN.rtf"
    )
    set(CPACK_WIX_PRODUCT_ICON
        "${CMAKE_CURRENT_SOURCE_DIR}/cpp/app/resources/xlsOne.ico"
    )
    set(XLSONE_WIX_LAUNCH_CHECKBOX_TEXT "启动 xlsOne")
    file(MAKE_DIRECTORY
        "${CMAKE_CURRENT_BINARY_DIR}/packaging/windows"
    )
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/c/packaging/windows/shortcuts.wxs.in"
        "${CMAKE_CURRENT_BINARY_DIR}/packaging/windows/shortcuts.wxs"
        @ONLY
    )
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/c/packaging/windows/shortcuts.wxs.patch.in"
        "${CMAKE_CURRENT_BINARY_DIR}/packaging/windows/shortcuts.wxs.patch"
        @ONLY
    )
    set(CPACK_WIX_EXTRA_SOURCES
        "${CMAKE_CURRENT_BINARY_DIR}/packaging/windows/shortcuts.wxs"
    )
    set(CPACK_WIX_PATCH_FILE
        "${CMAKE_CURRENT_BINARY_DIR}/packaging/windows/shortcuts.wxs.patch"
    )
else()
    set(CPACK_GENERATOR "DEB")
    if(XLSONE_PACKAGE_ARCH)
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE
            "${XLSONE_PACKAGE_ARCH}")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
    else()
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
    endif()
    set(CPACK_PACKAGE_FILE_NAME
        "xlsOne-${PROJECT_VERSION}-linux-${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}"
    )
    set(CPACK_DEBIAN_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}.deb")
    set(CPACK_DEBIAN_PACKAGE_SECTION "office")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    if(XLSONE_C_FETCH_SDL2)
        set(CPACK_DEBIAN_PACKAGE_DEPENDS
            "curl, libexpat1, zlib1g, zenity"
        )
    else()
        set(CPACK_DEBIAN_PACKAGE_DEPENDS
            "curl, libsdl2-2.0-0, libexpat1, zlib1g, zenity"
        )
    endif()
endif()
include(CPack)
