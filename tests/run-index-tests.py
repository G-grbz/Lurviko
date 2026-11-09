#!/usr/bin/env python3
"""Check index filtering/pruning; optionally compare a saved source baseline.

--baseline /path/contentindexmodel.cpp builds and measures the same fixture.
--home-settings /path/Lurviko.conf runs a read-only real home scan (60s limit).
"""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]


def find_moc():
    """Qt keeps moc in QT_INSTALL_LIBEXECS (Arch: /usr/lib/qt6, Debian: .../qt6/libexec)."""
    import shutil
    for tool in ("qmake6", "qtpaths6", "qmake"):
        if shutil.which(tool):
            query = subprocess.run([tool, "-query", "QT_INSTALL_LIBEXECS"] if "qmake" in tool
                                   else [tool, "--query", "QT_INSTALL_LIBEXECS"],
                                   capture_output=True, text=True)
            candidate = Path(query.stdout.strip()) / "moc"
            if query.returncode == 0 and candidate.is_file():
                return str(candidate)
    return shutil.which("moc-qt6") or "/usr/lib/qt6/moc"


moc = find_moc()
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', type=Path)
parser.add_argument('--home-settings', type=Path)
args = parser.parse_args()
flags = shlex.split(subprocess.check_output(
    ['pkg-config', '--cflags', '--libs', 'Qt6Core', 'Qt6Concurrent', 'Qt6Sql'], text=True))
with tempfile.TemporaryDirectory(prefix='lurviko-index-tests-') as temporary:
    root = Path(temporary)
    subprocess.run([moc, str(repo / 'src/contentindexmodel.h'),
                    '-o', str(root / 'moc.cpp')], check=True)
    subprocess.run([moc, str(repo / 'src/directorysizeindex.h'),
                    '-o', str(root / 'moc_size.cpp')], check=True)
    environment = dict(os.environ, XDG_CONFIG_HOME=str(root / 'config'),
        XDG_DATA_HOME=str(root / 'data'), XDG_CACHE_HOME=str(root / 'cache'))
    sources = [('current', repo / 'src/contentindexmodel.cpp')]
    if args.baseline:
        sources.insert(0, ('baseline', args.baseline))
    for name, source in sources:
        (root / 'index_source.cpp').write_text(source.read_text())
        subprocess.run(['c++', '-O2', '-fPIC', '-std=c++17', '-I' + str(root), '-I' + str(repo / 'src'),
                        *(['-DBASELINE'] if name == 'baseline' else []),
                        str(repo / 'tests/index/index_scan.cpp'), str(root / 'moc.cpp'), str(root / 'moc_size.cpp'),
                        str(repo / 'src/directorysizeindex.cpp'), str(repo / 'src/directorysizestore.cpp'),
                        '-o', str(root / name), *flags], check=True)
        print(name + ':', flush=True)
        subprocess.run([str(root / name)], env=environment, check=True, timeout=90)
        if args.home_settings:
            subprocess.run([str(root / name), str(args.home_settings)], env=environment, check=True, timeout=90)
