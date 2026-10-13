#!/usr/bin/env python3
"""Exercise the real descriptor helper; --authenticate additionally tests polkit/root.

Ordinary tests never grant authority or bypass production authentication.
The authenticated case modifies only this script's temporary fixture.
"""
import argparse
import json
import os
from pathlib import Path
import select
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument("--authenticate", action="store_true")
args = parser.parse_args()
build = Path(os.environ.get("LURVIKO_TEST_BUILD_DIR", str(Path(__file__).resolve().parents[1] / "build")))
helper = build.resolve() / "lurviko-access-helper"
assert helper.is_file(), "Build lurviko-access-helper first"


def request(paths, mode=0o744):
    return dict(paths=[str(path) for path in paths], mode=mode, mask=0o777,
                owner=-1, group=-1, recursive=True,
                identities=[dict(device=str(path.lstat().st_dev), inode=str(path.lstat().st_ino))
                            for path in paths])


def invoke(message, authenticate=False, cancel=False):
    command = [str(helper)]
    if authenticate:
        command = ["pkexec", "--disable-internal-agent", *command]
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        process.stdin.write(json.dumps(message).encode() + b"\n")
        process.stdin.flush()
        if cancel:
            process.stdin.write(b"cancel\n")
            process.stdin.flush()
        # Keep the lifetime pipe open. communicate(input=...) would immediately
        # close it and cancel the authorized helper before it completes.
        output = b""
        deadline = time.monotonic() + (120 if authenticate else 15)
        while True:
            remaining = deadline - time.monotonic()
            assert remaining > 0, "Helper/authentication timed out"
            ready, _, _ = select.select([process.stdout], [], [], min(remaining, 1))
            if ready:
                data = os.read(process.stdout.fileno(), 65536)
                if not data:
                    break
                output += data
        code = process.wait(timeout=5)
        reports = [json.loads(line) for line in output.splitlines()]
        assert reports and reports[-1]["finished"], (code, process.stderr.read().decode())
        return reports[-1]
    finally:
        process.stdin.close()  # EOF cancels even a root helper on test failure.
        if process.poll() is None:
            process.wait(timeout=10)
        process.stdout.close()
        process.stderr.close()


with tempfile.TemporaryDirectory(prefix="lurviko-admin-test-") as temporary:
    base = Path(temporary)
    tree = base / "selected"
    nested = tree / "nested"
    nested.mkdir(parents=True)
    image = nested / "icon.svg"
    contents = b'<svg xmlns="http://www.w3.org/2000/svg"><path d="M1 2"/></svg>'
    image.write_bytes(contents)
    outside = base / "outside.svg"
    outside.write_bytes(b"outside unchanged")
    outside.chmod(0o600)
    (tree / "outside-link").symlink_to(outside)
    tree.chmod(0o777)
    result = invoke(request([tree]))
    assert result["failed"] == 0 and result["changed"] == 3, result
    assert (tree.stat().st_mode & 0o777) == 0o744
    assert (image.stat().st_mode & 0o777) == 0o644
    assert image.read_bytes() == contents and outside.read_bytes() == b"outside unchanged"
    assert (outside.stat().st_mode & 0o777) == 0o600
    print("PASS: writable trees, recursive +X, unchanged content and external symlink target")

    for mode in [-1, 0o1000, 1.5, "0744"]:
        result = invoke(request([tree], mode))
        assert result["code"] == "invalid-request" and result["changed"] == 0, result
    print("PASS: malformed permission requests rejected")

    symbolic_parent = base / "symbolic-parent"
    symbolic_parent.symlink_to(tree, target_is_directory=True)
    result = invoke(request([symbolic_parent / "nested"]))
    assert result["code"] == "unsafe-root" and result["changed"] == 0, result
    saved = request([tree])
    saved["identities"][0]["inode"] = "0"
    result = invoke(saved)
    assert result["code"] == "root-changed" and result["changed"] == 0, result
    print("PASS: intermediate symlinks and changed selected root identities rejected")

    missing = request([tree, image])
    missing["paths"][1] = str(base / "missing")
    result = invoke(missing)
    assert result["code"] == "unsafe-root" and result["changed"] == 0, result
    print("PASS: all selected roots validated before any mutation")
    result = invoke(request([tree]), cancel=True)
    assert result["cancelled"] and result["changed"] == 0, result
    print("PASS: cancellation queued during authorization does not mutate files")

    if args.authenticate:
        # Reproduce the exact 0644 directory regression under real root authority.
        nested.chmod(0o644)
        tree.chmod(0o644)
        try:
            authorized = request([tree])
            authorized["group"] = 0  # Exercise privileged fchownat as well as chmod.
            result = invoke(authorized, authenticate=True)
            assert result["failed"] == 0 and result["changed"] == 3, result
            assert (tree.stat().st_mode & 0o777) == 0o744
            assert (nested.stat().st_mode & 0o777) == 0o744
            assert image.read_bytes() == contents
            assert image.stat().st_gid == 0 and tree.stat().st_gid == 0
            assert (outside.stat().st_mode & 0o777) == 0o600
            print("PASS: real polkit authorization repairs recursive 0644 directories without changing SVG content or outside targets")
        finally:
            tree.chmod(0o700)
            nested.chmod(0o700)
