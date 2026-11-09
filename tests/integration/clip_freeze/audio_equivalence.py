"""Audio comparison for the deterministic dry synth fixture; no third-party dependencies."""

import heapq
import itertools
import math


def rms(samples):
    return math.sqrt(sum(x * x for x in samples) / len(samples))


def envelope(samples):
    # Stereo energy in 10ms bins. Note timing and silent iterations matter, not just average loudness.
    return [rms(samples[i : i + 882]) for i in range(0, len(samples) - 881, 882)]


def compare(live, frozen, period_loops):
    a, b = envelope(live), envelope(frozen)
    period = 100 * period_loops

    def fold(values):
        return [sum(values[i::period]) / len(values[i::period]) for i in range(period)]

    pa, pb = fold(a), fold(b)
    shift = min(
        range(period),
        key=lambda s: sum((pa[i] - pb[(i + s) % period]) ** 2 for i in range(period)),
    )
    # Refine the circular alignment to 1ms before measuring 10ms-bin energy.
    # Otherwise a half-bin offset alone can move all four edges of two notes
    # and report a 4% activity mismatch for sample-identical audio.
    frames = period * 441
    first_period = frozen[: frames * 2]
    energy = [
        ((first_period[2 * i] ** 2 + first_period[2 * i + 1] ** 2) / 2)
        for i in range(frames)
    ]
    prefix = list(itertools.accumulate(itertools.chain(energy, energy), initial=0))

    def aligned_envelope(offset):
        return [
            math.sqrt(
                max(0, prefix[offset + (i + 1) * 441] - prefix[offset + i * 441]) / 441
            )
            for i in range(period)
        ]

    choices = [(shift * 441 + delta) % frames for delta in range(-441, 442, 44)]
    offset = min(
        choices,
        key=lambda start: sum(
            (x - y) ** 2 for x, y in zip(pa, aligned_envelope(start))
        ),
    )
    aligned = aligned_envelope(offset)
    error = rms([x - y for x, y in zip(pa, aligned)]) / rms(pa)
    threshold = max(pa) * 0.10
    mask_error = (
        sum((x > threshold) != (y > threshold) for x, y in zip(pa, aligned)) / period
    )

    # Compare waveform, too: find the first complete note attack in each window and align a steady portion.
    # The first selected notes may have different pitches, so consider all attacks within a full pattern.
    def attacks(values, samples):
        peak = max(values)
        starts = [
            i
            for i in range(1, len(values) - 15)
            if values[i] > peak * 0.25 and values[i - 1] <= peak * 0.25
        ]
        return [
            samples[(i * 441 + 2205) * 2 : (i * 441 + 2205 + 2048 + 882) * 2 : 2]
            for i in starts
        ]

    wa, wb = attacks(a, live), attacks(b, frozen)
    waveform_errors = []
    for reference in wa[:4]:
        reference = reference[441 : 441 + 2048]
        # The cache is 24-bit PCM in a 32-bit mix: one quantization step is 256.
        # A decaying note can be below that noise floor by the chosen steady window.
        # Relative waveform error is meaningless there; silence is checked above.
        if not reference or rms(reference) < 1024:
            continue
        # Attacks are located in 10ms bins. Search the full +/-10ms uncertainty;
        # a carrier-phase match alone is insufficient when its amplitude is LFO-modulated.
        best = float("inf")
        for candidate in wb:
            offsets = heapq.nsmallest(
                8,
                range(883),
                key=lambda offset: sum(
                    (reference[i] - candidate[i + offset]) ** 2
                    for i in range(0, 2048, 16)
                ),
            )
            best = min(
                best,
                min(
                    sum(
                        (reference[i] - candidate[i + offset]) ** 2 for i in range(2048)
                    )
                    for offset in offsets
                ),
            )
        waveform_errors.append(math.sqrt(best / 2048) / rms(reference))
    return {
        "envelope_relative_rms_error": error,
        "note_activity_mismatch_fraction": mask_error,
        "live_silent_fraction": sum(x <= threshold for x in pa) / period,
        "frozen_silent_fraction": sum(x <= threshold for x in aligned) / period,
        "waveform_relative_rms_errors": waveform_errors,
        "alignment_bins": shift,
        "alignment_samples": offset,
    }


def snapshot_reference(recording, frozen, period_loops):
    """Locate the captured pass within the native audio around the arm boundary.

    Search only the two seconds following the enable press. These samples precede
    cache playback; compare random modulation against its recording, not another
    independent performance. Coarse 50ms bins select a pass; compare() then uses
    the usual 10ms envelope and sample-aligned waveform checks.
    """
    a, b = envelope(recording)[::5], envelope(frozen)[::5]
    period = 20 * period_loops
    b = [sum(b[i::period]) / len(b[i::period]) for i in range(period)]
    best = (float("inf"), 0)
    for start in range(min(41, len(a) - period + 1)):
        window = a[start : start + period]
        error = min(
            sum((window[i] - b[(i + shift) % period]) ** 2 for i in range(period))
            for shift in range(period)
        )
        best = min(best, (error, start))
    offset = best[1] * 2205 * 2
    return recording[offset : offset + 44100 * period_loops * 2]
