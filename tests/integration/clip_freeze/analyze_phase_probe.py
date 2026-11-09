#!/usr/bin/env python3
"""Diagnose matcher rejection, musical phase and fixed-duration morphs on identical stems.
Dry stems include clip FX, but exclude the shared song reverb/master processing.
"""
import argparse
import array
import csv
import io
import html
import json
import math
from pathlib import Path
import statistics
import struct
import subprocess
import sys

from compare_crossfades import write_wav
from transition_spectrum import bands, SIZE

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
RATE = 44100

def pcm(path):
    a = array.array('i', path.read_bytes())
    if sys.byteorder != 'little':
        a.byteswap()
    return a

def compare(a, b):
    pa = sum(float(x)*x for x in a)
    pb = sum(float(x)*x for x in b)
    cross = sum(float(x)*y for x,y in zip(a,b))
    return dict(gain_db=10*math.log10(max(1,pa)/max(1,pb)),
                correlation=cross/math.sqrt(max(1,pa)*max(1,pb)),
                normalized_error=math.sqrt(sum((float(x)-y)**2 for x,y in zip(a,b))/max(1,pb)))

def shape(a, b, frame):
    x,y = bands(a,frame),bands(b,frame)
    sx,sy=max(1,sum(x)),max(1,sum(y))
    delta=[abs(10*math.log10(max(v/sx,1e-8)/max(w/sy,1e-8))) for v,w in zip(x,y) if w/sy>.001]
    return statistics.mean(delta) if delta else 0

def build(out):
    include=out/'include'
    headers=['definitions_cxx.hpp','dsp/stereo_sample.h','model/sample/sample_holder.h',
             'model/sample/sample_playback_guide.h','model/clip/instrument_clip.h','model/sample/sample.h',
             'model/sample/sample_recorder.h','model/voice/voice_sample.h','playback/playback_handler.h',
             'processing/engines/audio_engine.h','storage/cluster/cluster_priority_queue.h']
    for name in headers:
        path=include/name;path.parent.mkdir(parents=True,exist_ok=True)
        path.write_text(f'#include "{HERE / "stubs.h"}"\n')
    baseline=ROOT/'build/clip-freeze/phase-probe-song'
    (include/'model/clip/clip_freeze.h').write_bytes((baseline/'benchmark-clip_freeze.h').read_bytes())
    binary=out/'render-morph'
    subprocess.run(['clang++','-std=c++20','-O2','-fno-access-control','-I',str(include),
                    '-I',str(ROOT/'src/deluge'),str(baseline/'benchmark-clip_freeze.cpp'),
                    str(HERE/'phase_morph.cpp'),'-o',str(binary)],check=True)
    return binary

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=Path);args=p.parse_args()
    folder=args.folder.resolve();out=folder/'morphs';out.mkdir(exist_ok=True)
    report=json.loads((folder/'results.json').read_text());meta=report['probe']
    live,cache=pcm(folder/'probe-live.s32le'),pcm(folder/'probe-cache.s32le')
    assert len(live)==len(cache)==meta['count']*2
    assert meta['rowBytes']==40
    rows=list(struct.iter_unpack('<4i6f',(folder/'probe-rows.bin').read_bytes()))
    total=meta['observedSamples']
    rejected=[meta[f'rejectedSamples[{b}]'] for b in range(5)]
    both=meta['passedSamples']
    assert sum((folder/'probe-blocks.bin').read_bytes()) == meta['count'], 'Truncated callback log'
    result={'source':str(folder),'probe':meta,
            'phase_at_native_start_seconds':meta['firstCacheSample']/RATE,
            'matcher':{'level_reject_fraction':rejected[0]/total,
                       'band_reject_fractions':[n/total for n in rejected[1:]],
                       'all_conditions_pass_fraction':both/total,
                       'longest_continuous_match_ms':meta['longestMatch']/RATE*1000},
            'limitations':'Dry clip-output comparison; excludes shared reverb/master. Metrics are not perceptual scores.',
            'windows':[]}
    for seconds in [.5,1,2,4,8,16,24,30,31,32]:
        f=round(seconds*RATE);end=min(f+RATE,meta['count'])
        if end-f<SIZE: continue
        item={'native_age_seconds':seconds,**compare(live[2*f:2*end],cache[2*f:2*end])}
        item['mean_octave_shape_difference_db']=statistics.mean(shape(live,cache,n) for n in range(f,end-SIZE,RATE//10))
        if f>=8*RATE:
            item['live_vs_live_eight_seconds_earlier']=compare(live[2*f:2*end],live[2*(f-8*RATE):2*(end-8*RATE)])
        result['windows'].append(item)
    binary=build(out)
    rendered=subprocess.check_output([str(binary),str(folder),str(out)],text=True)
    manifest=list(csv.DictReader(io.StringIO(rendered)))
    for item in manifest: item['reason']='fixed wait'
    candidates=[]
    for hold in [20,50,100,250]:
        eligible=[r for r in rows if r[0]>=11025 and r[2]>=hold*RATE/1000]
        if not eligible: continue
        row=eligible[0];age=row[0]/RATE
        candidates.append(dict(hold_ms=hold,native_age_seconds=age,observed_matching_run_ms=row[2]/RATE*1000))
        sub=out/f'hold-{hold}ms';sub.mkdir(exist_ok=True)
        text=subprocess.check_output([str(binary),str(folder),str(sub),str(age)],text=True)
        for item in csv.DictReader(io.StringIO(text)):
            item['file']=str(Path(sub.name)/item['file'])
            item['reason']=f'first sampled point with {hold} ms sustained match'
            manifest.append(item)
    result['short_hold_candidates']=candidates
    scan=subprocess.check_output([str(binary),str(folder),str(out),'--scan'],text=True)
    (folder/'averaging-scan.csv').write_text(scan)
    average_candidates=[]
    for timing in csv.DictReader(io.StringIO(scan)):
        sample=int(timing['first_match_sample'])
        if sample<0 or timing['average_ms']=='baseline': continue
        tag=timing['average_ms'];age=sample/RATE
        average_candidates.append(dict(average_ms=tag,native_age_seconds=age))
        sub=out/('average-'+tag);sub.mkdir(exist_ok=True)
        text=subprocess.check_output([str(binary),str(folder),str(sub),str(age)],text=True)
        for item in csv.DictReader(io.StringIO(text)):
            item['file']=str(Path(sub.name)/item['file'])
            item['reason']=f"{tag} ms averaging; original tolerances and 250 ms hold"
            manifest.append(item)
    result['averaged_candidates']=average_candidates
    with (out/'manifest.csv').open('w') as file:
        writer=csv.DictWriter(file,fieldnames=list(manifest[0]));writer.writeheader();writer.writerows(manifest)
    peak=max(max(map(abs,pcm(out/r['file']))) for r in manifest)
    scale=.85*(2**31-1)/max(1,peak)
    result['common_morph_export_gain']=scale
    # One gain across every duration/fade in this capture.
    for row in manifest:
        data=pcm(out/row['file'])
        write_wav((out/row['file']).with_suffix('.wav'),[(round(data[i]*scale),round(data[i+1]*scale)) for i in range(0,len(data),2)])
    waits=sorted({float(r['wait_seconds']) for r in manifest})
    fades=sorted({float(r['fade_seconds']) for r in manifest})
    cells={(float(r['wait_seconds']),float(r['fade_seconds'])):r for r in manifest}
    page=['<!doctype html><meta charset="utf-8"><title>Phase and rebuild comparison</title>',
          '<style>body{font:16px system-ui;margin:2rem;max-width:1300px}table{border-collapse:collapse}td,th{padding:12px;border:1px solid #ccc}audio{width:240px}small{display:block;color:#555}</style>',
          f'<h1>{html.escape(folder.name)}</h1>',
          '<p>Every version below uses the same captured live/cache signals and the benchmark fade implementation. One shared export gain applies to all versions.</p>',
          '<p>These are dry clip outputs, including clip effects but excluding shared song reverb and master processing. Shorter fades are experiments, not recommended settings.</p>',
          '<p>The fade starts two seconds into clips with rebuild waits of at least two seconds. For the 0.5- and 1-second waits, it starts at 0.5 and 1 second respectively. The 30-second / 3-second case uses the benchmark fallback timing.</p>',
          '<table><tr><th>Native rebuild wait</th>'+''.join(f'<th>{f:g} s fade</th>' for f in fades)+'</tr>']
    for wait in waits:
        reason=next(r['reason'] for r in manifest if float(r['wait_seconds'])==wait)
        page.append(f'<tr><th>{wait:g} seconds<small>{html.escape(reason)}</small></th>')
        for fade in fades:
            row=cells.get((wait,fade))
            if row:
                name=str(Path(row['file']).with_suffix('.wav'))
                page.append(f'<td><audio controls preload="none" src="{name}"></audio><small>{html.escape(name)}</small></td>')
            else: page.append('<td>Not enough captured audio</td>')
        page.append('</tr>')
    page.append('</table>')
    (out/'listening.html').write_text('\n'.join(page))
    (folder/'phase-analysis.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__':main()
