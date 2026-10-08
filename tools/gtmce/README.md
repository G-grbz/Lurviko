# G-TMCE engine bridge

This directory contains the headless subset of G-TMCE used by Lurviko's video
viewer for Whisper subtitle generation. `worker.py` communicates with the C++
`GtmceManager` via newline-delimited JSON.

On Linux the bridge reuses `/opt/G-TMCE/vendor` when a normal G-TMCE install is
present, so large Python/CUDA wheels are not duplicated. Override the locations
with `GTMCE_VENDOR_DIR`, `LURVIKO_GTMCE_PYTHON`, or `LURVIKO_GTMCE_WORKER`.

## Resumable jobs

Lurviko keeps durable ASR/translation state under
`$XDG_CACHE_HOME/Lurviko/gtmce/jobs` (falling back to
`~/.cache/Lurviko/gtmce/jobs`). The job identity includes the canonical media
path, file size/mtime, audio-stream index, requested source language and Whisper
profile. Once Whisper finishes, the source-language SRT is cached and also
written next to the video. AI translation checkpoints are appended per
translation unit, so cancelling a translation does not discard completed work;
restarting the same job skips Whisper and resumes from the saved unit.

Set `LURVIKO_GTMCE_CACHE_DIR` to override the cache root.
