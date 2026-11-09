# Offline track freezing

With playback stopped, hold a clip in Grid view, press SELECT, and choose **[ ] Freeze Track** in Clip Settings. The operation applies to the clip's whole track/output, including its other nonempty session clips and unique arrangement-only clips. Clips keep their original synth, kit or audio type and all editable source data.

Freezing uses an internal `StemExportType::FREEZE_TRACK` operation. It is deliberately absent from the normal stem export menu. The existing offline renderer runs each clip in isolation, without sending its rendered audio to the speakers. Progress starts at “Frozen 0 of N clips” and shows the number of clips completed, with the SD saving animation in the top-right corner. Successful completion opens a “Freezing Done!” dialog; SELECT acknowledges its OK option and returns to the song view. BACK opens the existing export-cancellation confirmation.

When export finishes, playback remains stopped. The next Play streams each clip's prepared files directly through the existing freeze-cache playback path. There is no live-to-frozen fade, spectral search, hidden thaw rebuild or frozen-to-live handoff in this workflow.

To unfreeze, stop playback and choose **[x] Freeze Track** from any clip on that track. Every cache on the track is released and its temporary WAV files are deleted through the SD card routine. The next Play uses the original renderers. Freezing again creates a fresh set of exports. Both menu actions reject a running transport, including external-clock playback.

## Rendering and playback

- Synths, audio-only kits, and loaded audio clips in Player/Sampler modes are supported. Empty clips are skipped. MIDI/CV outputs, kits containing external MIDI/gate drums, live-monitoring audio clips and clips being recorded cannot be frozen. Existing sidechain/arp restrictions still apply.
- Each clip warms up for one complete known pattern before recording another complete pattern. The period includes note iterance, independent row lengths and supported synchronized LFO periods. Combined two- and three-loop conditions require six loops. The 64-loop and file-size limits still apply.
- Drone notes, free-running/random modulation, DX7 oscillators and independently clocked effects use the existing best-effort snapshot calculation, normally at least eight clip loops. This captures a particular performance, including its actual random choices; it does not reproduce unlimited future evolution.
- Dry stereo output and the mono contribution to shared reverb are recorded separately. Kit row and affect-entire processing, audio-clip source processing, and clip automation pass through their normal renderers. Shared song processing remains live at playback, avoiding double processing or recording other tracks into the cache. Freeze does not use the user's stem normalization or song-FX export settings.
- The loop recording includes its existing continuation margin for approximately 46 ms of compensated loop crossfade. This overlap is outside the musical loop duration. It is a file-loop join, not a transition to or from the native engine.
- At the end of capture, ordinary transport note-offs release the native engine. Offline rendering continues to record a separate stop tail until the existing quiet-period criterion is met, bounded at thirty seconds. Fully frozen clips retain no native voices or delay buffers. Stopping cached playback streams the recorded tail with the existing short bridge/taper.
- All loop and release files must finish writing before a cache becomes ready. A track operation that is cancelled or fails discards both completed and incomplete exports from that operation. Playback stays stopped, and original clip activation, solo, recording-arm and pending-overdub flags are restored. Automatic section follow actions are disabled during export and restored afterward.
- Arrangement-only clips are temporarily presented to the session renderer with a valid session section. Their original array positions, section numbers and output assignments are restored. Each unique clip is exported once, regardless of how many arrangement instances refer to it.
- Tempo changes retain caches and use pitch-preserving time stretching. Cached playback can follow an external clock; offline capture itself uses the song's internal tempo.

## Cache lifetime

The cache remains session-local, as in the earlier clip-freezing implementation. Saving a song preserves its editable sources, not the freeze cache. Reloaded songs and cloned clips use their original renderers. Cached files use a dedicated `FREEZE` subfolder in the existing temporary clip-recording storage and are removed when their cache is discarded.

Editing notes or sound settings while frozen does not rerender the files. Unfreeze and freeze the track again to hear a fresh export. Changes that invalidate playback bounds or rendering mode can discard an affected clip's cache through the existing invalidation rules. New clips are not automatically exported into an already-frozen track. Recording and normal stem-export operations retain their existing native-render requirements.

## Verification

`python3 tests/integration/clip_freeze/run.py` runs the production cache controller against SD/audio doubles with address and undefined-behavior sanitizers. The offline cases verify complete-pattern warmup/capture, no premature playback, required SD finalization, release-write failure, immediate cached startup without a handoff, and cache reuse after transport stops, including an immediate restart at the same clock tick. The earlier streaming, cycle-length, failure-injection and tail tests remain available.

`run_offline_track.py` uses delugemu's front-panel interface with read-only GDB inspection. It creates two clips sharing an output, checks that freeze/unfreeze are rejected while playing, freezes the stopped track, checks all cache files on the emulator's FAT image, launches the second clip, unfreezes from that clip and verifies file deletion and return to ordinary playback. `--arrangement` adds an arrangement-only clip; `--other-track` checks that an unrelated track and its flags remain unchanged; `--cancel` interrupts after the first clip completes; `--reject-second` tests rollback when another clip cannot be rendered; `--refreeze` repeats the render/delete cycle. `--audio` selects audio-clip inspection; a kit fixture uses the instrument path, with `--period-loops` setting its comparison period for note iterance.

The dry synth comparison checks note/silence timing and aligned waveforms against native playback. Real presets and shared effects can vary, so lifecycle checks are not a claim of sample-identical arbitrary sounds. These tests do not measure hardware CPU percentages or real-card throughput.

Validated with Debug and Release firmware builds, 31 sanitizer-backed cache scenarios, and delugemu synth, kit and audio-clip runs. Emulator checks covered arrangement-only clips, unrelated-track isolation, cancellation and failure rollback, physical WAV deletion, and repeated freeze/unfreeze. Final dry comparisons had zero note-activity mismatch for the synth fixture and 0.5% for the two-cycle kit fixture; aligned waveform relative RMS errors were below 0.2% for both. These controlled comparisons do not establish equivalence for arbitrary evolving presets.

## Previous live-transition experiment

The previous implementation, documentation, host tests and audio-analysis scripts were preserved in branch **`backup/live-clip-freezing-20261109`**, commit **`25368aba33`**, before the offline changes. Earlier listening captures remain under `build/clip-freeze/`. Live transition timing and spectral-matching experiments are on hold; the Clip Settings workflow no longer invokes them.
