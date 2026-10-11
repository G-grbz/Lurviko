#!/usr/bin/env python3
"""Run the real video viewer with embedded subtitles and a controlled AI stream."""
import os
from pathlib import Path
import shlex
import shutil
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
    link[link.index("-o") + 1] = str(root / "subtitle-tests")
    subprocess.run(link, cwd=build, check=True)
    (root / "source.srt").write_text("1\n00:00:00,000 --> 00:00:20,000\nEN original subtitle\n\n")
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
        "-i", "color=c=blue:s=320x240:r=25", "-i", str(root / "source.srt"),
        "-f", "lavfi", "-i", "anullsrc=r=8000:cl=mono", "-t", "120",
        "-map", "0:v", "-map", "1:0", "-map", "1:0", "-map", "1:0",
        "-map", "2:a", "-map", "2:a", "-map", "2:a", "-c:a", "aac",
        "-c:v", "mpeg4", "-c:s", "srt", "-metadata:s:s:0", "language=eng",
        "-disposition:s:0", "default", "-disposition:s:1", "forced",
        "-disposition:s:2", "hearing_impaired", str(root / "fixture.mkv")], check=True)
    slowProbes = root / "slow-probes"
    slowProbes.mkdir()
    for program in ("ffmpeg", "ffprobe"):
        wrapper = slowProbes / program
        wrapper.write_text("#!/usr/bin/python3\nimport os, sys, time\ntime.sleep(0.6)\n"
                           + "os.execv(" + repr(shutil.which(program)) + ", sys.argv)\n")
        wrapper.chmod(0o755)
    environment = dict(os.environ, QT_QPA_PLATFORM=os.environ.get("LURVIKO_SUBTITLE_QPA", "offscreen"),
        XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
        XDG_CACHE_HOME=str(root / "cache"), LURVIKO_SUBTITLE_TEST_ROOT=str(root),
        LURVIKO_SLOW_PROBE_DIR=str(slowProbes),
        LURVIKO_SUBTITLE_AI_WORKER=str(repo / "tests/subtitles/fake_worker.py"),
        QT_FFMPEG_DECODING_HW_DEVICE_TYPES=",", QT_DISABLE_HW_TEXTURES_CONVERSION="1")
    if environment["QT_QPA_PLATFORM"] == "offscreen":
        environment["QT_QUICK_BACKEND"] = "software"
    soak = int(environment.get("LURVIKO_VIDEO_SOAK_SECONDS", "0"))
    timeout = 90 if environment.get("LURVIKO_VIDEO_REAL_PATH") else 45
    subprocess.run([str(root / "subtitle-tests")], env=environment, check=True, timeout=max(timeout, soak + 40))
    restartEnvironment = dict(environment, LURVIKO_SUBTITLE_RESTART_ONLY="1")
    subprocess.run([str(root / "subtitle-tests")], env=restartEnvironment, check=True, timeout=30)
