#!/usr/bin/env python3
"""Small regression checks for SRT structure protection around local MT."""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / "tools" / "subtitle-ai"
sys.path.insert(0, str(ENGINE))

from gfile_subtitle_ai.transcription import (  # noqa: E402
    _restore_subtitle_syntax,
    _translation_source_text,
)


def assert_balanced(text: str) -> None:
    assert text.count("[") == text.count("]"), text
    for tag in ("i", "b", "u"):
        opens = len(re.findall(fr"<{tag}(?:\\s[^>]*)?>", text, re.I))
        closes = len(re.findall(fr"</{tag}>", text, re.I))
        assert opens == closes, (tag, text)
    assert not re.search(r"<(?:strong|p|em)\\b|</(?:strong|p|em)>", text, re.I), text


def check(source: str, translated: str, expected_parts: tuple[str, ...]) -> None:
    safe = _translation_source_text(source)
    assert "<" not in safe and ">" not in safe and "[" not in safe and "]" not in safe, safe
    restored = _restore_subtitle_syntax(source, translated)
    assert_balanced(restored)
    for part in expected_parts:
        assert part in restored, (part, restored)


def main() -> int:
    check(
        "[CEO] <i>At Ultima Robotix,</i>\n<i>we thrive on connection.</i>",
        "Ultima Robotix'te bağlantıya önem veriyoruz. <strong>BAD</strong>",
        ("[CEO]", "<i>", "</i>"),
    )
    check(
        "-[rapid beeping]\n-Too much.",
        "Hızlı bip sesi. Çok fazla.",
        ("-[", "]", "-"),
    )
    check(
        "-[heavy breathing]\n-[choking continues]",
        "Ağır nefes. Boğulma devam ediyor.",
        ("-[", "]"),
    )
    check(
        "<i>We started with Elsie,</i>\n<i>your favorite home assistant.</i>",
        "Elsie ile başladık, en sevdiğiniz ev asistanıyla.",
        ("<i>", "</i>"),
    )
    print("PASS subtitle translation formatting")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
