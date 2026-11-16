#!/usr/bin/env python3
"""Check SVG previews across name swaps and preserved-metadata edits, and the
embedded previews of packages, launchers, models, e-books and comics."""
import base64
import io
import os
import gzip
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import tarfile
import tempfile
import zipfile
import zlib


def png(width, height, rgb):
    """A solid-colour RGB PNG without third-party modules."""
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = b"".join(b"\0" + bytes(rgb) * width for _ in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def write_format_fixtures(formats, data_home):
    """Every fixture's correct preview is red; decoys are green or yellow."""
    formats.mkdir()
    red, green, yellow = png(64, 64, (255, 0, 0)), png(64, 64, (0, 160, 0)), png(64, 64, (255, 255, 0))

    # APK: a v2 signing block between the entries and the central directory
    # (KZip rejects that layout); no manifest icon, so the highest-density
    # launcher bitmap wins over the mdpi one.
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as apk:
        apk.writestr("AndroidManifest.xml", b"not binary xml")
        apk.writestr("res/mipmap-mdpi-v4/ic_launcher.png", green)
        apk.writestr("res/mipmap-xxxhdpi-v4/ic_launcher.png", red)
    data = buffer.getvalue()
    eocd = data.rindex(b"PK\x05\x06")
    directory = struct.unpack_from("<I", data, eocd + 16)[0]
    block = b"\0" * 4096 + b"APK Sig Block 42"
    data = (data[:directory] + block + data[directory:eocd + 16]
            + struct.pack("<I", directory + len(block)) + data[eocd + 20:])
    (formats / "signed.apk").write_bytes(data)
    with zipfile.ZipFile(formats / "bundle.xapk", "w") as xapk:
        xapk.writestr("manifest.json", "{}")
        xapk.writestr("icon.png", red)
    (formats / "broken.apk").write_bytes(b"PK\x05\x06" + b"\xff" * 18)

    # Launchers: an absolute icon path and a name from the hicolor theme.
    (formats / "red-icon.png").write_bytes(red)
    (formats / "absolute.desktop").write_text(
        f"[Desktop Entry]\nType=Application\nName=A\nIcon={formats / 'red-icon.png'}\nExec=true\n")
    theme = data_home / "icons/hicolor"
    (theme / "48x48/apps").mkdir(parents=True)
    (theme / "index.theme").write_text("[Icon Theme]\nName=Hicolor\nDirectories=48x48/apps\n\n"
                                       "[48x48/apps]\nSize=48\nType=Threshold\n")
    (theme / "48x48/apps/lurviko-test-red.png").write_bytes(red)
    (formats / "themed.desktop").write_text(
        "[Desktop Entry]\nType=Application\nName=T\nName[tr]=T\nIcon=lurviko-test-red\nExec=true\n"
        "[Desktop Action other]\nIcon=/nonexistent.png\n")

    # Blender, pre-5.0 layout: rows stored bottom-up (blue bottom, red top).
    width = height = 64
    pixels = b"".join((b"\0\0\xff\xff" if y < height // 2 else b"\xff\0\0\xff") * width for y in range(height))
    test = struct.pack("<ii", width, height) + pixels
    def bhead(code, payload):
        return code + struct.pack("<iQii", len(payload), 0, 0, 1) + payload
    (formats / "legacy.blend").write_bytes(b"BLENDER-v300" + bhead(b"REND", b"\0" * 8)
                                           + bhead(b"TEST", test) + bhead(b"ENDB", b""))
    (formats / "truncated.blend").write_bytes(b"BLENDER-v300" + bhead(b"TEST", test)[:200])

    with zipfile.ZipFile(formats / "model.3mf", "w") as model:
        model.writestr("_rels/.rels", '<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/'
                       'package/2006/relationships"><Relationship Target="/Metadata/cover.png" Id="r1" Type="http://'
                       'schemas.openxmlformats.org/package/2006/relationships/metadata/thumbnail"/></Relationships>')
        model.writestr("Metadata/thumbnail.png", green)
        model.writestr("Metadata/cover.png", red)

    # FictionBook: the cover page image, not the first binary.
    fb2 = ('<?xml version="1.0" encoding="utf-8"?><FictionBook xmlns="http://www.gribuser.ru/xml/fictionbook/2.0" '
           'xmlns:l="http://www.w3.org/1999/xlink"><description><title-info><coverpage><image l:href="#cover.png"/>'
           '</coverpage></title-info></description><body><section><p>Text</p></section></body>'
           f'<binary id="other.png" content-type="image/png">{base64.encodebytes(green).decode()}</binary>'
           f'<binary id="cover.png" content-type="image/png">{base64.encodebytes(red).decode()}</binary></FictionBook>')
    (formats / "book.fb2").write_text(fb2)
    with zipfile.ZipFile(formats / "book.fb2.zip", "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("book.fb2", fb2)

    # Mobipocket: EXTH 201 points one record past the first image.
    exth = b"EXTH" + struct.pack(">II", 24, 1) + struct.pack(">III", 201, 12, 1)
    mobi = bytearray(0xE8)
    mobi[0:4] = b"MOBI"
    struct.pack_into(">I", mobi, 4, len(mobi))
    struct.pack_into(">I", mobi, 0x6C - 16, 2)
    struct.pack_into(">I", mobi, 0x80 - 16, 0x40)
    records = [b"\0" * 16 + bytes(mobi) + exth, b"text", green, red]
    header = bytearray(78)
    header[60:68] = b"BOOKMOBI"
    struct.pack_into(">H", header, 76, len(records))
    offset, table = 78 + 8 * len(records) + 2, b""
    for index, record in enumerate(records):
        table += struct.pack(">IBBH", offset, 0, 0, index)
        offset += len(record)
    (formats / "book.azw3").write_bytes(bytes(header) + table + b"\0\0" + b"".join(records))

    # Comics: natural page order puts 2 before 10.
    pages = formats / "pages"
    pages.mkdir()
    (pages / "page10.png").write_bytes(yellow)
    (pages / "page2.png").write_bytes(red)
    with tarfile.open(formats / "comic.cbt", "w") as tar:
        tar.add(pages / "page10.png", "page10.png")
        tar.add(pages / "page2.png", "page2.png")
    tools = {}
    if shutil.which("7z"):
        for name in ("comic.cb7", "mislabelled.cbr"):
            subprocess.run(["7z", "a", "-bd", "-t7z", str(formats / name), "page10.png", "page2.png"],
                           cwd=pages, check=True, stdout=subprocess.DEVNULL)
        tools["LURVIKO_TEST_HAS_7Z"] = "1"
    if shutil.which("rar") and any(shutil.which(tool) for tool in ("unrar", "7z", "bsdtar")):
        subprocess.run(["rar", "a", str(formats / "comic.cbr"), "page10.png", "page2.png"],
                       cwd=pages, check=True, stdout=subprocess.DEVNULL)
        tools["LURVIKO_TEST_HAS_RAR"] = "1"
    return tools

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
    format_tools = write_format_fixtures(root / "formats", root / "data")
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
        XDG_CACHE_HOME=str(root / "cache"), LURVIKO_THUMBNAIL_TEST_ROOT=str(root), **format_tools)
    if environment["QT_QPA_PLATFORM"] == "offscreen":
        environment["QT_QUICK_BACKEND"] = "software"
    subprocess.run([str(root / "thumbnail-tests")], env=environment, check=True, timeout=90)
