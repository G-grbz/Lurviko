# Lurviko Subtitle AI

Headless subtitle creation and local AI translation engine used directly by Lurviko.
It has no GUI dependency and does not require another application to be installed.

Runtime Python packages can live in `~/.local/share/Lurviko/subtitle-ai/vendor` or
be supplied with `LURVIKO_SUBTITLE_AI_VENDOR_DIR`. Model caches live under
`$XDG_CACHE_HOME/Lurviko/subtitle-ai/models` by default.

Existing subtitle translation has a translation-only runtime path: it does not
load or install `faster-whisper`. Whisper is provisioned only when audio
transcription is requested.

## Translation quality

The translation stage uses G-TMCE's newer MADLAD decoding and quality checks:
complete EOS-terminated inputs, sentence context across adjacent cues, preserved
speaker/style boundaries, and alternative ranking by forward fluency and source
reconstruction with the same local model. Suspicious outputs receive bounded
retries; these checks improve consistency but cannot guarantee perfect meaning.
The default balanced profile applies to existing subtitles and Whisper output.

Live output, checkpoints and final SRT files share the same readable-cue layout.
Translation cache entries have a separate engine version: upgrading this stage
recomputes old translations while retaining extracted subtitles and Whisper
results. Ranking uses bounded caches; GPU memory pressure reduces batch/beam
sizes and can fall back to CPU without discarding completed cues. The extra
quality scoring can increase translation time, particularly on CPU.

## Download permission and dependency locks

Package installation and remote model downloads default to disabled. Enable
the download checkbox in the subtitle dialog to allow them; the permission is
saved and can be revoked there. Newly provisioned packages use exact versions
and SHA256 hashes from `requirements.txt`, `requirements-translation.txt` and
optional `requirements-gpu.txt`. Pip uses `--require-hashes --only-binary=:all:`
and PyPI; no source build or unconditional upgrade occurs. Python 3.11+ is
required. Existing user-managed runtimes are not automatically upgraded.
An approved install repairs an incomplete vendor directory with the locked
versions; pip's target replacement flag is used only for that repair.

For headless use, explicitly set `LURVIKO_SUBTITLE_AI_AUTO_INSTALL=1` for
missing-package installation and `LURVIKO_SUBTITLE_AI_ALLOW_MODEL_DOWNLOAD=1`
for model downloads. Leave these unset/zero for offline operation.

`model-revisions.json` pins built-in repositories to full commit SHAs.
Custom remote models require `LURVIKO_SUBTITLE_AI_ASR_MODEL_REVISION` or
`LURVIKO_SUBTITLE_AI_TRANSLATION_MODEL_REVISION` containing a full 40-character
commit. Local directories remain supported; old model caches are retained.

Maintainers edit/review the `.in` declarations and regenerate the locks:

```bash
uv pip compile --universal --python-version 3.11 --generate-hashes requirements.in -o requirements.txt
uv pip compile --universal --python-version 3.11 --generate-hashes requirements-translation.in -o requirements-translation.txt
uv pip compile --universal --python-version 3.11 --generate-hashes requirements-gpu.in -o requirements-gpu.txt
```

Review dependency changes/model revisions before publishing a new version.
