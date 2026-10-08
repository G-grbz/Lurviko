#!/usr/bin/env python3
"""JSON-lines bridge between Lurviko and the headless G-TMCE ASR engine.

The bridge deliberately keeps durable job state outside the video directory.
A completed ASR source subtitle is reusable across translation attempts, and AI
translation checkpoints are append-only so cancelling never throws useful work
away.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import threading
from typing import Any


CACHE_VERSION = 2


def emit(event: str, **payload: Any) -> None:
    print(json.dumps({"event": event, **payload}, ensure_ascii=False), flush=True)


def configure_import_paths() -> None:
    here = Path(__file__).resolve().parent
    if str(here) not in sys.path:
        sys.path.insert(0, str(here))

    vendor_candidates = []
    configured = os.environ.get("GTMCE_VENDOR_DIR", "").strip()
    if configured:
        vendor_candidates.append(Path(configured).expanduser())
    vendor_candidates.append(Path("/opt/G-TMCE/vendor"))
    for vendor in vendor_candidates:
        if vendor.is_dir() and str(vendor) not in sys.path:
            sys.path.insert(0, str(vendor))


def detect_channel_layout(input_path: Path, ffprobe: str | None, audio_track_index: int = 0) -> str | None:
    if not ffprobe:
        return None
    try:
        completed = subprocess.run(
            [
                ffprobe,
                "-v", "error",
                "-select_streams", f"a:{max(0, audio_track_index)}",
                "-show_entries", "stream=channel_layout",
                "-of", "json",
                str(input_path),
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            timeout=12,
            check=False,
        )
        if completed.returncode != 0:
            return None
        payload = json.loads(completed.stdout or "{}")
        streams = payload.get("streams") or []
        if not streams:
            return None
        value = str(streams[0].get("channel_layout") or "").strip()
        return value or None
    except Exception:
        return None


def cache_root() -> Path:
    configured = os.environ.get("LURVIKO_GTMCE_CACHE_DIR", "").strip()
    if configured:
        return Path(configured).expanduser()
    xdg = os.environ.get("XDG_CACHE_HOME", "").strip()
    base = Path(xdg).expanduser() if xdg else Path.home() / ".cache"
    return base / "Lurviko" / "gtmce" / "jobs"


def cache_key(input_path: Path, audio_track_index: int, language: str, quality_profile: str) -> str:
    stat = input_path.stat()
    # Millisecond precision is intentional: Qt's QFileInfo exposes mtime in ms,
    # allowing the UI and Python worker to derive exactly the same key.
    mtime_ms = stat.st_mtime_ns // 1_000_000
    material = (
        f"v{CACHE_VERSION}\n{input_path.resolve()}\n{stat.st_size}\n{mtime_ms}\n"
        f"{max(0, int(audio_track_index))}\n{language.strip().lower()}\n{quality_profile.strip().lower()}\n"
    )
    return hashlib.sha256(material.encode("utf-8")).hexdigest()


def atomic_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
    temporary.replace(path)


def read_json(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        return data if isinstance(data, dict) else {}
    except Exception:
        return {}


def cue_payload(cues: list[Any]) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for cue in cues:
        text = str(getattr(cue, "text", "") or "").strip()
        start = float(getattr(cue, "start", 0.0) or 0.0)
        end = float(getattr(cue, "end", 0.0) or 0.0)
        if text and end > start:
            result.append({"start": start, "end": end, "text": text})
    return result


def load_translation_checkpoint(path: Path, subtitle_cue_type: Any) -> tuple[int, int, list[Any]]:
    completed_units = 0
    total_units = 0
    translated: list[Any] = []
    if not path.is_file():
        return completed_units, total_units, translated
    try:
        with path.open("r", encoding="utf-8") as handle:
            for raw in handle:
                raw = raw.strip()
                if not raw:
                    continue
                try:
                    record = json.loads(raw)
                except json.JSONDecodeError:
                    # A killed process can leave only its final line truncated.
                    break
                index = int(record.get("completed_units", 0) or 0)
                total = int(record.get("total_units", 0) or 0)
                if index != completed_units + 1 or total <= 0:
                    break
                pieces = record.get("cues") or []
                restored: list[Any] = []
                for piece in pieces:
                    try:
                        start = float(piece.get("start", 0.0))
                        end = float(piece.get("end", 0.0))
                        text = str(piece.get("text", "") or "").strip()
                    except Exception:
                        continue
                    if text and end > start:
                        restored.append(subtitle_cue_type(start, end, text))
                translated.extend(restored)
                completed_units = index
                total_units = total
    except OSError:
        return 0, 0, []
    return completed_units, total_units, translated


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--language", default="und")
    parser.add_argument("--translate-to", default="")
    parser.add_argument("--audio-track-index", type=int, default=0)
    parser.add_argument("--quality-profile", default="slow", choices=("fast", "medium", "slow", "slower"))
    args = parser.parse_args()

    configure_import_paths()
    cancel_event = threading.Event()

    def request_cancel(_signum: int, _frame: Any) -> None:
        cancel_event.set()
        emit("log", message="G-TMCE: iptal isteği alındı; tamamlanan cache korunuyor…")

    signal.signal(signal.SIGTERM, request_cancel)
    signal.signal(signal.SIGINT, request_cancel)

    try:
        from gtmce.core import OperationCancelled, UserVisibleError, ffprobe_path
        from gtmce.transcription import (
            SubtitleCue,
            generated_subtitle_path,
            generated_translation_path,
            normalise_asr_language,
            read_srt,
            transcribe_audio_to_srt,
            translate_cues_with_ai,
            write_srt,
        )
    except Exception as exc:
        emit(
            "error",
            message=(
                "G-TMCE Python bağımlılıkları yüklenemedi. "
                "Kurulu /opt/G-TMCE/vendor ortamını veya faster-whisper bağımlılıklarını kontrol et. "
                f"Ayrıntı: {exc}"
            ),
        )
        return 3

    input_path = Path(args.input).expanduser().resolve()
    if not input_path.is_file():
        emit("error", message=f"Kaynak video bulunamadı: {input_path}")
        return 2

    requested_language = str(args.language or "und").strip().lower() or "und"
    target_language = str(args.translate_to or "").strip().lower()
    track_index = max(0, int(args.audio_track_index))
    job_key = cache_key(input_path, track_index, requested_language, args.quality_profile)
    job_dir = cache_root() / job_key
    job_dir.mkdir(parents=True, exist_ok=True)
    state_path = job_dir / "state.json"
    source_path = job_dir / "source.srt"
    translation_checkpoint = job_dir / f"translation-{target_language}.jsonl" if target_language else None
    translation_final = job_dir / f"translation-{target_language}.srt" if target_language else None

    stat = input_path.stat()
    state = read_json(state_path)
    state.update(
        {
            "version": CACHE_VERSION,
            "job_key": job_key,
            "input": str(input_path),
            "size": int(stat.st_size),
            "mtime_ms": int(stat.st_mtime_ns // 1_000_000),
            "audio_track_index": track_index,
            "requested_language": requested_language,
            "quality_profile": args.quality_profile,
            "source_path": str(source_path),
        }
    )
    state.setdefault("translations", {})
    atomic_json(state_path, state)

    progress_re = re.compile(r"^Progress:\s*(\d+)%")
    detected_re = re.compile(r"detected language=([a-zA-Z-]+)")
    last_progress = -1
    phase = "asr"
    detected_language = str(state.get("detected_language") or "").strip().lower()
    if not detected_language and requested_language not in {"", "und", "auto"}:
        try:
            detected_language = normalise_asr_language(requested_language)
        except Exception:
            detected_language = requested_language

    def publish_progress(raw_value: int, message: str) -> None:
        nonlocal last_progress
        raw_value = max(0, min(100, int(raw_value)))
        if target_language:
            if phase == "asr":
                value = int(round(raw_value * 0.70))
            elif phase == "translation":
                value = 70 + int(round(raw_value * 0.30))
            else:
                value = raw_value
        else:
            value = raw_value
        value = max(0, min(100, value))
        if value != last_progress:
            last_progress = value
            emit("progress", value=value, message=message)

    def log(message: str) -> None:
        nonlocal detected_language
        text = str(message or "").strip()
        if not text:
            return
        detected_match = detected_re.search(text)
        if detected_match:
            try:
                detected_language = normalise_asr_language(detected_match.group(1))
            except Exception:
                detected_language = detected_match.group(1).lower()
        match = progress_re.match(text)
        if match:
            publish_progress(int(match.group(1)), "Altyazı oluşturuluyor…")
            return
        emit("log", message=text)

    try:
        probe = ffprobe_path(auto_install=False)
        layout = detect_channel_layout(input_path, probe, track_index)
        if layout:
            emit("log", message=f"G-TMCE: ses kanal düzeni {layout}")
        emit("ready", message="G-TMCE altyazı motoru hazır.")

        source_ready = bool(state.get("source_ready")) and source_path.is_file() and source_path.stat().st_size > 0
        translation_requested = bool(target_language)

        def source_live(cues: list[Any], stage: str = "source") -> None:
            if translation_requested or stage != "source":
                return
            payload = cue_payload(cues)
            if payload:
                emit("live_cues", stage="source", cues=payload)

        if not source_ready:
            phase = "asr"
            emit("cache_status", source_ready=False, detected_language=detected_language)
            transcribe_audio_to_srt(
                input_path,
                requested_language,
                output_path=source_path,
                quality_profile=args.quality_profile,
                channel_layout=layout,
                translate_to=None,
                audio_stream_index=track_index,
                partial_callback=source_live,
                cancel_event=cancel_event,
                log=log,
            )
            # Persist the expensive ASR result before honouring a cancellation
            # that may have arrived in the tiny hand-off window between ASR and
            # translation.  If source.srt exists, Whisper has completed.
            if not detected_language:
                detected_language = requested_language if requested_language not in {"", "und", "auto"} else "und"
            state["source_ready"] = True
            state["detected_language"] = detected_language
            state["source_cue_count"] = len(read_srt(source_path))
            atomic_json(state_path, state)
            source_ready = True
            if cancel_event.is_set():
                emit("cancelled")
                return 130
            emit(
                "cache_status",
                source_ready=True,
                detected_language=detected_language,
                message=f"{detected_language} kaynak altyazı hazır",
            )
        else:
            if not detected_language:
                detected_language = str(state.get("detected_language") or requested_language or "und")
            state["source_ready"] = True
            state["detected_language"] = detected_language
            atomic_json(state_path, state)
            emit("log", message=f"G-TMCE cache: {detected_language} kaynak altyazı hazır; Whisper atlandı.")
            publish_progress(100, "Kaynak altyazı hazır; Whisper atlandı.")
            emit(
                "cache_status",
                source_ready=True,
                detected_language=detected_language,
                message="Kaynak altyazı cache'ten kullanılıyor; Whisper atlandı.",
            )

        source_cues = read_srt(source_path)
        if not source_cues:
            raise UserVisibleError("Kaynak altyazı cache'i boş veya okunamıyor.")

        # Keep the completed source transcript as a normal sidecar too.  A
        # cancelled translation must never make the expensive Whisper result
        # disappear from the user's movie folder.
        source_sidecar_language = detected_language if detected_language not in {"", "und", "auto"} else requested_language
        if source_sidecar_language not in {"", "und", "auto"}:
            source_sidecar = generated_subtitle_path(input_path, source_sidecar_language)
            shutil.copy2(source_path, source_sidecar)
            state["source_output"] = str(source_sidecar)
            atomic_json(state_path, state)

        if not translation_requested:
            final_source_language = detected_language if detected_language not in {"", "und", "auto"} else requested_language
            if final_source_language in {"", "und", "auto"}:
                raise UserVisibleError("Konuşma dili algılanamadı.")
            output = generated_subtitle_path(input_path, final_source_language)
            shutil.copy2(source_path, output)
            final_payload = cue_payload(source_cues)
            if final_payload:
                emit("live_cues", stage="source", replace=True, cues=final_payload)
            publish_progress(100, "Altyazı oluşturuldu.")
            emit("completed", output=str(output.resolve()))
            return 0

        source_language = detected_language
        if source_language in {"", "und", "auto"}:
            raise UserVisibleError("AI çeviri için kaynak konuşma dili algılanamadı.")
        source_language = normalise_asr_language(source_language)
        target_language = normalise_asr_language(target_language)
        if target_language == source_language:
            emit("log", message=f"G-TMCE AI Translation: kaynak zaten {source_language}; çeviri atlandı.")
            output = generated_translation_path(input_path, target_language)
            shutil.copy2(source_path, output)
            payload = cue_payload(source_cues)
            if payload:
                emit("live_cues", stage="translated", replace=True, cues=payload)
            state["translations"][target_language] = {
                "complete": True,
                "completed_units": 0,
                "total_units": 0,
                "output": str(output),
            }
            atomic_json(state_path, state)
            publish_progress(100, "Kaynak dil hedef dille aynı; hazır altyazı kullanıldı.")
            emit("completed", output=str(output.resolve()))
            return 0

        translation_state = state["translations"].get(target_language, {})
        translation_complete = bool(translation_state.get("complete")) and translation_final is not None and translation_final.is_file()
        output = generated_translation_path(input_path, target_language)
        if translation_complete:
            shutil.copy2(translation_final, output)
            final_cues = read_srt(translation_final)
            payload = cue_payload(final_cues)
            if payload:
                emit("live_cues", stage="translated", replace=True, cues=payload)
            emit("log", message=f"G-TMCE cache: {target_language} çevirisi hazır; AI modeli çalıştırılmadı.")
            emit(
                "cache_status",
                source_ready=True,
                detected_language=source_language,
                translation_ready=True,
                target_language=target_language,
                translation_progress=100,
            )
            publish_progress(100, "Çeviri cache'ten hazır.")
            emit("completed", output=str(output.resolve()))
            return 0

        assert translation_checkpoint is not None
        assert translation_final is not None
        completed_units, checkpoint_total, restored_cues = load_translation_checkpoint(
            translation_checkpoint, SubtitleCue
        )
        if restored_cues:
            payload = cue_payload(restored_cues)
            if payload:
                emit("live_cues", stage="translated", replace=True, cues=payload)
        if checkpoint_total > 0:
            percent = min(99, int(completed_units * 100 / checkpoint_total))
            emit(
                "cache_status",
                source_ready=True,
                detected_language=source_language,
                translation_ready=False,
                target_language=target_language,
                translation_completed_units=completed_units,
                translation_total_units=checkpoint_total,
                translation_progress=percent,
            )
            emit(
                "log",
                message=f"G-TMCE AI Translation: {target_language} çevirisi {completed_units}/{checkpoint_total} cache'ten geri yüklendi.",
            )

        phase = "translation"
        if completed_units == 0:
            # ASR is already done at this point, whether from this run or cache.
            publish_progress(0, "Kaynak altyazı hazır; AI çeviri başlıyor…")
        elif checkpoint_total > 0:
            resume_percent = int(completed_units * 100 / max(1, checkpoint_total))
            publish_progress(resume_percent, f"AI çeviri devam ediyor: %{resume_percent}")

        checkpoint_handle = translation_checkpoint.open("a", encoding="utf-8", buffering=1)

        def translated_live(cues: list[Any], stage: str = "translated") -> None:
            if stage != "translated":
                return
            payload = cue_payload(cues)
            if payload:
                emit("live_cues", stage="translated", cues=payload)

        def translation_checkpoint_callback(done_units: int, total_units: int, pieces: list[Any]) -> None:
            payload = cue_payload(pieces)
            checkpoint_handle.write(
                json.dumps(
                    {
                        "completed_units": int(done_units),
                        "total_units": int(total_units),
                        "cues": payload,
                    },
                    ensure_ascii=False,
                )
                + "\n"
            )
            checkpoint_handle.flush()
            percent = int(done_units * 100 / max(1, total_units))
            state["translations"][target_language] = {
                "complete": False,
                "completed_units": int(done_units),
                "total_units": int(total_units),
                "progress": percent,
            }
            atomic_json(state_path, state)
            publish_progress(percent, f"AI çeviri: %{percent} ({done_units}/{total_units})")
            emit(
                "cache_status",
                source_ready=True,
                detected_language=source_language,
                translation_ready=False,
                target_language=target_language,
                translation_completed_units=done_units,
                translation_total_units=total_units,
                translation_progress=percent,
            )

        try:
            translated = translate_cues_with_ai(
                source_cues,
                source_language,
                target_language,
                partial_callback=translated_live,
                resume_unit_offset=completed_units,
                initial_translated=restored_cues,
                unit_callback=translation_checkpoint_callback,
                cancel_event=cancel_event,
                log=log,
            )
        finally:
            checkpoint_handle.close()

        if cancel_event.is_set():
            emit("cancelled")
            return 130
        if not translated:
            raise UserVisibleError("AI çeviri çıktı üretmedi.")

        write_srt(translation_final, translated)
        shutil.copy2(translation_final, output)
        state["translations"][target_language] = {
            "complete": True,
            "completed_units": int(state["translations"].get(target_language, {}).get("completed_units", 0)),
            "total_units": int(state["translations"].get(target_language, {}).get("total_units", 0)),
            "progress": 100,
            "cache_path": str(translation_final),
            "output": str(output),
        }
        atomic_json(state_path, state)
        final_payload = cue_payload(read_srt(translation_final))
        if final_payload:
            emit("live_cues", stage="translated", replace=True, cues=final_payload)
        publish_progress(100, "AI çeviri tamamlandı.")
        emit(
            "cache_status",
            source_ready=True,
            detected_language=source_language,
            translation_ready=True,
            target_language=target_language,
            translation_progress=100,
        )
        emit("completed", output=str(output.resolve()))
        return 0
    except OperationCancelled:
        emit("cancelled")
        return 130
    except UserVisibleError as exc:
        emit("error", message=str(exc))
        return 2
    except Exception as exc:
        emit("error", message=f"G-TMCE ASR/çeviri hatası: {exc}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
