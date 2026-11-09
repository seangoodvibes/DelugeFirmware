#!/usr/bin/env python3
"""Compile the real freeze controller against host doubles for the SD/audio boundaries."""

import subprocess
import tempfile
from pathlib import Path

here = Path(__file__).resolve().parent
root = here.parents[2]
headers = [
    "definitions_cxx.hpp",
    "dsp/stereo_sample.h",
    "model/sample/sample_holder.h",
    "model/sample/sample_playback_guide.h",
    "model/clip/instrument_clip.h",
    "model/sample/sample.h",
    "model/sample/sample_recorder.h",
    "model/voice/voice_sample.h",
    "playback/playback_handler.h",
    "processing/engines/audio_engine.h",
    "storage/cluster/cluster_priority_queue.h",
]
with tempfile.TemporaryDirectory(prefix="clip-freeze-test-") as temp:
    build = Path(temp)
    for header in headers:
        target = build / header
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(f'#include "{here / "stubs.h"}"\n')
    binary = build / "test"
    subprocess.run(
        [
            "clang++",
            "-std=c++20",
            "-g",
            "-fsanitize=address,undefined",
            "-I",
            str(build),
            "-I",
            str(root / "src/deluge"),
            str(root / "src/deluge/model/clip/clip_freeze.cpp"),
            str(here / "main.cpp"),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary)], check=True)
