cmake_minimum_required(VERSION 3.22)

project(xlsOneQt
    VERSION 1.0.2
    DESCRIPTION "Cross-platform C++/Qt port of xlsOne"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Network Test)

enable_testing()

# Translations — use lrelease to compile .ts → .qm
set(_i18n_dir "${CMAKE_CURRENT_SOURCE_DIR}/i18n")
find_program(LRELEASE_EXECUTABLE lrelease)
if(LRELEASE_EXECUTABLE)
    add_custom_target(translations ALL
        COMMAND ${LRELEASE_EXECUTABLE}
            "${_i18n_dir}/xlsone_zh_CN.ts"
            "${_i18n_dir}/xlsone_en.ts"
            "${_i18n_dir}/xlsone_ja.ts"
            -qm "${_i18n_dir}/xlsone_zh_CN.qm"
            -qm "${_i18n_dir}/xlsone_en.qm"
            -qm "${_i18n_dir}/xlsone_ja.qm"
        WORKING_DIRECTORY "${_i18n_dir}"
        COMMENT "Compiling Qt translations"
    )
else()
    message(WARNING "lrelease not found — translations will not be compiled")
endif()
install(DIRECTORY "${_i18n_dir}/"
    DESTINATION i18n
    FILES_MATCHING PATTERN "*.qm"
)

add_subdirectory(core)
add_subdirectory(app)
add_subdirectory(tests)
add_subdirectory(tools)

# ---- Bundle Qt libraries for self-contained DEB (UOS + Kylin compatible) ----
set(XLSONE_QT_BUNDLE_DIR "/opt/deepin-shared-libs/Qt5.15.10gles/lib"
    CACHE PATH "Directory containing Qt .so files to bundle")
set(XLSONE_QT_PLUGIN_DIR "/opt/deepin-shared-libs/Qt5.15.10gles/plugins"
    CACHE PATH "Directory containing Qt plugins to bundle")
set(XLSONE_BUNDLE_LIB_DEST "lib/xlsone")
option(XLSONE_BUNDLE_QT "Bundle Qt libraries into a self-contained DEB" ON)

if(UNIX AND NOT APPLE)
    # Decide whether we are producing a self-contained bundle or a distro-native
    # package that uses the system Qt.  Set -DXLSONE_BUNDLE_QT=OFF to force
    # system Qt mode (useful when building on the same OS as the target, e.g.
    # building on UOS for UOS).
    if(XLSONE_BUNDLE_QT)
        if(IS_DIRECTORY "${XLSONE_QT_BUNDLE_DIR}"
           AND EXISTS "${XLSONE_QT_BUNDLE_DIR}/libQt5Core.so")
            message(STATUS "Qt bundle directory found; producing self-contained .deb")
        else()
            message(WARNING "Qt bundle requested but Qt libraries not found in ${XLSONE_QT_BUNDLE_DIR}")
            message(STATUS "Falling back to system Qt packages")
            set(XLSONE_BUNDLE_QT OFF)
        endif()
    else()
        message(STATUS "Qt bundling disabled (XLSONE_BUNDLE_QT=OFF); .deb will use system Qt packages")
    endif()

    if(XLSONE_BUNDLE_QT)
        # Helper to glob and install a bundled Qt library with all its symlinks
        macro(xlsone_bundle_qt_lib libname)
            file(GLOB _so_files "${XLSONE_QT_BUNDLE_DIR}/${libname}.*")
            if(_so_files)
                install(FILES ${_so_files} DESTINATION ${XLSONE_BUNDLE_LIB_DEST})
            else()
                message(WARNING "Qt bundle: ${libname} not found under ${XLSONE_QT_BUNDLE_DIR}")
            endif()
        endmacro()

        xlsone_bundle_qt_lib("libQt5Core.so")
        xlsone_bundle_qt_lib("libQt5Gui.so")
        xlsone_bundle_qt_lib("libQt5Widgets.so")
        xlsone_bundle_qt_lib("libQt5Network.so")
        xlsone_bundle_qt_lib("libQt5DBus.so")
        xlsone_bundle_qt_lib("libQt5XcbQpa.so")

        # Bundle ICU (required by QtCore; version must match the bundled Qt).
        # The bundled Qt5 from deepin-shared-libs needs ICU 63, while the build
        # host may only ship ICU 66. Prefer the ICU libraries shipped alongside
        # the Qt bundle (e.g. deepin-qt5.15/icu63/...), fall back to the Qt dir
        # itself, and finally to the system ICU dir.
        set(_icu_search_dirs
            "${PROJECT_SOURCE_DIR}/deepin-qt5.15/icu63/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
            "${XLSONE_QT_BUNDLE_DIR}"
            "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
        )
        set(_icu_found OFF)
        foreach(_dir ${_icu_search_dirs})
            if(NOT _icu_found)
                file(GLOB _icu_files
                    "${_dir}/libicudata.so.[0-9]*"
                    "${_dir}/libicui18n.so.[0-9]*"
                    "${_dir}/libicuuc.so.[0-9]*"
                )
                if(_icu_files)
                    install(FILES ${_icu_files} DESTINATION ${XLSONE_BUNDLE_LIB_DEST})
                    set(_icu_found ON)
                    message(STATUS "Bundling ICU libraries from ${_dir}")
                endif()
            endif()
        endforeach()
        if(NOT _icu_found)
            message(WARNING "ICU libraries not found — target may need system ICU")
        endif()

        # Bundle libdouble-conversion (required by Qt5Core for string↔number conversion)
        # Check both the system lib dir and the Qt bundle dir (KylinOS may have it in either)
        set(_dc_search_dirs "${_icu_dir}" "${XLSONE_QT_BUNDLE_DIR}" "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}")
        set(_dc_found OFF)
        foreach(_dir ${_dc_search_dirs})
            if(NOT _dc_found)
                file(GLOB _dc_files "${_dir}/libdouble-conversion.so.*")
                if(_dc_files)
                    install(FILES ${_dc_files} DESTINATION ${XLSONE_BUNDLE_LIB_DEST})
                    set(_dc_found ON)
                endif()
            endif()
        endforeach()
        if(NOT _dc_found)
            message(WARNING "libdouble-conversion not found in ${_dc_search_dirs} — target may need it from system")
        endif()

        # Bundle libpcre2-16 (required by Qt5Core >= 5.14 for QRegularExpression)
        set(_pcre_found OFF)
        foreach(_dir ${_dc_search_dirs})
            if(NOT _pcre_found)
                file(GLOB _pcre_files "${_dir}/libpcre2-16.so.*")
                if(_pcre_files)
                    install(FILES ${_pcre_files} DESTINATION ${XLSONE_BUNDLE_LIB_DEST})
                    set(_pcre_found ON)
                endif()
            endif()
        endforeach()
        if(NOT _pcre_found)
            message(WARNING "libpcre2-16 not found — Qt may require it for regex support")
        endif()

        # Bundle libzstd (required by Qt5Core in some builds)
        set(_zstd_found OFF)
        foreach(_dir ${_icu_search_dirs})
            if(NOT _zstd_found)
                file(GLOB _zstd_files "${_dir}/libzstd.so.*")
                if(_zstd_files)
                    install(FILES ${_zstd_files} DESTINATION ${XLSONE_BUNDLE_LIB_DEST})
                    set(_zstd_found ON)
                endif()
            endif()
        endforeach()
        if(NOT _zstd_found)
            message(WARNING "libzstd not found — Qt may require it")
        endif()

        # Bundle Qt platform, image, and input-method plugins
        if(IS_DIRECTORY "${XLSONE_QT_PLUGIN_DIR}")
            install(DIRECTORY "${XLSONE_QT_PLUGIN_DIR}/platforms/"
                DESTINATION "${XLSONE_BUNDLE_LIB_DEST}/plugins/platforms"
                FILES_MATCHING PATTERN "*.so"
            )
            install(DIRECTORY "${XLSONE_QT_PLUGIN_DIR}/imageformats/"
                DESTINATION "${XLSONE_BUNDLE_LIB_DEST}/plugins/imageformats"
                FILES_MATCHING PATTERN "*.so"
            )
            install(DIRECTORY "${XLSONE_QT_PLUGIN_DIR}/platforminputcontexts/"
                DESTINATION "${XLSONE_BUNDLE_LIB_DEST}/plugins/platforminputcontexts"
                FILES_MATCHING PATTERN "*.so"
            )
        else()
            message(WARNING "Qt plugin bundle directory not found: ${XLSONE_QT_PLUGIN_DIR}")
        endif()
    else()
        # Native Kylin/Debian build: rely on distro Qt packages.
        # CPACK_DEBIAN_PACKAGE_DEPENDS is set below.
    endif()

    # Wrapper script sets QT_PLUGIN_PATH so the binary finds bundled plugins.
    # When no Qt libs are bundled, the non-existent path is ignored and the
    # system Qt plugin path is used.
    install(PROGRAMS packaging/linux/xlsone.sh
        DESTINATION bin
    )

    # RPATH so the binary finds bundled libs without relying on wrapper
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
    set_target_properties(xlsone_app PROPERTIES
        INSTALL_RPATH "$ORIGIN/../${XLSONE_BUNDLE_LIB_DEST}"
    )
endif()

set(CPACK_PACKAGE_NAME "xlsone")
set(CPACK_PACKAGE_VENDOR "xlsOne")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "同构 Excel 工作簿智能合并与校验工具")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "831261@qq.com")

# Architecture-tagged package filename (e.g. xlsone-1.0.2-linux-arm64.deb)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(_xlsone_pkg_arch "arm64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(_xlsone_pkg_arch "amd64")
else()
    set(_xlsone_pkg_arch "${CMAKE_SYSTEM_PROCESSOR}")
endif()
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-linux-${_xlsone_pkg_arch}")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE")
endif()

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
elseif(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "xlsOne <831261@qq.com>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
    if(XLSONE_BUNDLE_QT)
        set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libstdc++6, zlib1g")
    else()
        set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libstdc++6, zlib1g, libqt5core5a, libqt5gui5, libqt5widgets5, libqt5network5")
    endif()
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/postinst"
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/postrm"
    )

    install(FILES packaging/linux/xlsone.desktop
        DESTINATION share/applications
    )
    install(FILES packaging/linux/xlsone-mime.xml
        DESTINATION share/mime/packages
        RENAME xlsone.xml
    )
    # Install app icon in multiple sizes for hicolor theme
    set(HICOLOR_SIZES 16 22 24 32 48 64 96 128 256 512)
    foreach(SIZE ${HICOLOR_SIZES})
        set(ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/app/resources/xlsOne_${SIZE}x${SIZE}.png")
        if(EXISTS "${ICON_FILE}")
            install(FILES "${ICON_FILE}"
                DESTINATION "share/icons/hicolor/${SIZE}x${SIZE}/apps"
                RENAME xlsone.png)
        endif()
    endforeach()
    # Fallback icon for desktop environments that don't chain to hicolor
    install(FILES app/resources/xlsOne_256x256.png
        DESTINATION share/pixmaps
        RENAME xlsone.png
    )
endif()

include(CPack)
