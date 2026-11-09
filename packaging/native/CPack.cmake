# This file is only included with -DLURVIKO_ENABLE_NATIVE_PACKAGES=ON.
# Never generate DEB and RPM on the same build host and claim they are tested:
# build and validate each in its matching distribution container/VM.
if(NOT LURVIKO_PACKAGED_BUILD)
    message(FATAL_ERROR "Native packages require -DLURVIKO_PACKAGED_BUILD=ON")
endif()
if(LURVIKO_INSTALL_SYSTEM_FILEMANAGER1)
    message(FATAL_ERROR "Native packages require -DLURVIKO_INSTALL_SYSTEM_FILEMANAGER1=OFF")
endif()
if(NOT CMAKE_INSTALL_PREFIX STREQUAL "/usr")
    message(FATAL_ERROR "Native packages must use -DCMAKE_INSTALL_PREFIX=/usr")
endif()

set(CPACK_PACKAGE_NAME "lurviko")
set(CPACK_PACKAGE_VENDOR "Lurviko contributors")
set(CPACK_PACKAGE_CONTACT "Lurviko maintainers <gkhn.gurbuz@hotmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Qt Quick file manager for Linux")
set(CPACK_PACKAGE_DESCRIPTION
    "Lurviko is a Qt Quick file manager with media libraries, cloud storage and an encrypted vault.")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/G-grbz/Lurviko")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "lurviko-${PROJECT_VERSION}-1-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_STRIP_FILES OFF)
set(CPACK_DEBIAN_PACKAGE_NAME "lurviko")
set(CPACK_DEBIAN_PACKAGE_RELEASE "1")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS OFF)
# dpkg-shlibdeps sees linked libraries but not QML runtime imports or optional
# external tools. Keep these as explicit dependencies in the native .deb.
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "qml6-module-qtquick (>= 6.8), qml6-module-qtquick-controls (>= 6.8), qml6-module-qtquick-layouts (>= 6.8), qml6-module-qtquick-window (>= 6.8), qml6-module-qtquick-templates (>= 6.8), qml6-module-qtquick-effects (>= 6.8), qml6-module-qtquick-dialogs (>= 6.8), qml6-module-qtquick-shapes (>= 6.8), qml6-module-qtqml-models (>= 6.8), qml6-module-qtcore (>= 6.8), qml6-module-qtmultimedia (>= 6.8), qml6-module-qtqml-workerscript (>= 6.8), kio6, libqt6sql6-sqlite (>= 6.8), libxcb-cursor0, qt6-qpa-plugins (>= 6.8), qt6-svg-plugins (>= 6.8), ffmpeg")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "kio-extras, qt6-wayland, python3")
set(CPACK_RPM_PACKAGE_NAME "lurviko")
set(CPACK_RPM_PACKAGE_RELEASE "1")
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later AND CC-BY-4.0")
set(CPACK_RPM_PACKAGE_GROUP "Applications/File")
set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)
# Fedora's package names differ from Debian's. Automatic ELF requires don't
# detect dynamic QML imports, so list the corresponding runtime families.
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtdeclarative >= 6.8, qt6-qtmultimedia >= 6.8, qt6-qtbase-gui >= 6.8")
set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
include(CPack)
