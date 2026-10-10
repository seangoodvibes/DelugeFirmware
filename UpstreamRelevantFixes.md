# Upstream-relevant fixes

Reviewed on 2026-10-10 against these local Git revisions:

- Branch: `Mirror-Deluge-v2`, `d3fa3037bf27460f92b895a6b6fc51642e5d496e`.
- Comparison: `upstream/main`, `2121be1985a35f2892dda1b268481e05978adb7a`.

This is a curated list of small, independently useful bug fixes. Each recommended
item addresses code still present in the upstream revision above and can be
implemented without mirroring, independent UI sessions, or the new lifetime
framework. The remote was not fetched for this review.

Commit hashes identify the original fixes for inspection with `git show`. They
are **not a promise that the entire commit can be cherry-picked unchanged**:
some include mirror documentation, branch-specific test setup, or surrounding
code that has since changed. Port the minimal production change and its relevant
regression cases. The tests linked below are evidence in this branch; the proposed
upstream ports have not yet been built or tested separately.

## First choices: memory safety and small correctness fixes

### 1. Release sample references before freeing their pointer table

**Commit:** `7752d5d51a`  
**Location:** [sample_browser.cpp](src/deluge/gui/ui/browser/sample_browser.cpp), `importFolderAsMultisamples`.

On range-allocation failure, upstream frees `sortArea` and then reads sample
pointers from it to release their temporary references. This is a direct
use-after-free on an out-of-memory recovery path.

**Minimal fix:** release the sample references first, then free the pointer table.
No import redesign is needed. This is the strongest first candidate.

**Regression:** [multisample_import_cleanup_tests.cpp](tests/unit/multisample_import_cleanup_tests.cpp),
`allocation_failure_releases_samples_before_freeing_pointer_table`. Keep the
sanitizer check for access after table deallocation.

### 2. Reacquire a Kit range after oscillator-type conversion

**Commit:** `03d3cca285`  
**Location:** [sample_browser.cpp](src/deluge/gui/ui/browser/sample_browser.cpp), `importFolderAsKit`.

The importer saves a range pointer before `setOscType(SAMPLE)`. Converting from a
different range representation can replace and free that storage; the importer
then uses the old pointer.

**Minimal fix:** reacquire the first range after conversion and handle a null
result through the existing error path. Do not bring over the later lifetime
watch or staged-conversion changes.

**Regression:** [kit_sample_import_tests.cpp](tests/unit/kit_sample_import_tests.cpp),
`type_conversion_uses_the_replacement_range_for_sample_assignment` and
`unavailable_range_after_type_conversion_cancels_import_and_releases_temporary_samples`.
The first test actually replaces the range storage in its dependency double.

### 3. Use an integer for the single-note tuning target

**Commit:** `967795466f`  
**Location:** [sample_holder_for_voice.cpp](src/deluge/model/sample/sample_holder_for_voice.cpp)
and [its header](src/deluge/model/sample/sample_holder_for_voice.h), `setTransposeAccordingToSamplePitch`.

The target MIDI note is declared as `bool`, so ordinary nonzero note numbers
become `1`. This breaks the comparison used to discard very small detuning for a
sample assigned to exactly that note.

**Minimal fix:** change the target-note parameter to `int32_t` in the declaration
and definition. Leave the tuning algorithm unchanged. The later `pitch_correction`
argument on this branch is not part of this fix.

**Regression:** [sample_pitch_tuning_tests.cpp](tests/unit/sample_pitch_tuning_tests.cpp).
Verify small detuning at the actual target note, detuning outside the tolerance,
a different target note, and unchanged octave-minimization behavior.

### 4. Preserve an audio lock already held by the caller

**Commit:** `cb74e1d19a`  
**Location:** [sound.cpp](src/deluge/processing/sound/sound.cpp), upstream `Sound::deleteMultiRange`.

Range deletion unconditionally clears `AudioEngine::audioRoutineLocked` on exit.
If its caller already held the lock, deletion unexpectedly releases it.

**Minimal fix:** save the previous lock value before setting it and restore that
value afterward. Keep upstream's existing method and interface; the later
`try_delete_multi_range` API is unnecessary for this fix.

**Regression:** [sound_range_deletion_tests.cpp](tests/unit/sound_range_deletion_tests.cpp),
`preserves_existing_audio_lock_after_range_reclamation` and
`releases_its_own_audio_lock_after_range_reclamation`.

### 5. Report pattern import and file-close errors correctly

**Commit:** `931d163ce7`  
**Location:** [storage_manager.cpp](src/deluge/storage/storage_manager.cpp), `loadPatternFile`.

`FR_OK` is zero, but the error path tests `!fileSuccess`. A successful close can
therefore replace the original import error with `SD_CARD`; a failed close after
a successful import can return success.

**Minimal fix:** return the import error if one occurred; otherwise return
`NONE` only when closing returned `FR_OK`, and `SD_CARD` on close failure.

**Regression to retain/add:** combinations of open, import, and close success or
failure, including both import and close failing. The originating commit records
an eight-combination extracted-method check, but a dedicated retained
`loadPatternFile` test was not found in the current test tree. Include one in the
upstream submission. Other storage loaders need their own review; this candidate
only covers pattern loading.

## Folder import error handling and ownership

### 6. Treat directory-read errors as failure, not end-of-directory

**Commit:** `3d8eb114b3`  
**Location:** [sample_browser.cpp](src/deluge/gui/ui/browser/sample_browser.cpp), `loadAllSamplesInFolder`.

A directory-read error currently breaks the enumeration loop just like normal
end-of-directory. An incomplete list can then be imported as a successful load.

**Minimal fix:** distinguish the failed read from a successful empty filename,
close the directory, set `Error::SD_CARD`, and use the existing sample-reference
cleanup path. Adapt the patch to upstream's `staticDIR`/`staticFNO`; local-directory
and per-load ownership refactors are not required for this bounded fix.

**Regression:** [folder_sample_loading_tests.cpp](tests/unit/folder_sample_loading_tests.cpp),
`directory_read_failure_rejects_partial_sample_list_and_releases_references` and
`initial_directory_read_failure_does_not_allocate_or_publish_samples`.

### 7. Check allocation failures while constructing import paths

**Commit:** `9f12e41080`  
**Location:** [sample_browser.cpp](src/deluge/gui/ui/browser/sample_browser.cpp), `loadAllSamplesInFolder`.

The separator and filename append calls ignore their error results. After an
allocation failure, the loader can continue with an incomplete path.

**Minimal fix:** check both `concatenateAtPos` results and use the existing
close-and-cleanup error path before requesting an audio file. Port the checks to
upstream's existing directory variables.

**Regression:** [folder_sample_loading_tests.cpp](tests/unit/folder_sample_loading_tests.cpp),
`directory_separator_allocation_failure_does_not_load_a_truncated_path` and
`filename_allocation_failure_releases_prior_samples_without_loading_wrong_file`.

### 8. Release temporary references for skipped multisample entries

**Commit:** `c8ef348e51`, building on the cleanup ordering in item 1.  
**Location:** [sample_browser.cpp](src/deluge/gui/ui/browser/sample_browser.cpp), `importFolderAsMultisamples`.

Samples skipped because pitch detection failed or because they produce no new
range retain their temporary load references. The no-usable-ranges return also
leaks the sorting table.

**Minimal fix:** record which entries have released their temporary reference,
release the remaining entries, and free the table before the empty-result return.
Keep exactly one release per acquired reference, including repeated pointers.
This is a small ownership fix; it does not require staged range publication.

**Regression:** [multisample_import_cleanup_tests.cpp](tests/unit/multisample_import_cleanup_tests.cpp)
and [kit_sample_import_tests.cpp](tests/unit/kit_sample_import_tests.cpp): skipped
pitches, duplicate pitches, all-invalid results, and no double release of completed entries.

### 9. Retain the selected filename during folder loading

**Commit:** `53546ed705`  
**Location:** [sample_browser.cpp](src/deluge/gui/ui/browser/sample_browser.cpp), `loadAllSamplesInFolder`.

Prefix calculation retains a raw pointer into the selected `FileItem`'s filename
while the loader services callbacks. Replacing that item or filename can leave a
stale pointer or change the prefix used for the original import.

**Minimal fix:** retain a `String` copy using the existing reference-counted String
API and reject a missing selected item. Use upstream's existing browser accessors;
no session-state conversion is needed.

**Regression:** [folder_sample_loading_tests.cpp](tests/unit/folder_sample_loading_tests.cpp):
changed filename, destroyed selection, missing selection, and unchanged normal
file/folder prefix behavior. This protects the filename, not every browser object.

## MIDI mapping bounds and persistence

These candidates are local changes in
[midi_follow.cpp](src/deluge/io/midi/midi_follow.cpp). Their regression cases are in
[midi_feedback_mapping_tests.cpp](tests/unit/midi_feedback_mapping_tests.cpp).

### 10. Reject invalid CC numbers before indexing mapping tables

**Commit:** `1d7a88070e`  
**Function:** `readDefaultMappingsFromFile`.

The file supplies an integer CC number which is used as an array index without
checking it. Negative values and values above 127 can access outside the table.

**Minimal fix:** skip values outside `0..kMaxMIDIValue`, consume the tag, and
continue parsing. Preserve prior valid mappings.

**Regression:** `invalid_file_cc_values_preserve_mappings_and_consume_tags` and
`file_mapping_recovers_after_invalid_entry_and_accepts_boundaries`. Test both valid
endpoints and a valid entry following an invalid one.

### 11. Check parameter IDs before feedback mapping lookup

**Commit:** `c3d3eb25bd`  
**Function:** `getCCFromParam`.

Negative and out-of-range parameter IDs can index outside the sound/global
feedback maps.

**Minimal fix:** reject negative IDs and enforce the appropriate bound for each
parameter kind before indexing. Return the existing `MIDI_CC_NONE` sentinel.

**Regression:** `invalid_ids_do_not_index_feedback_tables`,
`valid_table_boundaries_keep_mapped_values`, and
`incompatible_context_and_unlearned_entries_have_no_cc`.

### 12. Restore the saved MPE upper zone

**Commit:** `a536ed2b25`  
**Function:** `readSpecificChannelSettingsFromFile`.

The writer stores `channelOrZone + 1`. The upper MPE zone therefore serializes as
`NUM_CHANNELS`, but the reader's strict `< NUM_CHANNELS` check rejects it.

**Minimal fix:** accept the inclusive upper bound. This is a one-condition fix.

**Regression:** `all_saved_channels_and_mpe_zones_restore` and
`invalid_saved_channels_preserve_previous_selection`.

### 13. Round-trip unassigned MIDI follow channels

**Commit:** `d31792588d`; apply with item 12.  
**Functions:** `writeSpecificChannelSettingsToFile`, `readSpecificChannelSettingsFromFile`.

The unassigned sentinel is 255. The old writer emits 256, which the reader rejects,
so loading an unassigned setting can leave an earlier channel selection active.

**Minimal fix:** serialize unassigned as zero, which the existing reader maps back
to the sentinel, and explicitly accept legacy value 256 when reading older files.

**Regression:** `saved_channel_round_trip_includes_unassigned_and_both_mpe_zones`
and `legacy_unassigned_channel_value_clears_previous_selection`.

### 14. Save both distinct mappings assigned to the same CC

**Commit:** `8acb009ecc`  
**Function:** `writeDefaultMappingsToFile`.

A sound mapping suppresses serialization of a global mapping on the same CC,
even when the two parameter names differ. Saving and reloading loses the global
mapping.

**Minimal fix:** consider sound and global mappings independently; write both when
the names differ and write a shared name only once. This is a small serializer
change, with slightly more behavioral review than the bounds fixes above.

**Regression:** `distinct_sound_and_global_mappings_on_same_cc_round_trip`,
`shared_mapping_name_is_saved_once_and_restores_both_contexts`, and
`global_only_and_unpatched_sound_mappings_survive_save`.

## Small UI and naming fixes

### 15. Render clip LEDs from the supplied clip

**Commit:** `978595bc37`  
**Location:** [view.cpp](src/deluge/gui/views/view.cpp), `drawOutputNameFromDetails`.

A local `InstrumentClip* clip = nullptr` shadows the supplied clip. Its subsequent
condition can never succeed, so Keyboard, Scale Mode, and Cross Screen Edit LEDs
are cleared regardless of the supplied instrument clip's state.

**Minimal fix:** use a differently named, type-checked pointer derived from the
argument, with a null output check for Scale Mode. Use upstream's `onKeyboardScreen`
and `wrapEditing` fields rather than this branch's session accessors.

**Regression:** [output_clip_led_tests.cpp](tests/unit/output_clip_led_tests.cpp):
instrument flags, Kit scale suppression, missing output, audio clip, and null
clip. Only the single-panel cases are needed upstream.

### 16. Avoid inserting an empty MIDI CC label when allocation fails

**Commit:** `370e96d223`  
**Location:** [midi_instrument.cpp](src/deluge/model/instrument/midi_instrument.cpp), `setNameForCC`.

`labels[cc] = name` inserts an empty value before assigning the string. If string
allocation throws, the map retains an empty entry despite the failed operation.

**Minimal fix:** construct new labels with `try_emplace(cc, name)`; if the entry
already exists, update its string normally. No map or allocator redesign is needed.
This preserves storage on failure; it does not add UI-level exception recovery.

**Regression:** [midi_cc_name_tests.cpp](tests/unit/midi_cc_name_tests.cpp): failed
new-string allocation, failed existing-string update, failed map-node allocation,
invalid CCs, and valid boundaries.

## Deliberately excluded

- **Mirror transport, MIDI filtering, independent navigation and session routing:**
  feature work rather than independent upstream bug fixes.
- **Lifetime watches, retirement propagation, undo transactions, staged cloning,
  staged imports and range-conversion ownership redesign:** useful work, but too
  broad for this list of small, low-risk changes.
- **`042ce102b1`, swapping range element sizes:** fixes the branch's new general
  range-swap wrapper. Upstream's existing conversion already sets matching element
  sizes before swapping. No equivalent upstream failing path was established here.
- **`5fe58929b2`, parameter self-transfer:** upstream already returns early on
  self-transfer. That commit restores behavior during this branch's hardening work.
- **`6f00c86727`, timer-cache repair:** targets the new per-panel timer-bank
  implementation. An upstream timer bug would need separate evidence and a port.
- **`45fbd79d95`, learned Kit pitch-bend routing:** potentially relevant, but the
  one-line patch uses a row-index API added elsewhere in this branch. It needs a
  separate upstream model-stack review before being called a straightforward port.

## Suggested submission order

1. Items 1–5 as separate small fixes: direct use-after-free, stale range pointer,
   parameter type, lock restoration, and error reporting.
2. Items 10–13: bounds checks and channel round-trip repairs; submit 12 and 13
   together if convenient.
3. Items 6–9: folder-import error handling and temporary-resource cleanup.
4. Items 14–16: mapping persistence, clip LEDs, and label insertion.

For each submission, keep the patch limited to its stated behavior and bring the
corresponding regression cases into upstream's test setup. Avoid cherry-picking
the full mirror test infrastructure or unrelated changes from the current file.
