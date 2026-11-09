#!/usr/bin/env python3
"""Source regression: disk-image mounting wiring.

Static check, complementing the runtime Qt tests (which require Qt 6): the
context menu, sidebar and Explore must show mounted images only while at least
one is mounted, and mounted images must not also appear as ordinary disks.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
read = lambda p: (root / p).read_text()
cmake, main = read('CMakeLists.txt'), read('src/main.cpp')
cpp, header = read('src/diskimagemanager.cpp'), read('src/diskimagemanager.h')
storage = read('src/storagemodel.cpp')
pane, sidebar = read('qml/components/BrowserPane.qml'), read('qml/components/AppSidebar.qml')
shell, home = read('qml/components/SidebarShell.qml'), read('qml/pages/HomePage.qml')
card, app = read('qml/components/DiskCard.qml'), read('qml/Main.qml')
lang = read('src/languagemanager.cpp')

# Build and registration. The singleton must sit before PlaybackResumeManager:
# the QML test runners cut the registration block there and keep what precedes.
assert 'src/diskimagemanager.cpp' in cmake and 'src/diskimagemanager.h' in cmake
reg = main.index('"DiskImages"')
assert reg < main.index('    PlaybackResumeManager playbackResumeManager;')
assert reg > main.index('    qmlRegisterType<StorageModel>')

# Formats: loop-mountable images use UDisks; VM formats pass an explicit qemu format.
for suffix in ('iso', 'img', 'squashfs', 'erofs', 'ext4'):
    assert re.search(r'\{"%s", Backend::Loop' % suffix, cpp), suffix
for suffix, fmt in (('qcow2', 'qcow2'), ('vmdk', 'vmdk'), ('vdi', 'vdi'), ('vhd', 'vpc'), ('vhdx', 'vhdx')):
    assert '{"%s", Backend::Nbd, "%s"' % (suffix, fmt) in cpp, suffix
assert '"--format="' in cpp.replace("QStringLiteral(", "").replace(")", "") or 'QStringLiteral("--format=")' in cpp

# Mounted images are not duplicated as ordinary disks.
assert 'topType == QStringLiteral("loop")' in storage and 'startsWith(QStringLiteral("nbd"))' in storage

# Context menu: Mount when detached, Unmount when attached, local single files only.
assert 'DiskImages.canMount(selectedLocalPath)' in pane
assert re.search(r'mountDiskImageAction.*?visible: root\.selectedCanMountImage && !root\.selectedImageMounted', pane, re.S)
assert re.search(r'unmountDiskImageAction.*?visible: root\.selectedCanMountImage && root\.selectedImageMounted', pane, re.S)
assert 'DiskImages.mountImage(root.selectedLocalPath)' in pane
assert 'DiskImages.unmountImage(root.selectedLocalPath)' in pane

# Sidebar: own section, hidden without images, right-click removes (unmounts).
assert 'visible: sidebar.showsSection("images") && DiskImages.count > 0' in sidebar
assert 'removeText: lang.t("unmount")' in sidebar
assert 'onRemoveRequested: DiskImages.unmountImage(imagePath)' in sidebar
assert 'if (DiskImages.count > 0)' in shell and 'key: "images"' in shell

# Explore: header and grid are both hidden without images; same DiskCard as Disks.
assert home.count('visible: DiskImages.count > 0') == 2
assert 'model: DiskImages' in home and 'delegate: DiskCard' in home[home.index('model: DiskImages'):]
for flag in ('diskReorder: false', 'favoritesEnabled: false', 'editable: false', 'unmountInMenu: true'):
    assert flag in home, flag
assert 'enabled: card.diskReorder' in card

# Feedback and live updates.
assert 'target: DiskImages' in app and 'function onTotalsChanged() { DiskImages.refresh() }' in app
for key in ('mount_image', 'unmount_image', 'mounted_images', 'image_mount_failed', 'image_unmount_failed'):
    assert '{"%s"' % key in lang, key
    assert 'lang.t("%s")' % key in (pane + sidebar + home + app), key

# Discovery is read from the running system, not persisted.
assert 'QSettings' not in cpp and 'backing_file' in cpp
print('disk image mounting source checks passed')
