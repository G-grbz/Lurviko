#!/usr/bin/env python3
"""Read-only recovery/export for Lurviko's optional recovery envelope v1.

This tool does not reset the password or consume the recovery key. Exports can
be repeated while the key matches the supplied vault header. Lurviko password
resets consume the key in the current header; older backups can retain it.
Requires Python 3 and cryptography. Never accepts secrets on the command line.
See docs/vault-recovery.md for the exact format and security limits.
"""
import argparse
import base64
import getpass
import hashlib
import hmac
import json
import os
from pathlib import Path
import re
import shutil
import stat
import sys
import tempfile
import uuid
import warnings

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.ciphers.aead import AESGCM


class RecoveryError(ValueError):
    """Actionable public-state error; never contains key or plaintext data."""


def check_locations(vault, output):
    """Catch input/output path mistakes before asking for a sensitive key."""
    try:
        root = os.open(vault, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC)
    except FileNotFoundError:
        raise RecoveryError("Vault directory not found. Check the vault path; the default is ~/.local/share/Lurviko/Lurviko/.private-vault.") from None
    except NotADirectoryError:
        raise RecoveryError("Vault path must be a real directory, not a file or symbolic link.") from None
    except PermissionError:
        raise RecoveryError("Permission denied while opening the vault directory.") from None
    try:
        try:
            with open_regular("vault.json", root):
                pass
        except FileNotFoundError:
            raise RecoveryError("vault.json was not found in the supplied directory. Select the .private-vault directory from a complete vault backup.") from None
    finally:
        os.close(root)
    output = Path(output)
    if os.path.lexists(output):
        raise RecoveryError("The output path already exists. Choose a new directory; existing files will not be overwritten.")
    if not output.parent.is_dir():
        raise RecoveryError("The output parent directory does not exist. Create the parent or choose another output path.")


def decode_key(text):
    if len(text) > 256:
        raise ValueError("Invalid recovery key")
    compact = re.sub(r"[- \t\r\n]", "", text).upper()
    if not re.fullmatch(r"LRK1[0-9A-F]{72}", compact, flags=re.ASCII):
        raise ValueError("Invalid recovery key")
    key = bytes.fromhex(compact[4:68])
    check = hashlib.sha256(b"lurviko-recovery-key-v1:" + key).digest()[:4]
    if not hmac.compare_digest(check, bytes.fromhex(compact[68:])):
        raise ValueError("Recovery key checksum mismatch")
    return key


def canonical_uuid(value):
    if not isinstance(value, str) or len(value) != 36:
        raise ValueError("Invalid UUID")
    parsed = uuid.UUID(value)
    if not parsed.int or str(parsed) != value:
        raise ValueError("Invalid UUID")
    return value


def b64(value, size=None):
    if not isinstance(value, str) or len(value) > 65536:
        raise ValueError("Invalid base64url field")
    data = base64.b64decode(value + "=" * (-len(value) % 4), altchars=b"-_", validate=True)
    if base64.urlsafe_b64encode(data).decode().rstrip("=") != value or (size is not None and len(data) != size):
        raise ValueError("Invalid base64url field")
    return data


def unwrap(header, text):
    if header.get("magic") != "g-file-private-vault" or type(header.get("version")) is not int or header["version"] != 1:
        raise ValueError("Unsupported vault format")
    recovery = header.get("recovery")
    if recovery is None:
        raise RecoveryError("Recovery is not enabled in this vault header. The key may have been used for a password reset or disabled. Generate a new key in Private > Security, or use a complete backup whose header still accepts the saved key.")
    if not isinstance(recovery, dict):
        raise ValueError("Unsupported recovery format")
    if type(recovery.get("version")) is not int or recovery["version"] != 1 or recovery.get("algorithm") != "aes-256-gcm":
        raise ValueError("Unsupported recovery format")
    associated = b"lurviko-private/recovery-master-v1\0" + canonical_uuid(header["vaultId"]).encode("ascii")
    associated += b"\0" + canonical_uuid(recovery["id"]).encode("ascii")
    master = AESGCM(decode_key(text)).decrypt(b64(recovery["iv"], 12),
        b64(recovery["wrappedKey"], 32) + b64(recovery["tag"], 16), associated)
    return master


def open_regular(name, parent):
    descriptor = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK, dir_fd=parent)
    if not stat.S_ISREG(os.fstat(descriptor).st_mode):
        os.close(descriptor)
        raise ValueError("Expected a regular file")
    return os.fdopen(descriptor, "rb")


def decrypt_blob(objects, item, master, destination):
    with open_regular(canonical_uuid(item["blob"]), objects) as source:
        size = os.fstat(source.fileno()).st_size
        length = size - 8 - 12 - 16
        if length < 0 or length > (1 << 36) - 32 or source.read(8) != b"GFBLOB01":
            raise ValueError("Invalid blob")
        iv = source.read(12)
        source.seek(-16, os.SEEK_END)
        tag = source.read(16)
        source.seek(20)
        decryptor = Cipher(algorithms.AES(master), modes.GCM(iv, tag)).decryptor()
        decryptor.authenticate_additional_data(b"g-file-private/blob-v1:" + item["id"].encode("ascii"))
        # An unlinked temporary file is never published before tag verification.
        with tempfile.TemporaryFile(dir=destination.parent) as temporary:
            while length:
                chunk = source.read(min(length, 1024 * 1024))
                if not chunk:
                    raise ValueError("Truncated blob")
                length -= len(chunk)
                temporary.write(decryptor.update(chunk))
            temporary.write(decryptor.finalize())
            temporary.seek(0)
            fd = os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600)
            with os.fdopen(fd, "wb") as target:
                shutil.copyfileobj(temporary, target, 1024 * 1024)


def recover(vault, text, output):
    """Export authenticated objects to a NEW private directory; never alter vault."""
    root = os.open(vault, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC)
    objects = None
    created = False
    output = Path(output)
    try:
        with open_regular("vault.json", root) as source:
            content = source.read(65537)
        if len(content) > 65536:
            raise ValueError("Header too large")
        header = json.loads(content)
        master = unwrap(header, text)
        manifest = json.loads(AESGCM(master).decrypt(b64(header["manifestIv"], 12),
            b64(header["manifestCipher"]) + b64(header["manifestTag"], 16), b"g-file-private/manifest-v1"))
        if type(manifest.get("version")) is not int or manifest["version"] != 1 or not isinstance(manifest.get("items"), list):
            raise ValueError("Invalid manifest")
        items = manifest["items"]
        ids = set()
        for item in items:
            identifier = canonical_uuid(item["id"])
            name = item["name"]
            if identifier in ids or not isinstance(name, str) or not name or name in (".", "..") or any(c in name for c in ("/", "\\", "\0")) or type(item.get("dir")) is not bool:
                raise ValueError("Unsafe manifest entry")
            ids.add(identifier)
        objects = os.open("objects", os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=root)
        os.mkdir(output, 0o700)  # Refuse existing outputs, including symlinks.
        created = True
        paths = {"root": output}
        unresolved = list(items)
        while unresolved:
            remaining = []
            for item in unresolved:
                if item["parent"] not in paths:
                    remaining.append(item)
                    continue
                destination = paths[item["parent"]] / item["name"]
                if item["dir"]:
                    os.mkdir(destination, 0o700)
                    paths[item["id"]] = destination
                else:
                    decrypt_blob(objects, item, master, destination)
            if len(remaining) == len(unresolved):
                raise ValueError("Missing or cyclic manifest parent")
            unresolved = remaining
        return len(items)
    except BaseException:
        if created:
            shutil.rmtree(output)
        raise
    finally:
        if objects is not None:
            os.close(objects)
        os.close(root)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("vault", type=Path, help=".private-vault directory containing vault.json")
    parser.add_argument("output", type=Path, help="new export directory (must not exist)")
    args = parser.parse_args()
    if not sys.stdin.isatty():
        parser.exit(1, "Run in an interactive terminal; recovery keys must not be echoed or passed as arguments.\n")
    try:
        check_locations(args.vault, args.output)
        print("Recovery exports plaintext. Use a trusted machine and protected storage.")
        print("Read-only export: the password is unchanged and the recovery key is not consumed.")
        with warnings.catch_warnings():
            warnings.simplefilter("error", getpass.GetPassWarning)
            key = getpass.getpass("Recovery key (hidden): ")
        count = recover(args.vault, key, args.output)
    except RecoveryError as error:
        parser.exit(1, f"Recovery failed: {error} Vault was not modified.\n")
    except (Exception, KeyboardInterrupt):
        parser.exit(1, "Recovery failed: wrong key, damaged/unsafe data, or unavailable output. Vault was not modified.\n")
    print(f"Recovered {count} items. Vault was not modified.")


if __name__ == "__main__":
    main()
