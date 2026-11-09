"""Pitch and musical-time checks for controlled time-stretched freeze fixtures."""
import math
import statistics
from transition_spectrum import fft, SIZE, RATE, WINDOW


def folded_envelope(pcm, period, bins=200):
    cycles = int(len(pcm) / 2 / period + .001)
    result = []
    for b in range(bins):
        power = count = 0
        for cycle in range(cycles):
            start = round((cycle + b / bins) * period) * 2
            end = min(len(pcm), round((cycle + (b + 1) / bins) * period) * 2)
            power += sum(float(x) * x for x in pcm[start:end])
            count += end - start
        result.append(math.sqrt(power / max(1, count)))
    return result


def pitches(pcm):
    windows = []
    for start in range(0, len(pcm) // 2 - SIZE, SIZE):
        # Inspect the louder channel; folding stereo to mono could cancel it.
        channels = [pcm[2 * start + c:2 * (start + SIZE):2] for c in (0, 1)]
        tone = max(channels, key=lambda a: sum(float(x) * x for x in a))
        power = sum(float(x) * x for x in tone)
        windows.append((power, tone))
    peak = max(p for p, _ in windows)
    result = []
    for power, tone in windows:
        if power < peak * .15:
            continue
        spectrum = [abs(v) for v in fft([x * w for x, w in zip(tone, WINDOW)])[:SIZE // 2]]
        k = max(range(2, len(spectrum) - 1), key=spectrum.__getitem__)
        a, b, c = [math.log(max(1e-30, spectrum[i])) for i in (k - 1, k, k + 1)]
        delta = .5 * (a - c) / (a - 2 * b + c)
        result.append((k + delta) * RATE / SIZE)
    return result


def compare_tempo(reference, changed, reference_period, changed_period):
    a, b = folded_envelope(reference, reference_period), folded_envelope(changed, changed_period)
    rms_a = math.sqrt(sum(x*x for x in a) / len(a))
    rms_b = math.sqrt(sum(x*x for x in b) / len(b))
    a = [x / max(1, rms_a) for x in a]
    b = [x / max(1, rms_b) for x in b]
    shift = min(range(len(a)), key=lambda s: sum((x - b[(i+s) % len(b)])**2 for i,x in enumerate(a)))
    b = b[shift:] + b[:shift]
    expected, heard = pitches(reference), pitches(changed)
    errors = [min(abs(h / e - 1) for e in expected) for h in heard]
    return dict(median_pitch_error_fraction=statistics.median(errors),
                envelope_error=math.sqrt(sum((x-y)**2 for x,y in zip(a,b)) / len(a)),
                activity_mismatch_fraction=sum((x > max(a)*.1) != (y > max(b)*.1) for x,y in zip(a,b)) / len(a),
                gain_ratio=rms_b / max(1,rms_a))
