#!/usr/bin/env python3
"""Compare preserved firmware captures; export transition A/Bs with one shared gain.

Local spectral movement and discontinuities are diagnostics, not listening scores.
Evolving presets are separate performances and cannot be expected to null.
"""
import argparse
import array
import json
import math
from pathlib import Path
import statistics
import sys

from compare_crossfades import write_wav
from measure_transition_times import measure
from transition_spectrum import compare_window, SIZE

RATE = 44100


def excerpt(folder, event):
    with (folder / 'audio.s32le').open('rb') as stream:
        stream.seek((event - 2 * RATE) * 8)
        pcm = array.array('i', stream.read(8 * RATE * 8))
    if sys.byteorder != 'little':
        pcm.byteswap()
    if len(pcm) != 8 * RATE * 2:
        raise ValueError('Incomplete transition excerpt')
    return pcm


def metrics(pcm):
    # Keep stereo channel energy; do not cancel out-of-phase channels to mono.
    windows = [math.sqrt(sum(x*x for x in pcm[i:i+882]) / 882)
               for i in range(0, len(pcm), 882)]
    rms = math.sqrt(sum(x*x for x in pcm) / len(pcm))
    steps = [abs(pcm[i]-pcm[i-2]) for i in range(2, len(pcm))]
    movement = [compare_window(pcm, f, f-4410)
                for f in range(4410, len(pcm)//2-SIZE, 4410)]
    return {
        'minimum_10ms_stereo_rms_over_median': min(windows)/statistics.median(windows),
        'max_channel_step_over_stereo_rms': max(steps)/rms,
        'mean_adjacent_100ms_spectral_shape_change_db': statistics.mean(
            m['mean_octave_shape_difference_db'] for m in movement),
        'mean_absolute_adjacent_100ms_gain_change_db': statistics.mean(
            abs(m['gain_db']) for m in movement),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('out', type=Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    result = {'note': 'Separate evolving performances; measurements do not establish perceptual equivalence.',
              'timing': {}, 'audio': {}}
    passages = {}
    for label, folder in [('baseline', args.baseline), ('candidate', args.candidate)]:
        report = json.loads((folder/'results.json').read_text())
        result['timing'][label] = measure(folder)
        thaw = report['last_toggle_frame_bounds'][0] + sum(map(int, report['thaw_state'][1:]))
        for transition, event in [('entry', report['freeze_handoff_approx_frame']), ('thaw', thaw)]:
            name = f'{label}-{transition}'
            passages[name] = excerpt(folder, event)
            result['audio'][name] = metrics(passages[name])
    scale = .85 * (2**31-1) / max(max(map(abs, pcm)) for pcm in passages.values())
    result['shared_export_gain'] = scale
    result['handoff_starts_at_seconds_in_wavs'] = 2
    for name, pcm in passages.items():
        write_wav(args.out/(name+'.wav'), [(round(pcm[i]*scale), round(pcm[i+1]*scale))
                                        for i in range(0, len(pcm), 2)])
    (args.out/'comparison.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result['audio'], indent=2))


if __name__ == '__main__':
    main()
