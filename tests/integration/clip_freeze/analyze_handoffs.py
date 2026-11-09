#!/usr/bin/env python3
"""Summarize captured thaw dips and export before/after with shared gain.

These are different live performances, not sample-identical A/Bs. The minimum
100-ms level over the capture describes a dropout; modulation can also change
that level. The first second is still frozen and supplies the reference energy.
"""
import array
import json
import math
from pathlib import Path
from drone_analysis import export_pair

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/clip-freeze/handoff-comparison"
OUT.mkdir(parents=True, exist_ok=True)
report = {}
for patch in ("blade", "brick"):
    pair = []
    measurements = {}
    for label, name in [("before", f"crossfade-{patch}-baseline"), ("after", f"handoff-{patch}")]:
        path = ROOT / "build/clip-freeze" / name / "thaw.s32le"
        audio = array.array("i", path.read_bytes())
        pair.append(audio)
        window = 4410 * 2
        reference = math.sqrt(sum(x*x for x in audio[:44100*2]) / (44100*2))
        levels = [math.sqrt(sum(x*x for x in audio[i:i+window]) / window) / reference
                  for i in range(0, len(audio)-window+1, window)]
        measurements[label] = {
            "minimum_100ms_level_relative_to_first_second": min(levels),
            "seconds_below_20_percent_reference": sum(x < .2 for x in levels) / 10,
            "levels": levels,
        }
    folder = OUT / patch
    folder.mkdir(exist_ok=True)
    export_pair(folder, *pair)
    (folder / "live.wav").replace(folder / "before.wav")
    (folder / "frozen.wav").replace(folder / "after.wav")
    report[patch] = measurements
(OUT / "results.json").write_text(json.dumps(report, indent=2))
for patch, data in report.items():
    print(patch, {k: {n: v for n,v in values.items() if n != "levels"} for k,values in data.items()})
