#!/usr/bin/env python3
"""Independent full-output and same-source diagnostics for the Brick tempo A/B.

Scores are not perceptual identity tests. Recordings are aligned by modulation
envelope and compared with and without constant gain differences; no oscillator
phase restoration or time warping is applied to make the waveforms match.
"""
import argparse
import array
import json
import math
from pathlib import Path
import statistics
from compare_crossfades import write_wav
from tempo_analysis import folded_envelope
from transition_spectrum import bands, SIZE, RATE

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('folder', type=Path)
args = p.parse_args()
folder = args.folder
period = RATE * 16 * 120 / 130
names = ['frozen130', 'audio-resampled130', 'audio-same-dry130', 'native130']
clips = {name: array.array('i', (folder / (name + '.s32le')).read_bytes()) for name in names}


def rms(pcm):
    return math.sqrt(sum(float(x)*x for x in pcm) / len(pcm))


def align(a, b):
    ea, eb = folded_envelope(a, period, 512), folded_envelope(b, period, 512)
    ra = math.sqrt(sum(x*x for x in ea) / len(ea))
    rb = math.sqrt(sum(x*x for x in eb) / len(eb))
    ea, eb = [x / ra for x in ea], [x / rb for x in eb]
    shift = min(range(512), key=lambda s: sum((v-eb[(i+s)%512])**2 for i,v in enumerate(ea)))
    offset = round(shift * period / 512)
    # Rotate within the two recorded periods, retaining a continuous passage.
    b = b[offset*2:] + b[:offset*2]
    return b, offset, math.sqrt(sum((v-eb[(i+shift)%512])**2 for i,v in enumerate(ea))/512)


def spectral(a, b):
    results = []
    for frame in range(0, min(len(a), len(b))//2-SIZE, RATE//10):
        x, y = bands(a, frame), bands(b, frame)
        tx, ty = max(1,sum(x)), max(1,sum(y))
        diffs = [abs(10*math.log10(max(v/tx,1e-8)/max(w/ty,1e-8)))
                 for v,w in zip(x,y) if v/tx > .001]
        results.append(dict(time=frame/RATE, gain_db=10*math.log10(ty/tx),
                            shape_db=statistics.mean(diffs)))
    return results

reference = clips['frozen130']
summary = {}
aligned = {'frozen130': reference}
for name in names[1:]:
    b, offset, envelope_error = align(reference, clips[name])
    aligned[name] = b
    values = spectral(reference, b)
    summary[name] = dict(gain_db=20*math.log10(rms(b)/rms(reference)),
                         alignment_seconds=offset/RATE, normalized_envelope_error=envelope_error,
                         median_octave_shape_difference_db=statistics.median(x['shape_db'] for x in values),
                         p95_octave_shape_difference_db=sorted(x['shape_db'] for x in values)[int(len(values)*.95)])
    (folder/(name+'-spectral.json')).write_text(json.dumps(values,indent=2))
# Native variability at the same musical phase over consecutive eight-bar passages.
length = round(period)*2
native = clips['native130']
variation = spectral(native[:length], native[length:])
summary['native_cycle_variation'] = dict(median_octave_shape_difference_db=statistics.median(x['shape_db'] for x in variation))
# Constant RMS matching is explicit, in addition to the unmodified WAVs. Apply
# one final common headroom gain so no file clips and none is peak-normalized.
gains = {name:rms(reference)/rms(pcm) for name,pcm in aligned.items()}
peak = max(max(map(abs,pcm))*gains[name] for name,pcm in aligned.items())
headroom = .85*(2**31-1)/peak
for name,pcm in aligned.items():
    gain = gains[name]*headroom
    write_wav(folder/(name+'-aligned-matched.wav'), ((round(pcm[i]*gain),round(pcm[i+1]*gain)) for i in range(0,len(pcm),2)))
(folder/'comparison.json').write_text(json.dumps(summary,indent=2))
print(json.dumps(summary,indent=2))
