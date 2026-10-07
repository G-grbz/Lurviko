#!/usr/bin/env python3
"""Move the legacy g-file data tree into g-File without copying or replacing data."""
import argparse
import os
from pathlib import Path


def migrate(data_root: Path, apply: bool = False) -> list[str]:
    legacy = data_root / "g-file"
    canonical = data_root / "g-File"
    if not legacy.exists():
        return []
    if legacy.is_symlink() or canonical.is_symlink():
        raise RuntimeError("Data roots must be real directories, not symbolic links.")
    entries = sorted(legacy.iterdir())
    collisions = [entry.name for entry in entries if (canonical / entry.name).exists()
                  or (canonical / entry.name).is_symlink()]
    if collisions:
        raise RuntimeError("Migration stopped before changes: destination already contains " + ", ".join(collisions))
    if not apply:
        return [entry.name for entry in entries]
    canonical_existed = canonical.exists()
    canonical.mkdir(parents=True, exist_ok=True)
    moved = []
    try:
        for entry in entries:
            destination = canonical / entry.name
            if destination.exists() or destination.is_symlink():
                raise RuntimeError("Destination appeared during migration: " + entry.name)
            before = entry.stat(follow_symlinks=False)
            entry.rename(destination)
            moved.append(entry.name)
            after = destination.stat(follow_symlinks=False)
            if (before.st_dev, before.st_ino) != (after.st_dev, after.st_ino):
                raise RuntimeError("Migration did not retain the original inode: " + entry.name)
        legacy.rmdir()  # Remove only the now-empty legacy directory.
    except Exception:
        legacy.mkdir(exist_ok=True)
        for name in reversed(moved):
            (canonical / name).rename(legacy / name)
        if not canonical_existed:
            canonical.rmdir()
        raise
    return moved


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-root", type=Path,
                        default=Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")))
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    names = migrate(args.data_root, args.apply)
    print(("Moved" if args.apply else "Ready to move") + ": " + (", ".join(names) or "nothing"))
