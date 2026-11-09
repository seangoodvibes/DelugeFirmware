#!/usr/bin/env python3
"""Build isolated diagnostic ELFs from the sound benchmark, restoring workspace sources."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
BENCHMARK = '2efd9ebc12aacc30ab4e0e2d538136b31836ceb7'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--alignment', choices=['song', 'cache'], required=True)
p.add_argument('--averaged', action='store_true', help='Test one-second averages plus the original instantaneous checks')
a = p.parse_args()
out = ROOT / 'build/clip-freeze' / ('phase-probe-' + a.alignment + ('-averaged' if a.averaged else ''))
out.mkdir(parents=True, exist_ok=True)
paths = [ROOT / ('src/deluge/model/clip/clip_freeze.' + suffix) for suffix in ('cpp', 'h')]
saved = [f.read_bytes() for f in paths]
try:
    for f in paths:
        baseline = subprocess.check_output(['git', 'show', f'{BENCHMARK}:{f.relative_to(ROOT)}'], cwd=ROOT)
        (out / ('benchmark-' + f.name)).write_bytes(baseline)
        f.write_bytes(baseline)
    cpp = paths[0].read_text()
    cpp = cpp.replace('namespace {', f'#include "{HERE / "phase_probe.h"}"\n\nnamespace {{', 1)
    cpp = cpp.replace('void ClipFreeze::beginThaw(bool alignToPattern) {', '''void ClipFreeze::beginThaw(bool alignToPattern) {
    freezeProbe.reset();
    freezeProbe.recordedStartRepeat = captureStartRepeat_;
    freezeProbe.periodTicks = period_;
    freezeProbe.clipTicks = length_;''')
    cpp = cpp.replace('waitingForNativeStart_ = false;\n\t\t}', 'waitingForNativeStart_ = false;\n\t\t\tfreezeProbe.nativeStartRepeat = repeatCount;\n\t\t}', 1)
    if a.alignment == 'cache':
        cpp = cpp.replace('waitingForNativeStart_ && repeatCount % (period_ / length_) == 0', 'waitingForNativeStart_ && phase(0, repeatCount) == 0')
    cpp = cpp.replace('\tif (!render({cached, dry.size()}, {cachedSend, dry.size()}))', '''    int32_t probePosition = dry_.voice ? dry_.voice->getPlaySample(static_cast<Sample*>(dry_.holder.audioFile), &dry_.guide) : -1;
\tif (!render({cached, dry.size()}, {cachedSend, dry.size()}))''', 1)
    cpp = cpp.replace('\tif (thawing_ || entering_) {\n\t\tif (!handoffFade_.length)', '''    if (thawing_)
        freezeProbe.capture(dry, {cached, dry.size()}, probePosition);
\tif (thawing_ || entering_) {\n\t\tif (!handoffFade_.length)''', 1)
    cpp = cpp.replace('\t\t\tmatchedSamples_ = matches ? matchedSamples_ + dry.size() : 0;', '''\t\t\tmatchedSamples_ = matches ? matchedSamples_ + dry.size() : 0;
            if (thawing_)
                freezeProbe.observe(matchedSamples_, warmLivePower_, warmCachePower_, handoffBandPower_, dry, {cached, dry.size()});''', 1)
    cpp = cpp.replace('handoffFade_.reset(thawMatched_ ? 44100 : 132300);', 'handoffFade_.reset(thawMatched_ ? 44100 : 132300);\n                if (thawing_) freezeProbe.fadeSamples = handoffFade_.length;')
    if a.averaged:
        header = paths[1].read_text().replace('int32_t matchedSamples_ = 0;', 'int32_t matchedSamples_ = 0;\n    int32_t slowMatchedSamples_ = 0;\n    float slowPower_[2]{}, slowBands_[2][4]{};')
        paths[1].write_text(header)
        cpp = cpp.replace('matchedSamples_ = 0;\n\tthawMatched_', 'matchedSamples_ = slowMatchedSamples_ = 0;\n    std::fill_n(slowPower_, 2, 0);\n    for (auto& bands : slowBands_) std::fill_n(bands, 4, 0);\n\tthawMatched_', 1)
        cpp = cpp.replace('warmupSamples_ += dry.size();', 'warmupSamples_ += dry.size();\n            float slowAlpha = float(dry.size()) / (dry.size() + 44100);\n            slowPower_[0] += slowAlpha * (live / dry.size() - slowPower_[0]);\n            slowPower_[1] += slowAlpha * (cache / dry.size() - slowPower_[1]);', 1)
        cpp = cpp.replace('float totals[2]{};', 'float totals[2]{}, slowTotals[2]{};', 1)
        cpp = cpp.replace('totals[stream] += power;', 'totals[stream] += power;\n                    slowBands_[stream][band] += slowAlpha * (powers[stream][band] / dry.size() - slowBands_[stream][band]);\n                    slowTotals[stream] += slowBands_[stream][band];', 1)
        old = 'if ((warmupSamples_ >= 11025 && matchedSamples_ >= 11025) || warmupSamples_ >= 44100 * 30) {'
        new = """bool slowMatches = slowPower_[0] >= slowPower_[1] * .81f && slowPower_[0] <= slowPower_[1] * 1.235f;
            for (int band = 0; band < 4 && slowMatches; ++band) {
                float target = slowBands_[1][band] / std::max(1.f, slowTotals[1]);
                float actual = slowBands_[0][band] / std::max(1.f, slowTotals[0]);
                if (slowBands_[1][band] > slowTotals[1] * .0001f)
                    slowMatches = actual >= target * .63f && actual <= target * 1.585f;
            }
            slowMatchedSamples_ = slowMatches ? slowMatchedSamples_ + dry.size() : 0;
            bool ready = thawing_ ? (warmupSamples_ >= 88200 && slowMatchedSamples_ >= 11025 && matches)
                                  : (warmupSamples_ >= 11025 && matchedSamples_ >= 11025);
            if (ready || warmupSamples_ >= 44100 * 30) {"""
        assert old in cpp
        cpp = cpp.replace(old, new, 1).replace('thawMatched_ = matchedSamples_ >= 11025;', 'thawMatched_ = ready;', 1)
        cpp = cpp.replace('handoffFade_.reset(thawMatched_ ? 44100 : 132300);', 'handoffFade_.reset(thawing_ ? 132300 : (thawMatched_ ? 44100 : 132300));', 1)
    paths[0].write_text(cpp)
    (out / 'instrumented.cpp').write_text(cpp)
    (out / 'handoff-line.txt').write_text(str(next(i for i,l in enumerate(cpp.splitlines(),1) if 'handoffFade_.reset(' in l and 'thawMatched_' in l)))
    with (out / 'build.log').open('w') as log:
        subprocess.run(['./dbt','build','Debug'], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    (out / 'deluge.elf').write_bytes((ROOT/'build/Debug/deluge.elf').read_bytes())
    print(out)
finally:
    for f, data in zip(paths, saved):
        f.write_bytes(data)
