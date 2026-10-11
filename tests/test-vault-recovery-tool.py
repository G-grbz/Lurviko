#!/usr/bin/env python3
"""Independent recovery implementation: authenticated export and path boundaries."""
import base64
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest

from cryptography.hazmat.primitives.ciphers.aead import AESGCM

repo = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("recovery", repo / "tools/recover-vault.py")
recovery = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recovery)


class RecoveryTests(unittest.TestCase):
    key = bytes(range(32))
    master = bytes(range(32, 64))
    vault_id = "11223344-5566-7788-99aa-bbccddeeff00"
    recovery_id = "11223344-5566-7788-99aa-bbccddeeff01"
    object_id = "11223344-5566-7788-99aa-bbccddeeff02"
    blob = "11223344-5566-7788-99aa-bbccddeeff03"

    def text(self):
        return "LRK1-" + self.key.hex() + "-" + hashlib.sha256(b"lurviko-recovery-key-v1:" + self.key).digest()[:4].hex()

    def encoded(self, data):
        return base64.urlsafe_b64encode(data).decode().rstrip("=")

    def fixture(self, root, name="photo.txt"):
        vault = root / "vault"
        vault.mkdir()
        (vault / "objects").mkdir()
        iv = bytes(range(12))
        associated = b"lurviko-private/recovery-master-v1\0" + self.vault_id.encode() + b"\0" + self.recovery_id.encode()
        wrapped = AESGCM(self.key).encrypt(iv, self.master, associated)
        item = dict(id=self.object_id, parent="root", name=name, dir=False, blob=self.blob)
        plain = json.dumps(dict(version=1, items=[item])).encode()
        manifest = AESGCM(self.master).encrypt(iv, plain, b"g-file-private/manifest-v1")
        header = dict(magic="g-file-private-vault", version=1, vaultId=self.vault_id,
            recovery=dict(version=1, algorithm="aes-256-gcm", id=self.recovery_id,
                          iv=self.encoded(iv), wrappedKey=self.encoded(wrapped[:-16]), tag=self.encoded(wrapped[-16:])),
            manifestIv=self.encoded(iv), manifestCipher=self.encoded(manifest[:-16]), manifestTag=self.encoded(manifest[-16:]))
        # Fixed IVs above are fixture vectors, never production key generation.
        blob_iv = bytes(range(12, 24))
        cipher = AESGCM(self.master).encrypt(blob_iv, b"Recovered bytes", b"g-file-private/blob-v1:" + self.object_id.encode())
        (vault / "objects" / self.blob).write_bytes(b"GFBLOB01" + blob_iv + cipher)
        (vault / "vault.json").write_text(json.dumps(header))
        return vault

    def test_path_diagnostics_before_requesting_a_key(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            with self.assertRaisesRegex(recovery.RecoveryError, "Vault directory not found"):
                recovery.check_locations(root / "missing-vault", root / "output")
            vault = self.fixture(root)
            recovery.check_locations(vault, root / "output")
            occupied = root / "occupied"; occupied.mkdir()
            with self.assertRaisesRegex(recovery.RecoveryError, "output path already exists"):
                recovery.check_locations(vault, occupied)
            broken = root / "broken"; broken.symlink_to(root / "missing")
            with self.assertRaisesRegex(recovery.RecoveryError, "output path already exists"):
                recovery.check_locations(vault, broken)
            with self.assertRaisesRegex(recovery.RecoveryError, "output parent directory does not exist"):
                recovery.check_locations(vault, root / "missing-parent" / "output")
            (vault / "vault.json").unlink()
            with self.assertRaisesRegex(recovery.RecoveryError, "vault.json was not found"):
                recovery.check_locations(vault, root / "output")

    def test_disabled_recovery_explains_why_export_cannot_work(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); vault = self.fixture(root)
            header = json.loads((vault / "vault.json").read_bytes())
            del header["recovery"]
            with self.assertRaisesRegex(recovery.RecoveryError, "Recovery is not enabled"):
                recovery.unwrap(header, self.text())

    def test_encoding_and_checksum(self):
        self.assertEqual(recovery.decode_key(self.text()), self.key)
        with self.assertRaises(ValueError):
            recovery.decode_key(self.text()[:-1] + ("0" if self.text()[-1] != "0" else "1"))
        with self.assertRaises(ValueError):
            recovery.decode_key("ＬＲＫ１" + self.text()[4:])

    def test_export_and_private_permissions(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); vault = self.fixture(root); output = root / "output"
            before = (vault / "vault.json").read_bytes()
            self.assertEqual(recovery.recover(vault, self.text(), output), 1)
            self.assertEqual((output / "photo.txt").read_bytes(), b"Recovered bytes")
            self.assertEqual(output.stat().st_mode & 0o777, 0o700)
            self.assertEqual((output / "photo.txt").stat().st_mode & 0o777, 0o600)
            self.assertEqual((vault / "vault.json").read_bytes(), before)
            # Offline export is deliberately repeatable, unlike an in-app reset.
            second = root / "second-output"
            self.assertEqual(recovery.recover(vault, self.text(), second), 1)
            self.assertEqual((second / "photo.txt").read_bytes(), b"Recovered bytes")
            self.assertEqual((vault / "vault.json").read_bytes(), before)

    def test_manifest_traversal_is_rejected(self):
        for name in ("../outside", "a/b", "..", "a\\b"):
            with tempfile.TemporaryDirectory() as folder:
                root = Path(folder); vault = self.fixture(root, name); output = root / "output"
                with self.assertRaises(ValueError):
                    recovery.recover(vault, self.text(), output)
                self.assertFalse(output.exists())
                self.assertFalse((root / "outside").exists())

    def test_symlink_blob_and_existing_output_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); vault = self.fixture(root); output = root / "output"
            original = vault / "objects" / self.blob
            outside = root / "outside"; outside.write_bytes(original.read_bytes())
            original.unlink(); original.symlink_to(outside)
            with self.assertRaises(OSError):
                recovery.recover(vault, self.text(), output)
            self.assertFalse(output.exists())
            output.mkdir(); (output / "keep").write_bytes(b"keep")
            with self.assertRaises(FileExistsError):
                recovery.recover(vault, self.text(), output)
            self.assertEqual((output / "keep").read_bytes(), b"keep")

    def test_tampered_blob_is_not_published(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); vault = self.fixture(root); output = root / "output"
            blob = vault / "objects" / self.blob
            data = bytearray(blob.read_bytes()); data[-1] ^= 1; blob.write_bytes(data)
            with self.assertRaises(Exception):
                recovery.recover(vault, self.text(), output)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
