#!/usr/bin/env python3
"""Compare a thaw with the corresponding preceding frozen loop, beyond volume.

Uses Hann-windowed FFT octave-band powers, independent of the firmware's
one-pole readiness filters. This is a diagnostic, not a perceptual identity test.
"""
import argparse
import array
import cmath
import json
import math
from pathlib import Path
import sys
from compare_crossfades import write_wav

RATE = 44100
SIZE = 2048
BANDS = (0, 125, 250, 500, 1000, 2000, 4000, 8000, 22051)
WINDOW = [0.5 - 0.5 * math.cos(2 * math.pi * i / (SIZE - 1)) for i in range(SIZE)]
TWIDDLES = {n: [cmath.exp(-2j * math.pi * k / n) for k in range(n // 2)]
            for n in (2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048)}


def fft(values):
    data = [complex(x) for x in values]
    j = 0
    for i in range(1, len(data)):
        bit = len(data) >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i < j:
            data[i], data[j] = data[j], data[i]
    length = 2
    while length <= len(data):
        half = length // 2
        for start in range(0, len(data), length):
            for k, w in enumerate(TWIDDLES[length]):
                a = data[start + k]
                b = w * data[start + k + half]
                data[start + k], data[start + k + half] = a + b, a - b
        length *= 2
    return data


def bands(pcm, frame):
    power = [0.0] * (SIZE // 2 + 1)
    for channel in (0, 1):
        transformed = fft([pcm[(frame + i) * 2 + channel] * WINDOW[i] for i in range(SIZE)])
        for k in range(len(power)):
            power[k] += abs(transformed[k]) ** 2
    return [sum(power[math.ceil(lo * SIZE / RATE):math.ceil(hi * SIZE / RATE)])
            for lo, hi in zip(BANDS, BANDS[1:])]


def compare_window(pcm, actual, reference):
    a, b = bands(pcm, actual), bands(pcm, reference)
    total_a, total_b = max(1, sum(a)), max(1, sum(b))
    differences = [abs(10 * math.log10(max(x / total_a, 1e-8) / max(y / total_b, 1e-8)))
                   for x, y in zip(a, b) if y / total_b > .001]
    return {
        "gain_db": 10 * math.log10(total_a / total_b),
        "mean_octave_shape_difference_db": sum(differences) / len(differences) if differences else 0,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    parser.add_argument("--period", type=int, default=8)
    args = parser.parse_args()
    report = json.loads((args.folder / "results.json").read_text())
    pcm = array.array("i", (args.folder / "audio.s32le").read_bytes())
    if sys.byteorder != "little":
        pcm.byteswap()
    event = report["last_toggle_frame_bounds"][0]
    state = report.get("thaw_state", [False, 0])
    warmup = int(state[1]) + (int(state[2]) if len(state) > 2 else 0)
    handoff = event + warmup
    measurements = []
    # Before thaw, the previous-cycle comparison supplies the floor/noise of the
    # metric. Afterward it asks how much the heard performance departs from cache.
    for tenths in range(-10, 61):
        frame = handoff + round(tenths * RATE / 10)
        reference = frame - args.period * RATE
        if reference >= 0 and frame + SIZE < len(pcm) // 2:
            measurements.append({"seconds_after_handoff": tenths / 10,
                                 **compare_window(pcm, frame, reference)})
    (args.folder / "thaw-spectrum.json").write_text(json.dumps(measurements, indent=2))
    for lo, hi in [(-1, 0), (0, 1), (1, 3), (3, 6)]:
        group = [m for m in measurements if lo <= m["seconds_after_handoff"] < hi]
        print(lo, hi, {k: round(sum(m[k] for m in group) / len(group), 3)
                       for k in ("gain_db", "mean_octave_shape_difference_db")})
    # Common gain for heard output and the frozen continuation at the same phase.
    length = min(RATE * 6, len(pcm) // 2 - handoff)
    actual = pcm[2 * handoff:2 * (handoff + length)]
    reference = pcm[2 * (handoff - args.period * RATE):2 * (handoff - args.period * RATE + length)]
    scale = .85 * (2**31 - 1) / max(max(map(abs, actual)), max(map(abs, reference)))
    for name, data in [("thaw-heard", actual), ("thaw-frozen-reference", reference)]:
        write_wav(args.folder / (name + ".wav"), [(round(data[i] * scale), round(data[i+1] * scale))
                                                for i in range(0, len(data), 2)])


if __name__ == "__main__":
    main()
