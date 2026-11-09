#!/usr/bin/env python3
"""Play identical passages through actual AudioClip anti-click and baked production freeze.

The freeze variant was rendered by the real ClipFreeze controller on the host,
then streamed as an AudioClip with no pre-margin (no second anti-click fade).
The audio variant uses native AudioClip pre-margin looping. GDB verifies that
its anti-click function is actually reached; captures occur after detaching.
"""
import argparse
import array
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

from compare_crossfades import ROOT, RATE, read_pcm, write_wav

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--source", required=True)
p.add_argument("--seconds", type=int, default=1)
p.add_argument("--root", type=Path, default=ROOT / "build/clip-freeze/crossfade-ab")
args = p.parse_args()
source = args.root / args.source
out = source / f"emulator-{args.seconds}s"
card = out / "card"
for folder in ("SONGS", "SAMPLES", "SETTINGS"):
    (card / folder).mkdir(parents=True, exist_ok=True)
(card / "SETTINGS/CommunityFeatures.XML").write_text(
    '<runtimeFeatureSettings><setting name="emulatedDisplay" value="1"/></runtimeFeatureSettings>'
)
for name, file, start in [("Native", "source.wav", RATE),
                           ("Frozen", f"freeze-production-{args.seconds}s.wav", 0)]:
    shutil.copyfile(source / file, card / "SAMPLES" / f"{name}.WAV")
    song = ET.parse(Path(__file__).parent / "Freeze.XML")
    instruments = song.find("./instruments")
    instruments.clear()
    ET.SubElement(instruments, "audioTrack", name="Compare", mode="Player",
                  modFXType="none", inputChannel="left", lpfMode="24dB", hpfMode="HPLadder")
    clips = song.find("./sessionClips")
    clips.clear()
    clip = ET.SubElement(clips, "audioClip", trackName="Compare", pitchSpeedIndependent="1",
                         transpose="0", reversed="0", attack="-2147483648", priority="1",
                         isPlaying="1", isSoloing="0", isArmedForRecording="0", section="0", selected="1")
    ET.SubElement(clip, "params", volume="0x00000000", pan="0x00000000",
                  lpfFrequency="0x7FFFFFFF", hpfFrequency="0x80000000",
                  delayFeedback="0x80000000", reverbAmount="0x80000000", modFXDepth="0x80000000")
    clip.set("filePath", f"SAMPLES/{name}.WAV")
    clip.set("startSamplePos", str(start))
    clip.set("endSamplePos", str(start + RATE * args.seconds))
    clip.set("length", str(192 * args.seconds))
    clip.find("params").set("pan", "0x00000000")
    song.write(card / "SONGS" / f"{name}.XML")

emu = ROOT.parent / "delugemu"
sys.path.insert(0, str(emu / "pylib"))
from delugemu import DelugeSession
elf = ROOT / "build/Debug/deluge.elf"
gdb = ROOT / "toolchain/v22/darwin-arm64/arm-none-eabi-gcc/bin/arm-none-eabi-gdb"
audio_path = out / "audio.s32le"
report = {}


def debug(commands):
    result = subprocess.run([str(gdb), "-q", "-batch", str(elf),
                             *[v for c in ["set language c++", "target remote :1257", *commands, "detach"]
                               for v in ("-ex", c)]], capture_output=True, text=True, timeout=90, check=True)
    with (out / "debugger.log").open("a") as f:
        f.write(result.stdout + result.stderr)
    return result.stdout


with DelugeSession.launch(firmware=str(elf), sd=str(card), repo_root=str(emu),
                          sock="/tmp/dzfreeze.sock", int_log=str(out / "interrupts.log"),
                          run_log=str(out / "emulator.log"), audio="none", icount=1, boot_wait=7,
                          extra_qemu=["-gdb", "tcp::1257", "-global", "rza1l-gpio.edge-spacing-ns=5000000"],
                          env={"DELUGEMU_SSIF_DUMP": str(audio_path)}) as dz:
    def wait_audio(seconds):
        target = audio_path.stat().st_size + round(seconds * RATE) * 8
        deadline = time.monotonic() + max(120, seconds * 10)
        while audio_path.stat().st_size < target:
            assert time.monotonic() < deadline, "Audio stalled"
            dz.wait(.25)

    for name in ("Native", "Frozen"):
        dz.load_song(f"/SONGS/{name}.XML")
        wait_audio(2)
        dz.press("PLAY")
        if name == "Native":
            evidence = debug(["tbreak VoiceSample::fudgeTimeStretchingToAvoidClick", "continue",
                              "p phaseIncrement", "p numSamplesTilLoop"])
            values = re.findall(r"^\$\d+ = (.+)$", evidence, re.MULTILINE)
            assert values[0] == "16777216" and 0 < int(values[1]) <= 100, evidence
            report["native_anti_click"] = values
        wait_audio(3 * args.seconds + 1)
        with audio_path.open("rb") as f:
            f.seek(-RATE * args.seconds * 3 * 8, 2)
            pcm = f.read()
        path = out / f"{name.lower()}.s32le"
        path.write_bytes(pcm)
        write_wav(out / f"{name.lower()}.wav", read_pcm(path))
        dz.assert_no_aborts()
        dz.press("PLAY")
        wait_audio(.5)
        print(name, "captured; no aborts", flush=True)
    report["passed"] = True
    (out / "results.json").write_text(json.dumps(report, indent=2))
