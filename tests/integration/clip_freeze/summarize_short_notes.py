#!/usr/bin/env python3
"""Measure short-note handoffs and export common-gain listening excerpts."""
import argparse
import array
import json
from pathlib import Path
from audio_equivalence import compare, snapshot_reference
from compare_crossfades import write_wav
from compare_transition_speed import excerpt
from measure_transition_times import measure


def summarize(folder):
    report = json.loads((folder / 'results.json').read_text())
    assert all(c['passed'] for c in report['checks'])
    assert report['checks'][-1]['check'] == 'no firmware aborts'
    timing = measure(folder)
    period = next(c for c in report['checks'] if c['check'] ==
                  'single-note capture has expected snapshot policy and length')
    assert period['passed']
    frozen = array.array('i', (folder / 'frozen.s32le').read_bytes())
    # The single-note harness captures exactly three full frozen patterns.
    loops, remainder = divmod(len(frozen), 3 * 44100 * 2)
    assert loops > 0 and remainder == 0
    if report['exact_capture']:
        reference = array.array('i', (folder / 'live.s32le').read_bytes())
    else:
        # Compare against the native passage being recorded, not another random performance.
        with (folder / 'audio.s32le').open('rb') as audio:
            audio.seek(report['freeze_toggle_frame_bounds'][0] * 8)
            recording = array.array('i', audio.read((loops + 2) * 44100 * 8))
        reference = snapshot_reference(recording, frozen, loops)
    equivalence = compare(reference, frozen, loops)
    event = report['last_toggle_frame_bounds'][0] + sum(map(int, report['thaw_state'][1:]))
    passages = {
        'entry': excerpt(folder, report['freeze_handoff_approx_frame']),
        'thaw': excerpt(folder, event),
        'live': array.array('i', (folder / 'live.s32le').read_bytes())[:8 * 44100 * 2],
        'frozen': frozen[:8 * 44100 * 2],
    }
    scale = .85 * (2**31 - 1) / max(max(map(abs, pcm)) for pcm in passages.values())
    for name, pcm in passages.items():
        write_wav(folder / ('short-' + name + '.wav'),
                  [(round(pcm[i] * scale), round(pcm[i+1] * scale)) for i in range(0, len(pcm), 2)])
    result = {'timing': timing, 'note_duration_seconds': report['note_duration_seconds'],
              'recording_loops': loops, 'audio_comparison': equivalence,
              'shared_export_gain': scale,
              'limitations': 'Guest audio latency, not CPU cost. Wet effects and independent modulation can differ. '
                             'Short-note native voices may sleep between attacks. No listening-quality assertion.'}
    (folder / 'short-note-analysis.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folders', nargs='+', type=Path)
    args = parser.parse_args()
    for folder in args.folders:
        print(json.dumps(summarize(folder), indent=2))
