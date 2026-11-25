# Preset navigation unit tests

`PresetNavigationTests` compiles `src/deluge/gui/ui/browser/preset_navigation.cpp`,
the same source included by the firmware build. The browser and deferred loader
call these production policies:

- `performPresetLoad` consumes the pending preview, holds the busy UI mode across
  timer cancellation and loading, clears stale interruption state while retaining
  interruptions received during the load, records the result, and restores the old mode.
- `PresetNavigationContext` validates UI/selection identities, records validated
  pad releases, chooses clip, arranger, or whole-output resumption, cancels
  session navigation when arrangement recording starts, and waits for clip
  audition to end without accepting unrelated UI or target changes.
- `presetInputAction` resolves pending selections before press events, defers only
  those presses while the card or parser is busy, and preserves both edges
  while a forced load is in progress. Otherwise releases pass.
- `presetEditInputResult` redelivers only edits that are temporarily busy. Edits
  rejected by a held UI mode or failed load are consumed, so they can't stall the
  input queue ahead of the release that ends that mode.
- `presetLoadWasNavigationCancelled` distinguishes SELECT interruptions from genuine
  load failures, so an interrupted edit can be retried without hiding errors.
- `PresetEditGesture` suppresses the remaining events of a failed edit gesture
  but allows edits again after 150 ms without another rejected event.
- `PresetNavigationSteps` retains accumulated detents across catalog pages and
  wraps without counting unavailable candidates. After a full pass, it drops
  whole cycles so large offsets don't rescan the folder once per detent.
- `presetInstrumentAvailable` implements the shared availability rules, including
  the exemption that lets navigation return to the original loaded instrument.

Fixtures use distinct addresses for identity comparisons; they never dereference
or substitute implementations of Song, Clip, UI, or Instrument. Loader callbacks
supply success/failure results and observe the busy state. No hardware mocks or
allocator adapters are needed.

```sh
cmake -S tests -B build/host-tests -G Ninja
cmake --build build/host-tests --target UnitTests PresetNavigationTests
ctest --test-dir build/host-tests -R '^(UnitTests|PresetNavigationTests)$' --output-on-failure
build/host-tests/unit/preset_navigation/PresetNavigationTests -g PresetNavigationContextTests -v
# The CI workflow also collects the standard per-group JUnit reports:
mkdir -p build/host-tests/results
(cd build/host-tests/results && ../unit/preset_navigation/PresetNavigationTests -ojunit)
```

The x86 PR workflow builds the suite, runs it through CTest, and collects its
CppUTest JUnit reports alongside the other unit suites.

These tests check the production policies, not FAT/XML parsing, catalog sorting,
model lifetimes, scheduler timing, GUI input dispatch, or audio. The loader still
checks instrument/clip membership and names against the live song before using
saved pointers.

## Hardware regression checks

Run on both OLED and 7SEG, covering the preset browser, instrument clip and
keyboard views, and session/arranger navigation:

1. Scroll through disk, hibernating, and inactive in-song synth/kit presets.
   Intermediate selections must not replace the loaded sound; the final selection
   loads after SELECT settles for 150 ms, and the loading indicator clears.
2. Press an edit button or pad while a preview is pending, then turn SELECT during
   the forced load. The original edit should be retried for the new selection,
   without an abort-error popup. A corrupt file must still reject the edit.
3. Turn a gold encoder while SELECT interrupts a forced preset load. Deferred
   movement must apply once after the new selection resolves.
4. Make an in-song candidate unavailable during the delay (for example by starting
   its session clip). It must not replace the target clip. Remove a candidate or
   change UI/selection during the delay and confirm the pending task cancels safely.
5. Hold Backspace or an audition pad while a preview is pending, press an edit
   control, and release the held input. The input queue must remain responsive.
6. Cancel a pending selection with SELECT's global controls and confirm the live
   preset name is restored. Check corrupt presets and large equal-name groups too.

With allocator fault injection, fail `unknownSettings.insertAtIndex()` while
reading an unrecognized community setting. Verify the deserializer is closed and
subsequent preset previews and edits are not blocked. This needs an integration
harness or instrumented firmware; the host policy suite does not exercise it.
