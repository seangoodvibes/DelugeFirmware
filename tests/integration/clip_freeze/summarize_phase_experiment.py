#!/usr/bin/env python3
"""Create a local listening index and compact results for the phase experiment."""
import csv
import html
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/clip-freeze'
summary={'benchmark':'2efd9ebc12aacc30ab4e0e2d538136b31836ceb7','phase_runs':[],'averaged_runs':[]}
page=['<!doctype html><meta charset="utf-8"><title>Freeze transition experiments</title>',
      '<style>body{font:17px system-ui;line-height:1.5;margin:2rem;max-width:1100px}table{border-collapse:collapse}td,th{padding:12px;border:1px solid #bbb}audio{width:300px}small{display:block}</style>',
      '<h1>Freeze transition experiments</h1>',
      '<h2>Full-audio benchmark versus guarded averaging</h2>',
      '<p>Each pair shares one export gain and includes shared reverb/master processing. The fade starts at two seconds. These are separate performances of evolving presets, so this is a listening comparison, not a null test.</p>',
      '<p>The experiment retains the three-second fade. It requires a sustained match of one-second averages and a passing original short-term check at the handoff. It remains experimental; faster timing does not prove equivalent sound.</p>',
      '<table><tr><th>Preset</th><th>Preserved benchmark</th><th>Guarded averaging</th></tr>']
for preset in ['brick','blade']:
    folder=OUT/f'phase-averaged-{preset}'
    report=json.loads((folder/'results.json').read_text())
    timing=json.loads((folder/'transition-timing.json').read_text())
    assert all(c['passed'] for c in report['checks'])
    scan=list(csv.DictReader((folder/'averaging-scan.csv').open()))
    predicted=next(int(r['first_match_sample']) for r in scan if r['average_ms']=='1000+instant')
    assert predicted==int(report['thaw_state'][1]), 'Offline replay disagrees with actual guarded handoff'
    assert report['slow_match_run_samples']>=11025 and report['probe']['fadeSamples']==132300
    summary['averaged_runs'].append({'preset':preset,'warmup_seconds':int(report['thaw_state'][1])/44100,
                                    'total_thaw_seconds':timing['thaw']['audible_handoff_complete_seconds'],
                                    'sustained_average_match_ms':report['slow_match_run_samples']/44.1,
                                    'offline_replay_agrees':True})
    prefix=f'phase-averaged-ab-{preset}'
    page.append(f'<tr><th>{preset.title()}</th><td><audio controls preload="none" src="{prefix}/baseline-thaw.wav"></audio></td><td><audio controls preload="none" src="{prefix}/candidate-thaw.wav"></audio><small>{timing["thaw"]["audible_handoff_complete_seconds"]:.1f} s from toggle to live</small></td></tr>')
page+=['</table>','<h2>Controlled rebuild and short-fade matrices</h2>',
       '<p>Within each matrix, every variant uses the same live/cache stems and one common gain. These include clip effects but exclude shared song reverb/master processing. The recordings end at file boundaries; their ending is not a clip release-tail test.</p>',
       '<table><tr><th>Preset / alignment</th><th>File phase at restart</th><th>Longest original match</th><th>Level rejected</th><th>Listening matrix</th></tr>']
for preset in ['brick','blade']:
    for alignment in ['song','cache']:
        name=f'phase-{alignment}-{preset}-v2'
        analysis=json.loads((OUT/name/'phase-analysis.json').read_text())
        m=analysis['matcher']
        item={'preset':preset,'alignment':alignment,'cache_phase_seconds':analysis['phase_at_native_start_seconds'],**m}
        summary['phase_runs'].append(item)
        page.append(f'<tr><th>{preset.title()} / {alignment}</th><td>{item["cache_phase_seconds"]:.1f} s</td><td>{m["longest_continuous_match_ms"]:.1f} ms</td><td>{100*m["level_reject_fraction"]:.1f}%</td><td><a href="{name}/morphs/listening.html">Open matrix</a></td></tr>')
page+=['</table>','<p>The original gate requires 250 ms of uninterrupted matching. File-phase alignment alone did not satisfy it on either patch. Short-hold and short-fade variants are audition experiments, not recommended defaults.</p>']
# Confirm all primary audio/index links exist before publishing the local index.
import re
for target in re.findall(r'(?:src|href)="([^"]+)"',''.join(page)):
    assert (OUT/target).is_file(),target
(OUT/'phase-experiment.html').write_text('\n'.join(page))
(OUT/'phase-experiment.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary['averaged_runs'],indent=2))
