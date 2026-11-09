#!/usr/bin/env python3
"""Compare captured live/frozen stops without claiming phase-identical releases."""
import argparse
import array
import json
import math
from pathlib import Path
from compare_crossfades import write_wav


def ac_rms(pcm, start, end):
    """Per-channel AC energy; a lingering DC offset is not an audible release."""
    energy = 0
    for channel in (0, 1):
        data = pcm[2*start+channel:2*end:2]
        if not data:
            continue
        mean = sum(data)/len(data)
        energy += sum((float(x)-mean)**2 for x in data)/len(data)
    return math.sqrt(energy/2)


def initial_tail_end(windows):
    """Separate the initial release from isolated later events such as a DC cut."""
    quiet = 0
    for i, value in enumerate(windows):
        quiet = quiet + 1 if value <= .01 else 0
        if quiet == 3:
            return (i - 2) / 10
    audible = [i for i, value in enumerate(windows) if value > .01]
    return (audible[-1] + 1) / 10 if audible else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=Path)
    args = parser.parse_args()
    clips = {name: array.array('i', (args.folder / (name + '-stop.s32le')).read_bytes())
             for name in ('native', 'frozen')}
    report = {}
    rate = 44100
    for name, pcm in clips.items():
        reference = max(1, ac_rms(pcm, 0, rate))
        windows = [ac_rms(pcm, i, i+rate//10)/reference
                   for i in range(rate, len(pcm)//2-rate//10, rate//10)]
        audible = [i for i, v in enumerate(windows) if v > .01]
        report[name] = {'initial_tail_end_seconds': initial_tail_end(windows),
                        'last_above_minus40db_seconds': (audible[-1]+1)/10 if audible else 0,
                        'late_activity_bins_seconds': [i/10 for i in audible if i/10 >= initial_tail_end(windows) + .3],
                        'first_100ms_relative_rms': windows[0],
                        'relative_rms_100ms': windows,
                        'pre_stop_ac_rms': reference,
                        'measurement': 'Per-channel RMS with per-window DC removed'}
    # One gain for both files preserves their raw relationship, including DC.
    scale = .85*(2**31-1)/max(max(map(abs, pcm)) for pcm in clips.values())
    listening_frames = round((2 + max(x['initial_tail_end_seconds'] for x in report.values())) * rate)
    for name, pcm in clips.items():
        pcm = pcm[:2*listening_frames]
        write_wav(args.folder / (name+'-stop.wav'), ((round(pcm[i]*scale), round(pcm[i+1]*scale)) for i in range(0,len(pcm),2)))
    (args.folder/'release-analysis.json').write_text(json.dumps(report, indent=2))
    print(json.dumps({name: {k:v for k,v in details.items() if k != 'relative_rms_100ms'} for name,details in report.items()},indent=2))


if __name__ == '__main__':
    main()
