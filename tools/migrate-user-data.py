#!/usr/bin/env python3
"""Preview the g-File to Lurviko migration, or run the application's tested migrator."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    roots = [("config", Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))),
             ("data", Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share"))),
             ("cache", Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")))]
    for kind, root in roots:
        for name in ("g-File", "g-file", "GFile"):
            if (root / name).exists():
                print(f"{kind}: {root / name} -> {root / 'Lurviko'}", flush=True)
    if args.apply:
        candidate = Path(__file__).resolve().parents[1] / "build/lurviko"
        binary = str(candidate) if candidate.exists() else shutil.which("lurviko")
        if not binary:
            parser.error("Build or install Lurviko first, then close the old application.")
        subprocess.run([binary, "--migrate-only"], check=True,
                       env=dict(os.environ, QT_QPA_PLATFORM="offscreen"))
        print("Migration complete.")
    else:
        print("Preview only. Close g-File and pass --apply to migrate.")


if __name__ == "__main__":
    main()
