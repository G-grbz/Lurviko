#!/usr/bin/env python3
"""JSON-lines worker for Lurviko's built-in subtitle AI pipeline.

Two source modes are supported:
  * transcribe: audio -> Whisper -> optional local AI translation
  * subtitle: existing embedded/sidecar subtitle -> local AI translation

Completed source subtitles and translation checkpoints are durable, so cancelling
never discards work that can be reused on the next run.
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import subprocess
import sys
import threading
from typing import Any

CACHE_VERSION = 2
EXISTING_CACHE_VERSION = 3


def emit(event: str, **payload: Any) -> None:
    print(json.dumps({"event": event, **payload}, ensure_ascii=False), flush=True)


def user_data_vendor() -> Path:
    override = os.environ.get("LURVIKO_SUBTITLE_AI_VENDOR_DIR", "").strip()
    if override:
        return Path(override).expanduser()
    if os.name == "nt":
        base = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))
        return base / "Lurviko" / "subtitle-ai" / "vendor"
    if sys.platform == "darwin":
        return Path.home() / "Library" / "Application Support" / "Lurviko" / "subtitle-ai" / "vendor"
    base = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local" / "share"))
    return base / "Lurviko" / "subtitle-ai" / "vendor"


def configure_import_paths() -> Path:
    here = Path(__file__).resolve().parent
    if str(here) not in sys.path:
        sys.path.insert(0, str(here))
    candidates = [here / "vendor", user_data_vendor()]
    configured = os.environ.get("LURVIKO_SUBTITLE_AI_VENDOR_DIR", "").strip()
    if configured:
        candidates.insert(0, Path(configured).expanduser())
    for vendor in candidates:
        if vendor.is_dir() and str(vendor) not in sys.path:
            sys.path.insert(0, str(vendor))
    return user_data_vendor()


def runtime_ready(*, translation_only: bool = False) -> bool:
    try:
        import ctranslate2  # noqa: F401
        import sentencepiece  # noqa: F401
        import huggingface_hub  # noqa: F401
        if not translation_only:
            import faster_whisper  # noqa: F401
        return True
    except Exception:
        return False


def _private_cuda_lib_dirs(vendor: Path) -> list[Path]:
    return [
        vendor / "nvidia" / "cublas" / "lib",
        vendor / "nvidia" / "cudnn" / "lib",
    ]


def _preload_private_cuda_runtime(vendor: Path) -> bool:
    """Load Lurviko's private CUDA libraries into this already-running worker.

    LD_LIBRARY_PATH is captured by the dynamic loader when the worker starts.
    On the very first run the CUDA wheels can be installed *after* process
    startup, so merely changing the environment is insufficient. Loading the
    SONAMEs by absolute path with RTLD_GLOBAL makes them available to
    CTranslate2 immediately; later launches also receive the paths from the
    C++ manager through LD_LIBRARY_PATH.
    """
    if os.name != "posix":
        return False

    directories = [path for path in _private_cuda_lib_dirs(vendor) if path.is_dir()]
    if not directories:
        return False

    old_ld = os.environ.get("LD_LIBRARY_PATH", "")
    merged = [str(path) for path in directories]
    if old_ld:
        merged.append(old_ld)
    os.environ["LD_LIBRARY_PATH"] = os.pathsep.join(merged)

    # Dependency order matters for a first-run preload.  CUBLAS is sufficient
    # for the text translation model; CUDNN is also loaded when available for
    # the Whisper path. Missing optional pieces are deliberately ignored.
    preferred = (
        "libcublasLt.so.12",
        "libcublas.so.12",
        "libcudnn.so.9",
    )
    loaded_cublas = False
    for soname in preferred:
        candidate = next((directory / soname for directory in directories if (directory / soname).is_file()), None)
        if candidate is None:
            continue
        try:
            ctypes.CDLL(str(candidate), mode=ctypes.RTLD_GLOBAL)
            if soname == "libcublas.so.12":
                loaded_cublas = True
        except OSError as exc:
            emit("log", message=f"Lurviko Subtitle AI: özel CUDA kitaplığı yüklenemedi ({soname}): {exc}")
    return loaded_cublas


def _ensure_private_cuda_runtime(vendor: Path) -> bool:
    if not (os.name == "posix" and platform.machine().lower() in {"x86_64", "amd64"} and shutil.which("nvidia-smi")):
        return False

    cublas = vendor / "nvidia" / "cublas" / "lib" / "libcublas.so.12"
    cudnn = vendor / "nvidia" / "cudnn" / "lib" / "libcudnn.so.9"
    auto_install = os.environ.get("LURVIKO_SUBTITLE_AI_AUTO_INSTALL", "1").strip().lower() not in {"0", "false", "no", "off"}
    if (not cublas.is_file() or not cudnn.is_file()) and auto_install:
        vendor.mkdir(parents=True, exist_ok=True)
        emit("progress", value=0, message="Lurviko Subtitle AI GPU çalışma ortamı hazırlanıyor…")
        emit("log", message="Lurviko Subtitle AI: NVIDIA için özel CUDA çalışma zamanı hazırlanıyor…")
        completed = subprocess.run(
            [sys.executable, "-m", "pip", "install", "--isolated", "--disable-pip-version-check",
             "--no-warn-conflicts", "--upgrade", "--ignore-installed", "--target", str(vendor),
             "nvidia-cublas-cu12", "nvidia-cudnn-cu12==9.*"],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False,
        )
        if completed.returncode != 0:
            emit("log", message="Lurviko Subtitle AI: özel CUDA runtime kurulamadı; CPU/int8 kullanılacak.")

    return _preload_private_cuda_runtime(vendor)


def bootstrap_runtime(vendor: Path, *, translation_only: bool = False) -> None:
    packages_ready = runtime_ready(translation_only=translation_only)
    auto_install = os.environ.get("LURVIKO_SUBTITLE_AI_AUTO_INSTALL", "1").strip().lower() not in {"0", "false", "no", "off"}

    if not packages_ready and auto_install:
        requirements = Path(__file__).resolve().parent / (
            "requirements-translation.txt" if translation_only else "requirements.txt"
        )
        if requirements.is_file():
            vendor.mkdir(parents=True, exist_ok=True)
            emit("progress", value=0, message="Lurviko Subtitle AI çalışma ortamı hazırlanıyor…")
            emit("log", message=f"Lurviko Subtitle AI: Python paketleri {vendor} dizinine kuruluyor.")
            cmd = [
                sys.executable, "-m", "pip", "install", "--isolated", "--disable-pip-version-check",
                "--no-warn-conflicts", "--upgrade", "--ignore-installed", "--target", str(vendor),
                "-r", str(requirements),
            ]
            completed = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False)
            if completed.returncode != 0:
                tail = (completed.stdout or "")[-1800:]
                raise RuntimeError(f"Python çalışma ortamı kurulamadı. {tail}")
            if str(vendor) not in sys.path:
                sys.path.insert(0, str(vendor))

    # Do this even when the Python packages were already present.  The old
    # implementation returned above and therefore never installed CUBLAS/CUDNN.
    # If private CUDA cannot be prepared, translation/ASR will fall back to CPU.
    gpu_ready = _ensure_private_cuda_runtime(vendor)
    if gpu_ready:
        os.environ["LURVIKO_SUBTITLE_AI_PRIVATE_CUDA_READY"] = "1"
        emit("log", message="Lurviko Subtitle AI: özel CUDA çalışma zamanı hazır.")
    else:
        os.environ["LURVIKO_SUBTITLE_AI_PRIVATE_CUDA_READY"] = "0"
        emit("log", message="Lurviko Subtitle AI: CUDA runtime kullanılamıyor; CPU fallback etkin.")

    if not runtime_ready(translation_only=translation_only):
        raise RuntimeError("Gerekli Python paketleri yüklenemedi.")
    emit("log", message="Lurviko Subtitle AI: çalışma ortamı hazır.")


def cache_root() -> Path:
    configured = os.environ.get("LURVIKO_SUBTITLE_AI_CACHE_DIR", "").strip()
    if configured:
        return Path(configured).expanduser()
    xdg = os.environ.get("XDG_CACHE_HOME", "").strip()
    base = Path(xdg).expanduser() if xdg else Path.home() / ".cache"
    return base / "Lurviko" / "subtitle-ai" / "jobs"


def asr_cache_key(input_path: Path, audio_track_index: int, language: str, quality_profile: str) -> str:
    stat = input_path.stat()
    mtime_ms = stat.st_mtime_ns // 1_000_000
    material = (
        f"v{CACHE_VERSION}\n{input_path.resolve()}\n{stat.st_size}\n{mtime_ms}\n"
        f"{max(0, int(audio_track_index))}\n{language.strip().lower()}\n{quality_profile.strip().lower()}\n"
    )
    return hashlib.sha256(material.encode("utf-8")).hexdigest()


def subtitle_cache_key(
    media_path: Path,
    source_kind: str,
    subtitle_path: Path | None,
    subtitle_track_index: int,
    source_language: str,
) -> str:
    media_stat = media_path.stat()
    parts = [
        f"existing-v{EXISTING_CACHE_VERSION}", str(media_path.resolve()), str(media_stat.st_size),
        str(media_stat.st_mtime_ns // 1_000_000), source_kind, str(max(0, subtitle_track_index)),
        source_language.strip().lower() or "und",
    ]
    if source_kind == "external" and subtitle_path is not None:
        sub_stat = subtitle_path.stat()
        parts.extend([str(subtitle_path.resolve()), str(sub_stat.st_size), str(sub_stat.st_mtime_ns // 1_000_000)])
    return hashlib.sha256(("\n".join(parts) + "\n").encode("utf-8")).hexdigest()


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
    from gfile_subtitle_ai.transcription import readable_subtitle_cues
    result: list[dict[str, Any]] = []
    for cue in readable_subtitle_cues(cues):
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
                    break
                index = int(record.get("completed_units", 0) or 0)
                total = int(record.get("total_units", 0) or 0)
                if index != completed_units + 1 or total <= 0:
                    break
                restored: list[Any] = []
                for piece in record.get("cues") or []:
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


def detect_channel_layout(input_path: Path, ffprobe: str | None, audio_track_index: int = 0) -> str | None:
    if not ffprobe:
        return None
    try:
        completed = subprocess.run(
            [ffprobe, "-v", "error", "-select_streams", f"a:{max(0, audio_track_index)}",
             "-show_entries", "stream=channel_layout", "-of", "json", str(input_path)],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, timeout=12, check=False,
        )
        payload = json.loads(completed.stdout or "{}") if completed.returncode == 0 else {}
        streams = payload.get("streams") or []
        value = str(streams[0].get("channel_layout") or "").strip() if streams else ""
        return value or None
    except Exception:
        return None


def import_existing_subtitle(
    *, media_path: Path, source_kind: str, subtitle_path: Path | None, subtitle_track_index: int,
    output_path: Path, ffmpeg: str,
) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = output_path.with_suffix(".importing.srt")
    temporary.unlink(missing_ok=True)
    if source_kind == "external":
        if subtitle_path is None or not subtitle_path.is_file():
            raise RuntimeError("Harici altyazı dosyası bulunamadı.")
        if subtitle_path.suffix.lower() == ".srt":
            shutil.copy2(subtitle_path, temporary)
        else:
            cmd = [ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-i", str(subtitle_path),
                   "-map", "0:0", "-c:s", "srt", str(temporary)]
            completed = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False)
            if completed.returncode != 0:
                raise RuntimeError((completed.stderr or "Altyazı SRT biçimine dönüştürülemedi.").strip())
    elif source_kind == "embedded":
        cmd = [ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-i", str(media_path),
               "-map", f"0:s:{max(0, subtitle_track_index)}", "-c:s", "srt", str(temporary)]
        completed = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False)
        if completed.returncode != 0:
            raise RuntimeError((completed.stderr or "Gömülü altyazı çıkarılamadı.").strip())
    else:
        raise RuntimeError(f"Bilinmeyen altyazı kaynağı: {source_kind}")
    if not temporary.is_file() or temporary.stat().st_size <= 0:
        raise RuntimeError("Seçili altyazı metin olarak okunamadı.")
    temporary.replace(output_path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--mode", default="transcribe", choices=("transcribe", "subtitle"))
    parser.add_argument("--language", default="und")
    parser.add_argument("--translate-to", default="")
    parser.add_argument("--audio-track-index", type=int, default=0)
    parser.add_argument("--quality-profile", default="slow", choices=("fast", "medium", "slow", "slower"))
    parser.add_argument("--subtitle-kind", default="")
    parser.add_argument("--subtitle-path", default="")
    parser.add_argument("--subtitle-track-index", type=int, default=0)
    args = parser.parse_args()

    vendor = configure_import_paths()
    cancel_event = threading.Event()

    def request_cancel(_signum: int, _frame: Any) -> None:
        cancel_event.set()
        emit("log", message="Lurviko Subtitle AI: iptal isteği alındı; tamamlanan önbellek korunuyor…")

    signal.signal(signal.SIGTERM, request_cancel)
    signal.signal(signal.SIGINT, request_cancel)

    try:
        bootstrap_runtime(vendor, translation_only=(args.mode == "subtitle"))
        from gfile_subtitle_ai.core import OperationCancelled, UserVisibleError, ffmpeg_path, ffprobe_path
        from gfile_subtitle_ai.transcription import (
            SubtitleCue, generated_subtitle_path, generated_translation_path, normalise_asr_language,
            read_srt, transcribe_audio_to_srt, translate_cues_with_ai, write_srt,
        )
    except Exception as exc:
        emit("error", message=f"Lurviko Subtitle AI çalışma ortamı yüklenemedi: {exc}")
        return 3

    input_path = Path(args.input).expanduser().resolve()
    if not input_path.is_file():
        emit("error", message=f"Kaynak video bulunamadı: {input_path}")
        return 2

    mode = args.mode
    requested_language = str(args.language or "und").strip().lower() or "und"
    target_language = str(args.translate_to or "").strip().lower()
    audio_track_index = max(0, int(args.audio_track_index))
    subtitle_track_index = max(0, int(args.subtitle_track_index))
    subtitle_kind = str(args.subtitle_kind or "").strip().lower()
    subtitle_path = Path(args.subtitle_path).expanduser().resolve() if args.subtitle_path else None

    if mode == "subtitle" and not target_language:
        emit("error", message="Mevcut altyazı çevirisi için hedef dil seçilmelidir.")
        return 2

    try:
        if mode == "subtitle":
            job_key = subtitle_cache_key(input_path, subtitle_kind, subtitle_path, subtitle_track_index, requested_language)
        else:
            job_key = asr_cache_key(input_path, audio_track_index, requested_language, args.quality_profile)
    except OSError as exc:
        emit("error", message=f"Kaynak bilgileri okunamadı: {exc}")
        return 2

    job_dir = cache_root() / job_key
    job_dir.mkdir(parents=True, exist_ok=True)
    state_path = job_dir / "state.json"
    source_path = job_dir / "source.srt"
    translation_checkpoint = job_dir / f"translation-{target_language}.jsonl" if target_language else None
    translation_final = job_dir / f"translation-{target_language}.srt" if target_language else None

    state = read_json(state_path)
    media_stat = input_path.stat()
    state.update({
        "version": CACHE_VERSION if mode == "transcribe" else EXISTING_CACHE_VERSION,
        "mode": mode,
        "job_key": job_key,
        "input": str(input_path),
        "size": int(media_stat.st_size),
        "mtime_ms": int(media_stat.st_mtime_ns // 1_000_000),
        "requested_language": requested_language,
        "source_path": str(source_path),
    })
    if mode == "transcribe":
        state.update({"audio_track_index": audio_track_index, "quality_profile": args.quality_profile})
    else:
        state.update({
            "subtitle_kind": subtitle_kind, "subtitle_track_index": subtitle_track_index,
            "subtitle_file": str(subtitle_path) if subtitle_path else "",
        })
    state.setdefault("translations", {})
    atomic_json(state_path, state)

    progress_re = re.compile(r"^Progress:\s*(\d+)%")
    detected_re = re.compile(r"detected language=([a-zA-Z-]+)")
    last_progress = -1
    phase = "asr" if mode == "transcribe" else "source"
    detected_language = str(state.get("detected_language") or "").strip().lower()
    if not detected_language and requested_language not in {"", "und", "auto"}:
        try:
            detected_language = normalise_asr_language(requested_language)
        except Exception:
            detected_language = requested_language

    def publish_progress(raw_value: int, message: str) -> None:
        nonlocal last_progress
        raw_value = max(0, min(100, int(raw_value)))
        if target_language and mode == "transcribe":
            value = int(round(raw_value * 0.70)) if phase == "asr" else 70 + int(round(raw_value * 0.30))
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
        ffmpeg = ffmpeg_path()
        probe = ffprobe_path(auto_install=False)
        emit("ready", message="Lurviko Subtitle AI hazır.")
        source_ready = bool(state.get("source_ready")) and source_path.is_file() and source_path.stat().st_size > 0
        translation_requested = bool(target_language)

        if mode == "subtitle":
            if not source_ready:
                publish_progress(0, "Seçili altyazı hazırlanıyor…")
                import_existing_subtitle(
                    media_path=input_path, source_kind=subtitle_kind, subtitle_path=subtitle_path,
                    subtitle_track_index=subtitle_track_index, output_path=source_path, ffmpeg=ffmpeg,
                )
                cues = read_srt(source_path)
                if not cues:
                    raise UserVisibleError("Seçili altyazı boş veya metin tabanlı değil.")
                detected_language = requested_language or "und"
                state["source_ready"] = True
                state["detected_language"] = detected_language
                state["source_cue_count"] = len(cues)
                atomic_json(state_path, state)
                emit("cache_status", source_ready=True, detected_language=detected_language)
            else:
                detected_language = str(state.get("detected_language") or requested_language or "und")
                emit("log", message="Lurviko Subtitle AI: seçili altyazı önbellekten kullanılıyor; Whisper çalıştırılmadı.")
            source_cues = read_srt(source_path)
        else:
            layout = detect_channel_layout(input_path, probe, audio_track_index)
            if layout:
                emit("log", message=f"Lurviko Subtitle AI: ses kanal düzeni {layout}")

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
                    input_path, requested_language, output_path=source_path,
                    quality_profile=args.quality_profile, channel_layout=layout, translate_to=None,
                    audio_stream_index=audio_track_index, partial_callback=source_live,
                    cancel_event=cancel_event, log=log,
                )
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
                emit("cache_status", source_ready=True, detected_language=detected_language)
            else:
                if not detected_language:
                    detected_language = str(state.get("detected_language") or requested_language or "und")
                state["source_ready"] = True
                state["detected_language"] = detected_language
                atomic_json(state_path, state)
                emit("log", message=f"Lurviko Subtitle AI: {detected_language} kaynak altyazı hazır; Whisper atlandı.")
                publish_progress(100, "Kaynak altyazı hazır; Whisper atlandı.")

            source_cues = read_srt(source_path)
            if not source_cues:
                raise UserVisibleError("Kaynak altyazı önbelleği boş veya okunamıyor.")

            source_sidecar_language = detected_language if detected_language not in {"", "und", "auto"} else requested_language
            if source_sidecar_language not in {"", "und", "auto"}:
                source_sidecar = generated_subtitle_path(input_path, source_sidecar_language)
                write_srt(source_sidecar, source_cues)
                state["source_output"] = str(source_sidecar)
                atomic_json(state_path, state)

            if not translation_requested:
                final_source_language = detected_language if detected_language not in {"", "und", "auto"} else requested_language
                if final_source_language in {"", "und", "auto"}:
                    raise UserVisibleError("Konuşma dili algılanamadı.")
                output = generated_subtitle_path(input_path, final_source_language)
                write_srt(output, source_cues)
                payload = cue_payload(source_cues)
                if payload:
                    emit("live_cues", stage="source", replace=True, cues=payload)
                publish_progress(100, "Altyazı oluşturuldu.")
                emit("completed", output=str(output.resolve()))
                return 0

        if not source_cues:
            raise UserVisibleError("Kaynak altyazı boş veya okunamıyor.")

        source_language = detected_language or requested_language or "und"
        if source_language not in {"", "und", "auto"}:
            source_language = normalise_asr_language(source_language)
        else:
            source_language = "und"
        target_language = normalise_asr_language(target_language)
        if source_language != "und" and target_language == source_language:
            output = generated_translation_path(input_path, target_language)
            write_srt(output, source_cues)
            payload = cue_payload(source_cues)
            if payload:
                emit("live_cues", stage="translated", replace=True, cues=payload)
            state["translations"][target_language] = {"complete": True, "completed_units": 0, "total_units": 0, "progress": 100, "output": str(output)}
            atomic_json(state_path, state)
            publish_progress(100, "Kaynak dil hedef dille aynı; hazır altyazı kullanıldı.")
            emit("completed", output=str(output.resolve()))
            return 0

        translation_state = state["translations"].get(target_language, {})
        output = generated_translation_path(input_path, target_language)
        translation_complete = bool(translation_state.get("complete")) and translation_final is not None and translation_final.is_file()
        if translation_complete:
            cached_cues = read_srt(translation_final)
            write_srt(output, cached_cues)
            payload = cue_payload(cached_cues)
            if payload:
                emit("live_cues", stage="translated", replace=True, cues=payload)
            publish_progress(100, "Çeviri önbellekten hazır.")
            emit("completed", output=str(output.resolve()))
            return 0

        assert translation_checkpoint is not None and translation_final is not None
        completed_units, checkpoint_total, restored_cues = load_translation_checkpoint(translation_checkpoint, SubtitleCue)
        if restored_cues:
            payload = cue_payload(restored_cues)
            if payload:
                emit("live_cues", stage="translated", replace=True, cues=payload)
        if checkpoint_total > 0:
            percent = min(99, int(completed_units * 100 / checkpoint_total))
            emit("cache_status", source_ready=True, detected_language=source_language, translation_ready=False,
                 target_language=target_language, translation_completed_units=completed_units,
                 translation_total_units=checkpoint_total, translation_progress=percent)
            emit("log", message=f"Lurviko Subtitle AI: {target_language} çevirisi {completed_units}/{checkpoint_total} önbellekten geri yüklendi.")

        phase = "translation"
        resume_percent = int(completed_units * 100 / max(1, checkpoint_total)) if checkpoint_total > 0 else 0
        publish_progress(resume_percent, "AI çeviri başlıyor…" if completed_units == 0 else f"AI çeviri devam ediyor: %{resume_percent}")
        checkpoint_handle = translation_checkpoint.open("a", encoding="utf-8", buffering=1)

        def translated_live(cues: list[Any], stage: str = "translated") -> None:
            if stage == "translated":
                payload = cue_payload(cues)
                if payload:
                    emit("live_cues", stage="translated", cues=payload)

        def checkpoint_callback(done_units: int, total_units: int, pieces: list[Any]) -> None:
            payload = cue_payload(pieces)
            checkpoint_handle.write(json.dumps({"completed_units": int(done_units), "total_units": int(total_units), "cues": payload}, ensure_ascii=False) + "\n")
            checkpoint_handle.flush()
            percent = int(done_units * 100 / max(1, total_units))
            state["translations"][target_language] = {
                "complete": False, "completed_units": int(done_units), "total_units": int(total_units), "progress": percent,
            }
            atomic_json(state_path, state)
            publish_progress(percent, f"AI çeviri: %{percent} ({done_units}/{total_units})")
            emit("cache_status", source_ready=True, detected_language=source_language, translation_ready=False,
                 target_language=target_language, translation_completed_units=done_units,
                 translation_total_units=total_units, translation_progress=percent)

        try:
            translated = translate_cues_with_ai(
                source_cues, source_language, target_language, partial_callback=translated_live,
                resume_unit_offset=completed_units, initial_translated=restored_cues,
                unit_callback=checkpoint_callback, cancel_event=cancel_event, log=log,
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
        current = state["translations"].get(target_language, {})
        state["translations"][target_language] = {
            "complete": True, "completed_units": int(current.get("completed_units", 0)),
            "total_units": int(current.get("total_units", 0)), "progress": 100,
            "cache_path": str(translation_final), "output": str(output),
        }
        atomic_json(state_path, state)
        payload = cue_payload(read_srt(translation_final))
        if payload:
            emit("live_cues", stage="translated", replace=True, cues=payload)
        publish_progress(100, "AI çeviri tamamlandı.")
        emit("completed", output=str(output.resolve()))
        return 0
    except OperationCancelled:
        emit("cancelled")
        return 130
    except UserVisibleError as exc:
        emit("error", message=str(exc))
        return 2
    except Exception as exc:
        emit("error", message=f"Lurviko Subtitle AI işlemi başarısız: {exc}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
