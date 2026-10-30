#!/usr/bin/env python3
"""Check SVG previews across name swaps and preserved-metadata edits."""
import os
import gzip
from pathlib import Path
import shlex
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
build = Path(os.environ.get("LURVIKO_TEST_BUILD_DIR", repo / "build")).resolve()
with tempfile.TemporaryDirectory(prefix="gfile-thumbnail-tests-") as temporary:
    root = Path(temporary)
    compressed = root / "compressed-fixtures"
    compressed.mkdir()
    def svg(color):
        return ('<svg xmlns="http://www.w3.org/2000/svg" width="96" height="96">'
                '<filter id="big"><feGaussianBlur stdDeviation="999999"/></filter>'
                f'<rect width="96" height="96" fill="{color}" filter="url(#big)"/></svg>').encode()
    for name, payload in (("left.svgz", svg("#ff0000")), ("right.SVGZ", svg("#0000ff")),
                          ("oversized.svgz", svg("#ff0000") + b" " * (9 * 1024 * 1024))):
        (compressed / name).write_bytes(gzip.compress(payload, compresslevel=0 if name != "oversized.svgz" else 9, mtime=0))
        os.utime(compressed / name, (1700000000, 1700000000))
    (compressed / "invalid.svgz").write_bytes(b"not a gzip stream")
    (root / "green.svgz").write_bytes(gzip.compress(svg("#00ff00"), compresslevel=0, mtime=0))
    main = (repo / "src/main.cpp").read_text()
    includes = "\n".join(line for line in main.splitlines() if line.startswith('#include "'))
    registration = main[main.index('    qmlRegisterType<StorageModel>'):
                        main.index('    const QStringList launchLocations')]
    (root / "backend_test_types.h").write_text(includes)
    source = (repo / "tests/thumbnails/thumbnail_integration.cpp").read_text()
    (root / "main.cpp").write_text(source.replace("// REGISTER_BACKEND_TYPES", registration))
    commands = subprocess.check_output(
        ["ninja", "-C", str(build), "-t", "commands", "lurviko"], text=True).splitlines()
    args = next(shlex.split(line) for line in commands
                if " -c " in line and line.endswith("/src/main.cpp"))
    args = args[:args.index("-MD")] + ["-I" + str(repo / "src"),
        "-I" + str(root), "-o", str(root / "main.o"), "-c", str(root / "main.cpp")]
    subprocess.run(args, cwd=build, check=True)
    link = shlex.split(next(line for line in commands if " -o lurviko " in line))
    link = [arg for arg in link if arg not in (":", "&&")
            and not arg.startswith("-Wl,--dependency-file=")]
    link = [str(root / "main.o") if arg == "CMakeFiles/lurviko.dir/src/main.cpp.o" else arg
            for arg in link]
    link[link.index("-o") + 1] = str(root / "thumbnail-tests")
    subprocess.run(link, cwd=build, check=True)
    environment = dict(os.environ, QT_QPA_PLATFORM=os.environ.get("LURVIKO_THUMBNAIL_QPA", "offscreen"),
        XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
        XDG_CACHE_HOME=str(root / "cache"), LURVIKO_THUMBNAIL_TEST_ROOT=str(root))
    if environment["QT_QPA_PLATFORM"] == "offscreen":
        environment["QT_QUICK_BACKEND"] = "software"
    subprocess.run([str(root / "thumbnail-tests")], env=environment, check=True, timeout=90)
