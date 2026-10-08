#!/usr/bin/env python3
"""Validate the version tag and archive exactly the checked-out source commit."""
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
    destination = Path("release")
    destination.mkdir(exist_ok=True)
    archives = []
    for extension in ("tar.gz", "zip"):
        archive = destination / f"Lurviko-{version}.{extension}"
        subprocess.run(["git", "archive", f"--format={extension}", f"--prefix=Lurviko-{version}/",
                        "-o", str(archive), "HEAD"], check=True)
        archives.append(archive)
    (destination / "SHA256SUMS").write_text("".join(
        f"{hashlib.file_digest(p.open('rb'), 'sha256').hexdigest()}  {p.name}\n" for p in archives))
    with Path(os.environ["GITHUB_OUTPUT"]).open("a") as output:
        output.write(f"version={version}\n")


if __name__ == "__main__":
    main()
