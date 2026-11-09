#!/usr/bin/env python3
"""Brick Speedboat: 120->130 BPM AudioClip vs freeze vs native 130 BPM.

Captures the emulator's actual rendered output. An additional AudioClip plays
exactly the cache's dry WAV to control for the full-output resample's baked
shared reverb/master processing. All front-panel actions use DelugeSession.
"""
import argparse
import array
import copy
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import wave
import xml.etree.ElementTree as ET

from compare_crossfades import write_wav

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
RATE = 44100
CLIP_TICKS = 96  # One bar at tick magnitude zero.
PERIOD_TICKS = CLIP_TICKS * 8
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', type=Path, default=ROOT / 'build/clip-freeze/brick-tempo-ab')
p.add_argument('--debug-failure', action='store_true')
p.add_argument('--audio-only', action='store_true', help='Reuse the completed synth captures')
args = p.parse_args()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
card = out / 'card'
for folder in ('SONGS', 'SAMPLES', 'SETTINGS'):
    (card / folder).mkdir(parents=True, exist_ok=True)
(card / 'SETTINGS/CommunityFeatures.XML').write_text('<runtimeFeatureSettings><setting name="emulatedDisplay" value="1"/></runtimeFeatureSettings>')
sys.path.insert(0, str(ROOT.parent / 'delugemu/pylib'))
from delugemu import DelugeSession
elf = ROOT / 'build/Debug/deluge.elf'
gdb = ROOT / 'toolchain/v22/darwin-arm64/arm-none-eabi-gcc/bin/arm-none-eabi-gdb'
report_path = out / 'results.json'
report = json.loads(report_path.read_text()) if args.audio_only else {'checks': []}


def tempo(song, bpm):
    tick = round(110250 / bpm * 2**32)
    song.getroot().set('timePerTimerTick', str(tick >> 32))
    song.getroot().set('timerTickFraction', str(tick & 0xffffffff))


def synth_song(bpm):
    song = ET.parse(HERE / 'Freeze.XML')
    tempo(song, bpm)
    patch = ET.parse(ROOT / 'SDCard Backup/Brick Speedboat.XML').getroot()
    patch.set('presetName', 'SYNT8')
    patch.set('presetFolder', 'SYNTHS')
    params = patch.find('defaultParams')
    patch.remove(params)
    params.tag = 'soundParams'
    instruments = song.find('./instruments')
    instruments.clear()
    instruments.append(patch)
    clip = song.find('.//instrumentClip')
    clip.set('length', str(CLIP_TICKS))
    clip.remove(clip.find('soundParams'))
    clip.insert(1, params)
    arp = patch.find('arpeggiator')
    if arp is not None:
        clip.remove(clip.find('arpeggiator'))
        clip.insert(0, copy.deepcopy(arp))
    rows = clip.find('noteRows')
    row = copy.deepcopy(rows.find('noteRow'))
    rows.clear()
    data = row.get('noteDataWithSplitProb')[2:]
    row.set('noteDataWithSplitProb', f'0x00000000{CLIP_TICKS:08X}' + data[16:22] + '0000' + data[26:])
    rows.append(row)
    return song


def audio_song(filename, loop_frames):
    song = ET.parse(HERE / 'Freeze.XML')
    tempo(song, 120)
    instruments = song.find('./instruments')
    instruments.clear()
    ET.SubElement(instruments, 'audioTrack', name='Compare', mode='Player', modFXType='none',
                  inputChannel='left', lpfMode='24dB', hpfMode='HPLadder')
    clips = song.find('./sessionClips')
    clips.clear()
    clip = ET.SubElement(clips, 'audioClip', trackName='Compare', pitchSpeedIndependent='1', transpose='0',
                         reversed='0', attack='-2147483648', priority='1', isPlaying='1', isSoloing='0',
                         isArmedForRecording='0', section='0', selected='1', filePath='SAMPLES/' + filename,
                         startSamplePos='0', endSamplePos=str(loop_frames), length=str(PERIOD_TICKS))
    ET.SubElement(clip, 'params', volume='0x00000000', pan='0x00000000', lpfFrequency='0x7FFFFFFF',
                  hpfFrequency='0x80000000', delayFeedback='0x80000000', reverbAmount='0x80000000',
                  modFXDepth='0x80000000')
    return song


def check(name, condition, **details):
    report['checks'].append(dict(check=name, passed=bool(condition), **details))
    report_path.write_text(json.dumps(report, indent=2))
    print(report['checks'][-1], flush=True)
    assert condition, name


def inspect(expressions):
    commands = ['set language c++', 'target remote :1257', *['p ' + x for x in expressions], 'detach']
    result = subprocess.run([str(gdb), '-q', '-batch', str(elf),
                             *[x for cmd in commands for x in ('-ex', cmd)]],
                            capture_output=True, text=True, timeout=45, check=True)
    with (out / 'debugger.log').open('a') as log:
        log.write(result.stdout + result.stderr)
    values = re.findall(r'^\$\d+ = (.+)$', result.stdout, re.M)
    assert len(values) == len(expressions), result.stdout + result.stderr
    return values


class Capture:
    def __init__(self, name):
        self.path = out / (name + '-all.s32le')
        self.log = out / (name + '-emulator.log')
        self.session = DelugeSession.launch(firmware=str(elf), sd=str(card), repo_root=str(ROOT.parent / 'delugemu'),
            sock='/tmp/dzfreeze.sock', int_log=str(out / (name + '-interrupts.log')), run_log=str(self.log),
            audio='none', icount=1, boot_wait=7,
            extra_qemu=['-gdb', 'tcp::1257', '-global', 'rza1l-gpio.edge-spacing-ns=5000000'],
            env={'DELUGEMU_SSIF_DUMP': str(self.path)})

    def wait(self, seconds):
        target = self.path.stat().st_size + round(seconds * RATE) * 8
        deadline = time.monotonic() + max(120, seconds * 12)
        while self.path.stat().st_size < target:
            if time.monotonic() >= deadline:
                self.session.screenshot(str(out / 'stalled.png'))
                raise AssertionError('Audio stalled')
            self.session.wait(.25)

    def save(self, name, seconds):
        frames = round(seconds * RATE)
        with self.path.open('rb') as f:
            f.seek(-frames * 8, 2)
            data = array.array('i', f.read(frames * 8))
        (out / (name + '.s32le')).write_bytes(data.tobytes())
        write_wav(out / (name + '.wav'), zip(data[::2], data[1::2]))
        return data

    def set_tempo(self, target):
        for _ in range(4):
            tick, magnitude = map(int, inspect(['currentSong->timePerTimerTickBig', '(int)currentSong->insideWorldTickMagnitude']))
            bpm = 110250 / (tick / 2**32) / 2**magnitude
            if abs(bpm - target) < .01:
                check('tempo is ' + str(target), True, bpm=bpm)
                return
            self.session.encoder(2, round(target - bpm))
            self.wait(.4)
        raise AssertionError(f'Tempo {bpm} != {target}')

    def close(self):
        self.session.assert_no_aborts()
        self.session.close()


clip_expr = '((InstrumentClip*)currentSong->firstOutput->activeClip)'
freeze = clip_expr + '->freeze'


def wav_pcm(path):
    with wave.open(str(path), 'rb') as f:
        assert f.getsampwidth() == 3 and f.getnchannels() == 2
        raw = f.readframes(f.getnframes())
    return array.array('i', (int.from_bytes(raw[i:i+3], 'little', signed=True) * 256 for i in range(0, len(raw), 3)))


def locate_source(master, dry, lo, hi):
    # Same native performance; search only the next boundary after arming.
    length = min(8192, len(dry) // 2)
    channel = max((0, 1), key=lambda c: sum(float(x)*x for x in dry[c:length*2:2]))
    def score(frame):
        indices = range(0, length, 32)
        a = [float(dry[2*i+channel]) for i in indices]
        b = [float(master[2*(frame+i)+channel]) for i in indices]
        return sum(x*y for x,y in zip(a,b)) / max(1, math.sqrt(sum(x*x for x in a)*sum(y*y for y in b)))
    candidates = sorted(range(max(0,lo), min(hi, len(master)//2-length), 32), key=score, reverse=True)[:12]
    best = max((j for i in candidates for j in range(max(0,i-32), i+33)), key=score)
    return best, score(best)


if not args.audio_only:
    for bpm in (120, 130):
        synth_song(bpm).write(card / 'SONGS' / f'Synth{bpm}.XML')
    cap = Capture('synth')
    dz = cap.session
    try:
        from read_sd_file import read_sd_file
        image_path = re.search(r'SD image ready: ([^\n]+)', cap.log.read_text(errors='replace')).group(1)
        assert read_sd_file(image_path, 'SONGS/Synth120.XML') == (card / 'SONGS/Synth120.XML').read_bytes()
        dz.load_song('/SONGS/Synth120.XML')
        cap.wait(2)
        cap.set_tempo(120)
        dz.press('PLAY')
        cap.wait(2 if args.debug_failure else 16)
        # Hold clip, open settings, choose Freeze using the normal front panel.
        dz.pad_event(0, 7, True)
        cap.wait(.2)
        dz.press('SELECT_ENC', hold_ms=300)
        cap.wait(.2)
        dz.pad_event(0, 7, False)
        menu = 'deluge::gui::context_menu::clip_settings::clipSettings'
        for _ in range(10):
            if inspect([menu + '.currentOption'])[0] == '3':
                break
            dz.encoder(3, -1)
            dz.wait(.5)
        else:
            raise AssertionError('Freeze option not found')
        arm_frame = cap.path.stat().st_size // 8
        dz.press('SELECT_ENC')
        dz.press('BACK')
        for _ in range(40):
            cap.wait(2)
            state = int(inspect([f'(int){freeze}->state_'])[0])
            assert state not in (0,6), state
            if state == 5:
                break
        else:
            raise AssertionError('Freeze never completed')
        if args.debug_failure:
            commands = ['set language c++', 'target remote :1257', 'tbreak ClipFreeze::fail', 'continue',
                        'bt', 'frame 1', 'info locals', 'p *this', 'p currentSong->timePerTimerTickBig', 'detach']
            probe = subprocess.Popen([str(gdb), '-q', '-batch', str(elf),
                                      *[v for cmd in commands for v in ('-ex', cmd)]],
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            dz.wait(1)
            dz.encoder(2, 10)
            text, _ = probe.communicate(timeout=100)
            (out / 'failure-debugger.log').write_text(text)
            print(text[-8000:], flush=True)
            sys.exit(0)
        values = inspect([f'{freeze}->loopSamples_', f'{freeze}->captureStartRepeat_',
                          f'{freeze}->dry_.recorder->filePathCreated.stringMemory',
                          f'{freeze}->reverb_.recorder->filePathCreated.stringMemory'])
        loop_frames, capture_repeat = map(int, values[:2])
        check('one-bar drone captures eight loops at 120 BPM', loop_frames == RATE * 16, values=values[:2])
        from read_sd_file import read_sd_file
        sd_image = Path(re.search(r'SD image ready: ([^\n]+)', cap.log.read_text(errors='replace')).group(1))
        for label, value in zip(('cache-dry', 'cache-send'), values[2:]):
            relative = re.search(r'"([^"]+)"', value).group(1).lstrip('/')
            (out / (label + '.wav')).write_bytes(read_sd_file(sd_image, relative))
        dry = wav_pcm(out / 'cache-dry.wav')
        master = array.array('i', cap.path.read_bytes())
        start, correlation = locate_source(master, dry, arm_frame - 4096, arm_frame + RATE * 3)
        check('resample matches the exact native pass used by freeze', correlation > .8, correlation=correlation)
        source = master[2*start:2*(start+loop_frames+2176)]
        write_wav(out / 'resampled120.wav', zip(source[::2], source[1::2]))
        (out / 'resampled120.s32le').write_bytes(source.tobytes())
        report.update(loop_frames=loop_frames, capture_repeat=capture_repeat, source_start_frame=start,
                      source_correlation=correlation, source_arm_frame=arm_frame)
        cap.wait(18)
        cap.save('frozen120', 16)
        cap.set_tempo(130)
        cap.wait(16 * 120 / 130 * 3)
        cap.save('frozen130', 16 * 120 / 130 * 2)
        values = inspect([f'(int){freeze}->state_', f'{freeze}->dry_.voice->timeStretcher != 0',
                          '((SoundInstrument*)currentSong->firstOutput)->voices_._M_impl._M_finish - ((SoundInstrument*)currentSong->firstOutput)->voices_._M_impl._M_start'])
        check('frozen 130 uses time stretching with no synth voices', values == ['5', 'true', '0'], values=values)
        dz.press('PLAY')
        dz.load_song('/SONGS/Synth130.XML')
        cap.wait(2)
        cap.set_tempo(130)
        dz.press('PLAY')
        cap.wait((capture_repeat + 16) * 2 * 120 / 130)
        cap.save('native130', 16 * 120 / 130 * 2)
        check('native 130 comparison is not frozen', inspect([f'{freeze} == 0'])[0] == 'true')
        dz.press('PLAY')
        check('synth captures have no aborts', not dz.assert_no_aborts())
    finally:
        cap.close()
    report_path.write_text(json.dumps(report, indent=2))

loop_frames = report['loop_frames']
for name, filename in [('Resampled', 'resampled120.wav'), ('SameDry', 'cache-dry.wav')]:
    shutil.copyfile(out / filename, card / 'SAMPLES' / filename)
    audio_song(filename, loop_frames).write(card / 'SONGS' / (name + '.XML'))
cap = Capture('audio')
dz = cap.session
try:
    for song, label in [('Resampled', 'audio-resampled130'), ('SameDry', 'audio-same-dry130')]:
        dz.load_song('/SONGS/' + song + '.XML')
        cap.wait(2)
        cap.set_tempo(120)
        check('only audio playback active: ' + song, inspect(['currentSong->firstOutput->type == OutputType::AUDIO',
                                                           'currentSong->firstOutput->next == 0']) == ['true','true'])
        dz.press('PLAY')
        cap.wait(18)
        cap.set_tempo(130)
        cap.wait(16 * 120 / 130 * 3)
        cap.save(label, 16 * 120 / 130 * 2)
        values = inspect(['((AudioClip*)currentSong->firstOutput->activeClip)->voiceSample->timeStretcher != 0'])
        check('native AudioClip time stretcher active: ' + song, values == ['true'])
        dz.press('PLAY')
        dz.assert_no_aborts()
finally:
    cap.close()
report['completed'] = True
report_path.write_text(json.dumps(report, indent=2))
