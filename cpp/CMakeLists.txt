cmake_minimum_required(VERSION 3.22)

project(xlsOneQt
    VERSION 1.0.10
    DESCRIPTION "Cross-platform C++/Qt port of xlsOne"
    LANGUAGES C CXX
)

# --- Obfuscation / hardening options ---
option(XLSONE_OBFUSCATE "Enable compile-time obfuscation of secrets and strings" OFF)
option(XLSONE_LINUX_BUILTIN_LICENSE "Enable built-in Ed25519-signed license for Linux builds (Linux only)" OFF)
set(XLSONE_LICENSE_PUBLIC_KEY "" CACHE STRING
    "Ed25519 public key for license verification (64 hex chars). Required for a working obfuscated build.")

# Service base URLs.  Defined as CMake variables so they can be passed to the
# obfuscation generator in addition to being used as compile definitions.
if(NOT DEFINED XLSONE_ACTIVATION_BASE_URL)
    set(XLSONE_ACTIVATION_BASE_URL "https://api.xlsone.com")
endif()
if(NOT DEFINED XLSONE_UPDATE_BASE_URL)
    set(XLSONE_UPDATE_BASE_URL "https://z-pulse.cn")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Widgets Network Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Network Test)

if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    execute_process(
        COMMAND xcrun --sdk macosx --show-sdk-path
        OUTPUT_VARIABLE XLSONE_MACOS_SDK_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(XLSONE_MACOS_SDK_PATH
       AND NOT EXISTS "${XLSONE_MACOS_SDK_PATH}/System/Library/Frameworks/AGL.framework")
        get_target_property(_xlsone_wrap_opengl_libs WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
        if(_xlsone_wrap_opengl_libs)
            list(FILTER _xlsone_wrap_opengl_libs EXCLUDE REGEX "(^|/)AGL\\.framework$|^-framework AGL$")
            set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES
                INTERFACE_LINK_LIBRARIES "${_xlsone_wrap_opengl_libs}"
            )
        endif()
    endif()
endif()

enable_testing()

# Obfuscated secrets: when XLSONE_OBFUSCATE is ON, regenerate the secrets file
# in the build tree from the current public key / URLs.  When OFF, the getters
# are not compiled and the source code falls back to the XLSONE_*_BASE_URL
# compile definitions, preserving the original dev-build behavior.
set(XLSONE_OBFUSCATED_SECRETS "")

if(XLSONE_OBFUSCATE)
    # An obfuscated build without the real public key would silently embed the
    # all-zero fallback and fail every license verification — refuse to
    # configure instead of shipping a broken binary.
    if(NOT XLSONE_LICENSE_PUBLIC_KEY)
        message(FATAL_ERROR
            "XLSONE_OBFUSCATE=ON requires XLSONE_LICENSE_PUBLIC_KEY (64 hex chars). "
            "Set it via -DXLSONE_LICENSE_PUBLIC_KEY=... or the environment; "
            "refusing to build an obfuscated binary with the all-zero fallback key.")
    endif()
    find_package(Python3 REQUIRED)
    set(_xlsone_obf_secrets_out
        "${CMAKE_CURRENT_BINARY_DIR}/core/src/obfuscated_secrets.cpp")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/core/src")
    add_custom_command(
        OUTPUT "${_xlsone_obf_secrets_out}"
        COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_obfuscation.py"
            --public-key "${XLSONE_LICENSE_PUBLIC_KEY}"
            --activation-url "${XLSONE_ACTIVATION_BASE_URL}"
            --update-url "${XLSONE_UPDATE_BASE_URL}"
            --output "${_xlsone_obf_secrets_out}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_obfuscation.py"
        COMMENT "Generating obfuscated secrets"
        VERBATIM
    )
    add_custom_target(xlsone_obf_secrets DEPENDS "${_xlsone_obf_secrets_out}")
    set(XLSONE_OBFUSCATED_SECRETS "${_xlsone_obf_secrets_out}")

    # Hardening flags for release-like builds.
    string(TOUPPER "${CMAKE_BUILD_TYPE}" _xlsone_build_type_upper)
    if(_xlsone_build_type_upper MATCHES "RELEASE|MINSIZEREL|RELWITHDEBINFO")
        if(NOT MSVC)
            add_compile_options(
                -O3
                -fvisibility=hidden
                -fvisibility-inlines-hidden
                -fmerge-all-constants
                -fno-ident
            )
            add_link_options(-s)
        else()
            add_compile_options(/O2)
            add_link_options(/DEBUG:NONE /OPT:REF /OPT:ICF)
        endif()
    endif()
endif()

# Translations — use lrelease to compile .ts → .qm
set(_i18n_dir "${CMAKE_CURRENT_SOURCE_DIR}/i18n")
find_program(LRELEASE_EXECUTABLE NAMES lrelease lrelease-qt5)
if(LRELEASE_EXECUTABLE)
    add_custom_target(translations ALL
        COMMAND ${LRELEASE_EXECUTABLE}
            "${_i18n_dir}/xlsone_zh_CN.ts"
            "${_i18n_dir}/xlsone_zh_TW.ts"
            "${_i18n_dir}/xlsone_en.ts"
            "${_i18n_dir}/xlsone_ja.ts"
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

        # Bundle OpenSSL 1.1 (dlopen'ed by Qt5Network at runtime; the bundled
        # Qt5.15 was built against OpenSSL 1.x and is incompatible with the
        # OpenSSL 3.x shipped by newer distros).
        set(_ssl_search_dirs
            "${PROJECT_SOURCE_DIR}/deepin-qt5.15/openssl1.1/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
            "${XLSONE_QT_BUNDLE_DIR}"
        )
        set(_ssl_found OFF)
        foreach(_dir ${_ssl_search_dirs})
            if(NOT _ssl_found)
                file(GLOB _ssl_files
                    "${_dir}/libssl.so.1.1"
                    "${_dir}/libcrypto.so.1.1"
                )
                if(_ssl_files)
                    install(FILES ${_ssl_files} DESTINATION ${XLSONE_BUNDLE_LIB_DEST})
                    set(_ssl_found ON)
                    message(STATUS "Bundling OpenSSL 1.1 libraries from ${_dir}")
                endif()
            endif()
        endforeach()
        if(NOT _ssl_found)
            message(WARNING "OpenSSL 1.1 libraries not found — Qt5Network TLS will fail on OpenSSL 3.x systems")
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

        # The bundled Qt plugins carry RUNPATH=$ORIGIN/../../lib, which resolves
        # to /usr/lib/xlsone/lib. Install a "lib -> ." symlink there so the
        # plugins find the bundled Qt libraries instead of falling back to the
        # (ABI-incompatible) system Qt.
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bundle-symlink")
        file(CREATE_LINK "." "${CMAKE_CURRENT_BINARY_DIR}/bundle-symlink/lib" SYMBOLIC)
        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/bundle-symlink/lib"
            DESTINATION "${XLSONE_BUNDLE_LIB_DEST}")
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

# Architecture-tagged package filename (e.g. xlsone-1.0.4-linux-arm64.deb / xlsone-1.0.4-windows-amd64.zip)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(_xlsone_pkg_arch "arm64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(_xlsone_pkg_arch "amd64")
else()
    set(_xlsone_pkg_arch "${CMAKE_SYSTEM_PROCESSOR}")
endif()
if(WIN32)
    set(_xlsone_pkg_platform "windows")
elseif(APPLE)
    set(_xlsone_pkg_platform "macos")
else()
    set(_xlsone_pkg_platform "linux")
endif()
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${_xlsone_pkg_platform}-${_xlsone_pkg_arch}")

if(WIN32)
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "xlsOne")
    # Fixed upgrade GUID for xlsOne Windows installer. Do not change between releases.
    set(CPACK_WIX_UPGRADE_GUID "A1B2C3D4-E5F6-7890-1234-567890ABCDEF")
    set(CPACK_WIX_PROPERTY_ARPURLINFOABOUT "https://xlsone.com")
    # CPack keeps component GUIDs stable across major upgrades. If a previous
    # package contains a newer Qt file version, Windows Installer otherwise
    # costs the older candidate file as unnecessary, then removes the old file
    # with the superseded product. Force the app-private payload to match this
    # MSI exactly, including intentional Qt version downgrades.
    set(CPACK_WIX_PROPERTY_REINSTALLMODE "amus")
    # Skip ICE validation — fails on systems where Windows Installer service is unavailable
    # (common in CI/sandbox environments). ICE checks are install-time validations, not build-time.
    set(CPACK_WIX_LIGHT_EXTRA_OPTIONS "-sice:ICE")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app/resources/xlsOne.ico")
        set(CPACK_WIX_PRODUCT_ICON "${CMAKE_CURRENT_SOURCE_DIR}/app/resources/xlsOne.ico")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app/resources/xlsOne_256x256.png")
        set(CPACK_WIX_PRODUCT_ICON "${CMAKE_CURRENT_SOURCE_DIR}/app/resources/xlsOne_256x256.png")
    endif()

    # Installer language selection (affects EULA text and WixUI culture).
    set(XLSONE_INSTALLER_LANGUAGE "zh_CN" CACHE STRING
        "Windows installer language: zh_CN, zh_TW, en, ja")
    set(_xlsone_lang_map_zh_CN "zh-CN")
    set(_xlsone_lang_map_zh_TW "zh-TW")
    set(_xlsone_lang_map_en    "en-US")
    set(_xlsone_lang_map_ja    "ja-JP")
    set(_xlsone_wix_culture "${_xlsone_lang_map_${XLSONE_INSTALLER_LANGUAGE}}")
    if(NOT _xlsone_wix_culture)
        message(FATAL_ERROR "Unsupported XLSONE_INSTALLER_LANGUAGE: ${XLSONE_INSTALLER_LANGUAGE}")
    endif()

    set(_xlsone_license_rtf
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/licenses/EULA_${XLSONE_INSTALLER_LANGUAGE}.rtf")
    if(NOT EXISTS "${_xlsone_license_rtf}")
        message(FATAL_ERROR "License file not found: ${_xlsone_license_rtf}")
    endif()
    set(CPACK_RESOURCE_FILE_LICENSE "${_xlsone_license_rtf}")
    set(CPACK_WIX_CULTURES "${_xlsone_wix_culture}")

    # Optional Start Menu / Desktop shortcuts via a configured Wix patch.
    if(XLSONE_INSTALLER_LANGUAGE STREQUAL "zh_CN")
        set(XLSONE_WIX_FEATURE_START_MENU_TITLE "开始菜单快捷方式")
        set(XLSONE_WIX_FEATURE_START_MENU_DESC "在开始菜单中创建 xlsOne 快捷方式。")
        set(XLSONE_WIX_FEATURE_DESKTOP_TITLE "桌面快捷方式")
        set(XLSONE_WIX_FEATURE_DESKTOP_DESC "在桌面上创建 xlsOne 快捷方式。")
        set(XLSONE_WIX_LAUNCH_CHECKBOX_TEXT "启动 xlsOne")
    elseif(XLSONE_INSTALLER_LANGUAGE STREQUAL "zh_TW")
        set(XLSONE_WIX_FEATURE_START_MENU_TITLE "開始功能表捷徑")
        set(XLSONE_WIX_FEATURE_START_MENU_DESC "在開始功能表中建立 xlsOne 捷徑。")
        set(XLSONE_WIX_FEATURE_DESKTOP_TITLE "桌面捷徑")
        set(XLSONE_WIX_FEATURE_DESKTOP_DESC "在桌面上建立 xlsOne 捷徑。")
        set(XLSONE_WIX_LAUNCH_CHECKBOX_TEXT "啟動 xlsOne")
    elseif(XLSONE_INSTALLER_LANGUAGE STREQUAL "ja")
        set(XLSONE_WIX_FEATURE_START_MENU_TITLE "スタートメニューのショートカット")
        set(XLSONE_WIX_FEATURE_START_MENU_DESC "スタートメニューに xlsOne のショートカットを作成します。")
        set(XLSONE_WIX_FEATURE_DESKTOP_TITLE "デスクトップのショートカット")
        set(XLSONE_WIX_FEATURE_DESKTOP_DESC "デスクトップに xlsOne のショートカットを作成します。")
        set(XLSONE_WIX_LAUNCH_CHECKBOX_TEXT "xlsOne を起動する")
    else()
        set(XLSONE_WIX_FEATURE_START_MENU_TITLE "Start Menu shortcut")
        set(XLSONE_WIX_FEATURE_START_MENU_DESC "Create a shortcut for xlsOne in the Start Menu.")
        set(XLSONE_WIX_FEATURE_DESKTOP_TITLE "Desktop shortcut")
        set(XLSONE_WIX_FEATURE_DESKTOP_DESC "Create a shortcut for xlsOne on the Desktop.")
        set(XLSONE_WIX_LAUNCH_CHECKBOX_TEXT "Launch xlsOne")
    endif()

    set(_xlsone_wix_patch_in
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/shortcuts.wxs.patch.in")
    set(_xlsone_wix_patch_out
        "${CMAKE_CURRENT_BINARY_DIR}/packaging/windows/shortcuts.wxs.patch")
    configure_file("${_xlsone_wix_patch_in}" "${_xlsone_wix_patch_out}" @ONLY)
    set(CPACK_WIX_PATCH_FILE "${_xlsone_wix_patch_out}")

    # Generate shortcuts.wxs from template so the launch checkbox text is localized.
    set(_xlsone_wix_shortcuts_in
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/shortcuts.wxs.in")
    set(_xlsone_wix_shortcuts_out
        "${CMAKE_CURRENT_BINARY_DIR}/packaging/windows/shortcuts.wxs")
    configure_file("${_xlsone_wix_shortcuts_in}" "${_xlsone_wix_shortcuts_out}" @ONLY)
    set(CPACK_WIX_EXTRA_SOURCES "${_xlsone_wix_shortcuts_out}")

    # Use FeatureTree so users can opt out of optional shortcuts.
    set(CPACK_WIX_UI_REF "WixUI_FeatureTree")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE" AND NOT WIN32)
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE")
endif()

if(WIN32)
    set(CPACK_GENERATOR "ZIP;WIX")
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

# Windows: bundle Qt runtime + MinGW runtime during installation/packaging.
if(WIN32)
    # Qt bin dir holds Qt5*.dll PLUS the support libs (ICU, PCRE2, double-conversion,
    # zlib) that MinGW builds of Qt load dynamically. windeployqt is unreliable about
    # copying these on MinGW, which is exactly why clean Windows machines saw
    # "missing libicuin/libicuuc/libpcre2-16-0/libdouble-conversion". So we copy
    # them deterministically below instead of trusting windeployqt alone.
    get_filename_component(_qt_bin_dir "${Qt${QT_VERSION_MAJOR}_DIR}/../../../bin" ABSOLUTE)
    # MinGW bin dir holds the compiler runtime (libgcc/libstdc++/libwinpthread).
    get_filename_component(_mingw_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)

    find_program(WINDEPLOYQT_EXECUTABLE NAMES windeployqt windeployqt-qt5
        HINTS "${_qt_bin_dir}"
        DOC "Path to windeployqt"
    )

    # 1) windeployqt: deploy the Qt runtime and discover the plugins required by
    #    the staged executable. A failure must stop packaging; publishing a
    #    partially deployed Windows application is never safe.
    if(NOT WINDEPLOYQT_EXECUTABLE)
        message(FATAL_ERROR "windeployqt not found under ${_qt_bin_dir}")
    else()
        install(CODE "
            set(_bindir \"\${CMAKE_INSTALL_PREFIX}/bin\")
            set(_exe \"\${_bindir}/xlsOneQt.exe\")
            message(STATUS \"Running windeployqt: ${WINDEPLOYQT_EXECUTABLE} --release --no-opengl-sw --no-system-d3d-compiler --dir \${_bindir} \${_exe}\")
            execute_process(
                COMMAND \"${WINDEPLOYQT_EXECUTABLE}\" --release --no-opengl-sw --no-system-d3d-compiler --dir \${_bindir} \${_exe}
                RESULT_VARIABLE _deploy_result
            )
            if(NOT _deploy_result EQUAL 0)
                message(FATAL_ERROR \"windeployqt returned non-zero exit code: \${_deploy_result}\")
            endif()
        ")
    endif()

    # 2) GUARANTEED: copy EVERY *.dll from the Qt bin dir into the package bin/.
    #    This deterministically includes libicuin*/libicuuc*/libicudt*/libpcre2-16-0/
    #    libdouble-conversion/zlib1 (and any Qt5*.dll windeployqt may skip),
    #    eliminating the "missing DLL on clean Windows" failures.
    if(EXISTS "${_qt_bin_dir}")
        install(DIRECTORY "${_qt_bin_dir}/"
            DESTINATION bin
            FILES_MATCHING PATTERN "*.dll")
    else()
        message(FATAL_ERROR "Qt bin dir not found at ${_qt_bin_dir} — cannot bundle Qt runtime DLLs")
    endif()

    # 3) MinGW compiler runtime. Note: windeployqt --compiler-runtime only handles
    #    MSVC redistributables, so we copy these explicitly for MinGW builds.
    set(_mingw_runtime_dlls
        libgcc_s_seh-1.dll
        libstdc++-6.dll
        libwinpthread-1.dll)
    foreach(_dll ${_mingw_runtime_dlls})
        install(FILES "${_mingw_bin_dir}/${_dll}"
            DESTINATION bin
            OPTIONAL)
    endforeach()

    # 4) Qt plugins (platforms + imageformats + styles + iconengines + tls) as a safety net.
    #    MSYS2 layout: <prefix>/share/qt5/plugins/; aqt layout: <prefix>/plugins/
    set(_qt_plugin_search_dirs
        "${Qt${QT_VERSION_MAJOR}_DIR}/../../../share/qt${QT_VERSION_MAJOR}/plugins"
        "${Qt${QT_VERSION_MAJOR}_DIR}/../../../plugins")
    foreach(_qpd_raw ${_qt_plugin_search_dirs})
        # Normalize to forward slashes. Qt6_DIR arrives on Windows with
        # backslashes (e.g. C:\Qt\6.11.1\...); an un-normalized path would be
        # written into cmake_install.cmake and fail to re-parse ("\Q" escape).
        get_filename_component(_qpd "${_qpd_raw}" ABSOLUTE)
        if(EXISTS "${_qpd}/platforms")
            install(DIRECTORY "${_qpd}/platforms/"
                DESTINATION bin/platforms
                FILES_MATCHING PATTERN "qwindows*.dll")
            install(DIRECTORY "${_qpd}/imageformats/"
                DESTINATION bin/imageformats
                FILES_MATCHING PATTERN "*.dll")
            if(EXISTS "${_qpd}/styles")
                install(DIRECTORY "${_qpd}/styles/"
                    DESTINATION bin/styles
                    FILES_MATCHING PATTERN "*.dll")
            endif()
            if(EXISTS "${_qpd}/iconengines")
                install(DIRECTORY "${_qpd}/iconengines/"
                    DESTINATION bin/iconengines
                    FILES_MATCHING PATTERN "*.dll")
            endif()
            # TLS backends: include OpenSSL backend + cert-only backend as fallback
            if(EXISTS "${_qpd}/tls")
                install(DIRECTORY "${_qpd}/tls/"
                    DESTINATION bin/tls
                    FILES_MATCHING PATTERN "*.dll")
            endif()
            break()
        endif()
    endforeach()

    # 5) OpenSSL runtime DLLs required by qopensslbackend.dll for HTTPS on clean Windows.
    #    aqt sometimes installs these next to Qt6*.dll; MSYS2/MinGW places them under
    #    <mingw>/opt/bin. We copy from both locations as fallback.
    foreach(_ssl_dll libssl-3-x64.dll libcrypto-3-x64.dll libssl-1_1-x64.dll libcrypto-1_1-x64.dll)
        install(FILES "${_qt_bin_dir}/${_ssl_dll}"
            DESTINATION bin
            OPTIONAL)
        install(FILES "${_mingw_bin_dir}/opt/bin/${_ssl_dll}"
            DESTINATION bin
            OPTIONAL)
    endforeach()
endif()

include(CPack)
