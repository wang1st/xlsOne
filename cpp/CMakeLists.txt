cmake_minimum_required(VERSION 3.22)

project(xlsOneQt
    VERSION 1.0.1
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

# Translations — use lrelease to compile .ts → .qm (skip if .qm already present)
set(_i18n_dir "${CMAKE_CURRENT_SOURCE_DIR}/i18n")
if(EXISTS "${_i18n_dir}/xlsone_zh_CN.qm" AND EXISTS "${_i18n_dir}/xlsone_en.qm")
    message(STATUS "Pre-compiled .qm translations found — skipping lrelease")
else()
    find_program(LRELEASE_EXECUTABLE lrelease)
    if(LRELEASE_EXECUTABLE)
        add_custom_target(translations ALL
            COMMAND ${LRELEASE_EXECUTABLE}
                "${_i18n_dir}/xlsone_zh_CN.ts"
                "${_i18n_dir}/xlsone_en.ts"
                -qm "${_i18n_dir}/xlsone_zh_CN.qm"
                -qm "${_i18n_dir}/xlsone_en.qm"
            WORKING_DIRECTORY "${_i18n_dir}"
            COMMENT "Compiling Qt translations"
        )
    else()
        message(WARNING "lrelease not found — translations will not be compiled")
    endif()
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

if(UNIX AND NOT APPLE)
    # Decide whether we are producing a self-contained bundle (UOS/Deepin build
    # environment) or a distro-native package that uses the system Qt.
    set(XLSONE_BUNDLE_QT OFF)
    if(IS_DIRECTORY "${XLSONE_QT_BUNDLE_DIR}"
       AND EXISTS "${XLSONE_QT_BUNDLE_DIR}/libQt5Core.so")
        set(XLSONE_BUNDLE_QT ON)
        message(STATUS "Qt bundle directory found; producing self-contained .deb")
    else()
        message(STATUS "Qt bundle directory not available; .deb will use system Qt packages")
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

        # Bundle ICU (required by QtCore; version must match the bundled Qt)
        set(_icu_dir "/lib/${CMAKE_LIBRARY_ARCHITECTURE}")
        file(GLOB _icu_files
            "${_icu_dir}/libicudata.so.63*"
            "${_icu_dir}/libicui18n.so.63*"
            "${_icu_dir}/libicuuc.so.63*"
        )
        if(_icu_files)
            install(FILES ${_icu_files} DESTINATION ${XLSONE_BUNDLE_LIB_DEST})
        else()
            message(WARNING "ICU 63 libraries not found under ${_icu_dir} — target may need system ICU")
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
