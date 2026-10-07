#!/usr/bin/env python3
"""Readable AI/Whisper cues must preserve words, styling and source time spans."""
from __future__ import annotations

import importlib.util
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / "tools" / "subtitle-ai"
sys.path.insert(0, str(ENGINE))

from gfile_subtitle_ai.transcription import (  # noqa: E402
    SubtitleCue,
    _subtitle_visible_text,
    readable_subtitle_cues,
    read_srt,
    write_srt,
)

TEXT = ("Güçlendirici öğrenme, bir öğretmen tarafından verilen eğitimin içinde "
        "gerçekleşir. Güvenlik önlemlerini kullananlar.")


def check(cue: SubtitleCue, expected_count: int | None = None) -> list[SubtitleCue]:
    pieces = readable_subtitle_cues([cue])
    if expected_count is not None:
        assert len(pieces) == expected_count, pieces
    assert pieces[0].start == cue.start and pieces[-1].end == cue.end, pieces
    assert all(a.end == b.start for a, b in zip(pieces, pieces[1:])), pieces
    assert all(piece.start < piece.end for piece in pieces), pieces
    assert " ".join(_subtitle_visible_text(piece.text) for piece in pieces) == _subtitle_visible_text(cue.text)
    for piece in pieces:
        assert len(_subtitle_visible_text(piece.text)) <= 64, piece
        assert piece.text.count("[") == piece.text.count("]"), piece
        for tag in ("i", "b", "u", "font"):
            assert len(re.findall(fr"<{tag}(?:\s[^>]*)?>", piece.text, re.I)) == len(re.findall(fr"</{tag}>", piece.text, re.I)), piece
    assert readable_subtitle_cues(pieces) == pieces, pieces
    return pieces


def main() -> None:
    pieces = check(SubtitleCue(10, 19, TEXT), 2)
    natural = "Bütün bu bilgileri anlamak için biraz zamana ihtiyacımız var. Daha sonra yeniden birlikte konuşabiliriz."
    sentence_pieces = check(SubtitleCue(10, 19, natural), 2)
    assert _subtitle_visible_text(sentence_pieces[0].text).endswith("var."), sentence_pieces
    assert sentence_pieces[0].end - 10 > 19 - sentence_pieces[1].start, sentence_pieces
    weight = len(_subtitle_visible_text(pieces[0].text)) / sum(len(_subtitle_visible_text(piece.text)) for piece in pieces)
    assert abs(pieces[0].end - (10 + 9 * weight)) <= .001
    # Whole words can extend a line slightly past the nominal 32-character width.
    assert all(len(_subtitle_visible_text(line)) <= 40 for piece in pieces for line in piece.text.splitlines()), pieces

    for styled in (f"<i>{TEXT}</i>", f'<font color="#abcdef"><b>{TEXT}</b></font>',
                   f"{{\\an8}}<i>{TEXT}</i>", TEXT.replace("öğrenme,", "öğrenme,&nbsp;")):
        styled_pieces = check(SubtitleCue(10, 19, styled), 2)
        if styled.startswith("{\\an8}"):
            assert all(piece.text.startswith("{\\an8}") for piece in styled_pieces)
    check(SubtitleCue(10, 10.2, TEXT), 2)
    check(SubtitleCue(0, 30, " ".join([TEXT] * 5)))
    for text in ("<i>Kısa bir cümle.</i>", "-[hızlı bip sesi]\n-Çok fazla.", "[CEO] <i>Merhaba.</i>"):
        original = SubtitleCue(1.25, 3.75, text)
        assert check(original, 1) == [original]
    check(SubtitleCue(10, 19, f"[CEO] <i>{TEXT}</i>"), 2)
    no_punctuation = " ".join(["çeviri sırasında kelimeler olduğu gibi korunmalıdır"] * 3)
    check(SubtitleCue(0, 12, no_punctuation))
    # Real regressions from the user's newly generated Turkish movie sidecar:
    # both were below the old 84-character cue limit, despite long screen lines.
    real_cues = (
        "Dışarıdaki tüm yeni başlayan filozoflar\niçin, dışarıda olan bütün felsefeciler için.",
        "Ve şikayetçi olmak.\n- Harika bir iş bulman için izin verildiğini biliyor musun?",
        "-Thematic Apperception Test.The New York Times, 1986.\n-Kesinlikle, öyle.",
    )
    for text in real_cues:
        shorter = check(SubtitleCue(10, 15, text), 2)
        assert all(len(_subtitle_visible_text(line)) <= 40 for piece in shorter for line in piece.text.splitlines()), shorter

    spec = importlib.util.spec_from_file_location("subtitle_worker", ENGINE / "worker.py")
    worker = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(worker)
    with tempfile.TemporaryDirectory(prefix="gfile-readable-subtitles-") as temporary:
        output = Path(temporary) / "translated.srt"
        # Ready caches and fresh outputs use the same writer, without model inference.
        write_srt(output, [SubtitleCue(10, 19, f"<i>{TEXT}</i>")])
        saved = read_srt(output)
        live = worker.cue_payload([SubtitleCue(10, 19, f"<i>{TEXT}</i>")])
        assert len(saved) == len(live) == 2
        for cue, payload in zip(saved, live):
            assert cue.text == payload["text"], payload
            assert cue.start == payload["start"], payload
            assert cue.end == payload["end"], payload
        before = output.read_text()
        write_srt(output, saved)
        assert output.read_text() == before
    print("PASS readable subtitle splitting, timing, styling, live/cache parity")


if __name__ == "__main__":
    main()
