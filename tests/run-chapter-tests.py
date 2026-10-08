#!/usr/bin/env python3
"""Exercise embedded chapter parsing/probing; optionally test the actual player UI."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
build = Path(os.environ.get('LURVIKO_TEST_BUILD_DIR', repo / 'build'))
with tempfile.TemporaryDirectory(prefix='lurviko-chapter-tests-') as temporary:
    root = Path(temporary)
    metadata = root / 'chapters.txt'
    metadata.write_text(';FFMETADATA1\n'
        '[CHAPTER]\nTIMEBASE=1/1000\nSTART=0\nEND=2500\ntitle=Giriş · Şığ ıÖ\n'
        '[CHAPTER]\nTIMEBASE=1/1000\nSTART=2500\nEND=5000\ntitle=İkinci Bölüm\n'
        '[CHAPTER]\nTIMEBASE=1/1000\nSTART=5000\nEND=8000\n', encoding='utf-8')
    mkv = root / 'Bölümlü film ıİ.mkv'
    subprocess.run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i', 'color=c=blue:s=160x90:r=10:d=8',
                    '-f', 'ffmetadata', '-i', str(metadata), '-map', '0:v', '-map_metadata', '1',
                    '-map_chapters', '1', '-c:v', 'mpeg4', str(mkv)], check=True)
    subprocess.run(['ffmpeg', '-v', 'error', '-i', str(mkv), '-c', 'copy', str(root / 'chapters.mp4')], check=True)
    subprocess.run(['ffmpeg', '-v', 'error', '-i', str(mkv), '-map_chapters', '-1', '-c', 'copy',
                    str(root / 'no-chapters.mkv')], check=True)
    main = (repo / 'src/main.cpp').read_text()
    includes = '\n'.join(line for line in main.splitlines() if line.startswith('#include "'))
    registration = main[main.index('    qmlRegisterType<StorageModel>'):main.index('    const QStringList launchLocations')]
    (root / 'backend_test_types.h').write_text(includes)
    source = (repo / 'tests/chapters/chapter_integration.cpp').read_text()
    (root / 'main.cpp').write_text(source.replace('// REGISTER_BACKEND_TYPES', registration))
    commands = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', 'lurviko'], text=True).splitlines()
    args = next(shlex.split(line) for line in commands if ' -c ' in line and line.endswith('/src/main.cpp'))
    args = args[:args.index('-MD')] + ['-I' + str(repo / 'src'), '-I' + str(root), '-o', str(root / 'main.o'), '-c', str(root / 'main.cpp')]
    subprocess.run(args, cwd=build, check=True)
    link = shlex.split(next(line for line in commands if ' -o lurviko ' in line))
    link = [arg for arg in link if arg not in (':', '&&') and not arg.startswith('-Wl,--dependency-file=')]
    link = [str(root / 'main.o') if arg == 'CMakeFiles/lurviko.dir/src/main.cpp.o' else arg for arg in link]
    link[link.index('-o') + 1] = str(root / 'chapter-tests')
    subprocess.run(link, cwd=build, check=True)
    runtime = root / 'runtime'; runtime.mkdir(mode=0o700)
    environment = dict(os.environ, QT_QPA_PLATFORM=os.environ.get('LURVIKO_CHAPTER_QPA', 'offscreen'),
                       XDG_CONFIG_HOME=str(root / 'config'), XDG_DATA_HOME=str(root / 'data'),
                       XDG_CACHE_HOME=str(root / 'cache'), XDG_RUNTIME_DIR=str(runtime),
                       LURVIKO_CHAPTER_TEST_ROOT=str(root), PULSE_SERVER='unix:' + str(root / 'no-pulse-server'))
    command = [str(root / 'chapter-tests')]
    if environment['QT_QPA_PLATFORM'] == 'offscreen':
        environment['QT_QUICK_BACKEND'] = 'software'
        command.append('--backend-only')
    else:
        display = os.environ.get('WAYLAND_DISPLAY', 'wayland-0')
        environment['WAYLAND_DISPLAY'] = display if display.startswith('/') else str(Path(os.environ['XDG_RUNTIME_DIR']) / display)
        environment.pop('PULSE_SERVER', None)
    subprocess.run(command, env=environment, check=True, timeout=60)
