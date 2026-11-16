#!/usr/bin/env python3
"""Exercise the real uninstall template against isolated installation trees."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class SourceUninstallTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="lurviko-uninstall-")
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.source = self.base / "source"
        self.build = self.base / "build"
        self.prefix = self.base / "custom prefix"
        self.source.mkdir()
        (self.source / "lurviko").write_text("fixture binary")
        (self.source / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.21)\n'
            'project(UninstallFixture NONE)\n'
            f'configure_file("{ROOT / "cmake/uninstall.cmake.in"}" '
            '"${CMAKE_CURRENT_BINARY_DIR}/uninstall.cmake" @ONLY)\n'
            'add_custom_target(uninstall COMMAND "${CMAKE_COMMAND}" -P '
            '"${CMAKE_CURRENT_BINARY_DIR}/uninstall.cmake" VERBATIM)\n'
            'install(FILES lurviko DESTINATION bin)\n'
            'install(FILES lurviko DESTINATION share/Lurviko/subtitle-ai RENAME worker.py)\n'
            'install(FILES lurviko DESTINATION share/dbus-1/services '
            'RENAME org.freedesktop.impl.portal.desktop.lurviko.service)\n'
        )
        result = self.run_cmake("-S", self.source, "-B", self.build)
        self.assertEqual(result.returncode, 0, result.stdout)

    def run_cmake(self, *args, destdir=None):
        env = os.environ.copy()
        env.pop("DESTDIR", None)
        # Never touch the real user's portal configuration.
        env["XDG_CONFIG_HOME"] = str(self.base / "config")
        env["LURVIKO_UNINSTALL_NO_PORTAL_RESTART"] = "1"
        if destdir is not None:
            env["DESTDIR"] = str(destdir)
        return subprocess.run(
            ["cmake", *(str(arg) for arg in args)], env=env,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        )

    def install(self, destdir=None):
        result = self.run_cmake("--install", self.build, "--prefix", self.prefix, destdir=destdir)
        self.assertEqual(result.returncode, 0, result.stdout)
        return Path(str(destdir or "") + str(self.prefix))

    def uninstall(self, destdir=None):
        return self.run_cmake("--build", self.build, "--target", "uninstall", destdir=destdir)

    def test_override_prefix_preserves_personal_data_and_handles_repeated_removal(self):
        installed = self.install()
        alias = installed / "bin/g-file"
        alias.symlink_to("lurviko")
        personal = installed / "share/Lurviko/vault/personal.bin"
        personal.parent.mkdir()
        personal.write_bytes(b"encrypted personal content")
        unrelated = installed / "bin/another-app"
        unrelated.write_text("keep")
        result = self.uninstall()
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertFalse((installed / "bin/lurviko").exists())
        self.assertFalse((installed / "share/Lurviko/subtitle-ai/worker.py").exists())
        self.assertFalse(alias.is_symlink())
        self.assertEqual(personal.read_bytes(), b"encrypted personal content")
        self.assertEqual(unrelated.read_text(), "keep")
        self.assertEqual(self.uninstall().returncode, 0)

    def test_uninstall_drops_lurviko_from_portal_choice(self):
        portals = self.base / "config/xdg-desktop-portal"
        portals.mkdir(parents=True)
        created = portals / "kde-portals.conf"
        created.write_text("# Created by Lurviko (Settings).\n[preferred]\n"
                           "org.freedesktop.impl.portal.FileChooser=lurviko;kde\n")
        owned = portals / "portals.conf"
        owned.write_text("[preferred]\ndefault=kde\norg.freedesktop.impl.portal.FileChooser=lurviko;gtk\n"
                         "org.freedesktop.impl.portal.Settings=kde\n")
        env_script = self.base / "config/plasma-workspace/env/lurviko-file-dialogs.sh"
        env_script.parent.mkdir(parents=True)
        env_script.write_text("# Created by Lurviko (Settings).\nexport PLASMA_INTEGRATION_USE_PORTAL=1\n")
        foreign_script = env_script.with_name("other.sh")
        foreign_script.write_text("export X=1\n")
        self.install()
        result = self.uninstall()
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertFalse(created.exists())
        self.assertFalse(env_script.exists())
        self.assertTrue(foreign_script.exists())
        self.assertEqual(owned.read_text(), "[preferred]\ndefault=kde\n"
                         "org.freedesktop.impl.portal.FileChooser=gtk\norg.freedesktop.impl.portal.Settings=kde\n")
        only = "[preferred]\ndefault=kde\norg.freedesktop.impl.portal.FileChooser=lurviko\nx=1\n"
        owned.write_text(only)
        self.install()
        self.assertEqual(self.uninstall().returncode, 0)
        self.assertEqual(owned.read_text(), "[preferred]\ndefault=kde\nx=1\n")

    def test_destdir_does_not_touch_unstaged_paths_or_foreign_alias(self):
        unstaged = self.prefix / "bin/lurviko"
        unstaged.parent.mkdir(parents=True)
        unstaged.write_text("keep real installation")
        stage = self.base / "stage"
        installed = self.install(destdir=stage)
        alias = installed / "bin/g-file"
        alias.symlink_to("another-app")
        result = self.uninstall(destdir=stage)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertFalse((installed / "bin/lurviko").exists())
        self.assertEqual(unstaged.read_text(), "keep real installation")
        self.assertTrue(alias.is_symlink())

    def test_manifest_rejects_directories_before_removing_files(self):
        installed = self.install()
        with (self.build / "install_manifest.txt").open("a") as manifest:
            manifest.write(f"\n{installed / 'share/Lurviko'}\n")
        result = self.uninstall()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Refusing to remove a directory", result.stdout)
        self.assertTrue((installed / "bin/lurviko").exists())

    def test_missing_manifest_explains_required_build_tree(self):
        result = self.uninstall()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("No install_manifest.txt", result.stdout)

    def test_manifest_rejects_relative_and_parent_paths(self):
        installed = self.install()
        original = (self.build / "install_manifest.txt").read_text()
        for invalid in ("relative/path", f"{self.prefix}/../outside"):
            with self.subTest(path=invalid):
                (self.build / "install_manifest.txt").write_text(original + "\n" + invalid)
                result = self.uninstall()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Invalid installation path", result.stdout)
                self.assertTrue((installed / "bin/lurviko").exists())


if __name__ == "__main__":
    unittest.main()
