#!/usr/bin/env python3
"""Exercise freezing via delugemu's front panel; inspect state with its local GDB stub.

Requires a built Debug ELF and the sibling delugemu checkout (override --delugemu).
No real hardware is accessed. Audio is captured to a file, never host speakers.
"""

import argparse
import array
import copy
import json
import math
import re
import shutil
import struct
import subprocess
import sys
import time
import wave
import xml.etree.ElementTree as ET
from pathlib import Path

from audio_equivalence import compare, snapshot_reference

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--delugemu", type=Path, default=ROOT.parent / "delugemu")
p.add_argument("--gdb-port", type=int, default=1257)
p.add_argument("--socket", default="/tmp/dzfreeze.sock")
p.add_argument("--elf", type=Path, default=ROOT / "build/Debug/deluge.elf")
p.add_argument("--out", type=Path, default=ROOT / "build/clip-freeze/emulator")
p.add_argument(
    "--dry",
    action="store_true",
    help="Disable delay/reverb for a repeatable gain comparison",
)
p.add_argument(
    "--select-direction",
    type=int,
    choices=(-1, 1),
    default=-1,
    help="This local emulator has SELECT polarity opposite to the firmware",
)
p.add_argument(
    "--iterations",
    default="",
    help="Note iterance divisors, e.g. 2 for alternate loops or 2,3 for six loops",
)
p.add_argument(
    "--compare-fx",
    action="store_true",
    help="Compare gain and timing with effects; wet waveforms can vary between live cycles",
)
p.add_argument(
    "--audio",
    action="store_true",
    help="Use an audio clip with a generated stereo sample",
)
p.add_argument(
    "--audio-mode", default="Player", choices=("Player", "Sampler", "Looper/FX")
)
p.add_argument(
    "--drone",
    action="store_true",
    help="One full-length sustained note; export live/frozen WAVs and loop-boundary measurements",
)
p.add_argument("--note-ticks", type=int, default=192,
               help="Single-note diagnostic duration in ticks (192 = full one-second loop); requires --drone")
p.add_argument("--exact-capture", action="store_true",
               help="Expect a deterministic single-note capture instead of a snapshot")
p.add_argument("--exercise-thaw-cancel", action="store_true", help="Cancel delayed thaw both before and during native warmup")
p.add_argument("--release-test", action="store_true", help="Capture native and frozen stop tails, then verify restart")
p.add_argument("--handoff-line", type=int, help="Source line for a preserved baseline ELF")
p.add_argument("--fixed-fade", action="store_true", help="Test the fixed-rebuild fallback-fade implementation")
p.add_argument("--trace-slow", action="store_true", help="Inspect the averaged-matcher experiment")
p.add_argument("--phase-probe", action="store_true", help="Export read-only stem buffers and matcher diagnostics from a probe ELF")
p.add_argument("--trace-plan", action="store_true", help="Read advisory planning counters from a planner build")
p.add_argument("--trace-handoff", action="store_true", help="Locate the first live/cache fade with a one-shot GDB breakpoint")
p.add_argument("--external-after-freeze", action="store_true", help="Switch an existing internal-clock cache to MIDI clock")
p.add_argument("--external-clock", action="store_true", help="Capture and play frozen audio driven by real MIDI clock bytes")
p.add_argument("--tempo-sweep", action="store_true", help="Validate pitch and musical timing at faster and slower tempos")
p.add_argument("--audio-transpose", type=int, default=0)
p.add_argument("--audio-reversed", action="store_true")
p.add_argument(
    "--kit", action="store_true", help="Use a kit with a synth drum and a sample drum"
)
p.add_argument(
    "--lfo",
    action="append",
    default=[],
    help="ID:wave:syncLevel:syncType, e.g. 1:triangle:1:0; repeat for multiple LFOs",
)
p.add_argument(
    "--lfo-config",
    action="append",
    default=[],
    help="Override LFO settings without adding a patch cable",
)
p.add_argument(
    "--snapshot",
    action="store_true",
    help="Compare frozen playback to the captured native pass for nonperiodic sounds",
)
p.add_argument(
    "--dx7", action="store_true", help="Use the default DX7 oscillator patch"
)
p.add_argument("--mod-fx", help="Set modulation FX on the fixture")
p.add_argument(
    "--expected-loops",
    type=int,
    help="Expected full capture period, independently specified",
)
p.add_argument("--expect-rejected", action="store_true")
p.add_argument(
    "--continuous",
    action="store_true",
    help="Expect global-modulation cache to thaw on transport stop",
)
p.add_argument(
    "--deterministic-oscillators",
    action="store_true",
    help="Test variant: reset oscillator phases and use one unison voice to isolate modulation",
)
p.add_argument(
    "--patch", type=Path, help="Load an actual synth preset XML into the fixture"
)
p.add_argument("--sample-root", type=Path, default=ROOT / "SDCard Backup")
args = p.parse_args()
assert 0 < args.note_ticks <= 192
assert args.note_ticks == 192 or args.drone
iterations = [int(x) for x in args.iterations.split(",")] if args.iterations else [1]
assert all(1 <= x <= 8 for x in iterations)
period_loops = args.expected_loops or (8 if args.drone else math.lcm(*iterations))
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
card = out / "card"
for folder in ("SONGS", "SYNTHS", "KITS", "SAMPLES", "SETTINGS"):
    (card / folder).mkdir(parents=True, exist_ok=True)
fixture = ET.parse(HERE / "Freeze.XML")
if args.patch:
    text = re.sub(r"<\?xml[^>]*\?>", "", args.patch.read_text())
    # Older Deluge presets have multiple top-level XML elements.
    wrapper = ET.fromstring("<preset>" + text + "</preset>")
    patch = wrapper.find("sound")
    if patch is None:
        patch = wrapper.find("synth")
    assert patch is not None

    # The enclosing test song uses the current schema; preserve preset controls
    # without relabelling that modern song as a pre-2.x song.
    def attributes(node):
        for child in list(node):
            if (
                len(child) == 0
                and not child.attrib
                and child.text
                and child.text.strip()
            ):
                node.set(child.tag, child.text.strip())
                node.remove(child)
            else:
                attributes(child)

    attributes(patch)
    assert patch.tag in ("sound", "synth"), "Expected a synth preset"
    patch = copy.deepcopy(patch)
    patch.tag = "sound"
    patch.set("presetName", "SYNT8")
    patch.set("presetFolder", "SYNTHS")
    params = patch.find("defaultParams")
    assert params is not None, "Preset needs defaultParams"
    patch.remove(params)
    params.tag = "soundParams"
    clip_xml = fixture.find(".//instrumentClip")
    clip_xml.remove(clip_xml.find("soundParams"))
    clip_xml.insert(1, params)
    arp = patch.find("arpeggiator")
    if arp is not None:
        clip_xml.remove(clip_xml.find("arpeggiator"))
        clip_xml.insert(0, copy.deepcopy(arp))
    instruments = fixture.find("./instruments")
    instruments.remove(instruments.find("sound"))
    instruments.append(patch)
    for node in patch.iter():
        if "fileName" in node.attrib:
            relative = Path(node.get("fileName").lstrip("/"))
            assert ".." not in relative.parts
            target = card / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(args.sample_root / relative, target)
if args.dx7:
    osc = fixture.find("./instruments/sound/osc1")
    osc.set("type", "dx7")
    osc.set(
        "dx7randomdetune", "0"
    )  # Also allocates the engine's initialized default patch.
if args.mod_fx and not args.audio:
    fixture.find("./instruments/sound").set("modFXType", args.mod_fx)
if args.deterministic_oscillators:
    sound = fixture.find("./instruments/sound")
    for osc in (sound.find("osc1"), sound.find("osc2")):
        if osc is not None:
            osc.set("retrigPhase", "0")
    unison = sound.find("unison")
    if unison is not None:
        unison.set("num", "1")
for spec in args.lfo + args.lfo_config:
    number, waveform, level, sync_type = spec.split(":")
    number = int(number)
    assert 1 <= number <= 4
    sound = fixture.find("./instruments/sound")
    node = sound.find(f"lfo{number}")
    if node is None:
        node = ET.SubElement(sound, f"lfo{number}")
    node.attrib.update(type=waveform, syncLevel=level, syncType=sync_type)
    if spec in args.lfo:
        ET.SubElement(
            fixture.find(".//soundParams/patchCables"),
            "patchCable",
            source=f"lfo{number}",
            destination="volume",
            polarity="bipolar",
            amount="0x30000000",
        )

if args.dry:
    params = fixture.find(".//soundParams")
    params.set("reverbAmount", "0x80000000")
    params.set("delayFeedback", "0x80000000")
for i, row in enumerate(fixture.findall(".//noteRow")):
    divisor = iterations[i % len(iterations)]
    data = row.get("noteDataWithSplitProb")[2:]
    row.set(
        "noteDataWithSplitProb",
        "0x"
        + data[:22]
        + f"{(divisor << 8 | 1) if divisor > 1 else 0:04X}"
        + data[26:],
    )
if args.kit:
    instrument = fixture.find("./instruments/sound")
    instrument_clip = fixture.find(".//instrumentClip")
    params = instrument_clip.find("soundParams")
    instrument_clip.remove(params)
    instrument_clip.set("instrumentPresetFolder", "KITS")
    instrument_clip.set("inKeyMode", "0")
    instrument_clip.set("yScroll", "0")
    kit = ET.Element("kit", presetName="SYNT8", presetFolder="KITS")
    sources = ET.SubElement(kit, "soundSources")
    for i, row in enumerate(instrument_clip.findall("./noteRows/noteRow")):
        drum = copy.deepcopy(instrument)
        drum.set("name", f"Freeze Drum {i}")
        if i == 0:
            drum.set("sideChainSend", "1073741824")
        if i == 1:
            osc = drum.find("osc1")
            osc.set("type", "sample")
            osc.set("fileName", "SAMPLES/FreezeTone.WAV")
            osc.set("loopMode", "0")
            ET.SubElement(osc, "zone", startSamplePos="0", endSamplePos="44100")
        sources.append(drum)
        row.set("drumIndex", str(i))
        row.set("y", str(i))
        row.append(copy.deepcopy(params))
    instruments = fixture.find("./instruments")
    instruments.remove(instrument)
    instruments.append(kit)
    instrument_clip.insert(
        1,
        ET.Element(
            "kitParams",
            volume="0xE0000000",
            pan="0x20000000",
            reverbAmount="0x80000000" if args.dry else "0xD0000000",
        ),
    )
    with wave.open(str(card / "SAMPLES/FreezeTone.WAV"), "wb") as audio:
        audio.setparams((1, 2, 44100, 0, "NONE", "not compressed"))
        audio.writeframes(
            b"".join(
                struct.pack(
                    "<h", round(24000 * math.sin(2 * math.pi * 784 * i / 44100))
                )
                for i in range(44100)
            )
        )
if args.audio:
    assert not args.kit and not args.patch and not args.lfo and not args.lfo_config
    instruments = fixture.find("./instruments")
    instruments.clear()
    track = ET.SubElement(
        instruments,
        "audioTrack",
        name="Freeze Audio",
        mode=args.audio_mode,
        modFXType=args.mod_fx or "none",
        modFXCurrentParam="depth",
        inputChannel="left",
        lpfMode="24dB",
        hpfMode="HPLadder",
    )
    ET.SubElement(track, "delay", pingPong="1", analog="0", syncLevel="7", syncType="0")
    clips = fixture.find("./sessionClips")
    clips.clear()
    audio_clip = ET.SubElement(
        clips,
        "audioClip",
        clipName="Freeze Audio",
        trackName="Freeze Audio",
        filePath="SAMPLES/FreezeAudio.WAV",
        startSamplePos="0",
        endSamplePos="44100",
        pitchSpeedIndependent="1",
        transpose=str(args.audio_transpose),
        reversed=str(int(args.audio_reversed)),
        attack="-2147483648",
        priority="1",
        isPlaying="1",
        isSoloing="0",
        isArmedForRecording="0",
        length="192",
        section="0",
        selected="1",
    )
    ET.SubElement(
        audio_clip,
        "params",
        volume="0x00000000",
        pan="0x20000000",
        lpfFrequency="0x7FFFFFFF",
        hpfFrequency="0x80000000",
        delayFeedback="0x80000000" if args.dry else "0xC0000000",
        reverbAmount="0x80000000" if args.dry else "0xD0000000",
        modFXDepth="0x40000000" if args.mod_fx else "0x80000000",
        modFXRate="0x00000000",
    )
    with wave.open(str(card / "SAMPLES/FreezeAudio.WAV"), "wb") as audio:
        audio.setparams((2, 2, 44100, 0, "NONE", "not compressed"))
        # Two distinct notes, silent gaps and unequal channels detect missing audio,
        # mono/stereo gain errors and a shortened or misaligned cached loop.
        samples = []
        for i in range(44100):
            t = i / 44100
            active = t < 0.35 or 0.5 <= t < 0.85
            freq = 440 if t < 0.5 else 660
            value = round(16000 * math.sin(2 * math.pi * freq * t)) if active else 0
            samples.append(struct.pack("<hh", value, value // 2))
        audio.writeframes(b"".join(samples))
if args.drone:
    assert not args.audio and not args.iterations
    rows = fixture.find(".//noteRows")
    row = copy.deepcopy(rows.find("noteRow"))
    rows.clear()
    data = row.get("noteDataWithSplitProb")[2:]
    row.set(
        "noteDataWithSplitProb", f"0x00000000{args.note_ticks:08X}" + data[16:22] + "0000" + data[26:]
    )
    rows.append(row)
    if args.kit:
        # Each kit sound needs its row parameter manager. Remove the unused
        # sample drum together with its row when making a single-note fixture.
        sources = fixture.find("./instruments/kit/soundSources")
        for unused in list(sources)[1:]:
            sources.remove(unused)
fixture.write(card / "SONGS/Freeze.XML")
if args.kit:
    external_fixture = copy.deepcopy(fixture)
    ET.SubElement(
        external_fixture.find(".//soundSources"),
        "midiOutput",
        name="External",
        channel="0",
        note="60",
    )
    extra_row = copy.deepcopy(external_fixture.find(".//noteRow"))
    extra_row.set("y", "2")
    extra_row.set("drumIndex", "2")
    extra_row.remove(extra_row.find("soundParams"))
    external_fixture.find(".//noteRows").append(extra_row)
    external_fixture.write(card / "SONGS/External.XML")
(card / "SETTINGS/CommunityFeatures.XML").write_text(
    '<runtimeFeatureSettings><setting name="emulatedDisplay" value="1"/></runtimeFeatureSettings>'
)
sys.path.insert(0, str(args.delugemu.resolve() / "pylib"))
from delugemu import DelugeSession

gdb = ROOT / "toolchain/v22/darwin-arm64/arm-none-eabi-gcc/bin/arm-none-eabi-gdb"
clip = f"(({'AudioClip' if args.audio else 'InstrumentClip'}*)currentSong->firstOutput->activeClip)"
freeze = clip + "->freeze"
menu = "deluge::gui::context_menu::clip_settings::clipSettings"
report = {
    "fixed_fade": args.fixed_fade,
    "note_ticks": args.note_ticks,
    "note_duration_seconds": args.note_ticks / 192,
    "exact_capture": args.exact_capture,
    "audio_fixture": args.audio,
    "audio_transpose": args.audio_transpose,
    "audio_reversed": args.audio_reversed,
    "kit_fixture": args.kit,
    "dry_fixture": args.dry,
    "iterations": iterations,
    "lfos": args.lfo,
    "lfo_config": args.lfo_config,
    "deterministic_oscillators": args.deterministic_oscillators,
    "snapshot": args.snapshot,
    "dx7": args.dx7,
    "mod_fx": args.mod_fx,
    "patch": str(args.patch) if args.patch else None,
    "checks": [],
}
audio_path = out / "audio.s32le"
bypass_expression = (
    clip + "->voiceSample == 0"
    if args.audio
    else "((Kit*)currentSong->firstOutput)->drumsWithRenderingActive.numElements == 0"
    if args.kit
    else "((SoundInstrument*)currentSong->firstOutput)->skippingRendering"
)


def inspect(expressions):
    commands = ["set language c++", f"target remote :{args.gdb_port}"]
    commands += ["p " + expression for expression in expressions]
    commands += ["detach"]
    result = subprocess.run(
        [
            str(gdb),
            "-q",
            "-batch",
            str(args.elf.resolve()),
            *[arg for command in commands for arg in ("-ex", command)],
        ],
        capture_output=True,
        text=True,
        timeout=25,
        check=True,
    )
    with (out / "debugger.log").open("a") as log:
        log.write(result.stdout + result.stderr)
    values = re.findall(r"^\$\d+ = (.+)$", result.stdout, re.MULTILINE)
    assert len(values) == len(expressions), result.stdout + result.stderr
    return values


def check(name, condition, **details):
    report["checks"].append({"check": name, "passed": bool(condition), **details})
    (out / "results.json").write_text(json.dumps(report, indent=2))
    print(report["checks"][-1], flush=True)
    assert condition, name


def state():
    return int(inspect([f"{freeze} ? (int){freeze}->state_ : -1"])[0])


def open_freeze():
    dz.pad_event(0, 7, True)
    wait_audio(0.2)
    dz.press("SELECT_ENC", hold_ms=300)
    wait_audio(0.2)
    dz.pad_event(0, 7, False)
    for _ in range(10):
        if int(inspect([menu + ".currentOption"])[0]) == (2 if args.audio else 3):
            return
        dz.encoder(3, args.select_direction)
        dz.wait(0.5)
    raise AssertionError("Could not select Freeze in Clip Settings")


def toggle(check_thaw_phase=False, capture_reference=False):
    open_freeze()
    dz.screenshot(str(out / ("menu-" + str(len(report["checks"])) + ".png")))
    if check_thaw_phase:
        previous = int(inspect([clip + "->repeatCount"])[0])
    reference_start = audio_path.stat().st_size if capture_reference else None
    toggle_before = audio_path.stat().st_size // 8
    dz.press("SELECT_ENC")
    report["last_toggle_frame_bounds"] = [toggle_before, audio_path.stat().st_size // 8]
    report.setdefault("toggle_frame_bounds", []).append(report["last_toggle_frame_bounds"])
    dz.press("BACK")
    if check_thaw_phase:
        current = int(inspect([clip + "->repeatCount"])[0])
        check(
            "thaw preserves iteration counter",
            0 <= current - previous <= 2,
            before=previous,
            after=current,
        )
    return reference_start


def wait_audio(seconds):
    # Wait for rendered frames, not host time. Never halt the guest with GDB during capture.
    target = audio_path.stat().st_size + int(seconds * 44100) * 8
    deadline = time.monotonic() + max(120, seconds * 10)
    while audio_path.stat().st_size < target:
        if time.monotonic() >= deadline:
            dz.screenshot(str(out / "audio-stalled.png"))
            raise AssertionError("Audio rendering stalled")
        dz.wait(0.25)


def wait_frozen():
    wait_audio(1 if args.phase_probe else 3 * period_loops + 3)
    deadline = time.monotonic() + 120
    while time.monotonic() < deadline:
        current = state()
        if current == 5:
            pending = inspect([f"{freeze}->entering_", f"{freeze}->capturingRelease_"])
            if pending == ["false", "false"]:
                wait_audio(1) # Let completed tail files finish writing before a stop test.
                return
        assert current in (1, 2, 3, 4, 5), f"Freeze failed: state {current}"
        dz.wait(1)
    raise AssertionError("Freeze did not finish")


def audio_window(loops):
    frames = round(44100 * loops)
    with audio_path.open("rb") as audio:
        audio.seek(-frames * 8, 2)
        data = array.array("i")
        data.frombytes(audio.read(frames * 8))
    if sys.byteorder != "little":
        data.byteswap()
    return data


def levels():
    data = audio_window(period_loops)
    return [
        math.sqrt(sum(x * x for x in data[c::2]) / (len(data) // 2)) for c in (0, 1)
    ]


def check_phase():
    voice = freeze + "->dry_.voice"
    cluster = f"((Cluster**)&{voice}->clusters)[0]"
    play_sample = f"((int)(({cluster}->clusterIndex << Cluster::size_magnitude) + (unsigned){voice}->currentPlayPos - (unsigned){cluster}->data + 1) - 44) / 6"
    values = [
        int(x)
        for x in inspect(
            [clip + "->repeatCount", freeze + "->captureStartRepeat_", play_sample]
        )
    ]
    expected = (values[0] - values[1]) % period_loops
    actual = (values[2] // 44100) % period_loops
    check(
        "cached iteration matches sequencer phase",
        actual == expected,
        repeat=values[0],
        capture_repeat=values[1],
        sample=values[2],
        expected_phase=expected,
        actual_phase=actual,
    )


midi_qemu = []
if args.external_clock:
    Path("/tmp/dzfreeze-midi.sock").unlink(missing_ok=True)
    midi_qemu = ["-chardev", "socket,id=freezeMidi,path=/tmp/dzfreeze-midi.sock,server=on,wait=off",
                 "-serial", "chardev:freezeMidi", "-global", "rza1l-usb.midi=on"]

with (
    DelugeSession.launch(
        firmware=str(args.elf.resolve()),
        sd=str(card),
        repo_root=str(args.delugemu.resolve()),
        sock=args.socket,
        int_log=str(out / "interrupts.log"),
        run_log=str(out / "emulator.log"),
        audio="none",
        icount=1,  # One virtual timebase for CPU/audio; host scheduling must not shorten captured notes.
        boot_wait=7,
        extra_qemu=[
            "-gdb",
            f"tcp::{args.gdb_port}",
            "-global",
            "rza1l-gpio.edge-spacing-ns=5000000",
        ] + midi_qemu,
        env={"DELUGEMU_SSIF_DUMP": str(audio_path)},
    ) as dz
):
    dz.load_song("/SONGS/Freeze.XML")
    wait_audio(3)  # Allow loading to settle in guest time, even when the host is busy.
    check("starts unfrozen", state() == -1)
    if args.kit:
        values = inspect(
            [
                "currentSong->firstOutput->type == OutputType::KIT",
                "currentSong->firstOutput->type",
            ]
        )
        check("fixture loaded as kit", values[0] == "true", values=values)
        if not args.drone:
            sample_drum = "((SoundDrum*)((Kit*)currentSong->firstOutput)->firstDrum->next)"
            ranges = sample_drum + "->sources[0].ranges"
            holder = f"((MultisampleRange*)((char*){ranges}.memory + {ranges}.memoryStart * {ranges}.elementSize))->sampleHolder"
            values = inspect(
                [
                    sample_drum + "->sources[0].oscType == OscType::SAMPLE",
                    f"((Sample*){holder}.audioFile)->lengthInSamples",
                ]
            )
            check("sample drum loaded from WAV", values == ["true", "44100"], values=values)
    if args.audio:
        values = inspect(
            [
                "currentSong->firstOutput->type == OutputType::AUDIO",
                f"((Sample*){clip}->sampleHolder.audioFile)->lengthInSamples",
                f"{clip}->sampleHolder.transpose",
            ]
        )
        check(
            "audio clip and source sample loaded",
            values == ["true", "44100", str(args.audio_transpose)],
            values=values,
        )
    if args.patch:
        unison = patch.find("unison")
        expected_unison = (
            1
            if args.deterministic_oscillators
            else (int(unison.get("num", "1")) if unison is not None else 1)
        )
        sound = ("((SoundDrum*)((Kit*)currentSong->firstOutput)->firstDrum)"
                 if args.kit else "((SoundInstrument*)currentSong->firstOutput)")
        values = inspect([sound + "->numUnison", sound + "->transpose"])
        check(
            "preset controls loaded",
            int(values[0].split()[0]) == expected_unison
            and int(values[1]) == int(patch.get("transpose", "0")),
            values=values,
        )
    if args.dx7:
        sound = (
            "((SoundDrum*)((Kit*)currentSong->firstOutput)->firstDrum)"
            if args.kit
            else "((SoundInstrument*)currentSong->firstOutput)"
        )
        check(
            "DX7 engine loaded",
            inspect(
                [
                    sound + "->sources[0].oscType == OscType::DX7",
                    sound + "->sources[0].dxPatch != 0",
                ]
            )
            == ["true", "true"],
        )
    original_stereo = inspect(["AudioEngine::renderInStereo"])[0]
    report["render_in_stereo"] = original_stereo
    if args.external_clock and not args.external_after_freeze:
        from midi_clock import MidiClock
        clock = MidiClock(dz, audio_path, Path("/tmp/dzfreeze-midi.sock"))
        clock.start()
    else:
        dz.press("PLAY")
    wait_audio((3 if args.drone else 2) * period_loops + 2)
    live_levels = levels()
    live_audio = audio_window((3 if args.drone else 2) * period_loops)
    (out / "live.s32le").write_bytes(live_audio.tobytes())
    check("clip produces audio", min(live_levels) > 1000, rms=live_levels)
    if args.release_test:
        native_stop_start = audio_path.stat().st_size // 8 - 44100
        report["native_stop_bounds"] = [audio_path.stat().st_size // 8]
        dz.press("PLAY")
        report["native_stop_bounds"].append(audio_path.stat().st_size // 8)
        wait_audio(32)
        with audio_path.open("rb") as audio:
            audio.seek(native_stop_start * 8)
            (out / "native-stop.s32le").write_bytes(audio.read())
        dz.press("PLAY")
        wait_audio(3 * period_loops + 2)
    if args.audio and args.mod_fx:
        check(
            "audio modulation effect is rendering",
            inspect(
                [
                    "((AudioOutput*)currentSong->firstOutput)->currentModFXParam == ModFXParam::DEPTH",
                    "((AudioOutput*)currentSong->firstOutput)->modfx.modFXBuffer != 0",
                ]
            )
            == ["true", "true"],
        )
    if args.audio:
        source_before = inspect(
            [
                f"{clip}->sampleHolder.audioFile",
                f"{clip}->sampleHolder.startPos",
                f"{clip}->sampleHolder.endPos",
            ]
        )
        if args.audio_transpose:
            values = inspect(
                [
                    f"{clip}->sampleHolder.neutralPhaseIncrement",
                    f"{clip}->sampleControls.pitchAndSpeedAreIndependent",
                    f"{clip}->voiceSample && ({clip}->voiceSample->timeStretcher != 0 || {clip}->voiceSample->cache != 0)",
                ]
            )
            check(
                "live audio uses pitch/time-stretch processing or its native cache",
                int(values[0]) != 16777216 and values[1:] == ["true", "true"],
                values=values,
            )
    capture_start = toggle(capture_reference=args.snapshot or args.drone)
    report["freeze_toggle_frame_bounds"] = report["last_toggle_frame_bounds"]
    if args.expect_rejected:
        check("unsupported freeze request rejected", state() in (-1, 0))
        check(
            "rejection allocates no recorders",
            inspect(["AudioEngine::firstRecorder == 0"])[0] == "true",
        )
        dz.assert_no_aborts()
        check("no firmware aborts", True)
        sys.exit(0)
    if args.trace_handoff:
        assert args.drone
        commands = ["set language c++", f"target remote :{args.gdb_port}", f"tbreak clip_freeze.cpp:{args.handoff_line or next(i for i,line in enumerate((ROOT / 'src/deluge/model/clip/clip_freeze.cpp').read_text().splitlines(), 1) if ('handoffFade_.reset(fallbackFadeSamples)' if args.fixed_fade else 'handoffFade_.reset(thawMatched_') in line)}",
                    "continue", "next", f"p {freeze}->handoffFade_.remaining", f"p {freeze}->handoffFade_.length", f"p {freeze}->warmupSamples_", "detach"]
        result = subprocess.run([str(gdb), "-q", "-batch", str(args.elf.resolve()),
                                 *[arg for command in commands for arg in ("-ex", command)]],
                                capture_output=True, text=True, timeout=120, check=True)
        (out / "handoff-debugger.log").write_text(result.stdout + result.stderr)
        fade_values = re.findall(r"^\$\d+ = (.+)$", result.stdout, re.MULTILINE)
        assert len(fade_values) == 3 and fade_values[0] == fade_values[1] and int(fade_values[0]) > 0, result.stdout
        report["entry_fade_samples"] = int(fade_values[0])
        report["entry_wait_samples"] = int(fade_values[2])
        if args.trace_plan:
            report["entry_plan"] = inspect([f"{freeze}->cachedAnalysisSamples_", f"{freeze}->plannedWindows_", f"{freeze}->transitionPlan_.ready_", f"{freeze}->syncedStretch_"])
        # The file position after detach locates the handoff approximately; the
        # audio callback/SSIF buffering means this is not a sample-exact timestamp.
        report["freeze_handoff_approx_frame"] = audio_path.stat().st_size // 8
    wait_frozen()
    wait_audio((1 if args.phase_probe else 3 if args.drone else 2) * period_loops + 2)
    frozen_levels = levels()
    frozen_audio = audio_window((1 if args.phase_probe else 3 if args.drone else 2) * period_loops)
    (out / "frozen.s32le").write_bytes(frozen_audio.tobytes())
    if args.external_clock:
        from tempo_analysis import compare_tempo
        if args.external_after_freeze:
            from midi_clock import MidiClock
            clock = MidiClock(dz, audio_path, Path("/tmp/dzfreeze-midi.sock"))
            clock.start(start_transport=False)
            wait_audio(3)
        check("MIDI input clock drives playback", inspect(["(int)playbackHandler.playbackState"])[0] == "6")
        original_cache = inspect([f"{freeze}->dry_.recorder", f"{freeze}->loopSamples_"])
        for factor in (.8, 1.25, 1):
            clock.interval = 44100 / 192 * factor
            wait_audio(period_loops * factor * 7 + 2)
            changed = audio_window(period_loops * factor * 4)
            metrics = compare_tempo(frozen_audio, changed, 44100 * period_loops, 44100 * period_loops * factor)
            report.setdefault("external_tempo", []).append(metrics)
            (out / f"external-{factor}.s32le").write_bytes(changed.tobytes())
            check("external tempo change retains cache", state() == 5 and inspect([f"{freeze}->dry_.recorder", f"{freeze}->loopSamples_"]) == original_cache)
            check("external clock preserves pitch", metrics["median_pitch_error_fraction"] < .015, metrics=metrics)
            check("external clock preserves musical pattern", metrics["activity_mismatch_fraction"] < .10 and metrics["envelope_error"] < .35, metrics=metrics)
        check("MIDI clock sender healthy", clock.error is None, error=clock.error)
        toggle()
        wait_audio(2)
        for _ in range(35):
            if state() == 0:
                break
            wait_audio(1)
        check("external clock thaw releases cache", state() == 0)
        clock.close()
        dz.assert_no_aborts()
        check("no firmware aborts", True)
        sys.exit(0)
    if args.drone:
        from drone_analysis import analyze, export_pair
        check(
            "single-note capture has expected snapshot policy and length",
            inspect([f"{freeze}->snapshot_", f"{freeze}->loopSamples_"])
            == ["false" if args.exact_capture else "true", str(44100 * period_loops)],
        )
        report["drone"] = {
            "live": analyze(live_audio, period_loops) if not args.phase_probe else {"probe_capture": True},
            "frozen": analyze(frozen_audio, period_loops) if not args.phase_probe else {"probe_capture": True},
        }
        export_pair(out, live_audio, frozen_audio)
        check("drone cache bypasses renderer", inspect([bypass_expression])[0] == "true", measurements=report["drone"])
        sound = ("((SoundDrum*)((Kit*)currentSong->firstOutput)->firstDrum)"
                 if args.kit else "((SoundInstrument*)currentSong->firstOutput)")
        resources = inspect([sound + "->voices_._M_impl._M_finish - " + sound + "->voices_._M_impl._M_start",
                             sound + "->delay.primaryBuffer.start_ == 0", sound + "->delay.secondaryBuffer.start_ == 0"])
        check("frozen drone releases native voices and delay buffers", resources == ["0", "true", "true"], values=resources)
        with audio_path.open("rb") as audio:
            audio.seek(max(0, capture_start - 44100 * 8))
            (out / "freeze-transition.s32le").write_bytes(audio.read())
        if args.trace_handoff:
            from compare_crossfades import read_pcm, write_wav
            with audio_path.open("rb") as audio:
                audio.seek(max(0, report["freeze_handoff_approx_frame"] - 44100 * 2) * 8)
                (out / "freeze-entry.s32le").write_bytes(audio.read(44100 * 4 * 8))
            write_wav(out / "freeze-entry.wav", read_pcm(out / "freeze-entry.s32le"))
        if args.fixed_fade or args.release_test:
            release_metadata = inspect([f"{freeze}->releaseSamples_", f"{freeze}->releaseDry_.recorder->status == RecorderStatus::COMPLETE"])
            check("native release recorded to SD", int(release_metadata[0]) > 0 and release_metadata[1] == "true", values=release_metadata)
        if args.release_test:
            frozen_stop_start = audio_path.stat().st_size // 8 - 44100
            report["frozen_stop_bounds"] = [audio_path.stat().st_size // 8]
            dz.press("PLAY")
            report["frozen_stop_bounds"].append(audio_path.stat().st_size // 8)
            wait_audio(.25)
            check("stop streams release tail without native voices", inspect([f"{freeze}->releasing_", sound + "->voices_._M_impl._M_finish - " + sound + "->voices_._M_impl._M_start"]) == ["true", "0"])
            wait_audio(32)
            with audio_path.open("rb") as audio:
                audio.seek(frozen_stop_start * 8)
                (out / "frozen-stop.s32le").write_bytes(audio.read())
            check("release finishes and retains cache", state() == 4 and inspect([f"{freeze}->releasing_", f"{freeze}->dry_.voice == 0", f"{freeze}->releaseDry_.voice == 0"]) == ["false", "true", "true"])
            dz.press("PLAY")
            wait_audio(period_loops + 1)
            check("restart returns to frozen playback", state() == 5)
        if args.exercise_thaw_cancel:
            cached_recorder = inspect([f"{freeze}->dry_.recorder"])[0]
            for label, wait in [("boundary wait", .1), ("native warmup", period_loops + .5)]:
                toggle()
                wait_audio(wait)
                check("thaw pending during " + label, inspect([f"{freeze}->thawing_"])[0] == "true")
                toggle()
                check("cancel " + label + " reuses cache and stops native voices",
                      state() == 5 and inspect([f"{freeze}->thawing_", f"{freeze}->dry_.recorder",
                                               sound + "->voices_._M_impl._M_finish - " + sound + "->voices_._M_impl._M_start"])
                      == ["false", cached_recorder, "0"])
        # Preserve the audible transition, not just the eventual thawed state.
        transition_start = max(0, audio_path.stat().st_size - 44100 * 8)
        report["thaw_capture_start_frame"] = transition_start // 8
        toggle()
        wait_audio((14 if args.fixed_fade else 35) + period_loops)
        if args.fixed_fade:
            # Preserve the legacy result shape: no matching gate was selected.
            report["thaw_state"] = ["false"] + inspect(
                [f"{freeze}->warmupSamples_", f"{freeze}->nativeWaitSamples_"]
            )
        else:
            report["thaw_state"] = inspect(
                [f"{freeze}->thawMatched_", f"{freeze}->warmupSamples_", f"{freeze}->nativeWaitSamples_"]
            )
        if args.trace_plan:
            report["thaw_plan"] = inspect([f"{freeze}->cachedAnalysisSamples_", f"{freeze}->plannedWindows_"])
        with audio_path.open("rb") as audio:
            audio.seek(transition_start)
            transition = array.array("i", audio.read())
        if sys.byteorder != "little":
            transition.byteswap()
        (out / "thaw.s32le").write_bytes(transition.tobytes())
        check("single-note thaw returns to synthesis", state() == 0 and (
            min(levels()) > 1000 if args.note_ticks < 192 else inspect([bypass_expression])[0] == "false"))
        if args.trace_slow:
            report["slow_match_run_samples"] = int(inspect([f"{freeze}->slowMatchedSamples_"])[0])
        if args.phase_probe:
            fields = ["count", "rowCount", "blockCount", "nativeStartRepeat", "recordedStartRepeat", "periodTicks", "clipTicks", "firstCacheSample", "fadeSamples", "observedSamples", "passedSamples", "longestMatch"] + [f"rejectedSamples[{i}]" for i in range(5)]
            values = inspect(["freezeProbe." + field for field in fields] + ["sizeof(FreezeProbe::Row)"])
            report["probe"] = dict(zip(fields + ["rowBytes"], map(int, values)))
            commands = ["set language c++", f"target remote :{args.gdb_port}"]
            for field, count, filename in [("live", "count", "probe-live.s32le"), ("cache", "count", "probe-cache.s32le"), ("rows", "rowCount", "probe-rows.bin"), ("blockSizes", "blockCount", "probe-blocks.bin")]:
                commands.append(f"dump binary memory {out / filename} &freezeProbe.{field}[0] &freezeProbe.{field}[freezeProbe.{count}]")
            commands.append("detach")
            result = subprocess.run([str(gdb), "-q", "-batch", str(args.elf.resolve()),
                                     *[arg for command in commands for arg in ("-ex", command)]],
                                    capture_output=True, text=True, timeout=180, check=True)
            (out / "probe-dump.log").write_text(result.stdout + result.stderr)
            check("probe captured complete rebuild and fade", report["probe"]["count"] >= int(report["thaw_state"][1]) + report["probe"]["fadeSamples"] - 128
                  and (out / "probe-live.s32le").stat().st_size == report["probe"]["count"] * 8)

        dz.assert_no_aborts()
        check("no firmware aborts", True)
        sys.exit(0)
    if args.snapshot:
        with audio_path.open("rb") as audio:
            audio.seek(capture_start)
            recording = array.array("i")
            recording.frombytes(audio.read((period_loops + 2) * 44100 * 8))
        if sys.byteorder != "little":
            recording.byteswap()
        live_audio = snapshot_reference(recording, frozen_audio, period_loops)
        (out / "captured-reference.s32le").write_bytes(live_audio.tobytes())
        live_levels = [
            math.sqrt(sum(x * x for x in live_audio[c::2]) / (len(live_audio) // 2))
            for c in (0, 1)
        ]
        check("snapshot mode selected", inspect([f"{freeze}->snapshot_"])[0] == "true")
    if args.dry or args.compare_fx or args.patch or args.snapshot:
        report["audio_equivalence"] = compare(live_audio, frozen_audio, period_loops)
        if not args.dry:
            split = len(live_audio) // 2
            report["live_cycle_variation"] = compare(
                live_audio[:split], live_audio[split:], period_loops
            )
    values = inspect(
        [
            f"{freeze}->dry_.recorder->numSamplesCaptured",
            f"{freeze}->reverb_.recorder->numSamplesCaptured",
            bypass_expression,
            f"{freeze}->loopSamples_",
            f"{freeze}->crossfadeLength_",
        ]
    )
    check(
        "complete dry and send loops; voices bypassed",
        values[0] == values[1]
        and int(values[3]) == 44100 * period_loops
        and int(values[0]) == int(values[3]) + int(values[4]) + 128
        and values[2] == "true",
        values=values,
    )
    if args.audio:
        check(
            "freeze retains original source and markers",
            source_before
            == inspect(
                [
                    f"{clip}->sampleHolder.audioFile",
                    f"{clip}->sampleHolder.startPos",
                    f"{clip}->sampleHolder.endPos",
                ]
            ),
        )
    check(
        "freeze preserves output mode",
        inspect(["AudioEngine::renderInStereo"])[0] == original_stereo,
    )
    if args.kit:
        values = inspect(
            ["AudioEngine::audioSampleTimer", "AudioEngine::timeLastSideChainHit"]
        )
        age = (int(values[0]) - int(values[1])) & 0xFFFFFFFF
        check(
            "frozen kit continues sidechain triggers",
            age < 44100 * iterations[0],
            age_samples=age,
        )
    check("frozen stream produces audio", min(frozen_levels) > 1000, rms=frozen_levels)
    if args.dry or args.compare_fx or args.snapshot:
        ratios = [frozen / live for frozen, live in zip(frozen_levels, live_levels)]
        check(
            "playback gain matches live rendering",
            all(0.97 < ratio < 1.03 for ratio in ratios),
            ratios=ratios,
        )
    if args.dry or args.compare_fx or args.snapshot:
        comparison = report["audio_equivalence"]
        check(
            "multi-cycle note and silence pattern matches",
            comparison["envelope_relative_rms_error"] < 0.12
            and comparison["note_activity_mismatch_fraction"] < 0.04,
            comparison=comparison,
        )
        errors = comparison["waveform_relative_rms_errors"]
        if args.dry:
            check(
                "aligned note waveforms match",
                bool(errors) and max(errors) < 0.03,
                errors=errors,
            )
    if period_loops > 1:
        check_phase()
    dz.press("PLAY")
    dz.wait(0.5)
    for _ in range(35):
        if inspect([f"{freeze}->releasing_"])[0] == "false":
            break
        wait_audio(1)
    check(
        "stop invalidates song-relative modulation cache"
        if args.continuous
        else "stop retains completed cache",
        state() == (0 if args.continuous else 4),
    )
    dz.press("PLAY")
    dz.wait(2)
    if args.continuous:
        check("restart uses live modulation", state() == 0)
        toggle()
        wait_frozen()
    check(
        "restart streams cached audio"
        if not args.continuous
        else "refreeze restores caching",
        state() == 5,
    )
    if period_loops > 1:
        check_phase()
    toggle(check_thaw_phase=True)
    dz.wait(period_loops + 1)
    for _ in range(35):
        if state() == 0:
            break
        wait_audio(1)
    values = inspect(
        [
            f"(int){freeze}->state_",
            "AudioEngine::firstRecorder",
            bypass_expression,
        ]
    )
    check(
        "thaw restores instrument and releases recorders",
        values[0] == "0" and values[1].endswith("0x0") and min(levels()) > 1000,
        values=values,
    )
    toggle()
    wait_frozen()
    original_cache = inspect([f"{freeze}->dry_.recorder", f"{freeze}->loopSamples_"])
    for offset in ([96, -96, -96, 96] if args.tempo_sweep else [4]):
        dz.encoder(2, offset)
        wait_audio(2)
        check("tempo change retains frozen cache", state() == 5 and inspect([f"{freeze}->dry_.recorder", f"{freeze}->loopSamples_"]) == original_cache)
        if args.tempo_sweep:
            from tempo_analysis import compare_tempo
            tick, period = map(int, inspect(["currentSong->timePerTimerTickBig", f"{freeze}->period_"]))
            seconds = tick / 2**32 * period / 44100
            wait_audio(seconds * 5)
            changed = audio_window(seconds * 4)
            index = len(report.get("tempo_sweep", []))
            (out / f"tempo-{index}.s32le").write_bytes(changed.tobytes())
            metrics = compare_tempo(frozen_audio, changed, 44100 * period_loops, seconds * 44100)
            metrics["period_seconds"] = seconds
            metrics["stretcher_active"] = inspect([f"{freeze}->dry_.voice->timeStretcher != 0"])[0]
            report.setdefault("tempo_sweep", []).append(metrics)
            check("stretched pitch is preserved", metrics["median_pitch_error_fraction"] < .015, metrics=metrics)
            check("stretched musical pattern matches", metrics["activity_mismatch_fraction"] < .10 and metrics["envelope_error"] < .35, metrics=metrics)
            playback_status = inspect([f"(int){freeze}->state_", bypass_expression])
            check("stretched playback remains frozen", playback_status[0] == "5", values=playback_status)
    if args.kit:
        dz.press("PLAY")
        dz.load_song("/SONGS/External.XML")
        wait_audio(3)
        check(
            "external-row fixture loaded as kit",
            inspect(["currentSong->firstOutput->type == OutputType::KIT"])[0] == "true",
        )
        check(
            "MIDI drum loaded",
            inspect(
                [
                    "((Kit*)currentSong->firstOutput)->firstDrum->next->next->type == DrumType::MIDI"
                ]
            )[0]
            == "true",
        )
        dz.press("PLAY")
        toggle()
        check("MIDI kit cannot be frozen", state() == -1)
        check(
            "rejected freeze allocates no recorders",
            inspect(["AudioEngine::firstRecorder == 0"])[0] == "true",
        )
    dz.assert_no_aborts()
    check("no firmware aborts", True)
