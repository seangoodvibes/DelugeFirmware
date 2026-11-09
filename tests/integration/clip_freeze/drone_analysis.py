"""Boundary-sensitive measurements and matched-gain listening files for drones."""

import array
import math
import statistics
import sys
import wave


def analyze(stereo, period_loops):
    mono = [(stereo[i] + stereo[i + 1]) / 2 for i in range(0, len(stereo), 2)]
    rms = math.sqrt(sum(x * x for x in mono) / len(mono))
    steps = [abs(b - a) for a, b in zip(mono, mono[1:])]
    ordered = sorted(steps)
    windows = [
        math.sqrt(sum(x * x for x in mono[i : i + 441]) / 441)
        for i in range(0, len(mono) - 440, 441)
    ]
    peak = max(range(len(steps)), key=steps.__getitem__)
    period = 44100 * period_loops
    # Report recurrence of the largest edge. This is a click diagnostic for
    # smooth tones, but natural waveform edges can recur in complex presets too.
    repeats = [
        max(steps[max(0, p - 64) : min(len(steps), p + 65)])
        for p in range(peak % period, len(steps), period)
    ]
    return {
        "rms": rms,
        "max_step_over_rms": max(steps) / rms,
        "p999_step_over_rms": ordered[int(len(ordered) * 0.999)] / rms,
        "largest_step_frame": peak,
        "periodic_peak_steps": repeats,
        "minimum_10ms_rms_over_median": min(windows) / statistics.median(windows),
    }


def export_pair(out, live, frozen):
    # One common gain for both files: do not hide a change in level.
    scale = 28000 / max(max(map(abs, live)), max(map(abs, frozen)))
    for name, data in [("live", live), ("frozen", frozen)]:
        pcm = array.array(
            "h", (max(-32768, min(32767, round(x * scale))) for x in data)
        )
        if sys.byteorder != "little":
            pcm.byteswap()
        with wave.open(str(out / (name + ".wav")), "wb") as f:
            f.setparams((2, 2, 44100, 0, "NONE", "not compressed"))
            f.writeframes(pcm.tobytes())
