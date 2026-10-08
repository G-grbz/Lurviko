# Lurviko Subtitle AI

Headless subtitle creation and local AI translation engine used directly by Lurviko.
It has no GUI dependency and does not require another application to be installed.

Runtime Python packages can live in `~/.local/share/Lurviko/subtitle-ai/vendor` or
be supplied with `LURVIKO_SUBTITLE_AI_VENDOR_DIR`. Model caches live under
`$XDG_CACHE_HOME/Lurviko/subtitle-ai/models` by default.

Existing subtitle translation has a translation-only runtime path: it does not
load or install `faster-whisper`. Whisper is provisioned only when audio
transcription is requested.
