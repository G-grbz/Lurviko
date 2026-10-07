"""Deterministic subtitle stream; no AI downloads or inference required."""
import json
from pathlib import Path
import sys
import time


def emit(event, **payload):
    print(json.dumps(dict(event=event, **payload)), flush=True)


video = Path(sys.argv[sys.argv.index("--input") + 1])
emit("live_cues", stage="source", cues=[dict(start=0, end=20, text="EN original subtitle")])
time.sleep(0.5)
if "--translate-to" in sys.argv:
    emit("live_cues", stage="translated", cues=[dict(start=0, end=3, text="TR translated subtitle")])
    text = "TR translated subtitle"
    end = "03"
else:
    text = "EN original subtitle"
    end = "20"
time.sleep(1)
output = video.with_suffix(".result.srt")
output.write_text(f"1\n00:00:00,000 --> 00:00:{end},000\n{text}\n\n")
emit("completed", output=str(output))
