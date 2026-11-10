#!/usr/bin/env python3
"""Give every symbolic UI icon the same optical size.

Icon sets place their glyphs differently inside the 24x24 canvas (Fluent
glyphs mostly cover two thirds of it, others the whole canvas). The UI sizes
icons by their canvas, so mixed artwork looked randomly small or large.

For each SVG this tool measures the visible glyph (rendered with
rsvg-convert) and rewrites only the root viewBox so the glyph covers
TARGET_FILL of the canvas, centered. Paths are never touched, so the change is
lossless and re-running the tool is a no-op. Scaling is capped so thin glyphs
(a minus sign, a chevron) do not turn into heavy bars.

Usage: tools/normalize-symbolic-icons.py [--check] [files...]
Requires rsvg-convert and Pillow.
"""
import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

TARGET_FILL = 0.84
MAX_SCALE = 1.34
MIN_SCALE = 0.80
RENDER = 480
TOLERANCE = 0.01
# Written on the root element so re-running never rescales an icon twice.
MARKER = "data-lurviko-normalized"

repo = Path(__file__).resolve().parents[1]
default_dir = repo / "assets/icons/symbolic"


def root_tag(text):
    match = re.search(r"<svg\b[^>]*>", text, re.S)
    if not match:
        raise ValueError("no <svg> root")
    return match


def view_box(tag):
    match = re.search(r'viewBox\s*=\s*"([^"]+)"', tag)
    if not match:
        raise ValueError("no viewBox")
    return [float(v) for v in match.group(1).replace(",", " ").split()]


def glyph_box(path, box):
    """Visible glyph bounds in viewBox units."""
    with tempfile.TemporaryDirectory() as temporary:
        png = Path(temporary) / "icon.png"
        subprocess.run(["rsvg-convert", "-w", str(RENDER), "-h", str(RENDER), "-b", "none",
                        str(path), "-o", str(png)], check=True)
        alpha = Image.open(png).convert("RGBA").getchannel("A")
        bounds = alpha.point(lambda a: 255 if a > 20 else 0).getbbox()
    if not bounds:
        return None
    x, y, w, h = box
    sx, sy = w / RENDER, h / RENDER
    return (x + bounds[0] * sx, y + bounds[1] * sy, x + bounds[2] * sx, y + bounds[3] * sy)


def normalize(path, check_only):
    text = path.read_text(encoding="utf-8")
    tag_match = root_tag(text)
    tag = tag_match.group(0)
    if MARKER in tag:
        return "ok"
    box = view_box(tag)
    glyph = glyph_box(path, box)
    if glyph is None:
        return "empty"
    left, top, right, bottom = glyph
    extent = max(right - left, bottom - top)
    canvas = max(box[2], box[3])
    fill = extent / canvas
    scale = min(MAX_SCALE, max(MIN_SCALE, TARGET_FILL / fill))
    size = canvas / scale
    cx, cy = (left + right) / 2, (top + bottom) / 2
    current_cx, current_cy = box[0] + box[2] / 2, box[1] + box[3] / 2
    unchanged = abs(size - canvas) / canvas < TOLERANCE and abs(cx - current_cx) < 0.1 and abs(cy - current_cy) < 0.1
    if check_only:
        return f"needs normalizing (fill {fill:.0%})"


    def fmt(value):
        return f"{value:.3f}".rstrip("0").rstrip(".")

    new_box = f'viewBox="{fmt(cx - size / 2)} {fmt(cy - size / 2)} {fmt(size)} {fmt(size)}"'
    new_tag = tag if unchanged else re.sub(r'viewBox\s*=\s*"[^"]+"', new_box, tag, count=1)
    new_tag = new_tag.replace("<svg", f'<svg {MARKER}="{TARGET_FILL}"', 1)
    # Keep a 24 px intrinsic size so consumers that read it are unaffected.
    if not re.search(r'\bwidth\s*=', new_tag):
        new_tag = new_tag.replace("<svg", '<svg width="24" height="24"', 1)
    path.write_text(text[:tag_match.start()] + new_tag + text[tag_match.end():], encoding="utf-8")
    return "marked" if unchanged else f"normalized (fill {fill:.0%} -> {min(1.0, fill * scale):.0%})"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="only report icons that need normalizing")
    parser.add_argument("files", nargs="*", type=Path)
    args = parser.parse_args()
    files = args.files or sorted(default_dir.glob("*.svg"))
    pending = 0
    for path in files:
        result = normalize(path, args.check)
        if result != "ok":
            print(f"{path.name}: {result}")
            pending += result.startswith("needs")
    return 1 if args.check and pending else 0


if __name__ == "__main__":
    sys.exit(main())
