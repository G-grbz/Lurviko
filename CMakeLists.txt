cmake_minimum_required(VERSION 3.21)
project(Lurviko VERSION 1.3.0 LANGUAGES CXX)

include(GNUInstallDirs)

# Desktop launcher profile ----------------------------------------------------
# Select with e.g. -DLURVIKO_LAUNCH_PROFILE=nvidia
# Available profiles: default, wayland, wayland-opengl, nvidia, x11,
# x11-opengl, nvidia-x11, custom.
set(LURVIKO_LAUNCH_PROFILE "default" CACHE STRING
    "Desktop launcher profile: default, wayland, wayland-opengl, nvidia, x11, x11-opengl, nvidia-x11, custom")
set_property(CACHE LURVIKO_LAUNCH_PROFILE PROPERTY STRINGS
    default wayland wayland-opengl nvidia x11 x11-opengl nvidia-x11 custom)

set(LURVIKO_CUSTOM_EXEC "" CACHE STRING
    "Custom .desktop Exec command used when LURVIKO_LAUNCH_PROFILE=custom")

if(LURVIKO_LAUNCH_PROFILE STREQUAL "default")
    set(LURVIKO_DESKTOP_EXEC "lurviko %U")
elseif(LURVIKO_LAUNCH_PROFILE STREQUAL "wayland")
    set(LURVIKO_DESKTOP_EXEC "/usr/bin/env QT_QPA_PLATFORM=wayland lurviko %U")
elseif(LURVIKO_LAUNCH_PROFILE STREQUAL "wayland-opengl")
    set(LURVIKO_DESKTOP_EXEC "/usr/bin/env QT_QPA_PLATFORM=wayland QSG_RHI_BACKEND=opengl lurviko %U")
elseif(LURVIKO_LAUNCH_PROFILE STREQUAL "nvidia")
    set(LURVIKO_DESKTOP_EXEC "/usr/bin/env QT_QPA_PLATFORM=wayland QSG_RHI_BACKEND=opengl QSG_INFO=1 __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia lurviko %U")
elseif(LURVIKO_LAUNCH_PROFILE STREQUAL "x11")
    set(LURVIKO_DESKTOP_EXEC "/usr/bin/env QT_QPA_PLATFORM=xcb lurviko %U")
elseif(LURVIKO_LAUNCH_PROFILE STREQUAL "x11-opengl")
    set(LURVIKO_DESKTOP_EXEC "/usr/bin/env QT_QPA_PLATFORM=xcb QSG_RHI_BACKEND=opengl lurviko %U")
elseif(LURVIKO_LAUNCH_PROFILE STREQUAL "nvidia-x11")
    set(LURVIKO_DESKTOP_EXEC "/usr/bin/env QT_QPA_PLATFORM=xcb QSG_RHI_BACKEND=opengl QSG_INFO=1 __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia lurviko %U")
elseif(LURVIKO_LAUNCH_PROFILE STREQUAL "custom")
    if(LURVIKO_CUSTOM_EXEC STREQUAL "")
        message(FATAL_ERROR
            "LURVIKO_LAUNCH_PROFILE=custom requires -DLURVIKO_CUSTOM_EXEC=\"...\"")
    endif()
    set(LURVIKO_DESKTOP_EXEC "${LURVIKO_CUSTOM_EXEC}")
else()
    message(FATAL_ERROR
        "Unknown LURVIKO_LAUNCH_PROFILE='${LURVIKO_LAUNCH_PROFILE}'. "
        "Choose: default, wayland, wayland-opengl, nvidia, x11, x11-opengl, nvidia-x11, custom")
endif()

message(STATUS "Lurviko desktop launch profile: ${LURVIKO_LAUNCH_PROFILE}")
message(STATUS "Lurviko desktop Exec: ${LURVIKO_DESKTOP_EXEC}")

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/packaging/lurviko.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/lurviko.desktop
    @ONLY
)

configure_file(cmake/uninstall.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/uninstall.cmake @ONLY)
add_custom_target(uninstall
    COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/uninstall.cmake"
    COMMENT "Removing installed Lurviko files (keeping user settings and data)"
    VERBATIM
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

add_executable(lurviko-access-helper src/fileaccesshelper.cpp src/fileaccesssecurity.cpp)
target_link_libraries(lurviko-access-helper PRIVATE Qt6::Core Qt6::Concurrent)
file(RELATIVE_PATH LURVIKO_ACCESS_HELPER_RELATIVE_PATH
    "${CMAKE_INSTALL_FULL_BINDIR}" "${CMAKE_INSTALL_FULL_LIBEXECDIR}/lurviko-access-helper")

qt_standard_project_setup(REQUIRES 6.9)
qt_policy(SET QTP0004 NEW)

set_source_files_properties(qml/AppTheme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_executable(lurviko
    src/main.cpp
    src/appmigration.cpp
    src/appmigration.h
    src/updatechecker.cpp
    src/updatechecker.h
    src/videochapterreader.cpp
    src/videochapterreader.h
    src/popupdismissfilter.cpp
    src/popupdismissfilter.h
    src/storagemodel.cpp
    src/storagemodel.h
    src/millernavigationmodel.cpp
    src/millernavigationmodel.h
    src/directorymodel.cpp
    src/directorymodel.h
    src/gitstatuscache.cpp
    src/gitstatuscache.h
    src/fileoperations.cpp
    src/fileoperations.h
    src/archiveinspectionjob.cpp
    src/archiveinspectionjob.h
    src/archiveoutput.h
    src/favoritesmodel.cpp
    src/favoritesmodel.h
    src/quickaccessmodel.cpp
    src/quickaccessmodel.h
    src/iconreference.h
    src/contentindexmodel.cpp
    src/contentindexmodel.h
    src/languagemanager.cpp
    src/languagemanager.h
    src/keyboardshortcutmanager.cpp
    src/keyboardshortcutmanager.h
    src/cloudauthmanager.cpp
    src/cloudauthmanager.h
    src/thumbnailprovider.cpp
    src/thumbnailprovider.h
    src/systemiconprovider.cpp
    src/bundlediconprovider.cpp
    src/bundlediconprovider.h
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
    src/fileaccesssecurity.cpp
    src/fileaccesssecurity.h
    src/filepropertiesaccess.cpp
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
    src/musicmetadatamanager.cpp
    src/musicmetadatamanager.h
    src/musiclibrarymanager.cpp
    src/musiclibrarymanager.h
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
    src/vaultsecurity.cpp
    src/vaultsecurity.h
    src/vaultrecovery.cpp
    src/vaultrecovery.h
    src/archivearguments.h
    src/privatevaultmanager.h
)

target_compile_definitions(lurviko PRIVATE
    LURVIKO_ACCESS_HELPER_RELATIVE_PATH="${LURVIKO_ACCESS_HELPER_RELATIVE_PATH}"
    LURVIKO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    LURVIKO_VERSION="${PROJECT_VERSION}"
)

# Physical artwork is grouped for editing; stable resource aliases keep saved
# icon names and all QML/native renderers independent from the source folders.
file(GLOB LURVIKO_ICON_FILES CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/png/*.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/symbolic/*.svg")
file(GLOB_RECURSE LURVIKO_COLOR_ICON_FILES CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/color/*.svg")
list(APPEND LURVIKO_ICON_FILES ${LURVIKO_COLOR_ICON_FILES})
list(SORT LURVIKO_ICON_FILES)
set(LURVIKO_ICON_NAMES)
foreach(LURVIKO_ICON_FILE IN LISTS LURVIKO_ICON_FILES)
    get_filename_component(LURVIKO_ICON_NAME "${LURVIKO_ICON_FILE}" NAME)
    if(LURVIKO_ICON_NAME IN_LIST LURVIKO_ICON_NAMES)
        message(FATAL_ERROR "Duplicate bundled icon filename: ${LURVIKO_ICON_NAME}")
    endif()
    list(APPEND LURVIKO_ICON_NAMES "${LURVIKO_ICON_NAME}")
    set_source_files_properties("${LURVIKO_ICON_FILE}" PROPERTIES
        QT_RESOURCE_ALIAS "assets/icons/${LURVIKO_ICON_NAME}")
endforeach()

# Rebuild reusable raster sizes automatically when an editable folder SVG
# changes. No runtime SVG rendering, recoloring or external image tool needed.
add_executable(lurviko-render-folder-icons tools/render-folder-icons.cpp)
target_link_libraries(lurviko-render-folder-icons PRIVATE Qt6::Gui)
set(LURVIKO_FOLDER_RASTERS)
foreach(LURVIKO_ICON_FILE IN LISTS LURVIKO_COLOR_ICON_FILES)
    get_filename_component(LURVIKO_ICON_NAME "${LURVIKO_ICON_FILE}" NAME)
    if(NOT LURVIKO_ICON_NAME MATCHES "^(folder|(blue|green|orange|pink|purple|red|turquoise|yellow)-(folder|documents|download|music|picture|video))\\.svg$")
        continue()
    endif()
    set(LURVIKO_FOLDER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/assets/folder-raster/${LURVIKO_ICON_NAME}")
    set(LURVIKO_FOLDER_OUTPUTS)
    foreach(LURVIKO_EXTENT 64 128 192 256 320 384 448 512)
        set(LURVIKO_RASTER "${LURVIKO_FOLDER_OUTPUT_DIR}/${LURVIKO_EXTENT}.png")
        list(APPEND LURVIKO_FOLDER_OUTPUTS "${LURVIKO_RASTER}")
        set_source_files_properties("${LURVIKO_RASTER}" PROPERTIES GENERATED TRUE
            QT_RESOURCE_ALIAS "assets/folder-raster/${LURVIKO_ICON_NAME}/${LURVIKO_EXTENT}.png")
    endforeach()
    add_custom_command(OUTPUT ${LURVIKO_FOLDER_OUTPUTS}
        COMMAND lurviko-render-folder-icons "${CMAKE_CURRENT_SOURCE_DIR}/${LURVIKO_ICON_FILE}" "${LURVIKO_FOLDER_OUTPUT_DIR}"
        DEPENDS lurviko-render-folder-icons "${CMAKE_CURRENT_SOURCE_DIR}/${LURVIKO_ICON_FILE}"
        VERBATIM)
    list(APPEND LURVIKO_FOLDER_RASTERS ${LURVIKO_FOLDER_OUTPUTS})
endforeach()
qt_add_resources(lurviko folder_rasters PREFIX "/qt/qml/Lurviko/App" FILES ${LURVIKO_FOLDER_RASTERS})

qt_add_qml_module(lurviko
    URI Lurviko.App
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/AppTheme.qml
        qml/FolderArtwork.js
        qml/GButton.qml
        qml/GToolButton.qml
        qml/GTextField.qml
        qml/GSlider.qml
        qml/GSwitch.qml
        qml/GCheckBox.qml
        qml/GComboBox.qml
        qml/GAccountPicker.qml
        qml/GSpinBox.qml
        qml/GProgressBar.qml
        qml/GBusyIndicator.qml
        qml/GGlassPanel.qml
        qml/GViewerIcon.qml
        qml/components/AppSidebar.qml
        qml/components/SidebarItem.qml
        qml/components/BrowserPane.qml
        qml/components/MillerColumnsView.qml
        qml/components/GitStatusBadge.qml
        qml/components/GMenu.qml
        qml/components/GPopupWheelScroll.qml
        qml/components/GPopupDismissHandler.qml
        qml/components/GToolTip.qml
        qml/components/GModalSurface.qml
        qml/components/GModalPopup.qml
        qml/components/KeyboardShortcutsDialog.qml
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
        qml/components/SubtitleBackdrop.qml
        qml/components/SubtitleLabel.qml
        qml/components/MusicPlayer.qml
        qml/components/MusicExpandedView.qml
        qml/components/MusicArtists.js
        qml/components/MusicArtistLinks.qml
        qml/components/MusicPlaylistDialog.qml
        qml/components/MediaSession.qml
        qml/components/MusicLibraryView.qml
        qml/components/MusicHoverPreview.qml
        qml/components/MusicArtwork.qml
        qml/components/CategoryDateHeader.qml
        qml/pages/HomePage.qml
        qml/pages/ContentCategoryPage.qml
        qml/pages/BrowsePage.qml
        qml/pages/PrivateVaultPage.qml
        qml/components/VaultAttemptStatus.qml
    RESOURCES
        ${LURVIKO_ICON_FILES}
)

target_link_libraries(lurviko PRIVATE
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

install(TARGETS lurviko-access-helper RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
install(TARGETS lurviko
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/lurviko.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/org.freedesktop.FileManager1.service DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
install(FILES assets/icons/png/logo.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps RENAME lurviko.png)
install(FILES assets/icons/png/logo.png DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps RENAME lurviko.png)
install(FILES assets/LICENSE-FontAwesome.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/Lurviko/licenses)
install(FILES tools/recover-vault.py DESTINATION ${CMAKE_INSTALL_DATADIR}/Lurviko/tools)
install(FILES docs/vault-recovery.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/Lurviko)
install(DIRECTORY tools/subtitle-ai/ DESTINATION ${CMAKE_INSTALL_DATADIR}/Lurviko/subtitle-ai
    FILES_MATCHING
        PATTERN "*.py"
        PATTERN "README.md"
        PATTERN "requirements*.txt"
        PATTERN "model-revisions.json"
        PATTERN "LICENSE"
)
