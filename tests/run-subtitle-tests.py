#!/usr/bin/env python3
"""Run the real video viewer with embedded subtitles and a controlled AI stream."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
build = repo / "build"
with tempfile.TemporaryDirectory(prefix="gfile-subtitle-tests-") as temporary:
    root = Path(temporary)
    main = (repo / "src/main.cpp").read_text()
    includes = "\n".join(line for line in main.splitlines() if line.startswith('#include "'))
    registration = main[main.index('    qmlRegisterType<StorageModel>'):
                        main.index('    const QStringList launchLocations')]
    (root / "backend_test_types.h").write_text(includes)
    source = (repo / "tests/subtitles/subtitle_integration.cpp").read_text()
    (root / "main.cpp").write_text(source.replace("// REGISTER_BACKEND_TYPES", registration))
    commands = subprocess.check_output(
        ["ninja", "-C", str(build), "-t", "commands", "g-file"], text=True).splitlines()
    args = next(shlex.split(line) for line in commands
                if " -c " in line and line.endswith("/src/main.cpp"))
    args = args[:args.index("-MD")] + ["-I" + str(repo / "src"),
        "-I" + str(root), "-o", str(root / "main.o"), "-c", str(root / "main.cpp")]
    subprocess.run(args, cwd=build, check=True)
    link = shlex.split(next(line for line in commands if " -o g-file " in line))
    link = [arg for arg in link if arg not in (":", "&&")
            and not arg.startswith("-Wl,--dependency-file=")]
    link = [str(root / "main.o") if arg == "CMakeFiles/g-file.dir/src/main.cpp.o" else arg
            for arg in link]
    link[link.index("-o") + 1] = str(root / "subtitle-tests")
    subprocess.run(link, cwd=build, check=True)
    (root / "source.srt").write_text("1\n00:00:00,000 --> 00:00:20,000\nEN original subtitle\n\n")
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
        "-i", "color=c=blue:s=320x240:r=25", "-i", str(root / "source.srt"), "-t", "120",
        "-c:v", "mpeg4", "-c:s", "srt", "-metadata:s:s:0", "language=eng",
        "-disposition:s:0", "default", str(root / "fixture.mkv")], check=True)
    environment = dict(os.environ, QT_QPA_PLATFORM=os.environ.get("GFILE_SUBTITLE_QPA", "offscreen"),
        XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
        XDG_CACHE_HOME=str(root / "cache"), GFILE_SUBTITLE_TEST_ROOT=str(root),
        GFILE_SUBTITLE_AI_WORKER=str(repo / "tests/subtitles/fake_worker.py"),
        QT_FFMPEG_DECODING_HW_DEVICE_TYPES=",", QT_DISABLE_HW_TEXTURES_CONVERSION="1")
    if environment["QT_QPA_PLATFORM"] == "offscreen":
        environment["QT_QUICK_BACKEND"] = "software"
    soak = int(environment.get("GFILE_VIDEO_SOAK_SECONDS", "0"))
    subprocess.run([str(root / "subtitle-tests")], env=environment, check=True, timeout=max(45, soak + 40))
