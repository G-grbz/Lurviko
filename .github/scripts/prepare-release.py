#!/usr/bin/env python3
"""Validate a release tag and reproducibly archive its exported source tree."""
import hashlib
import os
from pathlib import Path
import re
import subprocess


def release_version(tag: str, cmake: str) -> str:
    match = re.fullmatch(r"(?:Lurviko[-_])?v?(\d+\.\d+\.\d+)", tag)
    project = re.search(r"project\(Lurviko\s+VERSION\s+(\d+\.\d+\.\d+)\s", cmake)
    if not match or not project or match[1] != project[1]:
        raise ValueError("Use a version tag matching CMakeLists.txt, for example v1.0.0 or Lurviko-v1.0.0.")
    return match[1]


def main() -> None:
    version = release_version(os.environ["RELEASE_TAG"], Path("CMakeLists.txt").read_text())
    # A tree object avoids embedding the eventual commit ID in tar/zip metadata.
    # The fixed timestamp and umask make staged-tree previews match the final
    # tagged commit. AUR checksum records are export-ignored, not app sources.
    revision = os.environ.get("RELEASE_REVISION", "HEAD")
    tree = subprocess.check_output(["git", "rev-parse", "--verify", "--end-of-options",
                                    f"{revision}^{{tree}}"], text=True).strip()
    destination = Path("release")
    destination.mkdir(exist_ok=True)
    archives = []
    # The uncompressed tar is canonical for AUR: zlib and zlib-ng may produce
    # different compressed bytes for identical source files across platforms.
    for extension in ("tar", "tar.gz", "zip"):
        archive = destination / f"Lurviko-{version}.{extension}"
        subprocess.run(["git", "-c", "tar.umask=0022", "archive", f"--format={extension}",
                        "--mtime=@946684800", f"--prefix=Lurviko-{version}/",
                        "-o", str(archive), tree], check=True, env={**os.environ, "TZ": "UTC"})
        archives.append(archive)
    (destination / "SHA256SUMS").write_text("".join(
        f"{hashlib.file_digest(p.open('rb'), 'sha256').hexdigest()}  {p.name}\n" for p in archives))
    with Path(os.environ["GITHUB_OUTPUT"]).open("a") as output:
        output.write(f"version={version}\n")


if __name__ == "__main__":
    main()
