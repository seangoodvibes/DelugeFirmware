#!/usr/bin/env python3
"""Recover transition timing from completed drone captures, without rerunning firmware.

Uses guest PCM sample time, not emulator wall time or a hardware CPU benchmark.
The freeze toggle was overwritten by the thaw toggle in older results.json files.
Recover it by locating the complete freeze-transition excerpt in the full capture;
that excerpt begins one second before the freeze toggle. Validate every byte.
"""
import argparse
import json
import mmap
from pathlib import Path

RATE = 44100
FRAME_BYTES = 8


def measure(folder):
    report = json.loads((folder / 'results.json').read_text())
    excerpt = (folder / 'freeze-transition.s32le').read_bytes()
    with (folder / 'audio.s32le').open('rb') as stream:
        with mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as capture:
            offset = capture.find(excerpt[:65536])
            if offset < 0 or offset % FRAME_BYTES or capture[offset:offset + len(excerpt)] != excerpt:
                raise ValueError('Could not uniquely validate the freeze excerpt against the full recording')
    toggle_frame = offset // FRAME_BYTES + RATE
    before_entry_fade = (report['freeze_handoff_approx_frame'] - toggle_frame) / RATE
    entry_fade = report['entry_fade_samples'] / RATE
    release_value = next((check['values'][0] for check in report['checks']
                          if check['check'] == 'native release recorded to SD'), None)
    release_frames = int(release_value) if release_value is not None else None
    matched, warmup, boundary = report['thaw_state']
    thaw_fade = report.get('probe', {}).get('fadeSamples', RATE * (1 if matched == 'true' else 3)) / RATE
    entry_wait = report["entry_wait_samples"] / RATE if "entry_wait_samples" in report else (30 if entry_fade == 3 else None)
    result = {
        'source': str(folder),
        'timebase': '44100 Hz guest audio capture; not host wall time',
        'precision': 'Approximate: button dispatch, audio buffering and debugger marker timing apply',
        'freeze_toggle_frame_recovered_from_validated_excerpt': toggle_frame,
        'freeze': {
            'capture_writing_and_boundary_wait_seconds': before_entry_fade - entry_wait if entry_wait is not None else None,
            'matching_wait_seconds': entry_wait,
            'fade_seconds': entry_fade,
            'audible_handoff_complete_seconds': before_entry_fade + entry_fade,
            'additional_native_release_preparation_seconds': release_frames / RATE if release_frames is not None else None,
            'native_rendering_finished_seconds': before_entry_fade + entry_fade + release_frames / RATE if release_frames is not None else None,
        },
        'thaw': {
            'pattern_boundary_wait_seconds': int(boundary) / RATE,
            'native_warmup_and_matching_seconds': int(warmup) / RATE,
            'fade_seconds': thaw_fade,
            'audible_handoff_complete_seconds': (int(boundary) + int(warmup)) / RATE + thaw_fade,
        },
        'derivation': 'Entry fade marker and release frame counts from captures; thaw wait/warmup from controller counters. '
                      'Entry wait is measured when available; older baseline runs use their known 30-second fallback. Thaw fade duration comes from the probe when present, otherwise the original 1/3-second policy. Unmeasured release preparation is null. '
                      'Native-render completion is derived from fade completion plus recorded release frames; SD finalization may finish later.',
    }
    (folder / 'transition-timing.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folders', nargs='+', type=Path)
    args = parser.parse_args()
    for folder in args.folders:
        print(json.dumps(measure(folder), indent=2))
