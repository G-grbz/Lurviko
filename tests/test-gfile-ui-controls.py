#!/usr/bin/env python3
"""Reject covered raw Qt Quick Controls in application-facing QML."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
TARGETS = [ROOT / "qml" / "Main.qml", *sorted((ROOT / "qml" / "pages").glob("*.qml")), *sorted((ROOT / "qml" / "components").glob("*.qml"))]
RAW = (
    "Button", "ToolButton", "TextField", "Slider", "Switch", "CheckBox",
    "ComboBox", "SpinBox", "ProgressBar", "BusyIndicator",
)
pattern = re.compile(r"(?<![A-Za-z0-9_])(" + "|".join(RAW) + r")\s*\{")

failures = []
for path in TARGETS:
    text = path.read_text(encoding="utf-8")
    for lineno, line in enumerate(text.splitlines(), 1):
        match = pattern.search(line)
        if match:
            failures.append(f"{path.relative_to(ROOT)}:{lineno}: raw {match.group(1)}")

if failures:
    print("G-File UI control guard failed:")
    print("\n".join(failures))
    sys.exit(1)

print(f"G-File UI control guard: OK ({len(TARGETS)} QML files checked)")
