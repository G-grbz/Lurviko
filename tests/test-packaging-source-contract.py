#!/usr/bin/env python3
"""Fast static guards for experimental packaging; no Qt build required."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PackagingContracts(unittest.TestCase):
    def test_distribution_mode_is_opt_in_and_safe(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertIn('option(LURVIKO_PACKAGED_BUILD', cmake)
        self.assertIn('option(LURVIKO_ENABLE_NATIVE_PACKAGES', cmake)
        self.assertIn('if(LURVIKO_INSTALL_SYSTEM_FILEMANAGER1)', cmake)
        self.assertIn('DESTINATION ${CMAKE_INSTALL_DATADIR}/Lurviko/dbus-1/services', cmake)
        self.assertIn('LURVIKO_SOURCE_DIR="${LURVIKO_EMBED_SOURCE_DIR}"', cmake)
    def test_staging_preserves_git_and_arch_behavior(self):
        arch = (ROOT / 'packaging/aur/lurviko/PKGBUILD').read_text()
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        version = re.search(r'project\(Lurviko VERSION ([0-9.]+)', cmake).group(1)
        self.assertIn(f'pkgver={version}\n', arch)
        self.assertIn('pkgrel=1', arch)
        self.assertRegex(arch, r"sha256sums=\('[0-9a-f]{64}'\)")
        native = (ROOT / 'packaging/native/CPack.cmake').read_text()
        self.assertIn('CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON', native)
        self.assertIn('CPACK_RPM_PACKAGE_AUTOREQPROV ON', native)
        self.assertIn('LURVIKO_INSTALL_SYSTEM_FILEMANAGER1', native)
    def test_appimage_can_not_reuse_stale_appdir(self):
        app = (ROOT / 'packaging/scripts/build-appimage.sh').read_text()
        self.assertIn('Refusing to overwrite existing AppDir', app)
        self.assertIn('QML_SOURCES_PATHS', app)
        self.assertIn('--plugin qt --output appimage', app)
        self.assertIn('LURVIKO_PACKAGED_BUILD=ON', app)
    def test_native_does_not_override_default_build(self):
        script = (ROOT / 'packaging/scripts/build-native.sh').read_text()
        self.assertIn('build-package-${kind}', script)
        self.assertIn('LURVIKO_INSTALL_SYSTEM_FILEMANAGER1=OFF', script)
        self.assertIn('cpack -G "$generator"', script)


if __name__ == '__main__':
    unittest.main()
