cmake_minimum_required(VERSION 3.24)

project(xlsOneQt
    VERSION 0.1.0
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

add_subdirectory(core)
add_subdirectory(app)
add_subdirectory(tests)
add_subdirectory(tools)

set(CPACK_PACKAGE_NAME "xlsone")
set(CPACK_PACKAGE_VENDOR "xlsOne")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Cross-platform C++/Qt port of xlsOne")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "xlsOne")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE")
endif()

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
elseif(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "xlsOne")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

    install(FILES packaging/linux/xlsone.desktop
        DESTINATION share/applications
    )
    install(FILES packaging/linux/xlsone-mime.xml
        DESTINATION share/mime/packages
        RENAME xlsone.xml
    )
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../App/xlsOneMacApp/Assets.xcassets/AppIcon.appiconset/icon_256x256.png")
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../App/xlsOneMacApp/Assets.xcassets/AppIcon.appiconset/icon_256x256.png"
            DESTINATION share/icons/hicolor/256x256/apps
            RENAME xlsone.png
        )
    endif()
endif()

include(CPack)
