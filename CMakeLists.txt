cmake_minimum_required(VERSION 3.21)
project(GFile VERSION 0.5.52 LANGUAGES CXX)

include(GNUInstallDirs)

# Desktop launcher profile ----------------------------------------------------
# Select with e.g. -DGFILE_LAUNCH_PROFILE=nvidia
# Available profiles: default, wayland, wayland-opengl, nvidia, x11,
# x11-opengl, nvidia-x11, custom.
set(GFILE_LAUNCH_PROFILE "default" CACHE STRING
    "Desktop launcher profile: default, wayland, wayland-opengl, nvidia, x11, x11-opengl, nvidia-x11, custom")
set_property(CACHE GFILE_LAUNCH_PROFILE PROPERTY STRINGS
    default wayland wayland-opengl nvidia x11 x11-opengl nvidia-x11 custom)

set(GFILE_CUSTOM_EXEC "" CACHE STRING
    "Custom .desktop Exec command used when GFILE_LAUNCH_PROFILE=custom")

if(GFILE_LAUNCH_PROFILE STREQUAL "default")
    set(GFILE_DESKTOP_EXEC "g-file %U")
elseif(GFILE_LAUNCH_PROFILE STREQUAL "wayland")
    set(GFILE_DESKTOP_EXEC "/usr/bin/env QT_QPA_PLATFORM=wayland g-file %U")
elseif(GFILE_LAUNCH_PROFILE STREQUAL "wayland-opengl")
    set(GFILE_DESKTOP_EXEC "/usr/bin/env QT_QPA_PLATFORM=wayland QSG_RHI_BACKEND=opengl g-file %U")
elseif(GFILE_LAUNCH_PROFILE STREQUAL "nvidia")
    set(GFILE_DESKTOP_EXEC "/usr/bin/env QT_QPA_PLATFORM=wayland QSG_RHI_BACKEND=opengl QSG_INFO=1 __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia g-file %U")
elseif(GFILE_LAUNCH_PROFILE STREQUAL "x11")
    set(GFILE_DESKTOP_EXEC "/usr/bin/env QT_QPA_PLATFORM=xcb g-file %U")
elseif(GFILE_LAUNCH_PROFILE STREQUAL "x11-opengl")
    set(GFILE_DESKTOP_EXEC "/usr/bin/env QT_QPA_PLATFORM=xcb QSG_RHI_BACKEND=opengl g-file %U")
elseif(GFILE_LAUNCH_PROFILE STREQUAL "nvidia-x11")
    set(GFILE_DESKTOP_EXEC "/usr/bin/env QT_QPA_PLATFORM=xcb QSG_RHI_BACKEND=opengl QSG_INFO=1 __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia g-file %U")
elseif(GFILE_LAUNCH_PROFILE STREQUAL "custom")
    if(GFILE_CUSTOM_EXEC STREQUAL "")
        message(FATAL_ERROR
            "GFILE_LAUNCH_PROFILE=custom requires -DGFILE_CUSTOM_EXEC=\"...\"")
    endif()
    set(GFILE_DESKTOP_EXEC "${GFILE_CUSTOM_EXEC}")
else()
    message(FATAL_ERROR
        "Unknown GFILE_LAUNCH_PROFILE='${GFILE_LAUNCH_PROFILE}'. "
        "Choose: default, wayland, wayland-opengl, nvidia, x11, x11-opengl, nvidia-x11, custom")
endif()

message(STATUS "g-File desktop launch profile: ${GFILE_LAUNCH_PROFILE}")
message(STATUS "g-File desktop Exec: ${GFILE_DESKTOP_EXEC}")

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/packaging/g-file.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/g-file.desktop
    @ONLY
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.9 REQUIRED COMPONENTS Quick QuickControls2 Network NetworkAuth Concurrent Widgets DBus Multimedia)
find_package(OpenSSL 3.2 REQUIRED COMPONENTS Crypto)
find_package(KF6KIO REQUIRED)
find_package(KF6Service REQUIRED)
find_package(KF6Wallet REQUIRED)
find_package(KF6IconThemes REQUIRED)
find_package(KF6Archive REQUIRED)
find_package(KF6WindowSystem REQUIRED)

qt_standard_project_setup(REQUIRES 6.9)
qt_policy(SET QTP0004 NEW)

set_source_files_properties(qml/AppTheme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_executable(g-file
    src/main.cpp
    src/popupdismissfilter.cpp
    src/popupdismissfilter.h
    src/storagemodel.cpp
    src/storagemodel.h
    src/directorymodel.cpp
    src/directorymodel.h
    src/fileoperations.cpp
    src/fileoperations.h
    src/favoritesmodel.cpp
    src/favoritesmodel.h
    src/quickaccessmodel.cpp
    src/quickaccessmodel.h
    src/contentindexmodel.cpp
    src/contentindexmodel.h
    src/languagemanager.cpp
    src/languagemanager.h
    src/cloudauthmanager.cpp
    src/cloudauthmanager.h
    src/thumbnailprovider.cpp
    src/thumbnailprovider.h
    src/systemiconprovider.cpp
    src/admineditmanager.cpp
    src/admineditmanager.h
    src/openwithmodel.cpp
    src/openwithmodel.h
    src/servicemenumodel.cpp
    src/servicemenumodel.h
    src/googledrivemanager.cpp
    src/googledrivemanager.h
    src/onedrivemanager.cpp
    src/onedrivemanager.h
    src/iconpickermanager.cpp
    src/iconpickermanager.h
    src/filepropertiesmanager.cpp
    src/filepropertiesmanager.h
    src/filemanager1interface.cpp
    src/filemanager1interface.h
    src/trashmonitor.cpp
    src/trashmonitor.h
    src/tmdbmetadatamanager.cpp
    src/tmdbmetadatamanager.h
    src/dlnamediamanager.cpp
    src/dlnamediamanager.h
    src/mpriscontroller.cpp
    src/mpriscontroller.h
    src/playbackresumemanager.cpp
    src/playbackresumemanager.h
    src/videoplayerinputmanager.cpp
    src/videoplayerinputmanager.h
    src/frameratematchclient.cpp
    src/frameratematchclient.h
    src/subtitleaimanager.cpp
    src/subtitleaimanager.h
    src/subtitlestylemanager.cpp
    src/subtitlestylemanager.h
    src/privatevaultmanager.cpp
    src/privatevaultmanager.h
)

target_compile_definitions(g-file PRIVATE
    GFILE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

qt_add_qml_module(g-file
    URI GFile.App
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/AppTheme.qml
        qml/GButton.qml
        qml/GToolButton.qml
        qml/GTextField.qml
        qml/GSlider.qml
        qml/GSwitch.qml
        qml/GCheckBox.qml
        qml/GComboBox.qml
        qml/GSpinBox.qml
        qml/GProgressBar.qml
        qml/GBusyIndicator.qml
        qml/GGlassPanel.qml
        qml/GViewerIcon.qml
        qml/components/AppSidebar.qml
        qml/components/SidebarItem.qml
        qml/components/BrowserPane.qml
        qml/components/GMenu.qml
        qml/components/GPopupDismissHandler.qml
        qml/components/GToolTip.qml
        qml/components/GModalSurface.qml
        qml/components/GModalPopup.qml
        qml/components/GModalButton.qml
        qml/components/DragPreview.qml
        qml/components/GMenuItem.qml
        qml/components/GMenuSeparator.qml
        qml/components/TextEditMenu.qml
        qml/components/CloudCard.qml
        qml/components/DiskCard.qml
        qml/components/QuickAccessTile.qml
        qml/components/IconSizeControl.qml
        qml/components/CrispIcon.qml
        qml/components/DeferredThumbnail.qml
        qml/components/SectionHeader.qml
        qml/components/StorageSummaryRow.qml
        qml/components/TopSearchBar.qml
        qml/components/ContentCategoryTile.qml
        qml/components/MediaGalleryCard.qml
        qml/components/MediaAlbumCard.qml
        qml/components/MediaHoverPreview.qml
        qml/components/TmdbHoverPreview.qml
        qml/components/TmdbPauseOverlay.qml
        qml/components/DlnaMediaView.qml
        qml/components/MediaGalleryView.qml
        qml/components/MediaRootsEditor.qml
        qml/components/CategoryGroupedView.qml
        qml/components/PhotoViewer.qml
        qml/components/VideoViewer.qml
        qml/components/MusicPlayer.qml
        qml/components/CategoryDateHeader.qml
        qml/pages/HomePage.qml
        qml/pages/ContentCategoryPage.qml
        qml/pages/BrowsePage.qml
        qml/pages/PrivateVaultPage.qml
    RESOURCES
        assets/icons/folder.svg
        assets/icons/file.svg
        assets/icons/image.svg
        assets/icons/video.svg
        assets/icons/audio.svg
        assets/icons/archive.svg
        assets/icons/pdf.svg
        assets/icons/drive.svg
        assets/icons/cloud-google.svg
        assets/icons/cloud-onedrive.svg
        assets/icons/cloud-dropbox.svg
        assets/icons/cloud-nextcloud.svg
        assets/icons/cloud-owncloud.svg
        assets/icons/cloud-mega.svg
        assets/icons/cloud-pcloud.svg
        assets/icons/trash.svg
        assets/icons/trashfull.svg
        assets/icons/network.svg
        assets/icons/root.svg
        assets/icons/favorite.svg
        assets/icons/private.svg
        assets/icons/logo.png
        assets/icons/dashboard.svg
        assets/icons/home-folder.svg
        assets/icons/desktop.svg
        assets/icons/downloads.svg
        assets/icons/documents.svg
        assets/icons/settings.svg
        assets/icons/cloud.svg
        assets/icons/plus.svg
        assets/icons/nav-back.svg
        assets/icons/nav-forward.svg
        assets/icons/nav-up.svg
        assets/icons/chevron-left.svg
        assets/icons/chevron-right.svg
        assets/icons/close-ui.svg
        assets/icons/edit.svg
        assets/icons/moon.svg
        assets/icons/sun.svg
        assets/icons/info.svg
        assets/icons/eye.svg
        assets/icons/search.svg
        assets/icons/split-view.svg
        assets/icons/view-layout.svg
        assets/icons/history.svg
        assets/icons/appimage.svg
        assets/icons/filelight.svg
        assets/icons/viewer-close.svg
        assets/icons/viewer-prev.svg
        assets/icons/viewer-next.svg
        assets/icons/viewer-zoom-in.svg
        assets/icons/viewer-zoom-out.svg
        assets/icons/viewer-fit.svg
        assets/icons/viewer-rotate-left.svg
        assets/icons/viewer-rotate-right.svg
        assets/icons/viewer-play.svg
        assets/icons/viewer-pause.svg
        assets/icons/viewer-folder.svg
        assets/icons/viewer-external.svg
        assets/icons/viewer-fullscreen.svg
        assets/icons/viewer-fullscreen-exit.svg
        assets/icons/viewer-rewind.svg
        assets/icons/viewer-forward.svg
        assets/icons/viewer-volume.svg
        assets/icons/viewer-volume-muted.svg
        assets/icons/music-shuffle.svg
        assets/icons/music-repeat.svg
        assets/icons/music-repeat-one.svg
        assets/icons/music-queue.svg
        assets/icons/music-playlist.svg
        assets/icons/music-refresh.svg
        assets/icons/music-minimize.svg
        assets/icons/music-expand.svg
        assets/icons/music-stop.svg
        assets/icons/music-note.svg
        assets/icons/music-chevron-up.svg
)

target_link_libraries(g-file PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
    Qt6::NetworkAuth
    Qt6::Concurrent
    Qt6::Widgets
    Qt6::DBus
    Qt6::Multimedia
    KF6::KIOCore
    KF6::KIOGui
    KF6::KIOWidgets
    KF6::Service
    KF6::Wallet
    KF6::IconWidgets
    KF6::Archive
    KF6::WindowSystem
    OpenSSL::Crypto
)

install(TARGETS g-file
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/g-file.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/org.freedesktop.FileManager1.service DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
install(FILES assets/icons/logo.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps RENAME g-file.png)
install(FILES assets/icons/logo.png DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps RENAME g-file.png)
install(DIRECTORY tools/subtitle-ai/ DESTINATION ${CMAKE_INSTALL_DATADIR}/g-File/subtitle-ai
    FILES_MATCHING
        PATTERN "*.py"
        PATTERN "README.md"
        PATTERN "requirements*.txt"
        PATTERN "LICENSE"
)

install(DIRECTORY tools/gtmce/ DESTINATION ${CMAKE_INSTALL_DATADIR}/g-File/gtmce
    FILES_MATCHING PATTERN "*.py" PATTERN "README.md" PATTERN "LICENSE"
)
