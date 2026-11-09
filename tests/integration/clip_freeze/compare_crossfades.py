#!/usr/bin/env python3
"""Compare fixed source passages with production freeze and modeled loop fades.

No listening-score claim: report join steps and local cancellation separately.
The 100-sample pre-margin model is additionally checked through real AudioClips
by run_crossfade_delugemu.py. All WAVs retain one common gain per source.
"""
import argparse
import array
import json
import math
from pathlib import Path
import subprocess
import sys
import wave

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
RATE = 44100


def read_pcm(path):
    data = array.array("i", path.read_bytes())
    if sys.byteorder != "little":
        data.byteswap()
    return list(zip(data[::2], data[1::2]))


def write_pcm(path, frames):
    data = array.array("i", (round(x) for frame in frames for x in frame))
    if sys.byteorder != "little":
        data.byteswap()
    path.write_bytes(data.tobytes())


def write_wav(path, frames):
    # Preserve signed PCM level, with 24-bit quantization matching the SD cache.
    data = bytearray()
    for frame in frames:
        for value in frame:
            sample = max(-8388608, min(8388607, round(value / 256)))
            data.extend((sample & 0xffffff).to_bytes(3, "little"))
    with wave.open(str(path), "wb") as f:
        f.setparams((2, 3, RATE, 0, "NONE", "not compressed"))
        f.writeframes(data)


def compile_renderer(out):
    headers = ["definitions_cxx.hpp", "dsp/stereo_sample.h", "model/sample/sample_holder.h",
               "model/sample/sample_playback_guide.h", "model/clip/instrument_clip.h",
               "model/sample/sample.h", "model/sample/sample_recorder.h", "model/voice/voice_sample.h",
               "playback/playback_handler.h", "processing/engines/audio_engine.h",
               "storage/cluster/cluster_priority_queue.h"]
    include = out / "include"
    for header in headers:
        target = include / header
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(f'#include "{HERE / "stubs.h"}"\n')
    binary = out / "render_crossfade"
    subprocess.run(["clang++", "-std=c++20", "-O2", "-fsanitize=address,undefined",
                    "-I", str(include), "-I", str(ROOT / "src/deluge"),
                    str(ROOT / "src/deluge/model/clip/clip_freeze.cpp"),
                    str(HERE / "render_crossfade.cpp"), "-o", str(binary)], check=True)
    return binary


def modeled(source, start, length, fade, pre=False, equal_power=False):
    output = list(source[start:start + length])
    for i in range(fade):
        w = i / fade
        if pre:
            index = length - fade + i
            old, new = source[start + index], source[start - fade + i]
        else:
            index = i
            old, new = source[start + length + i], source[start + i]
        a, b = (math.cos(w * math.pi / 2), math.sin(w * math.pi / 2)) if equal_power else (1 - w, w)
        output[index] = tuple(round(a * x + b * y) for x, y in zip(old, new))
    return output


def metrics(source, output, start, length, fade, pre, variant):
    # Fixed join coordinates, unlike selecting the largest edge anywhere in a file.
    lo, hi = (-fade - 256, 256) if pre else (-256, fade + 256)
    segment = [output[i % length] for i in range(lo, hi)]
    references = [[source[origin + i] for i in range(lo, hi)] for origin in (start, start + length)]
    def max_step(frames):
        return max(abs(b[c] - a[c]) for a, b in zip(frames, frames[1:]) for c in (0, 1))
    native_step = max(map(max_step, references))
    # Compare local energy with the uncorrelated weighted source powers. This
    # separates cancellation introduced by a fade from native envelope motion.
    actual, target = [], []
    for i in range(lo, hi):
        if pre:
            w = max(0, min(1, (i + fade) / fade))
        else:
            x = max(0, min(1, i / fade))
            if variant == "freeze-production":
                w = x * x * (3 - 2 * x)
            elif variant == "post-equalpower-2048":
                w = math.sin(x * math.pi / 2) ** 2
            else:
                w = x
        old, new = source[start + length + i], source[start + i]
        actual.append(sum(x * x for x in output[i % length]))
        target.append(sum((1 - w) * x * x + w * y * y for x, y in zip(old, new)))
    ratios = {}
    for window in (44, 441):
        values = [math.sqrt(sum(actual[j:j + window]) / max(1, sum(target[j:j + window])))
                  for j in range(0, len(actual) - window + 1, max(1, window // 4))]
        ratios[str(window)] = [min(values), max(values)]
    return {"join_step_over_native": max_step(segment) / max(1, native_step),
            "local_level_ratio_min_max_1ms_10ms": ratios,
            "peak": max(abs(x) for frame in output for x in frame)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "build/clip-freeze/crossfade-ab")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    binary = compile_renderer(args.out)
    sources = []
    for frequency in (31.5, 31.5625, 55.5, 55.5625, 110.5, 261.625565, 440.5):
        frames = [tuple([round(2**27 * math.sin(2 * math.pi * frequency * n / RATE))] * 2)
                  for n in range(RATE * 11)]
        sources.append((f"tone-{frequency}", frames))
    for preset in ("sine", "taurus158", "taurus001", "taurus125"):
        path = ROOT / f"build/clip-freeze/drone-final-{preset}/live.s32le"
        sources.append((preset, read_pcm(path)))
    for preset in ("blade", "brick"):
        path = ROOT / f"build/clip-freeze/crossfade-{preset}-baseline/live.s32le"
        sources.append((preset, read_pcm(path)))
    report = []
    for name, source in sources:
        folder = args.out / name
        folder.mkdir(exist_ok=True)
        write_pcm(folder / "source.s32le", source)
        write_wav(folder / "source.wav", source)
        for seconds in (1, 8):
            start, length = RATE, RATE * seconds
            target = folder / f"freeze-{seconds}s.s32le"
            subprocess.run([str(binary), str(folder / "source.s32le"), str(start), str(length), str(target)], check=True)
            variants = {
                "audio-linear-100": (modeled(source, start, length, 100, pre=True), 100, True),
                "freeze-production": (read_pcm(target), 2048, False),
                "post-linear-100": (modeled(source, start, length, 100), 100, False),
                "post-linear-2048": (modeled(source, start, length, 2048), 2048, False),
                "post-equalpower-2048": (modeled(source, start, length, 2048, equal_power=True), 2048, False),
            }
            for variant, (output, fade, pre) in variants.items():
                row = {"source": name, "seconds": seconds, "variant": variant,
                       **metrics(source, output, start, length, fade, pre, variant)}
                report.append(row)
                write_wav(folder / f"{variant}-{seconds}s.wav", output * 3)
                print(name, seconds, variant, round(row['join_step_over_native'], 3),
                      [round(x, 3) for x in row['local_level_ratio_min_max_1ms_10ms']['441']], flush=True)
    (args.out / "results.json").write_text(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
