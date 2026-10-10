# Independent Deluge mode: remaining blockers

Last reviewed: 2026-10-10. This is the current completion checklist; the
[incremental implementation notes](DelugeMirroring.md) remain the change history.

Independent mode remains disabled. `supported_session_modes = 1` in
[`mirror_protocol.h`](../../src/deluge/hid/mirror_protocol.h) advertises only
visible-host mirroring. Do not enable it merely because the build and native tests
pass. No completion percentage is assigned: the items below differ substantially
in size, and audit items may reveal additional work.

## Tracking rules

- **Confirmed gap:** current code or recent tests demonstrate the limitation.
- **Audit required:** safety/completeness has not been established; this is not a
  claim that every path in that area is broken.
- Keep each ID stable. Close an item only with an implementation reference,
  regression-test evidence, and any required hardware results recorded here.
- A guard returning an error is mitigation, not proof of rollback or lifetime
  protection. A passing mocked callback test is not proof that real destruction,
  parameter transfer, or two-device behavior is safe.
- Record newly discovered subcases under the relevant ID rather than adding a
  new top-level blocker for each guard.

## Lifetime checklist

### L1 — Detached objects and callback lifetime protection

- [ ] **Open — confirmed gap.** Allocation-free clip lifetime watches now cancel
  recording-clone work after retirement/destruction, including detached clips and
  same-address reuse. They signal cancellation rather than retaining objects.
  Other saved clip references, outputs, drums, samples, actions and consequences
  still lack comprehensive protection through callbacks. Checks after a callback
  do not establish safety inside that callback; pointer equality remains
  insufficient for targets without lifetime watches.
- **Where:** clip/consequence restoration; shared model mutation; retained UI,
  model-stack and action targets. Start with
  [`consequence_clip_existence.cpp`](../../src/deluge/model/consequence/consequence_clip_existence.cpp),
  [`clip.cpp`](../../src/deluge/model/clip/clip.cpp), and
  [`instrument_clip.cpp`](../../src/deluge/model/clip/instrument_clip.cpp).
- **Done when:** each retained target has an explicit ownership/lifetime contract
  across callbacks, with safe cancellation and reclamation. Registered and
  detached objects both have defined behavior; no reliance on a UI refresh alone.
- **Required tests:** destroy/replace targets during callbacks, including reused
  addresses, detached objects, nested edits and song replacement. Verify no stale
  access, double destruction or leaked ownership with real implementations where
  feasible, plus sanitizer runs where supported.

### L2 — Deletion callbacks and safe publication of deletion history

- [ ] **Open — confirmed gap.** `ConsequenceClipExistence::revert` calls note-stop,
  session, recording, removal, detachment and output callbacks. Successful return
  is followed by `Action::recordClipExistenceChange` publishing the private
  consequence and accessing the action. The extensive allocation guards do not
  establish lifetime safety across this later phase.
- **Done when:** callback invalidation cannot lead to stale action/clip/output
  access; a successfully detached clip always has exactly one valid owner; history
  publication either succeeds safely or follows a defined recovery/cleanup path.
  Freeing only the consequence storage must not leak its detached clip.
- **Required tests:** invalidation at every deletion callback, action/history
  replacement, clip return to the song, output removal, nested deletion, and
  publication failure after successful detachment. Assert model and owner state,
  not just the returned error.
- **Depends on:** L1.

### L3 — Failure cleanup and early output reclamation

- [ ] **Open — audit required, with known callback-lifetime concerns.** Review
  `Action::prepareForDestruction`, `ConsequenceClipExistence::prepareForDestruction`,
  and `ActionLogger::revert` failure cleanup. They must use a live owning song and
  retain required objects until all dependent consequences/references are gone.
  Undo-retained output tracking exists; it is not a complete reclamation proof.
- **Done when:** failed undo, history discard and song teardown release each
  detached object exactly once, preserve song-owned objects, and never reclaim an
  output still used by another clip, instance or history entry.
- **Required tests:** cleanup after partial restoration/detachment, changed song,
  returned clips, shared outputs, nested cleanup and both undo queues; allocation
  accounting and actual destruction checks.
- **Depends on:** L1, L2 and the ownership decisions in R1/R2.

## Recovery checklist

### R1 — Partial parameter restoration

- [ ] **Open — confirmed gap.** Base, MIDI and kit restoration can transfer
  parameter collections before a later failure. The guards stop further work;
  they do not restore already consumed backups or guarantee a safe retry.
  Reservation can also be followed by reattachment and then insertion failure.
- **Done when:** define and implement failure semantics for the entire operation:
  either restore the original state or complete a documented consistent recovery
  with explicit ownership and retry/discard behavior. Include row parameters,
  expression parameters, MIDI backup, kit-level parameters and clip insertion.
- **Required tests:** failure at every transfer/trim/insertion boundary; multiple
  rows; missing/invalid backups; changed assignments; actual parameter values and
  ownership before/after; retry and discard; no loss or double-free of collections.
- **Depends on:** L1; coordinate with L3. Existing kit callback tests are partial
  failure containment, not transactional recovery tests.

### R2 — Failed multi-consequence undo/redo and arrangement recording

- [ ] **Open — confirmed gap.** `Action::revert` may apply a prefix before failure.
  History remains reachable for cleanup, and the logger discards a failed action;
  that does not reverse the prefix. Arrangement recording additionally clears and
  rebuilds history while reverting.
- **Where:** [`action.cpp`](../../src/deluge/model/action/action.cpp) and
  [`action_logger.cpp`](../../src/deluge/model/action/action_logger.cpp).
- **Done when:** partial application, new/old arrangement history and failure
  cleanup have consistent model/ownership semantics; neither panel can continue
  using an invalid history direction or stale target.
- **Required tests:** fail each consequence position, both directions, mixed clip/
  instance/parameter consequences, partial arrangement clear, nested history and
  song/action invalidation; validate actual model state and both panels' recovery.
- **Depends on:** L1–L3 and R1 for clip-restoration failures.

### R3 — Arrangement batches containing clip-bearing deletions

- [ ] **Open — confirmed gap.** Recovery covers isolated moves, shortening and
  empty-instance deletion prefixes. Single-instance cleanup failure can restore a
  still-live reference. Successful clip detachment and earlier batch changes are
  outside that narrow recovery path.
- **Where:** [`arranger_view.cpp`](../../src/deluge/gui/views/arranger_view.cpp),
  `Action::rollback_instance_batch`, and song clip-instance cleanup.
- **Done when:** contraction/expansion failure involving a clip-bearing deletion
  leaves instances, clip ownership, automation and history mutually consistent.
- **Required tests:** mixed outputs, multiple deletions, failure after successful
  detachment, earlier moves/shortening, automation insertion/contraction, occupied
  restore destinations and exhausted retained capacity.
- **Depends on:** L2, R1 and R2.

### R4 — Recovery after the initiating context changes

- [ ] **Open — confirmed gap.** Current rollback deliberately rejects changed
  song, UI owner/revisions, action/history identity or arrangement cursor. This
  prevents rollback against the wrong context but can leave completed edits.
- **Done when:** define which partial edits remain committed and how they are
  represented, or recover them under a valid retained context; both panels reach
  safe screens/selections and all held inputs are released appropriately.
- **Required tests:** each invalidation dimension, nested callbacks, panel switch,
  disconnect and song replacement; check model, history, selections and held input.
- **Depends on:** L1–L3 and R2/R3.

### R5 — Import, preview, clone and pre-edit action acquisition audit

- [ ] **Open — audit required.** The completion notes still identify interleaved
  pattern previews, broader clone/note transactions and action acquisition before
  guarded edits. Numerous local fixes exist; end-to-end completeness is unproven.
- **Done when:** inventory the remaining entry points and close each with evidence.
  Failure must not destroy the original when cloning; previews must not overwrite
  another panel's committed edits; action acquisition must not leave stale targets.
- **Required tests:** real clone/preview cancellation and failure boundaries,
  allocation failure, concurrent panel edits, source preservation, nested action
  creation and retry. Record the audited entry-point list here.
- **Depends on:** L1 and applicable R2/R4 semantics. Schedule action-acquisition
  work after the other areas, per the requested priority.

## Other gates before enabling independent mode

These are separate from lifetime/recovery and prevent treating their completion as
completion of the whole feature.

- [ ] **G1 — Routing, transport and UI integration audit.** Independent-session
  scaffolding exists in [`mirror.cpp`](../../src/deluge/hid/mirror.cpp), including
  remote initialization/render paths and readiness checks. The advertised mode
  remains disabled. Establish end-to-end negotiation, ordered input/acknowledgement,
  Remote rendering, stale-session rejection, reconnect and resynchronization.
  Preserve SysEx-only traffic to the paired client. Audit remaining direct hardware,
  mutable singleton and shared-playback/view-state paths. Prioritize routing work.
- [ ] **G2 — Two-device acceptance.** Run the hardware matrix in
  [the main document](DelugeMirroring.md#validation): both USB-host roles, OLED and
  seven-segment devices, simultaneous same/different-menu edits, playback/storage,
  disconnect with held controls, repeated reconnect, load and memory pressure.
  Record firmware revision, device pair, observations and pass/fail results.
- [ ] **G3 — Enablement review.** Close L1–L3, R1–R5 and G1–G2 with evidence, review
  the remaining risk list, then change advertised support and add tests that prove
  requests negotiate independent mode without falling back to visible-host input.

## Suggested execution order and evidence

Prioritize G1 routing, while keeping its public capability disabled. For lifetime
and recovery: establish L1's ownership contract, implement L2, then R1 and L3;
finish R2/R3 and R4, audit R5 (action acquisition last), and perform G2/G3.
Dependencies express safety requirements, not a requirement to postpone useful
audits or test infrastructure.

Current baseline: native coverage includes undo, kit restoration, song cleanup,
parameter lifecycle, clone and mirror runtime suites. Changes validated on 2026-10-10 passed all
36 CTest suites and `./dbt build relwithdebinfo`. These results cover tested paths;
they do not close the open items above or replace hardware validation.

For each closure, append: **ID; implementation commit/PR; test names and results;
hardware evidence if required; residual limitations; reviewer/date.** No item in
this tracker has yet been closed.

### G1 progress — remote render locks (2026-10-01)

Remote UI servicing now defers timer dispatch under an existing navigation/pad
render lock and rechecks both locks after timer callbacks before rendering.
Regression: `remote_render_defers_existing_and_callback_render_locks`, covering
both lock types, unchanged render timestamp, restored Local ownership and resume.
G1 remains open; this is one service-boundary fix, not end-to-end routing proof.

### G1 progress — snapshot pad-render locks (2026-10-01)

Snapshot preparation now checks the Remote pad-render lock before timer dispatch,
after timers, and before declaring the snapshot ready. Regression:
`snapshot_waits_for_pad_render_lock_at_each_preparation_boundary` checks all three
boundaries, deferred acceptance/panel traffic, owner/guard restoration and retry
once unlocked. G1 remains open; independent capability remains disabled.

### G1 progress — startup storage/audio locks (2026-10-01)

Snapshot preparation now rejects an SD/audio lock before timer dispatch. Remote
root opening can acquire either lock after the outer routine's entry check.
Regression: `startup_lock_acquired_by_root_defers_snapshot_timers_until_unlock`
checks both locks, no timer/render/acceptance/panel work while locked, restored
owner/guard state and successful startup after unlocking. G1 remains open.

### G1 progress — lost remote UI during servicing (2026-10-01)

Remote UI service now fails the session when navigation is absent at entry or is
removed by timer callbacks. Previously those cases could silently defer rendering
and allow transport to continue. Regressions:
`missing_remote_ui_fails_service_before_timers_or_transport` and
`timer_removing_remote_ui_fails_before_render_or_transport` verify no stale
transport/rendering, owner restoration and subsequent teardown. G1 remains open.

### G1 progress — direct transport readiness (2026-10-01)

Direct transport servicing now rejects an accepted independent session whose
Remote UI is missing, before sending even a heartbeat. Pending initialization
still permits heartbeats while suppressing frame traffic. Regressions:
`direct_transport_rejects_missing_accepted_remote_ui` and
`direct_transport_allows_pending_remote_initialization_heartbeat`. G1 remains open.

### G1 progress — packet callback readiness (2026-10-01)

Packet sending now validates accepted independent UI readiness before transmission
and after yielding to transmission callbacks. A sent packet retains its sequence;
further display/heartbeat traffic is rejected after UI loss. Stop remains permitted
for teardown, and completed inputs retain their acknowledgement behavior.
Regression: `packet_callback_losing_remote_ui_stops_following_transport_packets`
checks heartbeat, sync LED, panel and OLED boundaries, packet counts, sequence
advancement, owner restoration and blocked subsequent transport. G1 remains open.

### G1 progress — song validity across transmission (2026-10-01)

Accepted independent transport now requires a current song and rejects a song
change during packet transmission, retaining the sent packet's sequence while
blocking subsequent traffic. Visible-host mirroring keeps its existing song-load
behavior. Regressions: `send_callback_song_change_stops_independent_transport_but_not_visible_mirroring`
and `accepted_independent_transport_requires_a_song_even_with_remote_navigation`.
G1 remains open; this detects invalidation, not lifetime pinning or recovery.

### G1 progress — shared readiness requires a song (2026-10-01)

Remote UI readiness now includes current-song presence, protecting input, timer
and snapshot paths as well as transport. Regressions:
`remote_readiness_requires_song_before_input_and_snapshot_callbacks` and
`remote_service_without_song_fails_before_timer_callbacks` cover queued input,
callback suppression, scope/guard restoration and resumption. G1 remains open.

### G1 progress — input dispatch context (2026-10-01)

Independent input now checks its initiating song, mode and owner after dispatch,
including pad/button deferral and encoder retries, before acknowledgement or the
next queued command. Visible-host song-change behavior is unchanged. Tests cover
completed/deferred input, completed/deferred encoders, mode/owner switches and
visible-host input. Invalidated independent sessions fail for teardown rather
than replaying queued input into a different song. G1 remains open.

### G1/R4 progress — deferred release after teardown (2026-10-01)

A deferred release now validates its original session identity before restoring
the prior held-key flag. Timeout/reconnect cleanup retains its existing ownership
behavior when the same session is still present. If the handler already ended the session, teardown's cleared hold
must remain cleared. Regression `deferred_release_cannot_restore_remote_hold_after_teardown`
failed on the prior code and covers visible-host and independent modes. Existing
valid-context retry tests remain applicable. G1 and broader R4 recovery remain open.

### G1 progress — handshake re-entry during teardown (2026-10-01)

Supported requests now honor sending/transport guards as well as the retained
closing-connection guard. Incoming/outgoing discovery and unsupported-mode replies
also defer while teardown callbacks run. Tests verify each guard, release-callback
re-entry without peer reservation or packets, and fresh negotiation after cleanup.
G1 remains open; independent mode is still not advertised.

### L2 progress — pre-detachment callback validation (2026-10-01)

Clip deletion now uses `prepare_for_deletion` to validate song, clip membership,
output, stack, owner and revisions after each pre-detachment callback. It resolves
the removal index after unsolo callbacks rather than retaining the earlier index.
`ClipDeletionPreparation` executes the production helper with injected callbacks,
including actual clip destruction at each boundary, changed contexts and a valid
reorder. The clip is not detached by this helper. L2 remains open for removal,
detachment and publication callbacks; L1 lifetime protection and recovery of prior
side effects are not provided by these guards.

### R4 progress — clip-existence entry context (2026-10-01)

- Clip-existence undo/redo rejects a replaced or missing current song before
  inspecting clip-array membership or constructing a timeline stack.
- A native fixture now executes the actual `revert` function. Tests reproduce
  both stale-song entry failures and verify that deletion-preparation and
  recreation-reservation failures do not proceed to detachment or publication.
- R4 remains open: entry validation does not protect the owning action or
  detached objects against callbacks later in the operation.

### L1/R4 progress — recreated clip activation (2026-10-01)

- Clip-existence reversion validates song, UI owner/revisions, timeline target,
  registered clip membership and output after session activation and output
  activation callbacks. Insertion's intentional peer refresh occurs before the
  revision snapshot.
- Runtime tests execute the real reversion function, destroy the restored clip at
  each activation boundary, and invalidate the song, stack, output and either
  panel's revision. The undo suite runs with address/undefined sanitizers enabled.
- This contains subsequent access after activation invalidation; it does not undo
  activation side effects or retain an action/consequence destroyed in a callback.
  L1 and R4 remain open.

### G1 progress — retain independent mode across callbacks (2026-10-01)

- Remote root startup, UI service and initial snapshot preparation recheck the host role
  and independent mode after timer and rendering callbacks. Packet transmission
  makes the same check after sending an independent-host packet, preserving the
  sequence of the packet already sent while stopping subsequent packets.
- Runtime regressions reproduced mode changes being accepted at these boundaries.
  They cover six root-startup callbacks, service/snapshot timer and render
  callbacks and transmission, verify
  that follow-up rendering/packets stop, and check scope/guard cleanup.
- G1 remains open for the broader integration audit. Advertised independent-mode
  support remains disabled.

### R4 progress — arrangement-instance undo context (2026-10-01)

- Instance movement, resizing, creation and deletion now reject a missing or
  replaced current song before output lookup or mutation. Recreation also checks
  that its caller's model-stack song survived the reservation callback.
- Runtime tests reproduce inactive-song mutation and stack replacement, verify
  no mutation or reservation on rejected entry, and exercise successful retry in
  both undo/redo directions. This does not restore an earlier action prefix; R4
  remains open.

### R2/R3 progress — occupied instance-change destinations (2026-10-01)

- Instance undo/redo checks both neighbors before moving or resizing its retained
  slot. A later edit can otherwise occupy or cross the destination while leaving
  the source snapshot unchanged, producing overlapping or unsorted instances.
- Regression tests reproduce the mutation, check predecessor/successor conflicts
  and crossed slots in both directions, verify adjacent destinations and retry,
  and round-trip complete expansion/contraction batches through real action undo.
- Rejection leaves this instance untouched. Earlier consequences in a failed
  action still require the broader recovery design; R2 and R3 remain open.

### L1/L3 progress — song teardown clip membership (2026-10-01)

- Song destruction removes each clip's array entry before destroying it. Cleanup
  pops from the end without allocation or array compaction, and rechecks the
  remaining count after audio servicing instead of retaining an earlier index.
- Tests execute the production song destructor and membership lookup with actual
  clip allocation/destruction. They reproduce stale membership, cover both arrays
  across multiple audio-service boundaries, and exercise nested removal and an
  array emptied by audio servicing. Remaining cleanup still runs exactly once.
- This does not pin the song through callbacks or clear every output/history/UI
  reference to retiring clips. L1 and L3 remain open for those ownership contracts.

### L1/L3 progress — bulk backup cleanup ownership (2026-10-09)

- Bulk parameter-backup cleanup removes each registry entry before destroying its
  collections. A local parameter manager takes ownership of every summary slot,
  including expression, malformed tails and aliases, without heap allocation or
  layout-dependent transfer. The entry is then removed without reallocating.
- Cleanup rechecks the registry after audio servicing. Tests compile the actual
  backup implementation and parameter collection cleanup, verify absence during
  collection deallocation, and cover nested cleanup from audio and collection
  callbacks, malformed layouts, exactly-once frees and retained/released storage.
- The implementation now lives beside the other song backup operations. This
  does not pin the song or protect every selective cleanup path; L1/L3 remain open.

### L1/L3 progress — selective backup cleanup (2026-10-09)

- Clip-only and output-specific cleanup now use the same unlink-before-destruction
  helper as bulk cleanup. Output cleanup resolves its key again after every
  destruction; clip-only cleanup searches the live table after audio servicing
  and after each destruction, retaining no pointer or run boundary over callbacks.
- Tests reproduce visible retiring entries, verify generic main/expression
  collection identities survive, cover nested clip-only cleanup from both callback
  boundaries, and remove preceding entries during output cleanup to verify shifted
  entries are not skipped. Allocation accounting checks exactly-once destruction.
- Clip-only cleanup trades grouped deletion for rescanning after callbacks; it
  uses no heap scratch space. Per-clip conversion to generic backups and lookup/
  transfer callbacks still need review. L1/L3 remain open.

### L1/R1 progress — retained backup transfer source (2026-10-09)

- Exact restoration detaches the selected backup into a local owner before
  destination cleanup can yield. Registry edits during cleanup cannot invalidate
  that source or cause restoration to consume a newly published replacement key.
- Preferred/fallback restoration uses the same transfer path, including rejection
  of destinations inside any backup entry. Previously that path could destroy its
  selected source or another entry and return a dangling destination.
- Tests reproduce destination aliasing and exercise exact/fallback selection,
  unrelated-entry aliases, destination cleanup publishing a same-key replacement,
  original collection identity, preserved destination expression and leak-free
  teardown. Existing malformed-backup cleanup remains covered.
- This retains the source collections, not the caller's destination, song, output
  or action. Operation-wide rollback and destination lifetime remain open in L1/R1.

### L1/L3 progress — publish generic conversion before retirement (2026-10-09)

- Deleting a clip's backup now publishes its main collections under the generic
  output key and removes/repositions the old clip key before destroying expression
  or superseded generic collections. Both retiring sets have local ownership, so
  callbacks may remove the new entry without leaving a retained array pointer.
- Slot reuse and capacity-preserving removal keep conversion allocation-free.
  A shared callback-free ownership move also serves backup detachment.
- Tests reproduce the old key remaining visible during destruction, cover first
  entries, sibling entries and existing generic backups, verify publication and
  collection identities during deallocation, and remove the new generic entry
  from a nested cleanup callback without leaks or resurrection.
- Song/output/action lifetime and whole-operation recovery remain open; this
  closes the identified conversion publication window, not L1/L3 as a whole.

### L1/L3 progress — replacement backup publication (2026-10-09)

- Replacing an existing backup now moves its old collections into a local owner,
  installs the incoming collections, then retires the old ownership. No registry
  pointer is used after retirement callbacks begin. Nested replacement/deletion
  therefore remains authoritative instead of being overwritten by the outer call.
- Tests reproduce the unpublished replacement, cover main-only and expression
  transfer, preserve source expression where requested, verify a nested latest
  replacement wins, and delete the entry during retirement with exact allocation
  accounting. Existing-slot replacement succeeds with insertion disabled.
- New-entry allocation still needs its own source/song lifetime contract; this
  change covers existing-entry replacement and does not close L1/L3.

### L3 progress — output-list teardown re-entry (2026-10-09)

- Output-list cleanup rechecks its head after audio servicing. Nested cleanup can
  empty the list before the outer iteration resumes; the old code dereferenced a
  null output at that point. The regression reproduced this under UBSan.
- Tests execute the production cleanup with real output allocation/destruction,
  cover nested cleanup from audio, reference-clearing and destructor callbacks,
  and replace the list during audio servicing. They check unlinking, exactly-once
  destruction/deallocation and completion against the current list.
- The owning song/list pointer itself is not pinned across callbacks. L3 remains
  open for that lifetime contract and other failure-cleanup paths.

### L3 / R1 progress — failed backup insertion retirement (2026-10-09)

- Failed insertion moves all source collections into a local retiring manager
  before running collection cleanup. Cleanup no longer accesses the caller after
  callbacks begin; a callback may destroy it or install new parameters without
  the outer cleanup destroying those new parameters.
- Regression coverage exercises both expression-transfer choices, checks that
  all old ownership and the expression offset are cleared before retirement,
  replaces source parameters during cleanup, and deletes the source during
  cleanup. Original failure semantics still discard old expression parameters
  even when expression transfer was not requested. Collection allocation
  accounting verifies exactly-once cleanup and no leaks.
- The allocation step itself still retains song, source, owner and clip pointers.
  This change contains retirement only; it does not establish their lifetime
  across allocation or close L3/R1.

### L1 / L3 validation — backup ownership sanitizers (2026-10-09)

- Native `SongBackupTests` and `SongBackupTestsNoDiagnostics` now enable AddressSanitizer
  and UndefinedBehaviorSanitizer by default on Clang/GNU. Instrumentation covers
  both the fixtures and the compiled production backup/parameter implementations;
  undefined behavior fails the test rather than merely printing a diagnostic.
- Configure with `-Dsong_backup_test_sanitizers=OFF` only when the host cannot run
  these sanitizers. Other native targets and firmware build flags are unchanged.
- This complements collection allocation accounting, particularly for tests that
  delete the caller during cleanup. It does not prove callback reachability or
  lifetime safety for model objects outside these fixtures.

### G1 progress — Local screensaver routing (2026-10-09)

- Remote input and timer callbacks no longer wake, activate, advance or inhibit
  the physical Local screensaver. Its existing Local-only OLED composition stays
  unchanged; no second animation/canvas allocation is needed.
- Screensaver settings remain shared. Changing them from either panel now wakes
  and dirties the Local display and updates the Local timer, then restores the
  caller's UI owner. Remote timer deadlines and display dirty state are preserved.
- `ScreensaverRoutingTests` compiles the production event methods with real
  `UITimerState` ownership and lightweight display/animation fixtures. Three regressions failed before the fix; all four cases
  pass afterward, including unchanged Local wake/rearm behavior. These are routing
  tests, not animation-pixel or hardware acceptance tests. G1 remains open and
  independent mode remains disabled.
- Validation: all 30 native CTest suites and `./dbt build relwithdebinfo` pass.

### G1 progress — specific recording-source menu (2026-10-09)

- Removed the singleton menu's cached edited-output pointer, source index and
  output count. Entry, encoder input and drawing now resolve the current panel's
  output and its live recording source. Reordering or adding tracks cannot leave
  the menu pointing at an old list position; same-track panels share one value.
- Source changes request a deferred peer shared-value refresh. The menu implements
  that refresh for OLED and seven-segment displays without restarting navigation
  or changing the source during rendering. Unchanged selections avoid rewriting
  monitoring ownership. Missing song/clip/output and non-audio contexts are ignored.
- `SourceMenuTests` includes the actual production menu header with lightweight
  song/output/display fixtures. Three initial regressions fail against the old
  header. Nine cases cover cross-panel editing, live source changes, list changes,
  invalid/empty sources, filtering, extreme encoder offsets, context loss and
  deferred peer redraw. The fixtures do not establish real monitoring-claim or
  clip/output destruction safety; those remain under L1/L3. G1 stays open and
  independent mode remains disabled.
- Validation: all 31 native CTest suites and `./dbt build relwithdebinfo` pass.

### G1 progress — developer SysEx menu drafts and labels (2026-10-09)

- Pending enable codes and option-label buffers now belong to each panel. Opening
  or drawing the menu on the other panel cannot replace the code being offered
  or alter a previously returned option string. The committed setting stays shared.
- Option generation resolves invalidated shared values before formatting the code,
  including selection paths that request options before reading the selected value.
- `RuntimeMenuTests` compiles the real menu with production session/cache state.
  Regressions reproduced overwritten pending codes, overwritten retained labels
  and stale options after a peer commit. Four tests now cover these cases and
  shared enable/disable refresh. Base-menu commit dispatch and hardware/SysEx
  acceptance remain outside this fixture. G1 remains open; independent mode stays
  disabled.
- Validation: all 32 native CTest suites and `./dbt build relwithdebinfo` pass.

### G1 progress — battery menu sampling and redraw routing (2026-10-09)

- Battery menu sampling counters, prior readings and charging estimates now belong
  to the active panel. Two timers cannot accelerate the four-tick measurement
  window, and opening one menu cannot reset the other's window or charging result.
  Re-entry clears that panel's stale estimate; both still read the host batteryMV.
- OLED updates now request a redraw under the current UI owner instead of calling
  the seven-segment-only scrolling-text path. Timer deadlines remain session-owned.
- Six battery cases in `RuntimeMenuTests` compile the production menu header with
  real timer/session storage. Four initial regressions reproduced cross-panel
  sampling/re-entry interference and the missing OLED redraw. Coverage also checks
  stale-status reset, stable voltage and percentage/full-status bounds. ADC input,
  real OLED pixels and two-device acceptance remain untested here. G1 stays open;
  independent mode remains disabled.
- Validation: all 32 native CTest suites (including ten runtime/battery menu cases)
  and `./dbt build relwithdebinfo` pass.

### G1 progress — negotiated display format lifetime (2026-10-09)

- Sessions now retain their negotiated OLED/seven-segment format. Liveness checks
  reject a subsequent format change before queued input dispatch, incoming packet
  processing or transport output. Transmission callbacks retain the sequence of an
  already sent packet but cannot send following packets after a format change.
- Teardown still permits Stop, releases the session and resumes client timers.
  A fresh request can negotiate the new format. This contains transport after a
  display change; it does not establish display-object replacement lifetime safety.
- Six new production-runtime cases reproduce the previous failures and cover both
  host formats, reconnection, Waiting/Client states, queued and received input, and
  changes during send. All 241 mirror runtime cases pass. G1 remains open and
  independent mode remains disabled.
- Validation: all 32 native CTest suites and `./dbt build relwithdebinfo` pass.

### G1 / L1 progress — physical display-switch ownership (2026-10-09)

- Display emulation changes now construct, retire, notify and render under Local
  UI ownership, even when requested from Remote settings. The caller's ownership
  is restored on return. The negotiated-format guard ends incompatible transport.
- After display-change notification, seven-segment focus resolves the current UI
  again rather than retaining the previous screen through its callback. A closed
  UI is not focused; a replacement receives focus.
- `DisplaySwapTests` compiles the production function with real session scopes.
  Three regressions reproduced wrong-owner switching and old-UI focus after
  replacement/removal. Four cases pass, including both formats, Local behavior
  and absent navigation. Real display allocation/destructor callback lifetime is
  not established by these fixtures. G1/L1 remain open; independent mode is disabled.
- Validation: all 33 native CTest suites and `./dbt build relwithdebinfo` pass.

### G1 progress — startup display compatibility across cleanup (2026-10-09)

- Client startup now retains discovery's display format in its continuation check.
  Cleanup callbacks that change the local format abort before later cleanup,
  timer suspension and Request emission instead of negotiating from stale evidence.
- Two production-runtime regressions failed before the fix and now pass: display
  changes during menu exit and audition cleanup. They verify no note-stop, no
  takeover/request, cleared discovery state and a successful subsequent compatible
  retry. All 243 mirror runtime cases pass. G1 remains open; independent mode is
  still disabled.
- Validation: all 33 native CTest suites and `./dbt build relwithdebinfo` pass.

### G1 progress — shared launch-style context menus (2026-10-09)

- Launch-style encoder edits now resolve the clip's live value before applying the
  turn, even before a pending peer refresh is serviced. Actual changes request a
  deferred peer refresh; OLED and seven-segment menus update without resetting UI
  mode or reopening navigation. Different-clip panels keep their own selection.
- Six `ContextMenuTests` cases compile the real launch-style implementation and
  base encoder/draw methods with real session/refresh storage. Three regressions
  reproduced stale edits and missing peer refresh. Other cases cover unchanged
  OLED boundaries and absent targets. Fixtures do not establish retained clip
  lifetime through callbacks; G1/L1 remain open and independent mode is disabled.
- Validation: all 34 native CTest suites and `./dbt build relwithdebinfo` pass.

### G1 progress — shared audio-input context menus (2026-10-09)

- Audio-input context menus resolve the live channel before encoder edits and OLED
  rendering. They now consume deferred shared-model refreshes, including Track
  source/name changes when the channel itself is unchanged. Refresh preserves the
  viewport when selection is unchanged and does not reset UI mode.
- Encoder and pad changes notify the peer only when channel/source changes. Existing
  monitoring-claim calls remain intact. Source lookup checks active-song membership
  before inspecting a retained source and handles an absent song.
- Eight new `ContextMenuTests` cases include the production selector and base input
  methods. Three regressions reproduced stale edits and absent peer notifications.
  Additional coverage includes leaving Track, source repair, unrelated outputs,
  mode locks, missing targets and rejected inputs. Monitoring claims and retained
  output lifetime remain outside these fixtures. G1/L1 remain open; independent
  mode stays disabled.
- Validation: all 34 native CTest suites and `./dbt build relwithdebinfo` pass.

### G1 / L1 progress — Remote navigation loss inside timer dispatch (2026-10-09)

- Timer dispatch now checks Remote navigation before each timer in the pass. If a
  callback closes it, later timers stay pending instead of running against a null
  current UI before mirror service can detect the loss. Local hardware servicing
  and the client OLED handshake retain their existing behavior.
- Six `TimerDispatchTests` cases compile the production manager header, timer state
  and dispatch/scheduling bodies. Two initial regressions demonstrated servicing
  without navigation and continued dispatch after closure; further tests cover
  the formerly unsafe exit/graphics calls, retained deadlines, resumed service,
  replacement navigation and hardware exceptions. Internal callback lifetime and
  whole-session timer cancellation remain separate concerns. G1/L1 stay open and
  independent mode remains disabled.
- Validation: all 35 native CTest suites and `./dbt build relwithdebinfo` pass.

### G1 / R4 progress — timer retries after UI replacement (2026-10-09)

- UI-specific and back/exit timer callbacks only rearm their implicit retry while
  the original UI remains current. Closing or replacing that UI no longer schedules
  its expired event for a different screen. An explicit timer scheduled by the new
  UI remains active with its own deadline; this change does not cancel that timer.
- Three added `TimerDispatchTests` cases cover both timer types, both panels,
  removal/replacement, unchanged-UI retries and explicit replacement deadlines.
  The departed-UI case failed before the fix. This is callback-result containment,
  not a lifetime pin or protection against same-address replacement. G1/R4/L1 remain
  open and independent mode stays disabled.
- Validation: all 35 native CTest suites and `./dbt build relwithdebinfo` pass.

### G1 / L1 progress — Local navigation loss during timer dispatch (2026-10-09)

- UI-specific, exit, root-note flash and graphics timers check for a current UI
  before dereferencing it. Local input servicing continues without navigation;
  periodic graphics stays scheduled so it can resume after navigation returns.
- Two additional production-dispatch regression cases cover absent Local UI and
  closure during a callback, hardware servicing, consumed one-shot events and
  resumed graphics. This protects dispatch boundaries, not retained objects inside
  callbacks. G1/L1 remain open and independent mode remains disabled.
- Validation: all 35 native CTest suites and `./dbt build relwithdebinfo` pass.

### G1 progress — deferred timer wraparound regression coverage (2026-10-09)

- Two additional production-dispatch tests verify distinct Local/Remote deadlines
  across sample-clock wrap, exact-deadline behavior and a Remote timer deferred
  without navigation then serviced after wrap. Servicing Remote leaves the Local
  deadline intact. All 35 native CTest suites pass; this piece changes tests only.
- The fixtures service each empty bank at their simulated start time. A separate
  remaining timing audit is needed for banks left unserviced for more than half
  the 32-bit clock range: a stale cached next-event deadline can then compare as
  future. These tests do not establish safety for that interval. G1 remains open;
  independent mode stays disabled.

### G1 progress — scheduling into an idle timer bank (2026-10-09)

- Scheduling now rebuilds an expired cached next-event deadline from active
  timers. This fixes a new timer being hidden by an idle bank's stale cache across
  clock wrap, and removes obsolete cached deadlines when a due timer is moved.
  The normal future-deadline scheduling path remains constant-time.
- A production-dispatch regression failed before the fix and now covers first
  scheduling into either idle panel bank across wrap. A storage regression checks
  that genuinely overdue timers remain first and rescheduling them reveals the
  next active timer. All 35 native suites and the RelWithDebInfo build pass.
- This closes the newly scheduled idle-bank case from the preceding entry, not
  arbitrary active timers left overdue for more than half the clock range. Those
  cannot be classified by signed 32-bit subtraction alone. G1 remains open and
  independent mode stays disabled.

### G1 / R4 progress — timer callback owner-change containment (2026-10-09)

- Timer dispatch stops when a callback returns under a different panel owner.
  Retry results require both the original owner and UI; graphics callbacks cannot
  overwrite the other panel's periodic deadline. End-of-pass deadline recomputation
  explicitly uses the initiating bank, and the existing scope restores the caller.
- Three fault-injection tests reproduced continued dispatch, a retry accepted with
  the same UI pointer under another owner, and a peer graphics deadline overwritten
  by the periodic rearm. Both initiating owners are covered. This is containment
  of owner imbalance, not evidence of a normal scoped callback leaking ownership.
- Validation: all 35 native suites and `./dbt build relwithdebinfo` pass. Individual
  callback internals and end-to-end routing remain open; independent mode stays
  disabled.

### G1 progress — client takeover inside a timer pass (2026-10-09)

- Dispatch rechecks client mode before each timer, so takeover during a callback
  suspends the remaining timers immediately at the next callback boundary. Their
  deadlines remain pending; subsequent client passes retain the OLED handshake
  exception and ordinary service resumes after leaving client mode.
- A production-dispatch regression failed before the fix, with battery and OLED
  callbacks still running after the injected input callback entered client mode.
  It now checks pending deadlines, suppressed graphics/console, handshake service
  and resumption. This fixture injects the transition rather than USB negotiation.
- Validation: all 35 native suites and `./dbt build relwithdebinfo` pass. G1 remains
  open and independent mode stays disabled.

### G1 progress — shared sticky Shift setting across panels (2026-10-09)

- Disabling sticky Shift now clears the latch in both panel states and restores
  the initiating owner. Clearing a latch preserves the panel's physically held
  Shift button, so a setting change on the peer cannot synthesize its release.
- Two added native cases compile the production setting-write body, button-reset
  bodies and button-coordinate implementation. They cover both initiating panels,
  peer latch clearing, held Shift preservation, notifications and the unchanged
  enable/LED-setting behavior. The peer-latch test failed before the fix. Settings
  storage is a fixture; physical LED/input acceptance remains in G2.
- Validation: all 35 native suites and `./dbt build relwithdebinfo` pass. G1 remains
  open; independent mode stays disabled.

### G1 progress — runtime settings changed outside their menu (2026-10-09)

- Runtime-setting menus key their presentation cache to the live stored value.
  Dependent changes (for example sticky Shift enabling the Shift LED setting),
  loaders and another menu instance therefore reload on the next value access or
  requested shared refresh, even without a commit on that particular menu object.
  Unrelated settings do not invalidate a pending selection.
- Four tests compile the production Setting header/implementation with the real
  shared-value cache and a fixture Selection base. Three failed before the fix;
  coverage includes both panel caches, distinct menu instances and stored values
  that differ from option indices. Commits bind the cache to the new model value;
  the round-trip test also caught a stale selection when another menu restored the
  earlier setting. This does not itself request a repaint for
  arbitrary external writes or establish end-to-end rendering.
- Validation: all 35 native suites and `./dbt build relwithdebinfo` pass. G1 remains
  open and independent mode stays disabled.

### G1 progress — Remote Shift LED feedback (2026-10-09)

- Physical input and Remote UI service now share an owner-aware Shift feedback
  helper. Remote service flushes its change flag into its own indicator state;
  initial Remote snapshot preparation does the same before rendering. The Local
  pending flag remains available for physical input service. Storage/audio locks
  defer Remote feedback, and a failed output enqueue stops subsequent rendering.
- Two unit cases compile the production feedback and modifier-state bodies with
  a fixture LED sink, checking panel isolation, change consumption, repeated calls
  and the disabled setting. Four mirror runtime cases exercise service/snapshot
  placement, owner restoration, lock deferral and real queue-overflow containment.
  Missing service/snapshot calls and continued rendering after enqueue failure
  were reproduced before their fixes. Physical LED acceptance remains in G2.
- Validation: all 35 native suites and `./dbt build relwithdebinfo` pass. G1 remains
  open and independent mode stays disabled.

### G1 progress — Shift LED setting transitions without input (2026-10-09)

- Each panel now remembers whether Shift LED feedback was enabled at its last
  service. A shared setting transition updates that panel's LED even without a
  new button event: enable reflects the current hold and disable turns it off.
  Repeated service does not emit redundant updates or alter held-button state.
- A new production-helper test reproduced the missing enable update and covers
  both panel banks, disable and repeated service. Startup-reset coverage also
  checks that Remote feedback state resets without changing Local feedback state.
- Validation: all 35 native suites and `./dbt build relwithdebinfo` pass. This is
  service-time synchronization; physical device acceptance remains in G2. G1 stays
  open and independent mode remains disabled.

### G1 progress — developer SysEx menus after settings replacement (2026-10-09)

- Developer SysEx menu caches now track the live stored code, including changes
  made by settings reset/reload rather than a menu commit. Both panels reload their
  enabled state and labels on access; a reset produces fresh per-panel drafts
  without enabling the setting. A commit records its new cache revision so a
  subsequent reset cannot appear unchanged by matching the pre-commit revision.
- Three new RuntimeMenuTests failed before the fix. They cover reset with both
  menus open, replacement of one nonzero code by another, and reset following the
  same menu's commit. Existing draft isolation and retained-label tests still pass.
- Validation: all 35 native suites and `./dbt build relwithdebinfo` pass. Settings
  replacement is injected into the fixture store; filesystem reset and end-to-end
  repaint remain outside these cases. G1 stays open; independent mode is disabled.

### G1 progress — panel state after community-settings reset (2026-10-09)

- After reset finishes reloading settings, both panels receive a deferred shared
  menu refresh. If the resulting sticky Shift setting is Off, both latches clear
  while physical holds remain intact. A reloaded On value preserves the latches.
  The initiating panel owner is restored after each update.
- Two production-reset-body tests reproduced retained latches and absent refresh
  requests. They cover both initiating owners, physical hold preservation,
  coalesced refresh, and using the reloaded setting rather than initial defaults.
  The real button-clear body is used; filesystem and settings loading are fixtures.
- Validation: all 35 native suites and `./dbt build relwithdebinfo` pass. This covers
  post-reset UI effects, not reset filesystem failure recovery or all setting side
  effects. G1 remains open and independent mode stays disabled.

### G1 / R4 progress — audio-source pad edits during storage (2026-10-09)

- Audio-input selection now defers pad presses while the SD routine is locked,
  before resolving a target or changing the recording source. Releases retain
  their normal handling. Retried presses resolve the current pad target after
  unlock rather than retaining the target from the blocked attempt.
- A production-selector regression failed before the fix and now covers both
  panel owners, unchanged channel/source/selection, absent redraw and peer refresh,
  release handling and a changed target on retry. Monitoring claims and storage
  are fixtures; this does not pin the menu's retained output pointer.
- Validation: all 35 native suites and `./dbt build relwithdebinfo` pass. G1/R4/L1
  remain open and independent mode stays disabled.

### G1 / L1 progress — departed audio-input menu targets (2026-10-09)

- Audio-input menu entry points now confirm the retained target belongs to the
  current song and has audio-output type before reading or editing it. Missing
  songs and departed targets are unavailable; reattachment permits normal use
  again. Greyout also rejects a missing root UI without writing output masks.
- Three added production-selector cases cover departure/reattachment on both
  panels, edits and rendering, missing songs, wrong output types and absent roots.
  Departure remained available before the fix. Fixtures now place their edited
  audio outputs in the song list, matching production membership requirements.
- Validation: all 35 native suites and `./dbt build relwithdebinfo` pass. This is
  entry-point membership validation, not a pin across callbacks or a generation
  check against same-address replacement. G1/L1 remain open; independent mode is
  disabled.

### G1 / L1 progress — departed launch-style menu targets (2026-10-09)

- Launch-style menu setup, edits and shared refresh require the retained clip to
  belong to the current song. The existing song membership check includes session
  and arrangement-only clips; missing or replaced songs reject the old target.
- Two added context-menu tests cover departure and reattachment on both panels,
  preserved UI mode when entry is rejected, and missing/replaced songs. The native
  fixture now compiles the production song-membership body and registers its clips
  in the appropriate lists, including different-clip menu coverage.
- Validation: all 35 native suites and `./dbt build relwithdebinfo` pass. This checks
  membership at entry, not lifetime across display callbacks or same-address reuse.
  G1/L1 remain open and independent mode stays disabled.

### G1 / L1 / R4 progress — launch-style display callback containment (2026-10-09)

- Encoder edits recheck the initiating song, clip, panel owner and live membership
  after shared refresh and base seven-segment feedback, before accessing the model
  again. Departure or retargeting cancels the pending edit.
- Three regression cases failed before the fix: removal during either redraw,
  retargeting to another live clip, and song/owner replacement during feedback.
  The display callback is injected; this is not an object pin or ABA protection.
- All 35 native suites and `./dbt build relwithdebinfo` pass. G1/L1/R4 remain open;
  independent mode remains disabled.

### G1 / L1 / R4 progress — audio-input encoder callback containment (2026-10-09)

- Audio-input encoder edits revalidate the initiating song, output, panel owner
  and live output membership after seven-segment feedback. Invalidated attempts
  stop before changing channel, recording source, global default or peer refresh.
- Three production-selector tests failed before the fix and now cover removal on
  both panels, retargeting to another live output, and song/owner replacement.
  Monitoring and callback delivery are fixtures; no object pin or generation
  protection is implied.
- All 35 native suites and `./dbt build relwithdebinfo` pass. G1/L1/R4 remain open
  and independent mode remains disabled.

### G1 / R4 progress — preserve model changes during encoder feedback (2026-10-09)

- Launch-style and audio-input encoder edits also compare their captured model
  values after feedback, once context and membership checks succeed. A changed
  launch style, input channel or recording source cancels the pending write,
  preserving the newer value without publishing a spurious peer refresh.
- Two regression cases failed before the fix. Audio cases independently exercise
  channel and source changes and verify no monitoring assignment or default change.
  These comparisons do not detect value ABA or substitute for object generations.
- All 35 native suites and `./dbt build relwithdebinfo` pass. G1/R4 and the broader
  lifetime/recovery blockers remain open; independent mode stays disabled.

### G1 / L1 progress — recording-source submenu output membership (2026-10-09)

- The specific recording-source submenu confirms its edited output belongs to the
  current song before reading its type, repairing its source, editing or rendering.
  Departed outputs are unavailable and reattachment permits editing again.
- Two production-header regressions failed before the fix, covering both owners
  and preventing automatic source repair/peer refresh on a departed output. The
  list-growth fixture now keeps the edited output registered in the song.
- All 35 native suites and `./dbt build relwithdebinfo` pass. This validates output
  membership, not the retained current clip or same-address reuse. G1/L1 remain
  open and independent mode remains disabled.

### G1 / L1 progress — recording-source submenu clip membership (2026-10-09)

- The submenu validates the selected clip against the current song before calling
  `getCurrentOutput`, which dereferences that clip. Session and arrangement-only
  clips are accepted; departed clips reject entry, edits, relevance and rendering.
- A regression failed before the fix and now covers both panel owners, no output
  lookup on rejection, and arrangement-only reattachment. The test compiles the
  production song-membership method against fixture clip lists.
- All 35 native suites and `./dbt build relwithdebinfo` pass. These are entry-point
  checks, not callback-spanning pins or generation checks. G1/L1 remain open and
  independent mode stays disabled.

### G1 / L1 progress — audio-source context-menu entry (2026-10-09)

- Audio-source entry validates song/clip membership before output lookup and output
  membership/type before casting. It opens the initiating panel's selector only
  after successful setup. Invalid context leaves navigation unchanged.
- Four production-entry tests cover valid separate panel selectors, departed
  outputs/clips, missing song/clip and non-audio outputs. Two cases reproduced
  failures before the fix. The real input selector and clip-membership body run;
  output lookup and UI opening are fixtures.
- All 35 native suites and `./dbt build relwithdebinfo` pass. Callback lifetime and
  same-address replacement remain outside these entry guards; G1/L1 remain open
  and independent mode stays disabled.

### G1 / L1 progress — Clip Settings retained targets (2026-10-09)

- Clip Settings validates current-song clip membership before setup, option reads,
  encoder input and action dispatch. Departed targets cannot trigger conversion,
  launch-style entry or rename. Launch-style opening respects failed setup.
- Five new cases compile the production Clip Settings implementation: departed
  targets on both owners, rejected setup/options/encoder, valid owner routing,
  missing song/null clip and arrangement-only audio options. Two cases failed
  before the fix. Conversion and rename are dispatch fixtures, not full model
  conversion or rename implementations.
- All 35 native suites and `./dbt build relwithdebinfo` pass. Callback lifetime,
  reclamation and same-address reuse remain unproven; G1/L1 stay open and
  independent mode remains disabled.

### G1 / R4 progress — deferred new-clip pad input (2026-10-09)

- New Clip Type preserves its menu when Session View defers pad input outside the
  SD routine. The retry can return to the same menu instead of a prematurely
  closed UI; successful handling retains the existing transition and closure.
- A regression failed before the fix and now exercises deferral and successful
  retry on both owners. It compiles the production input methods; Session View
  result delivery and UI closure are fixtures, not hardware replay validation.
- All 35 native suites and `./dbt build relwithdebinfo` pass. G1/R4 remain open and
  independent mode remains disabled.

### G1 progress — new-clip button routing (2026-10-09)

- New Clip Type ignores button releases instead of treating them as selections.
  Unhandled or deferred button presses return their dispatch result without
  closing the menu or scheduling its exit transition. Handled presses retain
  existing dispatch and closure behavior.
- Three added extracted-production tests cover release handling on both owners,
  unhandled/deferred results and handled Remote dispatch without Local closure.
  Two cases failed before the fix. Session View results and UI transitions are
  fixtures; this does not establish full hardware or session-creation coverage.
- All 35 native suites and `./dbt build relwithdebinfo` pass. G1 remains open and
  independent mode remains disabled.

### G1 progress — invalid new-clip selections (2026-10-09)

- New Clip Type rejects an out-of-range selection before dispatching a creation
  button. Select-encoder presses leave the menu open when selection is rejected,
  rather than dispatching an uninitialized button value and closing it.
- One regression failed before the fix and covers negative, upper-bound and
  extreme indices. Another verifies all five valid button mappings. Tests compile
  the production input bodies against the existing dispatch fixtures.
- All 35 native suites and `./dbt build relwithdebinfo` pass. G1 remains open and
  independent mode remains disabled.

### G1 / R4 progress — new-clip closure after delegated input (2026-10-09)

- New Clip Type rechecks the initiating panel owner and current UI after delegating
  pad/button input. If the owner changes or the menu is no longer current, it does
  not schedule a transition or call close on the old menu.
- Three regressions failed before the fix: UI changes during pad input, UI changes
  during Select and instrument-button dispatch, and owner changes during either
  input path. Existing successful-input tests continue to verify normal closure.
  Callbacks and closure are fixtures; no real UI-stack lifetime or generation
  protection is implied, and these guards do not restore a changed owner.
- All 35 native suites and `./dbt build relwithdebinfo` pass. G1/R4 remain open and
  independent mode remains disabled.

### G1 / L1 progress — retained clip rename targets (2026-10-09)

- Clip rename validates current-song clip and output membership before allowing
  entry, reading the name or checking duplicates/writing a name. Its retained clip
  starts null. Departed targets are unavailable; arrangement-only clips remain valid.
- Four extracted-production cases cover departure on both owners, departed output
  lookup rejection, valid/duplicate names, missing context and arrangement-only
  reattachment. Two cases failed before the fix. Clip membership uses the real
  song method; names, duplicate lookup and display are fixtures.
- All 35 native suites and `./dbt build relwithdebinfo` pass. These entry checks
  do not establish allocator-callback lifetime or allocation-failure recovery.
  G1/L1 remain open and independent mode remains disabled.

### G1 / R4 progress — clip rename allocation failure (2026-10-09)

- Clip rename stages the replacement before committing through String's
  non-allocating shared-storage setter. Failed allocation reports an error and
  preserves the original name. The saved old name shares its existing storage;
  only the replacement requires a new string allocation.
- After allocation, rename rechecks song, target, output, owner, membership and
  prior name before duplicate lookup and commit. Newer names and context changes
  cancel the edit; duplicate checks use the current output after allocation.
- Four added fixture-backed cases cover allocation failure/retry, clip departure,
  newer names, and changed song/target/output/owner or newly introduced duplicates.
  Three cases failed before the fix. The fixture models the destructive failure
  of String::set; it does not run the firmware allocator or establish lifetime
  pins, generation checks, or UI-stack recovery after arbitrary callbacks.
- All 35 native suites and `./dbt build relwithdebinfo` pass. G1/R4/L1 remain open
  and independent mode remains disabled.

### G1 / L1 progress — retained track rename targets (2026-10-09)

- Track rename checks current-song output membership before entry, name reads,
  duplicate lookup and writes. Its retained output starts null. Missing and
  departed targets are unavailable; reattachment permits editing again.
- Three extracted-production cases cover both panel owners, valid/duplicate/self
  names, missing context and reattachment. The departed-target case failed before
  the fix. Output lists, name storage and duplicate lookup are fixtures.
- All 35 native suites and `./dbt build relwithdebinfo` pass. Allocation recovery,
  callback lifetime and same-address replacement are not established by these
  entry guards. G1/L1 remain open and independent mode stays disabled.

### G1 / R4 progress — track rename allocation recovery (2026-10-09)

- Track rename stages its replacement name before a non-allocating commit. It
  reports allocation failure without discarding the original name and revalidates
  song, output, owner, membership and prior name after allocation. Duplicate lookup
  runs against the current song after those checks.
- Three added cases failed before the fix: failure/retry, a newer name, and callback
  changes to song/target/membership/owner or duplicates. Name allocation and
  callbacks are fixtures; actual allocator pressure, object pins and same-address
  replacement are not established by these tests.
- All 35 native suites and `./dbt build relwithdebinfo` pass. G1/R4/L1 remain open
  and independent mode stays disabled.

### G1 / R4 progress — rename errors after context changes (2026-10-09)

- Clip and track rename now validate context after allocation before displaying
  allocation errors. If the target or panel changed during allocation, the stale
  operation cancels without showing its error in the new context.
- Two injected-callback regressions failed before the fix, covering target and
  owner changes for both rename paths. Existing valid-context failure/retry tests
  continue to require error feedback and preservation of the original name.
- All 35 native suites and `./dbt build relwithdebinfo` pass. These tests use name
  and display fixtures; allocator lifetime, UI-stack recovery and same-address
  replacement remain outside this change. G1/R4 remain open; independent mode is
  disabled.

### G1 / R4 progress — rename edit-buffer initialization failure (2026-10-09)

- The shared rename dialog rejects opening when copying the existing name into
  its edit buffer fails, reports the allocation error and skips text/key drawing.
  It no longer accepts a failed copy as a successfully initialized empty editor.
- Two extracted-production cases cover failure/retry on both owners and rejected
  base opening/unavailable targets. The allocation case failed before the fix.
  Text allocation, Qwerty opening and rendering are fixtures.
- All 35 native suites and `./dbt build relwithdebinfo` pass. Callback lifetime and
  full UI-stack recovery remain outside this change; G1/R4 remain open and
  independent mode stays disabled.

### G1 / R4 progress — rename submission closure routing (2026-10-09)

- After a successful rename callback, the shared dialog verifies that its panel
  owner and current UI still match before calling exitUI. Changed context no
  longer closes a replacement UI or exits on the other panel.
- Four extracted-production cases cover changed UI on both owners, changed owner,
  success/failure closure on Remote without Local closure, and prohibited empty
  input. The two callback-change cases failed before the fix. Commit callbacks and
  UI closure are fixtures; actual UI-stack recovery and lifetime are not proved.
- All 35 native suites and `./dbt build relwithdebinfo` pass. G1/R4 remain open and
  independent mode stays disabled.

### G1 / R4 progress — missing UI targets during opening (2026-10-09)

- openUI rejects a null target returned by getUI before inserting it into the
  hierarchy, and rejected opening only restores focus when a previous UI exists.
- Five extracted-production tests cover redirected-null targets, rejected first
  UI, normal rejection on both panels, Remote opening and null/full-stack input.
  Navigation, callbacks and rendering are fixtures; this is not lifetime proof.
- All 35 native suites and `./dbt build relwithdebinfo` pass. Callback-driven stack
  changes still require protection. G1/R4 remain open; independent mode is disabled.

### G1 / R4 progress — UI-opening callback containment (2026-10-09)

- openUI captures the initiating owner and expected hierarchy/depth, then validates
  them after resolution, greyout, opening and rejection-focus callbacks. A changed
  stack cancels further rollback/focus/redraw work, preserving newer navigation.
  The caller's owner is restored by scope. The hierarchy snapshot is fixed-size
  stack storage and requires no allocation.
- Six added extracted-production cases cover nested opening, changed owner,
  resolution/greyout replacement, rejection-greyout replacement and focus callback
  owner changes. The first four failed before the fix. Existing tests retain
  normal opening and rollback behavior. UI methods and navigation are fixtures.
- All 35 native suites and `./dbt build relwithdebinfo` pass. Cancellation does not
  undo callback model changes, pin UI objects, detect same-address/stack ABA, or
  guarantee removal of a partially opened UI after context invalidation. G1/R4/L1
  remain open; independent mode stays disabled.

### G1 / R4 progress — rename initialization callback boundaries (2026-10-09)

- Rename initialization rechecks the owner, active UI and target availability after
  Qwerty opening, text allocation, text display and key drawing. Invalidated work
  stops before subsequent rendering or allocation-error feedback in a new context.
- Two extracted-production regressions failed before the fix. A phase matrix tests
  changed owner/UI/availability at four boundaries; a failed-copy case verifies
  no error is reported on the peer. Callbacks and buffers remain fixtures.
- All 35 native suites and `./dbt build relwithdebinfo` pass. These checks do not
  pin the name source during copying or roll back callback side effects. G1/R4/L1
  remain open and independent mode stays disabled.

### G1 / R4 progress — stale UI close requests (2026-10-09)

- closeUI validates depth, active hierarchy entries and target membership before
  rendering or popping. Missing targets, root-close requests and incomplete stacks
  leave navigation untouched instead of indexing below the root or dereferencing
  a missing UI. Valid closure still removes the target and its descendants.
- Four extracted-production tests cover nested Remote closure, absent/null/root
  targets, invalid depths and incomplete stacks. Callback/rendering dependencies
  are fixtures; invalid cases were added with the guards rather than executed
  against the unsafe pre-fix indexing paths.
- All 35 native suites and `./dbt build relwithdebinfo` pass. Callback lifetime and
  changes during closure remain separate work. G1/R4 remain open; independent
  mode stays disabled.

### G1 / R4 progress — UI closure callback containment (2026-10-10)

- closeUI checks its expected stack and initiating owner between rendering queries,
  greyout, focus restoration, pad rendering and main/sidebar transmission. Changed
  context stops further work; owner scope restores the initiating panel on return.
  Normal closure still redraws and sends both pad regions on that panel.
- Seven added extracted-production cases cover query/greyout replacement, owner
  changes during focus/transmission, main/sidebar render invalidation and normal
  sends. Five cases failed before the fix. Rendering and transport are fixtures.
- All 35 native suites and `./dbt build relwithdebinfo` pass. Stack snapshots use
  fixed local storage; they neither pin UI objects nor roll back completed drawing
  or pops, and they do not detect stack ABA. G1/R4/L1 remain open; independent mode
  stays disabled.

### G1 / R4 progress — UI replacement and sideways navigation (2026-10-10)

- UI replacement validates target, depth, level and active stack entries before
  mutation. Both replacement and sideways navigation check expected hierarchy and
  owner across resolution, greyout, opening and failure-focus callbacks. Newer
  navigation is preserved instead of being rolled back or redrawn by stale work.
- Nine added extracted-production cases cover valid rejection/success, nested
  opening, changed owner, resolution/greyout replacement, invalid inputs and
  malformed stacks. Four callback cases failed initially; a further regression
  caught sideways redraw after malformed-stack rejection during implementation.
- All 35 native suites and `./dbt build relwithdebinfo` pass. Navigation/callbacks
  are fixtures; fixed local snapshots are not object pins, ABA detection or full
  recovery of partially replaced UIs. G1/R4/L1 remain open; independent mode stays
  disabled.

### G1 / R4 progress — root navigation callback containment (2026-10-10)

- Normal and low-level root installation reject missing targets and invalid depth,
  and preserve navigation changed during target resolution. Normal root changes
  also stop after greyout/opening invalidation; both paths restore caller ownership.
  The arrangement-row timer exception and low-level no-open/no-render behavior remain.
- Seven extracted-production cases cover resolution/greyout replacement, owner
  changes, valid root changes, low-level installation and invalid inputs. The native
  fixture supplies the existing arrangement-row mode constant; navigation, UI and
  display dependencies are mocked. No pre-fix runtime failures are claimed here.
- All 35 native suites and `./dbt build relwithdebinfo` pass. Root opened() rejection
  still has no rollback contract; object lifetime, stack ABA and completed callback
  side effects remain unresolved. G1/R4/L1 remain open; independent mode is disabled.

### G1 / L1 progress — drum rename target membership (2026-10-10)

- Drum rename validates the selected clip before output lookup, the output before
  treating it as a kit, and the selected drum against that kit's live list. Missing
  selection now makes rename unavailable instead of freezing. Arrangement-only
  clips and reattached drums remain usable; each panel uses its own selected drum.
- Five extracted-production cases cover clip/drum departure, independent selections,
  duplicates, missing/wrong-type context and reattachment. Two cases failed before
  the fix. Membership uses the production song method against fixture lists.
- All 35 native suites and `./dbt build relwithdebinfo` pass. std::string name writes
  still require allocation/lifetime review; these entry guards do not establish
  callback-spanning drum retention. G1/L1/R4 remain open; independent mode is disabled.

### R4 / G1 progress — drum rename allocation errors (2026-10-10)

- Drum rename catches the firmware allocator's BAD_ALLOC exception, keeps rename
  open and reports insufficient memory only while the same song/panel/drum remains
  selected. Other exception kinds propagate. std::string's failure guarantee
  preserves the old name without allocating a second staging string.
- Four fixture-backed cases cover failure/retry, changed owner, target loss and
  propagation of a non-allocation exception. Fault injection exposed an uncaught
  allocation exception before the fix; the tests now capture unexpected exceptions
  explicitly. Actual firmware allocator pressure remains untested.
- All 35 native suites and `./dbt build relwithdebinfo` pass. Successful allocating
  writes still need callback-spanning lifetime protection; this error handling
  does not pin drums or roll back successful writes after context changes.
  G1/R4/L1 remain open; independent mode stays disabled.

### G1 / L1 progress — MIDI CC rename target validation (2026-10-10)

- MIDI CC rename validates current-song clip/output membership, MIDI output type
  and the supported CC range on entry, reads and writes. Missing context and the
  excluded modulation-wheel CC are rejected without writing or marking the
  instrument edited. Each panel resolves its own selected CC.
- Four extracted-production cases cover invalid CCs, departure, per-panel editing,
  missing/wrong-type context and arrangement-only reattachment. Native fixtures use
  the production membership method; label storage and instrument edits are mocked.
- All 35 native suites and `./dbt build relwithdebinfo` pass. Allocation failures
  and callback-spanning target lifetime remain separate work. G1/L1/R4 remain open;
  independent mode stays disabled.

### R4 progress — atomic MIDI CC label insertion (2026-10-10)

- New CC labels are constructed during map insertion. Failed string allocation
  no longer leaves an empty entry; existing labels retain their failure guarantee
  and can reuse capacity.
- Four extracted-production tests use real STL containers and fault-injecting
  allocators to cover string/node failure, retry and CC boundaries. The empty-entry
  regression failed before the fix. Firmware allocator pressure is not simulated.
- All 35 native suites and `./dbt build relwithdebinfo` pass. UI error recovery and
  callback-spanning lifetime remain separate; independent mode stays disabled.

### G1 / R4 progress — MIDI CC rename allocation recovery (2026-10-10)

- MIDI rename catches allocation failure without marking the instrument edited,
  preserves the original label and allows retry. Unrelated exceptions propagate.
  Error reporting requires the original owner, song, clip, instrument and CC.
- Three extracted-production fixture tests cover retry, six context changes during
  failure and unrelated exception propagation. All 35 native suites and
  `./dbt build relwithdebinfo` pass.
- Successful allocating writes still require object-lifetime protection. These
  checks contain stale error reporting; they do not pin objects during allocation.
  G1/L1/R4 remain open and independent mode stays disabled.

### G1 / R4 progress — drum rename error clip ownership (2026-10-10)

- Drum rename allocation errors now also require the initiating clip. Switching
  clips while retaining the same kit/drum no longer reports the old operation's
  error into the new clip context.
- An extracted-production regression failed before the guard and passes afterward.
  All 35 native suites and `./dbt build relwithdebinfo` pass. This does not add
  allocation-spanning object retention; G1/L1/R4 remain open and independent mode
  stays disabled.

### G1 progress — redraw visibility callback containment (2026-10-10)

- Redraw requests validate the active stack and stop after visibility callbacks
  change owner or hierarchy. The initiating owner is restored on return.
- Five extracted-production tests cover owner changes, hierarchy changes, sidebar
  callbacks, invalid stacks and normal region occlusion. Two callback regressions
  failed before the fix. All 35 native suites and `./dbt build relwithdebinfo` pass.
- This contains requests at callback boundaries; it does not pin UI objects or
  detect transient stack changes restored before return. G1/L1 remain open and
  independent mode remains disabled.

### G1 progress — grid rendering callback containment (2026-10-10)

- Grid rendering validates the stack, restores the initiating owner and stops after
  render/send callbacks change owner or hierarchy. Original rows are requeued on
  that owner without overwriting callback-generated requests; already sent rows
  may be sent again during retry.
- Six extracted-production fixture cases cover main/sidebar invalidation, retry,
  send-boundary owner changes, request preservation and invalid/animation deferral.
  Two regressions failed before the fix. All 35 native suites and
  `./dbt build relwithdebinfo` pass.
- Hardware sends and object retention are not exercised by these fixtures. G1/L1
  remain open and independent mode stays disabled.

### G1 progress — OLED rendering callback containment (2026-10-10)

- OLED rendering validates active stacks, consumes the current request before
  callbacks and preserves newly queued redraws. Owner/hierarchy changes stop the
  pass before subsequent rendering or sending and queue a retry on the initiating
  panel. Empty stacks defer rendering rather than sending an old UI image.
- Five extracted-production fixture cases cover request preservation, owner/stack
  changes, retry, layer coverage, clean sends and invalid stacks. Three regressions
  failed before the fix. All 35 native suites and `./dbt build relwithdebinfo` pass.
- These tests mock display operations; physical display behavior and UI retention
  remain G1/G2/L1 work. Independent mode remains disabled.

### G1 progress — low-level root swap ownership (2026-10-10)

- Root swaps used by automation/sound-editor navigation validate targets and active
  stacks and recheck owner/hierarchy after target resolution. Callback navigation
  is preserved; successful swaps retain overlays without opening or redrawing UIs.
- Five extracted-production tests cover owner/hierarchy/depth changes, normal
  Remote overlay preservation and missing/invalid targets. Two regressions failed
  before the fix. All 35 native suites and `./dbt build relwithdebinfo` pass.
- UI object retention and end-to-end routing remain open; independent mode stays
  disabled.

### G1 progress — render-pass owner and cleanup containment (2026-10-10)

- The render-pass wrapper validates its stack, retains the initiating owner and
  clears that owner's reentrancy flag on every exit, including exception unwind.
  A shared-model refresh that changes owner/hierarchy is requeued before stopping;
  grid-induced stack changes defer the OLED stage.
- Eight extracted-production tests cover refresh invalidation/retry, reentrancy,
  exception cleanup, storage/backpressure deferral, Remote hardware-queue bypass,
  client deferral and grid-to-OLED cancellation. Two regressions failed before the
  fix. Refresh trackers and output-readiness routing use their production types;
  hardware and UI callbacks are fixtures.
- All 35 native suites and `./dbt build relwithdebinfo` pass. G1/L1 remain open and
  independent mode stays disabled. Greyout visibility traversal and its caller's
  post-query updates still need equivalent callback containment; stack equality
  checks throughout this work do not detect transient ABA changes or pin objects.

### G1 progress — cancellable greyout visibility queries (2026-10-10)

- Greyout queries now return no result when owner/hierarchy changes during a UI
  callback or the stack is invalid. The caller skips mask/fade/timer updates for
  cancelled queries; valid empty stacks still request fade-out.
- Five extracted-production tests cover owner/stack invalidation, normal fades,
  immediate updates, unchanged masks and empty/invalid stacks. Both the query and
  pad reassessment bodies are exercised; timers/output are fixtures. Two
  regressions failed before the fix. All 35 native suites and
  `./dbt build relwithdebinfo` pass.
- This closes the greyout query/caller subcase noted above. Hardware acceptance,
  callback-internal lifetime and transient stack changes remain outside this
  coverage. G1/L1 remain open and independent mode remains disabled.

### G1 coverage — both owners and rendering output boundaries (2026-10-10)

- Five additional extracted-production cases exercise OLED setup/send callbacks,
  sidebar-send cancellation, existing greyout fade preservation and layered grid
  occlusion. Owner-change cases run in both directions and check pending work,
  reentrancy cleanup and peer output counters.
- All 35 native suites pass. This commit changes only tests/documentation; the
  preceding production revision passed `./dbt build relwithdebinfo`. Physical
  device acceptance and callback-internal object lifetime remain unproven.
- Session/arranger overview constructors were inspected and currently only
  initialize state; no additional yielding-constructor fix was justified there.
  G1/L1 remain open; advertised support remains `supported_session_modes = 1`.

### G1 progress — automation timer follow-up menu reads (2026-10-10)

- Automation-display timer follow-up reads require the initiating owner, current
  UI, root UI and menu to remain unchanged. Missing menus and client takeover skip
  the read instead of touching a new context.
- Two extracted-dispatch tests cover both owners, root/menu changes, takeover,
  normal reads and missing menus. The changed-context test failed before the fix.
  All 35 native suites and `./dbt build relwithdebinfo` pass.
- These checks do not retain menu objects across callbacks or detect same-address
  replacement. G1/L1 remain open; independent mode remains disabled.

### G1 progress — fallback automation display menu ownership (2026-10-10)

- `View::displayAutomation` snapshots its originating UI/root/menu before knob
  indicator updates and skips follow-up reads after context changes or client
  takeover. Missing menus are safe; the method restores its initiating owner.
- Two extracted-production cases execute the fallback through timer dispatch on
  both owners, covering five invalidations plus valid/missing menus. The context
  regression failed before the fix. All 35 native suites and
  `./dbt build relwithdebinfo` pass.
- Callback internals and menu lifetime still require L1 work; independent mode
  remains disabled and G1 remains open.

### G1 progress — output-name clip indicator state (2026-10-10)

- Output-name rendering no longer shadows its supplied clip with an always-null
  local. Keyboard/cross-screen LEDs use the owning panel's instrument-clip state;
  scale mode uses shared clip state and remains off for kits or missing outputs.
  Audio/missing clips clear these three indicators.
- Four extracted-helper tests cover both owners, independent navigation, shared
  scale, kit/missing output and absent/audio clips. The enclosing display renderer
  and physical LEDs are not exercised by these fixtures.
- All 35 native suites and `./dbt build relwithdebinfo` pass. G1/G2/L1 remain open;
  independent mode stays disabled.

### G1 progress — automation menu reads across song replacement (2026-10-10)

- Both automation display paths now require the originating song identity before
  follow-up menu reads, even when the UI/root/menu pointers remain unchanged.
- One regression runs both paths on both owners and verifies song replacement is
  preserved without rereading the menu. It failed before the fix. All 35 native
  suites and `./dbt build relwithdebinfo` pass.
- Song identity comparison is not lifetime retention or same-address replacement
  detection. G1/L1 remain open and independent mode stays disabled.

### G1 coverage — automation callback completion boundaries (2026-10-10)

- Three additional tests cover direct fallback owner restoration, menu removal
  during indicator updates and owner changes inside menu rereads. Both owners are
  exercised; later timers remain pending after the direct menu callback changes
  owners. The timer test README records the real/fixture coverage boundary.
- All 35 native suites pass. This tests/documentation-only change follows the
  production revision validated with `./dbt build relwithdebinfo`. G1/L1 and
  hardware acceptance remain open; independent mode remains disabled.

### G1 / R4 progress — unavailable knob indicator parameters (2026-10-10)

- Missing modulation targets/mappings no longer dereference null pointers. A
  missing plain parameter has a deterministic off level instead of an uninitialized
  value. Missing collections and fallback targets are handled without dereference.
- Six extracted-renderer tests cover these cases, both-owner output routing,
  current/automated values, nonexistent/patch defaults, stutter and patch scaling.
  Model collaborators and LEDs are fixtures; unsafe pre-fix null cases were not run.
- All 35 native suites and `./dbt build relwithdebinfo` pass. Callback-spanning
  target lifetime remains separate; G1/L1/R4 remain open and independent mode stays
  disabled.

### G1 progress — knob lookup context containment (2026-10-10)

- Knob rendering retains its initiating owner and checks song, controllable,
  parameter-manager, timeline and position identity after lookup/value retrieval
  and before LED output. Context changes cancel the update.
- Two extracted-renderer tests cover six lookup invalidations on both owners and
  owner changes during automated-value retrieval. The lookup regression failed
  before the fix. All 35 native suites, including real renderer linkage in the
  parameter-lifecycle suite, and `./dbt build relwithdebinfo` pass.
- These checks do not pin returned model/parameter objects or establish safety
  inside their methods. G1/L1 remain open; independent mode stays disabled.

### G1 progress — knob indicator batch context (2026-10-10)

- The two-indicator batch restores its owner and stops when a lookup changes its
  root, song, controllable, manager, timeline or position. A changed selection is
  not used to continue the old batch's second indicator.
- Three extracted-batch tests cover five invalidations on both owners, blinking,
  automation delegation and missing roots/targets. The manager-change regression
  failed before the fix. All 35 native suites and `./dbt build relwithdebinfo` pass.
- Already emitted indicator updates are not rolled back. Callback-internal
  retention remains L1 work; G1 stays open and independent mode stays disabled.

### G1 / R4 progress — modulation-region callback containment (2026-10-10)

- Region selection rechecks owner, song, root, model targets and full region after
  value retrieval and indicator updates before continuing to MIDI feedback. It
  preserves callback selection changes and restores the initiating owner.
- Individual/batched knob guards now also check region length and note-row ID,
  distinguishing selections at the same position.
- Five extracted-method tests cover song/owner changes during value retrieval,
  note-row/length changes during lookup, playback/type guards and feedback settings.
  The song-change regression performed two stale lookups before the fix. All 35
  native suites and `./dbt build relwithdebinfo` pass.
- Value retrieval and MIDI transmission are fixtures. Already applied parameter
  values are not rolled back; no callback-internal target retention is established.
  G1/L1/R4 remain open and independent mode stays disabled.

### G1 progress — modulation target selection follow-up work (2026-10-10)

- Selection without a timeline restores its initiating owner and revalidates song,
  root, model and region after LED/indicator/sidebar callbacks before continuing
  to MIDI feedback. Callback replacement selections are preserved.
- Five extracted-selection tests cover song/owner changes, replacement models,
  sidebar invalidation and normal updates. The song-change regression performed
  stale indicator lookups before the fix. All 35 native suites and
  `./dbt build relwithdebinfo` pass.
- These guards do not roll back emitted LEDs or retain model objects inside
  callbacks. G1/L1 remain open and independent mode stays disabled.

### G1 progress — timeline modulation target selection (2026-10-10)

- Timeline selection validates the original song/root/region/model after recording
  target resolution, before replacing the active model. It restores owner context
  and stops after activation or rendering callbacks invalidate the selection.
- Six extracted-selection tests cover resolution/activation changes, both owners,
  nested model replacement, null resolution and normal feedback preferences. The
  song-change test activated a stale target before the fix. All 35 native suites
  and `./dbt build relwithdebinfo` pass.
- A getter that replaces model fields while retaining the same timeline cannot
  generally be distinguished from its intended result here. Callback-internal
  lifetime and rollback remain open; G1/L1 remain open and independent mode stays
  disabled.

### G1 progress — knob parameter mapping consistency (2026-10-10)

- Knob rendering checks the returned parameter collection, auto-parameter, ID and
  controllable across availability, value, kind and conversion calls. An update
  does not mix an old value with a newly installed mapping or emit stale output.
- Two extracted-renderer tests cover five stages on both owners and replacement
  collection/parameter/controllable pointers. The mapping regression emitted an
  indicator update before the fix. All 35 native suites and
  `./dbt build relwithdebinfo` pass.
- Model-stack/parameter lifetime inside callbacks is still unprotected; equality
  checks are not pins and do not detect address reuse. G1/L1 remain open and
  independent mode stays disabled.

### G1 coverage — modulation selection completion boundaries (2026-10-10)

- Three additional extracted-method tests cover legacy fallback mapping changes,
  replaced timelines during activation and complete timeline selection through
  LED/lookup/sidebar invalidation. Both owners are exercised where applicable;
  replacement state is preserved and stale MIDI feedback is skipped.
- All 35 native suites pass. This tests/documentation-only commit follows the
  production revision validated with `./dbt build relwithdebinfo`. Callbacks and
  hardware output are fixtures; actual model destruction remains L1 coverage work.
  Independent mode remains disabled.

### G1 progress — encoder-button completion context (2026-10-10)

Encoder-button callbacks now retain the source panel scope and validate the song,
UI, modulation target and region before dirty marking or indicator updates. Menu
refresh requires the original non-null menu. Missing controllables are ignored.
Six extracted-production regression tests cover normal edit/no-edit completion,
song and owner changes, missing targets/menus, menu replacement and dirty-mark
callback changes. The replaced-song test failed before the fix. All 35 native
suites and `./dbt build relwithdebinfo` pass. These checks contain follow-up work;
they do not retain objects across callbacks or roll back edits already applied.
Independent mode remains disabled.

### G1 / R4 progress — modulation-button routing completion (2026-10-10)

Modulation-button presses now tolerate controllers without mode storage and stop
selection/indicator follow-up after sidebar or instrument callbacks change the
source panel, song, UI, target or region. The panel scope is restored on return.
Releases still reach controllers without mode storage. Seven extracted-production
tests cover these boundaries on both owners, normal selection and VU toggling,
and the arranger automation exception. All 35 native suites and
`./dbt build relwithdebinfo` pass. The fixtures replace model and hardware services;
this is not device acceptance or object retention. Changes already applied before
a callback are not rolled back. Independent mode remains disabled.

### G1 coverage — button completion and nested panel restoration (2026-10-10)

Two further extracted-production tests exercise encoder menu completion after
indicator lookups change the song, root, current UI, controller, parameter manager,
timeline, position or menu, on both panels. They also check that nested indicator
scopes restore temporary owner changes before a valid original menu refresh, and
that a mode lookup switching owners cannot redirect a modulation-button press.
This distinguishes a restored owner scope from persistent context replacement;
it does not prove object lifetime safety. All 35 native suites pass. Production
code is unchanged from the preceding successful RelWithDebInfo build.

### G1 / R4 progress — modulation LEDs with missing clip targets (2026-10-10)

Modulation LED routing checks arranger row bounds, song/output/clip availability,
and keyboard clip availability before reading automation state. Instrument affect
state requires an actual instrument clip. Four extracted-production tests cover
missing targets, invalid rows, mismatched clip types and shared clips with distinct
per-panel automation/affect state across session, arranger and keyboard views.
All 35 native suites and `./dbt build relwithdebinfo` pass. Model and hardware
services are fixtures; these guards do not retain objects or prove device
acceptance. Independent mode remains disabled.

### G1 progress — modulation mode lookup context (2026-10-10)

The view mode getter validates owner, song and modulation target after the virtual
controller lookup, before dereferencing its returned mode pointer. It restores the
source owner and returns no selection for changed or unavailable targets. Three
extracted-production tests cover controller replacement (failed before the fix),
owner/song changes on both panels, and normal/missing mode storage. All 35 native
suites and `./dbt build relwithdebinfo` pass. Pointer identity checks do not retain
objects or detect address reuse; lifetime protection remains open.

### G1 / R4 progress — VU meter without controller mode storage (2026-10-10)

VU rendering now uses the guarded view mode getter instead of directly
dereferencing controller mode storage. Two extracted-production tests cover
missing mode storage on both panels, normal volume-mode rendering, cached-meter
preservation when a clip is selected, disabling the meter and render-lock cleanup.
All 35 native suites and `./dbt build relwithdebinfo` pass. Meter level calculation
and pixel rendering are fixtures here; this coverage establishes selection and
control flow, not hardware output or general callback lifetime safety.

### G1 / R4 progress — incomplete MIDI feedback parameter mappings (2026-10-10)

View MIDI feedback now rejects a supplied automation parameter without its
parameter collection, instead of dereferencing the missing collection. Four
extracted-production tests cover partial mappings on both owners, valid CC/value
routing, disabled/song/unmapped cases and the existing no-parameter fallback.
All 35 native suites and `./dbt build relwithdebinfo` pass. MIDI transport is a
fixture in these tests; they do not establish physical cable filtering or lifetime
retention. Independent mode remains disabled.

### G1 progress — automation notification target availability (2026-10-10)

Null parameter-manager notifications no longer match an unbound view or editor
and schedule display/feedback timers. Three extracted-production tests cover that
case on both panels, matching only the visible editor's manager, unrelated
managers, timer coalescing, promotion of pending level refreshes and resetting the
flag when a new display timer is scheduled. All 35 native suites and
`./dbt build relwithdebinfo` pass. Timers are fixtures in this suite; notification
broadcast between panels and real transport are not established by these tests.

### G1 coverage — mode lookup target changes and VU deselection (2026-10-10)

Two additional extracted-production tests cover manager/timeline replacement
during mode lookup on both panels, plus VU deselection and missing controllers.
They check rejected mode values, panel restoration, no meter rendering and the
sidebar fallback. All 35 native suites pass. No production code changed since the
preceding successful RelWithDebInfo build. The baseline suite count above is now
updated to the current 35; all top-level blockers remain open.

### G1 coverage — production VU calculation and pixel rendering (2026-10-10)

Four tests now execute the extracted production VU level calculation, pixel
renderer and rendering selector together. Coverage includes silence, every meter
band, clipping, stereo colors, preserving the main grid, clearing old peaks,
per-panel caches, unchanged-level caching, re-enabling and render-lock cleanup.
All 35 native suites pass. This closes the prior pixel-renderer stub gap for these
cases, not G1: RGB storage, audio input and mode/context selection are fixtures,
and device acceptance remains outstanding. No production code changed; the
preceding successful RelWithDebInfo build remains applicable.

### G1 / R4 progress — MIDI feedback mapping bounds (2026-10-10)

Feedback CC lookup now rejects negative and out-of-range parameter IDs before
indexing global or sound mapping tables, including checking unpatched IDs before
adding their offset. Three extracted-production tests use the firmware parameter
constants and cover invalid IDs, table boundaries, context mismatches, valid
mappings and unlearned entries. The view-feedback fixture now also imports the
actual `MIDI_CC_NONE` constant (255), replacing its incorrect local -1 sentinel.
All 35 native suites and `./dbt build relwithdebinfo` pass. Context resolution is
still a fixture here; this does not close lifetime protection or two-device
acceptance. Independent mode remains disabled.

### G1 / R4 progress — invalid CC mappings in defaults files (2026-10-10)

Defaults loading now skips CC values outside 0–127 before writing either direction
of the parameter mapping. It consumes the invalid tag and continues with later
entries. Three extracted-production tests cover negative/oversized values for all
mapping kinds, preserved prior tables, unknown names, valid endpoints and recovery
after an invalid entry. All 35 native suites and `./dbt build relwithdebinfo` pass.
The deserializer and parameter-name lookup are fixtures; this is bounds/recovery
coverage, not filesystem or two-device acceptance.

### G1 / R4 progress — restore MIDI Follow MPE upper-zone settings (2026-10-10)

Channel settings loading now accepts the final serialized channel value (18),
which represents the MPE upper zone. The new all-channels regression failed before
this fix with an unassigned channel instead of upper zone 17. Two tests exercise
all channels/zones for the first and last Follow target, invalid values, tag
consumption and the explicit unassigned value. All 35 native suites and
`./dbt build relwithdebinfo` pass. XML tokenization and device references remain
fixtures; physical MPE routing is not established by these tests.

### G1 / R4 progress — unassigned MIDI Follow channel persistence (2026-10-10)

Channel settings now save unassigned as 0, which the reader understands. The
reader also accepts the old writer's 256 encoding so legacy files can clear an
existing assignment. Two new tests execute both production writer and reader for
all channels, both MPE zones and unassigned, plus legacy unassigned loading. The
round-trip test failed before the fix (256 written instead of 0). All 35 native
suites and `./dbt build relwithdebinfo` pass. Serialization tokens and device
references are fixtures; filesystem and physical MIDI acceptance remain open.

### G1 / R4 progress — preserve distinct mappings sharing a CC (2026-10-10)

The defaults writer now saves both sound and global mappings for a shared CC when
their parameter names differ. Identical names still produce one entry, which the
reader applies to both contexts. Three extracted-production tests cover the lost
global mapping (reproduced before the fix), shared-name deduplication, global-only
and unpatched sound entries, including CC endpoints and save/load. All 35 native
suites and `./dbt build relwithdebinfo` pass. Parameter names and serialization
are fixtures; filesystem/device acceptance remains outstanding.

### G1 / R4 progress — consistent duplicate MIDI mapping replacement (2026-10-10)

Defaults loading now removes superseded incoming-CC and feedback entries before
publishing each valid replacement. The last valid entry wins within each sound or
global mapping table. Three regressions cover changing a parameter's CC (failed
before the fix), assigning an occupied CC to another parameter, repeated identical
entries and invalid replacement values. All 35 native suites and
`./dbt build relwithdebinfo` pass. This establishes consistency of the two mapping
directions in the tested loading paths; it is not general shared-object recovery
or physical MIDI acceptance.

### G1 / R4 progress — missing MIDI Follow context outputs (2026-10-10)

Global-effectable context detection now requires a clip output before inspecting
its type, and verifies instrument clip type before accessing kit affect-entire
state. Three extracted-production tests cover missing clips/outputs, independent
kit affect-entire state, audio/synth contexts and a mismatched kit clip. All 35
native suites and `./dbt build relwithdebinfo` pass. Clip selection is a fixture
here; these availability guards do not retain objects or establish device safety.

### G1 / R4 progress — unavailable active-clip fallback (2026-10-10)

MIDI Follow's active-clip fallback now returns no target when the current clip has
no output, instead of returning that clip as if it were active. Two tests execute
the production selector with missing output/active clip and verify explicit
selection priority plus different panel selections. The missing-output regression
failed before the fix. All 35 native suites and `./dbt build relwithdebinfo` pass.
The context tests now also execute this selector rather than stubbing it; explicit
selection and output activity remain fixtures. This does not retain clip/output
lifetimes across callbacks. Independent mode remains disabled.

### G1 / R4 progress — performance clip selection without a song (2026-10-10)

Performance-view MIDI selection now checks song availability before reading the
panel's arrangement position. Three new tests cover missing song/arrangement
context, session/arranger/automation/clip routing on both panels, and simultaneous
different panel contexts. Existing context tests now execute the production
selected-clip function too. All 35 native suites and `./dbt build relwithdebinfo`
pass. Individual view selection providers remain fixtures; callback lifetime and
physical routing acceptance remain open.

### G1 / R4 progress — empty and unavailable MIDI track ranges (2026-10-10)

Track counting now returns zero without a song, and indexed lookup rejects missing
songs and indices outside its supplied track range before reverse-index arithmetic.
Three tests cover the reproduced empty-range wraparound, missing song/empty list,
and reverse ordering while skipping inactive outputs. All 35 native suites and
`./dbt build relwithdebinfo` pass. The output list/activity are fixtures; this does
not establish lifetime protection during shared output mutations or two-device
acceptance. Independent mode remains disabled.

### G1 / R4 progress — MIDI activation follow-up context (2026-10-10)

MIDI Follow now validates model-stack availability and captures the source panel,
song, clip and output before asking the clip minder to activate an instrument.
It rejects changed contexts before resolving the active clip and restores panel
scope. Four extracted-production tests cover the reproduced song-change result,
both owners, clip/output replacement, missing targets and normal activation.
All 35 native suites and `./dbt build relwithdebinfo` pass. The activation callback
is a fixture here; identity checks do not retain objects, detect address reuse or
roll back activation already performed. Independent mode remains disabled.

### G1 / R4 progress — clip-minder activation context (2026-10-10)

The shared clip activation helper now checks inputs, preserves panel scope and
validates song, selection, output and playback mode across output-availability and
activation calls. It reports success only if the clip is active after completion.
Six extracted-production tests cover missing/unavailable inputs, successful and
already-active cases, declined activation, song/owner changes and selection,
output or playback replacement at both boundaries. All 35 native suites and
`./dbt build relwithdebinfo` pass. Playback and output callbacks are fixtures;
checks do not retain objects or roll back activation already applied. Lifetime
and recovery blockers remain open, and independent mode stays disabled.

### G1 / R4 progress — MIDI selector output association (2026-10-10)

Selected-or-active MIDI targets now require an output even for explicit selection.
Fallback and post-activation targets must still belong to the output being routed.
Four regressions cover the reproduced explicit output-less target, detached active
clips, reassigned outputs and successful restoration of a valid association.
All 35 native suites and `./dbt build relwithdebinfo` pass. This validates current
associations; it does not retain clips/outputs or protect against freed pointers
and reused addresses. Independent mode remains disabled.

### G1 / R4 progress — track enumeration output association (2026-10-10)

Track counting and indexed lookup now use the same eligibility rule: the active
clip must still belong to that output. A new regression reproduces the prior
incorrect count and checks skipping output-less/reassigned clips, correct lookup,
and recovery after restoring the association. The normal reverse-order fixture
now gives each clip its actual output. All 35 native suites and
`./dbt build relwithdebinfo` pass. This assumes referenced clips remain alive;
lifetime protection and physical MIDI acceptance remain open.

### G1 / R4 progress — feedback target-list validation (2026-10-10)

Feedback target collection now rejects full/oversized lists before scanning them,
validates existing target indices and rejects channel values outside normal/MPE
ranges. Three extracted-production tests cover unchanged invalid lists, invalid
channels/types, valid MPE zones and duplicate suppression. All 35 native suites
and `./dbt build relwithdebinfo` pass. This is defensive validation of the collector;
it does not show that normal callers produce invalid lists, nor prove device
filtering or callback lifetime safety. Independent mode remains disabled.

### G1 coverage — complete feedback mode resolution (2026-10-10)

Three additional tests execute the production feedback-mode resolver and target
collector together: A/B/C, Track, all combined modes, track-first ordering,
duplicate suppression, missing-track fallback and clearing old targets for
unconfigured/disabled settings. All 35 native suites pass; production code is
unchanged from the preceding successful RelWithDebInfo build. Track selection and
MIDI engine configuration remain fixtures, and actual SysEx-only peer filtering
and physical output are outside this test suite.

### G1 / R4 progress — track matcher index validation (2026-10-10)

Track-specific MIDI matching now validates the signed track index before adding
the A/B/C offset. This prevents negative indices aliasing regular Follow channels
and avoids overflow for extreme indices. Three extracted-production tests cover
the reproduced alias, invalid extremes, Track 1/16, cable/channel forwarding, MPE
match preservation and regular-channel first-match priority. All 35 native suites
and `./dbt build relwithdebinfo` pass. Learned-device matching is a fixture; these
tests establish dispatch/index behavior, not physical input acceptance or lifetime
safety. Independent mode remains disabled.

### G1 / R4 progress — MIDI note-dispatch input availability (2026-10-10)

Note dispatch now checks clip/output, model stack, song and note range before
routing or indexing retained-note state. Four extracted-production tests cover
invalid notes and missing inputs, muted note-off delivery without recording,
muted note-on suppression, kit translation, track-specific dispatch and audio-clip
exclusion. All 35 native suites and `./dbt build relwithdebinfo` pass. Instrument
callbacks are fixtures; this does not address retained-note lifetime after
callbacks, rollback, or physical note delivery. Independent mode stays disabled.

### G1 / R4 progress — note-dispatch output type checks (2026-10-10)

Instrument note dispatch now requires kit, synth, MIDI-out or CV output type before
casting. Other outputs are returned for duplicate-route suppression without
instrument dispatch or note retention. Two new tests cover the reproduced
incompatible-output dispatch and boundary notes for all melodic output types,
including retention/release. All 35 native suites and
`./dbt build relwithdebinfo` pass. The instruments are fixtures; retained-note
lifetime and callback invalidation after dispatch remain unresolved.

### G1 / L1 progress — retained-note publication before callbacks (2026-10-10)

Note-on retention is now published before instrument delivery, allowing
`removeClip`/song cleanup during delivery to remove it permanently. Matching
note-off retention is cleared before delivery so a nested note-on is not erased
by outer completion; unrelated targets are preserved. Three tests cover the
reproduced callback-cleanup overwrite, nested note-on during note-off and unrelated
note-off completion. All 35 native suites and `./dbt build relwithdebinfo` pass.
This closes these publication-order cases without retaining objects or undoing
instrument effects. Broader callback lifetime and routing remain open.

### G1 / R4 progress — MIDI event routing batch context (2026-10-10)

Note, CC, pitch-bend and aftertouch routing now reject missing song/model-stack
inputs and check song/panel context after selected-target delivery and each track
delivery. A changed context stops subsequent tracks and restores panel scope.
Four tests exercise all four production batch methods, including song changes,
both panels, valid selected/missing-track suppression and the 16-track cap.
All 35 native suites and `./dbt build relwithdebinfo` pass. Individual delivery
callbacks and enumeration are fixtures; this does not protect objects inside
callbacks or roll back events already delivered.

### G1 / R4 progress — selected all-notes-off context boundaries (2026-10-10)

Selected-clip note handling now preserves the source panel and rejects changed
song/panel context after delivery. All-notes-off checks after each retained note
instead of continuing the batch in a replacement context. Three tests cover the
reproduced song-change result, panel changes on both owners and normal 128-note
release/retention cleanup. All 35 native suites and `./dbt build relwithdebinfo`
pass. Instrument callbacks remain fixtures, and general retained-object lifetime
and track-specific all-notes-off safety are not established by this change.

### G1 / L1 progress — specific-track all-notes-off target revalidation (2026-10-10)

Specific-track note delivery now requires a registered song output and matching
active clip. All-notes-off rechecks song/panel, output membership and clip
association before every delivery. Four tests cover missing/detached outputs,
normal 128-note dispatch without selected-note retention changes, and callbacks
that remove/delete the fixture output or clear/delete the fixture clip after the
first delivery. All 35 native suites and `./dbt build relwithdebinfo` pass.
The tests perform real deletion of fixture objects, not firmware destructors;
address reuse and destruction without registry/active-clip updates remain L1 gaps.

### G1 / L1 progress — track CC parameter-to-instrument boundary (2026-10-10)

Track CC delivery now validates input ranges, song output membership and active
clip association, then revalidates after parameter processing before instrument
delivery. Unsupported output types no longer take the melodic cast. Eight tests
cover fixture clip/output deletion, output/song removal, both panels, valid
internal/MIDI/CV routing, mute/solo, unavailable targets and feedback filtering.
All 35 native suites and `./dbt build relwithdebinfo` pass. Real deletion is limited
to fixture objects; firmware destructor behavior, address reuse and lifetime
inside parameter/instrument callbacks remain open.

### G1 progress — selected CC context boundaries (2026-10-10)

Selected CC routing now validates input bounds and the song/model stack before
processing. It checks song, panel and current-clip identity after parameter
handling and activation, and only reports the selected output while that context
remains current. Instrument casts exclude unsupported output types. Four native
regressions cover parameter/activation context changes, distinct selected and
active clips, and invalid inputs. All 35 native suites and
`./dbt build relwithdebinfo` pass. Identity checks do not protect against address
reuse or destruction inside callbacks; those remain L1 work.

### G1 progress — pitch bend and aftertouch target validation (2026-10-10)

Selected and track-specific expression delivery now rejects absent song/model
stacks and limits melodic casts to synth/MIDI/CV outputs. Track delivery requires
the active clip to belong to that output; selected delivery rejects missing
outputs. Four production-body tests exercise both message types, supported
instruments including kits, audio exclusion, missing context and mismatched
associations. All 35 native suites and `./dbt build relwithdebinfo` pass.
This validates dispatch entry conditions, not callback lifetime or address reuse.

### G1 progress — MIDI feedback sweep cancellation (2026-10-10)

Feedback sweeps require a current song and stop when song, panel or current-clip
identity changes between mappings or during parameter lookup. Incomplete
parameter stacks without a collection are skipped. Six production-body tests
cover ordinary/automation feedback, per-panel step-edit positions, unavailable
targets/parameters, incomplete stacks and callback context changes. All 35 native
suites and `./dbt build relwithdebinfo` pass. Parameter and sending services are
fixtures; cancellation between iterations is not object retention or rollback.

### G1 / R5 progress — incoming CC arrangement-clone routing (2026-10-10)

Incoming learned CC handling follows the updated timeline when arrangement
recording supplies a clone, instead of passing the original clip alongside the
clone's model stack. Unlearned/invalid CCs exit before cloning. Song/panel/current
clip changes during cloning cancel parameter lookup; a mismatched input timeline
is rejected. Five tests cover clone-target forwarding, unchanged targets,
unlearned/invalid input, context changes and mismatched/missing context. All 35
native suites and `./dbt build relwithdebinfo` pass. The clone service is a fixture;
its existing boolean result still conflates unnecessary cloning with failure, so
this change does not claim clone-failure recovery or original-state rollback.

### G1 / L1 progress — learned CC display after parameter mutation (2026-10-10)

Learned CC handling rejects incomplete parameter stacks and checks context after
lookup, parameter mutation and automation/performance refresh. Display identifiers
are captured before mutation, so later UI feedback does not reread the parameter
stack after a callback can invalidate it. Four regressions cover cleared stacks,
lookup/write context changes and refresh cancellation. All 35 native suites and
`./dbt build relwithdebinfo` pass. The tests use callback fixtures, not real
parameter destruction; clip retention during refresh and callback-internal
lifetime remain open under L1.

### G1 progress — unavailable parameter-lookup targets (2026-10-10)

MIDI Follow and Automation View clip parameter lookup now reject missing model
stacks, clips and outputs before accessing clip state. The existing parameter
lookup suites cover detached MIDI outputs (including requested error feedback)
and unavailable Automation View targets under both panel owners. All 35 native
suites, including diagnostics-on/off parameter lookup, and
`./dbt build relwithdebinfo` pass. These are entry guards; dangling non-null
objects and callback lifetime remain L1 concerns.

### G1 / R5 progress — arrangement-clone failure reporting for learned CC (2026-10-10)

`possiblyCloneForArrangementRecording` now offers an optional error result while
preserving its existing boolean and default-argument callers. Allocation,
missing-instance, insertion and clone failures are distinguishable from no clone
needed. Learned CC handling consumes that result and does not edit the original
when cloning fails. Five real-method fixture tests cover error propagation,
unchanged targets, successful cloning/reuse and legacy calls; an additional CC
test verifies failure stops parameter lookup/writes. All 35 native suites and
`./dbt build relwithdebinfo` pass. Other callers still omit the error result, and
partial instance edits before clone failure are not rolled back. R5 stays open;
this is failure reporting and CC containment, not complete clone recovery.

### R5 progress — preserve original audio instance when insertion fails (2026-10-10)

Repeated-audio arrangement cloning now shortens the original instance only after
inserting the new instance succeeds, reacquiring the original by index in case
storage moved. The regression failed before the fix (length 256 became 128 on
allocation failure) and now passes. A success test verifies prior repeats,
new-instance position/length and sample handoff. All 35 native suites and
`./dbt build relwithdebinfo` pass. Later clone/expansion/publication failures and
callback lifetime still require recovery; this closes only pre-insertion extent
mutation, not the entire clone transaction.

### G1 / R5 progress — melodic MIDI parameter clone failures (2026-10-10)

Melodic instrument parameter input now consumes the arrangement-clone error
result, stopping before parameter-manager lookup or writes when cloning fails.
Missing input/model parameter stacks are also rejected. Four production-body
tests cover failed/successful/unnecessary cloning, unavailable targets and
per-panel step editing against the resulting timeline. All 35 native suites and
`./dbt build relwithdebinfo` pass. Other clone callers still need migration and
live-expression fallback tests; callback lifetime remains unresolved.

### G1 / R5 progress — learned MIDI mapping clone failures (2026-10-10)

Learned clip CC and pitch-bend mappings consume clone errors and stop before
parameter lookup/writes on failure. Matching messages remain reported as handled;
missing model/parameter stacks are safe. Three production-body regressions cover
both message types, failures, valid delivery, missing targets and unmatched
mappings. All 35 native suites and `./dbt build relwithdebinfo` pass. Remaining
clone callers include encoder lookup, note recording and expression recording;
learned-mapping callback lifetime and container mutation remain separate gaps.

### G1 / R5 progress — encoder lookup clone failure (2026-10-10)

Encoder parameter lookup now returns no target when arrangement cloning reports
failure, rather than continuing with the original timeline's parameter. Four
real-method fixture tests cover failure, successful clone/context refresh,
unnecessary cloning, absent timelines and absent controllables. All 35 native
suites and `./dbt build relwithdebinfo` pass. This does not establish lifetime
safety across note-tail/clone/refresh callbacks; expression and note-recording
clone callers still need failure handling.

### G1 / R5 progress — live expression after arrangement-clone failure (2026-10-10)

Drum and melodic expression recording now consume clone errors and fall back to
live expression without recording into the original clip. Missing model stacks
also take the live path. Four production-body tests cover failed cloning,
recording into successful clones, unavailable timelines and row-recording
failure; smoothing is active during fallback and cleared afterward. All 35
native suites and `./dbt build relwithdebinfo` pass. Note-on recording is now the
remaining pair of clone callers without explicit error handling. Object lifetime
inside clone/row/sound callbacks remains open.

### G1 / R5 progress — note-on clone failure and caller migration (2026-10-10)

Melodic and kit note-on handling now skip recording when arrangement cloning
fails while preserving audition and note-off delivery. Four production-body
regressions cover failures without original-note edits/history acquisition,
successful clone recording, existing arrangement clips and nonrecording input.
All 35 native suites and `./dbt build relwithdebinfo` pass. A source audit confirms
all nine production call sites now request/check the clone error result (MIDI
Follow, encoder lookup, melodic note/parameter/expression, kit note, drum
expression, learned CC and learned pitch bend). **Caller error-result migration
is complete**, but failures ignored inside the clone operation, partial instance
edits, clone ownership and callback lifetime still keep R5/L1 open.

### R5 progress — validate arrangement-recording clone results (2026-10-10)

Arrangement-recording cloning now requires the current owning song and rejects
song changes after cloning. A successful result must be non-null, distinct from
the original, and absent from song/history ownership before it is repurposed or
published. Two production-body regressions cover null/original/already-owned
results and changes to either song pointer, verifying no clip-field mutation or
publication. All 35 native suites and `./dbt build relwithdebinfo` pass. Unexpected
results are not destroyed because their ownership is unknown; this does not
recover prior audio-instance edits or prove lifetime during callbacks.

### R5 / L3 progress — repeat and publication failures in recording clones (2026-10-10)

Recording cloning now checks repeat expansion and song insertion results. With
unchanged song/panel/structural revisions and an unadopted clone, failure restores
the source timeline and discards the unpublished clone. Song/history ownership,
active-output adoption or a clip instance prevents disposal and duplicate
publication. Five regressions exercise expansion/publication failure, failed and
successful expansion callbacks adopting the clone, and structural invalidation
on either panel. All 35 native suites and `./dbt build relwithdebinfo` pass.
Cleanup is counted through a fixture, not a real destructor. Invalidation without
notification/address reuse, cleanup after lost context, prior audio-instance
edits and later playback callbacks remain open; R5/L3 are not closed.

### L1 / R5 progress — recording-clone callback boundaries (2026-10-10)

Recording cloning now carries song/panel/structural checkpoints from entry,
restores the initiating panel and checks after reservation, audio-instance
insertion, cloning and playback callbacks. After publication it also requires
continued clip membership before following the clone pointer. Seven regressions
invalidate allocation, insertion, note-stop, positioning, resume, activation and
published membership, checking that later work stops. All 35 native suites and
`./dbt build relwithdebinfo` pass. A prefix may already be published or stopped;
these checks are cancellation, not rollback, and do not retain objects or handle
unnotified destruction/address reuse. Those remain L1/R5 blockers.

### R5 progress — initialized audio split and early-failure rollback (2026-10-10)

The temporary repeated-audio instance now references the original clip with a
valid length before cloning can yield. On clone/publication failure, an unchanged
split is removed without allocation and its original extent restored. Rollback
checks context, count and both instance fingerprints; callback edits are
preserved. Unrelated source instances are rejected. Three regressions failed
before the fix (uninitialized target and extra instances after both failures);
five added cases now pass, along with all 35 native suites and
`./dbt build relwithdebinfo`. Original clips are never disposed by this rollback.
The tests use instance/cleanup fixtures; the capacity-preserving deletion primitive
has existing native lifecycle coverage. Lost-context rollback, later playback
prefix recovery, unnotified mutation and general object retention remain open.


### L1 progress — allocation-free clip lifetime cancellation (2026-10-10)

Clips now expose non-owning lifetime watches. Retirement invalidates existing
watches and rejects new ones without allocating, reading freed storage or relying
on selection membership/address equality. Preparation and derived/base destructor
entry retire the clip before cleanup callbacks. Recording cloning watches both
the source and new clip before following saved references after callbacks.

Four primitive tests cover unlinking, retirement, observer/source destruction
order and address reuse. Four recording-clone regressions perform actual fixture
object deletion/reconstruction without registry updates. Four extracted production
preparation/destructor tests verify retirement precedes parameter/selection cleanup
and still occurs without a current song. The new `ClipLifetimeTests` target passes
with AddressSanitizer and UndefinedBehaviorSanitizer enabled; all 36 native suites
and `./dbt build relwithdebinfo` pass.

This adds a small per-clip watcher list and stack-local watches, not heap snapshots
or deferred reclamation. It assumes serialized/reentrant firmware callbacks and
requires a live object when acquiring a watch. It does not make callback internals
safe, protect outputs/songs/other model types, or cover every saved clip reference.
Those migrations and ownership/recovery decisions remain L1 work.

### L1 / G1 progress — MIDI retirement cleanup and watched dispatch (2026-10-10)

Clip retirement now clears MIDI Follow's retained note targets once, before
cleanup callbacks, including direct destruction without a current song. Track
CC/all-notes-off hold lifetime watches across delivery, and note dispatch rejects
retiring clips before retaining or dereferencing their output. Eight new
regressions cover idempotent retirement, matching-only cache cleanup, direct
destruction, retired input, destruction without active-pointer cleanup and
same-address replacement during CC/all-notes-off. These paths also run in the
ASan/UBSan `ClipLifetimeTests` target. All 36 native suites and
`./dbt build relwithdebinfo` pass. Other MIDI selection/parameter callbacks and
output lifetime are not comprehensively watched yet; callback-internal ownership
and concurrent-thread access remain outside this cancellation mechanism.

### L1 / G1 progress — watched MIDI parameter and feedback targets (2026-10-10)

Learned CC handling now watches the original and resulting clip across cloning,
lookup, parameter writes and display callbacks. Feedback sweeps watch their
selected target across lookup and sending. Five regressions destroy targets during
lookup/write, replace a feedback target at the same address, and reject retiring
targets before parameter services. They run under ASan/UBSan in `ClipLifetimeTests`;
all 36 native suites and `./dbt build relwithdebinfo` pass. These checks cover clip
retirement after a live watch is acquired, not lifetime inside parameter services
or independent destruction of parameter collections/outputs.

### L1 / G1 progress — watched clip activation (2026-10-10)

Clip activation and MIDI Follow's active-clip lookup now watch the initiating
clip across availability/activation callbacks and reject retiring source/active
clips. Six regressions cover destruction during availability/activation,
same-address replacement and retiring targets. These run under ASan/UBSan;
all 36 native suites and `./dbt build relwithdebinfo` pass. Output lifetime and
long-lived UI selection references still need their own protection; this change
covers clip lifetime after acquisition at these activation boundaries.

Output lifetime cancellation now uses the same allocation-free watch mechanism.
All five concrete output destructors retire before cleanup, and song deletion
retires before registry/audition cleanup. Hibernation does not retire an output.
Recording cloning watches both source and result outputs, rejecting destruction
or same-address replacement without relying on clip destruction or UI revisions.
Eight extracted destructor/deletion tests, an empty-watch test and two recording
regressions run under ASan/UBSan. All 36 native suites and
`./dbt build relwithdebinfo` pass. This protects already-acquired watches; stale
long-lived references and callback-internal ownership remain open under L1/G1.

Activation and MIDI Follow active-clip lookup now watch the source output before
activation/availability callbacks and reject retiring outputs even when their
clip is still active. Six sanitizer regressions cover output deletion,
same-address reuse and retirement before entry. All 36 native suites and
`./dbt build relwithdebinfo` pass. Persistent reference acquisition remains a
separate lifetime gap; these guards cover outputs known live at acquisition.

Specific-track note/CC routing now watches the output after verifying membership.
All-notes-off loops and CC instrument follow-up stop on retirement, destruction
or address reuse before traversing the list again. Single-note delivery rejects
retiring outputs before retaining the note. Six sanitizer regressions include
callbacks that leave the old output pointer in the list. All 36 native suites
and `./dbt build relwithdebinfo` pass. This does not repair a stale list for later
independent operations; deletion still must maintain the song's ownership list.

Learned-CC edits and feedback sweeps now watch source/result output lifetimes and
verify the clip still references the same output after lookup, writes and display
callbacks. Output-less song contexts remain supported. Six sanitizer regressions
cover deletion, address reuse, reassignment and retiring outputs, including a
recording clone whose output dies during its parameter write. All 36 native
suites and `./dbt build relwithdebinfo` pass. Parameter collection lifetime inside
lookup/write callbacks remains separately open.

Selected-clip CC routing now watches both the current and selected clip/output
pairs through parameter handling and activation. This prevents follow-up
activation from reacquiring a deleted target after the inner edit cancels.
Output-less selected contexts avoid a null dereference. Five sanitizer regressions
cover separate current/selected targets, deletion, output address reuse and the
output-less case. All 36 native suites and `./dbt build relwithdebinfo` pass.

Output retirement now clears MIDI Follow's retained notes for surviving clips
that still reference that output. Cleanup runs only on the first retirement,
before recorder/member callbacks, and leaves other outputs' retained notes alone.
Three sanitizer tests execute the real output retirement and retained-note cleanup
bodies, covering selective clearing, destructor ordering and reassignment before
later destruction. All 36 native suites and `./dbt build relwithdebinfo` pass.
This closes that retained-note acquisition gap; it does not cover other stored
clip/output references or repair stale song ownership lists.

Pitch-bend and aftertouch dispatch reject retiring clips and outputs for both
selected and specific-track delivery. Two sanitizer regressions exercise all
four paths with retiring clip/output targets. All 36 native suites and
`./dbt build relwithdebinfo` pass. This addresses reentrant dispatch during
retirement; it does not validate a pointer whose storage was already reclaimed
before entry or prove safety inside instrument expression callbacks.

MIDI Follow selection/fallback lookup and track enumeration now exclude retiring
clips and outputs. Seven sanitizer regressions cover selected targets, fallback
source/active targets and track filtering. All 36 native suites and
`./dbt build relwithdebinfo` pass. Lookup still requires live storage at entry;
long-lived reference ownership remains part of L1.

Clip retirement now starts at Song::deleteClipObject entry, before selection
invalidation, including whole-song destruction. Detached-clip consequence cleanup
retires after checking that the song has not reclaimed ownership and before
backup cleanup. A sanitizer test executes the real song deletion/clip retirement
bodies for both destruction modes; undo fixture tests verify retirement before
backup callbacks and no retirement for song-owned clips. All 36 native suites
and `./dbt build relwithdebinfo` pass. Hibernation and reversible detachment do not
call these retirement entry points. Reentrant ownership changes inside cleanup
still require the broader L1/L2 contract.

Arrangement-recording target reuse now verifies the existing active clip is live
and still attached to the source output before retargeting the model stack.
Two sanitizer regressions reject retiring/reassigned recording targets without
cloning or publishing another clip; the existing normal-reuse test still passes.
All 36 native suites and `./dbt build relwithdebinfo` pass.

Recording clone search/split preparation now rejects an overflowing search key,
nonpositive audio loop length, excessive repeat span and an audio split extending
past the sequence limit. Repeat/span arithmetic uses 64 bits and validated split
values are retained across insertion. Five sanitizer regressions verify failures
leave the original instance untouched and the exact sequence-end boundary still
works. All 36 native suites and `./dbt build relwithdebinfo` pass. Instrument
expansion and playback-position arithmetic are the next checks in this path.

Recording-clone repeat length and playback position now use checked 64-bit
arithmetic. Invalid/zero lengths and unrepresentable positions discard only the
unpublished copy; invalid source loop lengths are rejected before clone code.
Position is checked before publication and again after stopping the source, so a
callback cannot introduce signed overflow before resume. Nine sanitizer tests
cover extreme values, exact valid length, reversed wrapping, insertion rollback
and late callback failure. All 36 native suites and `./dbt build relwithdebinfo`
pass. A late post-publication failure still leaves the published prefix for R2/R4
recovery; this change does not claim transactional rollback of that prefix.

Recording cloning no longer writes through an arrangement-instance pointer kept
across callbacks. It validates count and instance fields, reacquires storage for
replacement, and checks the published instance after subsequent callbacks.
Seven sanitizer regressions include real vector relocation during cloning/source
stop and callback edits to position, length or count. Unpublished copies are
cleaned up when safe; callback edits are preserved. All 36 native suites and
`./dbt build relwithdebinfo` pass. Cancellation after publication still has the
previously documented recovery-prefix limitation.

Audio recording splits now reserve instance capacity before publication, validate
source count/fields and repeat state after reservation, and use the existing
allocation-free insertion primitive to initialize the split without a callback
window. Six additional sanitizer tests cover allocation failure, real relocation,
source edits/removal, output destruction and changed repeat count; prior callback
tests now inject at reservation. All 36 native suites and
`./dbt build relwithdebinfo` pass. Allocation-free insertion itself retains the
existing native ring-array test coverage; ownership changes inside reservation
remain subject to the broader array/object lifetime contract.

AudioClip::clone now watches the source, its output and an incoming clip timeline
through allocation/parameter/sample stages, checks song/owner/stack context, and
publishes the copy to the stack only after sample setup. Fifteen sanitizer tests
execute this production body with allocation/parameter/sample doubles, including
real source/output destruction, address reuse, errors, caller retargeting, a
separate recording timeline and a song timeline. The implementation uses the
existing Song/Clip timeline distinction rather than RTTI, which firmware disables.
All 36 native suites and `./dbt build relwithdebinfo` pass. Source lifetime inside
parameter/sample copying remains open; outer checks alone do not prove those
internals safe. The String-to-String path copy shares storage and cannot fail.

Audio arrangement rollover now watches current/source/copy/output lifetimes,
revalidates after base/reservation/clone callbacks, and delays shortening the
original instance or clearing its source link until both reserved insertions
succeed. Instance insertion failure removes the just-published song slot and
cleans up only the unowned copy. Activation/position callbacks are checked before
further work. Twenty-two sanitizer regressions execute the real rollover body
with model/clone doubles, including relocation, allocation/publication failure,
real deletion, adoption and callback edits. All 36 native suites and
`./dbt build relwithdebinfo` pass. Cancellation after activation preserves the
published prefix; full recovery and callback-internal parameter ownership remain
open. No original clip is deleted on clone/publication failure.

Sample-holder cloning now captures the four scalar range/view settings before
sample assignment, so source deletion or address reuse during cluster servicing
does not cause later source reads or mix replacement metadata into the copy.
Five sanitizer regressions execute both sample-holder clone bodies with shared
path/sample-assignment doubles. No sample data or audio buffers are copied.
All 36 native suites and `./dbt build relwithdebinfo` pass. Inspection also corrected
the prior path-error hypothesis: String::set(String const*) shares reference-counted
storage and cannot fail. Destination lifetime and callbacks inside sample/parameter
services remain covered by their separate ownership requirements.

Instrument cloning now watches its source/output through allocation, construction
and parameter-copy return, and rejects changed song, owner, stack, length or
direction before reading source rows. Eight sanitizer regressions cover real
source/output destruction, address reuse, retiring input and context changes.
Existing tests still require every shallow-copied row to finish ownership
normalization after row errors. All 36 native suites and
`./dbt build relwithdebinfo` pass. This closes early boundaries only; source
lifetime inside parameter/row copying and borrowed row-storage ownership remain
open and must not be bypassed by destroying half-normalized rows.

Audio/instrument clip cloning now passes its source lifetime watch into parameter
cloning. ParamManager checks it before source access, after each raw allocation
and after collection cloning returns, releasing temporary raw/constructed copies
on cancellation. Cleanup uses captured counts/layout rather than rereading a
retired source. Five native parameter lifecycle regressions cover actual source
manager destruction on the first/second allocation, expired entry, cancellation
after collection cloning and normal copies, with allocation accounting and
sanitizers. All 36 native suites and `./dbt build relwithdebinfo` pass. The optional
guard protects these boundaries; source collection mutation and callbacks inside
collection-specific cloning remain open. Unguarded callers retain their existing
source-ownership contract.

Parameter cloning now snapshots collection pointers, sizes and expression layout
before allocation, and rejects changed layouts before copying or continuing.
Four native lifecycle regressions cover removed/replaced collections and expression
addition with expression copying enabled/disabled. The manager preserves existing
destination state on cancellation. All 36 native suites and
`./dbt build relwithdebinfo` pass. These checks do not establish collection identity
across same-address reuse or protect borrowed automation inside collection cloning;
those lifetime boundaries remain open.

ResizeableArray clone entry points now accept an optional source lifetime watch.
Allocation checks retirement before inspecting source fields or copying borrowed
storage; shallow-copy cancellation detaches source storage without freeing it.
Five native sanitizer regressions exercise real source destruction, expired
entry, shallow normalization and independent successful copies. All 36 native
suites and `./dbt build relwithdebinfo` pass. This supplies the guarded storage
primitive; callers must propagate the watch through collection/node cloning.
Destination lifetime and unguarded callers remain separate ownership contracts.

AutoParam and lazy-node shallow cloning now accept and forward the source watch.
Normal copying checks before vector/source access; reverse copying checks after
node allocation before reading the old nodes. Cancellation leaves no borrowed
storage in the copy. Native sanitizer tests destroy the actual source at both
allocations for normal/reverse copies and cover expired entry and successful
semantics. All 36 native suites and `./dbt build relwithdebinfo` pass. Collection
callers still need to propagate the guard; source edits that do not retire its
owner and destination lifetime remain separate concerns.

The clip source watch now reaches fixed, expression, MIDI and patch-cable
collection cloning, including pooled parameter/cable acquisition, node storage
and destination-group copying. Cancellation normalizes every borrowed pointer
before the failed copy is destroyed. Native sanitizer sweeps retire and actually
destroy source managers at every allocation until successful completion, for
normal and reverse cloning with multiple parameters, automated expression and
patch destination groups. They check preserved destination state, layout and
allocation/pool balance. All 36 native suites and `./dbt build relwithdebinfo` pass.
This protects owner-retirement paths through these clone implementations; source
collection/parameter edits while the owner remains alive, shallow note-row clone
integration and destination lifetime still need their own protection.

Shallow ParamManager normalization now accepts the original owner's watch and
forgets unchanged borrowed entries after retirement, including an already-expired
source. It preserves a callback's different replacement layout. Native sanitizer
tests cover expired input, source deletion at every allocation in normal/reverse
clones and callback layout replacement, with pool/allocation accounting. All 36
native suites and `./dbt build relwithdebinfo` pass. The unpublished shallow
destination must remain alive; this does not establish same-address collection
identity or finish note-row integration.

Instrument cloning now passes the source watch into the raw note-row array copy.
Retirement during its allocation cancels before copying freed row storage or
starting row normalization. Three sanitizer regressions execute the real clip
clone body with a guarded array double, covering deletion, reuse and retirement;
the real array deletion behavior is covered by the native array tests above.
All 36 native suites pass (the updated routing source-contract check was rerun
separately), and `./dbt build relwithdebinfo` passes. Row-internal callbacks and
publication timing remain the next boundaries.

Instrument cloning now normalizes rows on a private model stack and publishes the
copy only after every row and the initiating context remain valid. Source/output
retirement skips output-dependent row lookup, finishes detaching borrowed rows,
and preserves callback stack retargeting. NoteRow cloning forwards the source
watch into parameters/notes, checks output lifetime after allocations, and clears
borrowed drum names even on early cancellation. Six clip-clone regressions cover
row-callback destruction/reuse, output death, delayed publication and stack edits;
six additional sanitizer tests execute the real row clone body with parameter,
array and model doubles. The real guarded parameter/array implementations are
covered separately by native tests above. All 36 suites and
`./dbt build relwithdebinfo` pass; the strengthened lifetime fixtures also pass
when rerun. Owner-preserving edits to borrowed rows, drums or automation, and
callback-internal sound/drum lifetime remain open. No original is deleted on
clone failure.

Drums now expose allocation-free retirement watches; base and all three concrete
destructors retire before member cleanup. Drum expression recording stops after
clone/record callbacks if the drum retires, and restores the prior smoothing flag
for nested dispatch. Five sanitizer routing regressions cover actual deletion,
address reuse, retiring entry and nested smoothing; four destructor regressions
execute production destructor bodies with cleanup doubles. Expression tests now
run in the sanitizer lifetime target. All 36 native suites and
`./dbt build relwithdebinfo` pass. Early deletion cleanup, linked-list dispatch
continuations, retained drum acquisition and note-row drum ownership still need
separate checks; adding watches does not make those paths safe automatically.

Six kit expression dispatch routes now watch the kit, routed clip and current
drum, and confirm that the drum is still linked before following its next pointer.
They stop on retirement/destruction or live detachment. Kit-wide CC rejects a
missing/mismatched clip; polyphonic aftertouch rejects missing context and
unlinked/retiring drum targets. Nine sanitizer tests execute all six production
route bodies and the real membership lookup with callback/model doubles, covering
normal dispatch, deletion, address reuse, detachment and owner retirement. All 36
native suites and `./dbt build relwithdebinfo` pass. Note-on/off and audition
internals remain separate routing work; membership checks and end-to-end timing
still require the two-device hardware validation in G2.

Kit per-drum note handling now validates kit/drum membership before watch
acquisition, watches original/resulting clips, and revalidates song/panel/stack,
output association and row identity after selection, cloning, recording and UI
callbacks. Removed or reused rows are resolved again before follow-up access.
Sixteen new sanitizer regressions exercise real note-handler bodies with model
and callback doubles, including actual owner destruction, stale entry, row
replacement/removal and preserved callback edits. The existing note-routing tests
now run in the sanitizer lifetime target. All 36 native suites and
`./dbt build relwithdebinfo` pass. Cancellation after a recording write preserves
that prefix; full recovery, the outer note-dispatch loop and audition internals
remain separate work.

Kit note handlers now report callback cancellation to their caller. The outer
note loop stops on invalidation but refreshes the routed clip after a successful
clone, preserving delivery to other mapped drums. Mute callbacks are revalidated
before rendering and traversal; whole-kit note mapping rejects absent or mismatched
clip context. The six-route lifetime sweeps now include note dispatch, with eleven
additional tests for handler status, successful retargeting, mute callbacks,
rendering and clip association. All 36 native suites and
`./dbt build relwithdebinfo` pass. Audition and arpeggiator internals remain open;
a safe outer loop does not establish their callback safety.

Kit audition entry points now reject detached/retiring drums and watch the kit,
drum and active clip. Note-on publishes audition state before dispatch, preserving
nested note-off edits and avoiding post-dispatch owner access. Note-off schedules
only the original surviving, still-associated active clip. Thirteen sanitizer
regressions execute the production audition bodies with model/dispatch doubles,
covering deletion, address reuse, retargeting, tail-query cancellation and normal
one-shot/drone behavior. All 36 native suites and `./dbt build relwithdebinfo`
pass. This does not establish the safety of arpeggiator internals or the outer
stop-all loop, nor protect row/parameter removal while their owners survive.

The stop-all-auditions loop now watches its kit, original active clip and each
current drum, stopping before list traversal after retirement, detachment or clip
replacement. Eight additional sanitizer regressions execute the real loop and
note-off body for owner deletion, same-address drum reuse, live detachment,
retargeting, multiple drums and no-clip audition. All 36 native suites and
`./dbt build relwithdebinfo` pass. Arpeggiator instruction storage and its nested
callback paths remain open; cancellation does not roll back notes already stopped.

All three kit-arp note-on dispatch sites now publish playing/reverse state before
the drum callback through a shared helper. Expression values are copied into a
small stack snapshot so nested arp reset cannot invalidate the data consumed by
the callback. Six sanitizer regressions exercise the production helper, including
actual heap deletion and nested note edits; a source contract checks all three
call sites. Native validation passes (the new contract's assignment matcher was
corrected to exclude equality comparisons), as does `./dbt build relwithdebinfo`.
This closes post-dispatch status writes only: arp generation, row selection and
subsequent tick/render traversal still require separate lifetime validation.

Kit pre-arp note-on/off now validate drum membership before acquiring a watch and
re-resolve the original note-row identity after tail queries and arp generation.
Retired owners, removed/reused rows, detached drums and active-clip/output changes
cancel dispatch. Thirteen sanitizer regressions execute both production entry
points and the shared dispatch helper with model/arp doubles, including real heap
deletion, bypass, one-shot and no-clip behavior. All 36 native suites and
`./dbt build relwithdebinfo` pass. This protects these callers after generation;
it does not establish safety inside arp generation or the tick/render batches.

Audition startup now also re-resolves note-row identity and checks model-stack and
output association after its tail query, before publishing audition state or
rebuilding parameter context. Five additional regressions cover removed/reused
rows, model-stack/output retargeting and a live no-row backup manager. All 36 native
suites and `./dbt build relwithdebinfo` pass. Row/parameter changes inside callbacks
still require their own contracts; these checks protect the audition caller.

MIDI/gate drum note-on, note-off and kill-all-voices now watch drum lifetime across
arp generation and each output dispatch. Note-on status is published before the
callback; retired drums cancel traversal and the final kill-all reset. Fourteen
sanitizer regressions execute all six production methods with arp/output doubles,
covering real heap deletion, nested reset, retired entry, chords, glide note-offs
and live/idle cleanup. All 36 native suites and `./dbt build relwithdebinfo` pass.
Owner-preserving replacement of an arp instruction and safety inside generation
or output callbacks remain separate concerns; these tests do not simulate hardware.

Kit-wide cut/choke loops now watch the kit and each current drum and check live
membership before following the next link after a voice callback. Six paired
sanitizer regressions execute both production loops and real membership lookup,
covering live traversal, heap deletion, same-address replacement, detachment and
retired entry. All 36 native suites and `./dbt build relwithdebinfo` pass. Voice
callback internals and rendering-list traversal remain separate lifetime work.

Kit bend-range routing now checks kit/clip/drum lifetime, row identity,
parameter-set association and song/panel context after expression allocation.
Unsupported range indices are rejected before array access. Fifteen sanitizer
regressions execute the production route and real membership lookup with allocation
and model doubles, covering destruction, replacement, allocation failure,
automation preservation and context changes. All 36 native suites and
`./dbt build relwithdebinfo` pass. The production expression-creation helper still
publishes into its manager after allocation without an owner guard: this is a
confirmed inner-callback gap, not closed by the new caller checks.

Expression creation now accepts an allocation-free synchronous owner validator,
checks it before owner access and after allocation, and discards private storage
when validation fails. It also rejects collection-layout changes or nested
expression publication during allocation, preserving the callback's manager state.
Kit bend-range routing supplies its clip/drum/row context validator to this inner
helper. Six new native parameter-lifecycle regressions run the real helper,
collections and allocator hooks with sanitizer/allocation accounting: owner heap
deletion, expired entry, nested creation, different/same-offset layout replacement
and normal reuse. All 36 suites and `./dbt build relwithdebinfo` pass. Unguarded
callers still require a live-manager contract; pointer snapshots alone do not
identify same-address collection replacement, and other expression-creation callers
have not all adopted owner validation.

Note-row edit-context validation now uses `find_note_row_from_id`, a non-creating
lookup that returns null for absent outputs, missing melodic rows and invalid kit
indices. Validation can no longer allocate a replacement row or freeze merely
because a kit row disappeared. Three sanitizer tests execute the production lookup
with array doubles; an edit-context regression verifies missing-row validation
never invokes the creating lookup. All 36 native suites and
`./dbt build relwithdebinfo` pass. This changes validation only; callers that
intentionally create rows retain `getNoteRowFromId`. Detached owner lifetime still
needs explicit watches in the edit context.

Note-row edit contexts now hold clip and output lifetime watches. Validation stops
before song/row lookup after owner retirement or destruction, including unpublished
clips and same-address replacement; output reassignment is rejected before lookup.
Six new sanitizer regressions exercise the production context with watched model
doubles for detached deletion, clip/output address reuse, output deletion and
retired entry. Existing bulk-edit/undo collaborator tests also pass. All 36 suites
and `./dbt build relwithdebinfo` pass. These watches do not retain owners or protect
song identity reuse and callbacks inside individual edit operations.

`ParamSet::getParam` now supports the same synchronous owner validation before
pool allocation and before slot publication. New objects remain private until
validation and slot/layout checks pass; nested slot creation is preserved rather
than overwritten, and cancelled objects return to the pool. Six real native
parameter/pool regressions cover owner deletion, nested slot creation, expired
entry, nested scalar edits, allocation failure and existing-slot reuse. All 36
suites and `./dbt build relwithdebinfo` pass. Unguarded callers still require owner
lifetime; adopting this guard at recording/UI allocation sites remains work.

Polyphonic expression recording now supplies row/collection validators to both
expression-set and AutoParam creation. It watches clip/output lifetime, validates
row identity and model-stack/song/panel association, and rechecks the parameter
slot after note-edit UI callbacks. Final validation permits legitimate scalar-slot
release after a successful write. Sixteen sanitizer regressions execute the real
recording body with model/parameter doubles, covering deletion at allocation/UI/
write boundaries, replacement, invalid dimensions, retargeting, normal recording
and scalar release. The allocation implementations are separately covered by the
real native lifecycle tests above. All 36 suites and `./dbt build relwithdebinfo`
pass. Automation write internals and rollback of already-applied writes remain
open; cancellation after a write preserves that prefix.

Clip repeat/chop, independent-row halving and the nested row-length setter now
watch clip/output lifetime before follow-up access. Their row validation uses the
non-creating lookup, and row-length/halving callbacks also preserve the initiating
panel. Six new sanitizer regressions exercise the production edit bodies across
repeat/chop/halving variants for unregistered owner deletion, output deletion,
address reuse, retired entry, non-creating validation and deletion during row
resume. All 36 suites and `./dbt build relwithdebinfo` pass. Changes already applied
before cancellation remain a prefix; parameter-repeat internals and full edit
rollback are not established by these caller checks.

The kit MIDI/gate arp-render batch now watches kit/clip/drum lifetime and validates
row identity/count, drum membership, active-clip association and song/panel context
after generation and every output dispatch. Note-on status is published before
the callback. Fifteen sanitizer regressions execute the production renderer with
arp/model doubles, including destruction at each dispatch boundary, row replacement,
retargeting, nested reset and normal multi-row MIDI/gate output. All 36 suites and
`./dbt build relwithdebinfo` pass. Owner-preserving instruction replacement remains
an audit gap: surviving owners alone do not identify which generated arp event is
still current. Kit tick/render siblings and the outer audio-render caller remain
separate work.

The outer kit audio renderer now watches kit/active-clip lifetime and validates
song, panel and model-stack association after pre-arp and audio callbacks before
continuing to later stages. Stem rendering publishes `renderedLastTime` only after
validation. Ten sanitizer regressions execute the production caller with render
and model doubles, covering both FX/stem paths, deletion, retargeting, retired
entry and no-active-clip rendering. All 36 suites and
`./dbt build relwithdebinfo` pass. The shared effects renderer still has its own
post-callback accesses, and backup/collection/recorder lifetime inside render
stages remains a separate concern.

The shared effects renderer now validates output/clip lifetime, parameter-manager
ownership, collection and recorder association after delay setup, source rendering,
FX, stutter and recording callbacks. No-active-clip backup managers are re-resolved
before dereference. Ten production-body sanitizer regressions cover cancellation,
heap deletion and normal rendering; the outer kit caller also tests recorder
replacement. All 36 native suites and `./dbt build relwithdebinfo` pass. These
checks do not establish safety inside the called helpers, same-address collection
identity, or recorder deletion without clearing its owner association. Independent
mode remains disabled.

Audio-output render entry now rejects missing model context, retired output/clip,
and mismatched clip ownership before looking up parameters. Two production-body
sanitizer regressions cover entry rejection, active/backup rendering and output
destruction in the final dispatched callback. All 36 native suites and
`./dbt build relwithdebinfo` pass. This adds caller entry protection; it does not
extend the lifetime guarantees inside audio rendering helpers.

Kit backup audio rendering now initializes its absent-row index and skips row
parameter ticking when no active clip exists, even if the caller's activity flag
is set. Two production-body sanitizer regressions cover backup rendering and
preserved active-row interpolation. All 36 native suites and
`./dbt build relwithdebinfo` pass. Callback invalidation inside this inner renderer
remains separate from these no-clip fixes.

The inner kit audio renderer now watches kit/clip lifetime and validates the
song, panel and timeline association after voice-stop, drum-render and parameter-
tick callbacks. Regression sweeps destroy both owners at each boundary and check
that clip retargeting cannot tick a replacement clip. All 36 native suites and
`./dbt build relwithdebinfo` pass. This stops traversal on owner invalidation;
mutations of drum/row lists while those owners survive remain separate work.

Inner kit render traversal now watches the current and next drum and rechecks the
next index after callbacks, preserving normal self-removal of a finished drum.
Parameter ticking checks drum lifetime, row count, pointer and identity before
continuing. Five sanitizer regressions cover self-removal, current/next drum
destruction, next-entry replacement, row removal and same-address row identity
change. All 36 native suites and `./dbt build relwithdebinfo` pass. Acquisition
still assumes the render list contains live pointers; changes deeper in a list,
parameter-collection identity and callback internals remain open. Timing overhead
still needs hardware measurement.

Inner kit rendering now rejects missing backup parameters and revalidates the
rendered row identity/drum mapping or backup-manager association after sound
rendering. Parameter ticking also detects row drum reassignment. Four sanitizer
regressions cover row deletion, missing/deleted backup managers and reassignment.
The test fixture resets backup association between cases. All 36 native suites
and `./dbt build relwithdebinfo` pass. These are boundary checks, not protection
inside sound rendering or collection ticking; same-address backup replacement
still needs identity tracking.

Parameter-manager sample ticking accepts a synchronous owner validator, supplied
by kit row ticking and the shared effects renderer. Guarded calls validate before
entry and after each collection callback, then compare the captured collection
layout before advancing. Five production-body sanitizer tests cover live guarded
and unguarded traversal, destroyed/expired owners and changed/deleted collections.
All 36 native suites and `./dbt build relwithdebinfo` pass. This protects traversal
between collections only; individual collection loops, same-address collection
reuse and unguarded callers retain their existing lifetime requirements.

Guarded sample ticking now forwards combined owner/layout validation into parameter
and patch-cable collections. Their loops validate after notifications; stale
scalar slots are skipped, and patch-cable compaction cancels the remaining batch.
Seven native parameter regressions execute real collections, automation and pool
code with notification consumer doubles, including actual owner deletion. An
additional manager regression checks forwarded layout/owner validation. All 36
native suites and `./dbt build relwithdebinfo` pass. Notification internals,
unguarded callers, tick-based automation and same-address collection/cable reuse
remain separate audit work. Hardware timing remains unmeasured.

The kit pre-render arp route now checks kit/clip/song/panel ownership after
generation and note-off callbacks, and validates drum membership and row identity
before continuing. Bounded noncreating row lookup rejects negative and excessive
indices. Seven production-body sanitizer regressions cover the live three-event
sequence, destruction at generation/dispatch boundaries and row/drum invalidation.
All 36 native suites and `./dbt build relwithdebinfo` pass. Generated instruction
storage can still change while these owners survive; event revision protection
and generation internals remain open.

Arpeggiators now expose an instruction revision, invalidated at the eleven known
public generation/reset/note/removal entry points. Kit pre-render note-off dispatch
checks it before reusing the remaining instruction. A sanitizer regression frees
the pending note while kit/clip/drum owners survive; production-header/pending-
method tests check revision behavior, with source contracts for all entry points.
All 36 suites and `./dbt build relwithdebinfo` pass. This does not protect mutation
inside generation, direct state writes outside those entry points, or other
instruction consumers until they adopt the check. Revisions require a live owner;
they do not replace lifetime watches. Hardware timing remains unverified.

MIDI/gate drum note-on and note-off batches and the kit non-audio render loop now
check instruction revision after each output callback, after establishing owner
lifetime. Three production-body regressions sweep note-on/off and glide/off/on
boundaries while replacing the event without deleting its owners. All 36 suites
and `./dbt build relwithdebinfo` pass. Kit tick routing and kill-voices cleanup
still need instruction-aware cancellation; generator internals and other direct
state mutation remain outside this batch check.

MIDI/gate kill-voices cleanup now uses a stop helper that reports callback
cancellation. It does not reset a replacement arp event after an interrupted
note-off batch. Two regressions retain the drum while replacing its event and
assert that cleanup performs no additional reset; existing live/retired/destruction
cases remain covered. All 36 native suites and `./dbt build relwithdebinfo` pass.
Output events already sent remain a prefix; this is not note-output rollback.

Tick-driven kit arp routing now validates kit/clip/song/panel, row identity/drum
membership and instruction revision through kit-level and per-drum generation and
dispatch. Non-audio note status is published before callbacks. Ten production-body
regressions cover sound/MIDI/gate live routing, all six dispatch boundaries,
generation destruction, event replacement, row/membership/model-stack changes,
invalid entry and bounded row indices. All 36 suites and
`./dbt build relwithdebinfo` pass. Cancellation retains already-sent output and
returns the existing no-next-event sentinel; hardware rescheduling behavior still
needs verification. Sound voice-start internals, generation internals and broader
render callers remain separate work.

Sound post-arp note starting accepts caller validation, supplied by kit tick
routing. It publishes note status before voice creation, copies the three MPE
values into local storage and validates after each start before touching another
instruction. Six production-body sanitizer tests cover owner/note deletion,
nested reset, voice-budget deferral, live chords, retired entry and already-playing
notes. All 36 native suites and `./dbt build relwithdebinfo` pass. Voice-start
internals and unguarded sound-render/instrument callers remain separate work;
this does not make the entire sound renderer lifetime-safe.

Sound-instrument tick routing now watches output/clip lifetime and validates song,
panel, model-stack/parameter association and arp revision between note-off events
and through the guarded sound note-start loop. Six sanitizer regressions execute
both production bodies together with model/voice doubles, covering live chords,
generation/output destruction, freed instructions with surviving owners, retargeting
and collection replacement. All 36 suites and `./dbt build relwithdebinfo` pass.
Sound render routing and MIDI/CV instrument routes remain distinct audit work;
callback internals and hardware rescheduling/timing are not established here.

MIDI/CV instrument render, tick and direct-note routing now share instruction
dispatch with pre-callback note publication and post-callback validation of output/
clip lifetime, song/panel, channel/type and instruction revision. Clipless direct
notes and CV note-release glide remain supported. Ten production-body sanitizer
regressions cover live modes, every dispatch boundary, generation/owner deletion,
freed pending storage, channel retargeting and nested reset. All 36 suites and
`./dbt build relwithdebinfo` pass. Concrete MIDI/CV output-method internals and
remaining sound-render consumers still require their own lifetime audit.

Concrete CV note-on output now validates output/clip, song/panel, channel/modes
and arp revision after pitch setup and note output before publishing further
state/output. Velocity is snapshotted, note indices are bounded, and negative MPE
pitch conversion uses defined multiplication. Seven production-body sanitizer
regressions cover live/clipless output, deletion at all three boundaries, freed
instructions, retargeting, retired entry and the negative pitch limit. All 36
native suites and `./dbt build relwithdebinfo` pass. CV helper internals and physical
output timing still require separate verification; emitted output is not rolled
back on cancellation.

Concrete MIDI note-on/off and the MPE output sequence now watch output/clip lifetime,
song/panel, routing settings and arp revision across output callbacks. MPE output
reports cancellation to its callers, snapshots expression values, and bounds member
channels; note-on snapshots velocity and rejects invalid zone ranges. Fourteen
production-body sanitizer regressions cover all three methods together, including
real heap deletion, pending-note replacement, zone/channel/collapse changes,
shared-channel averaging, internal/mono routes and stable values. All 36 suites
and `./dbt build relwithdebinfo` pass. Sent events/cache writes remain a prefix;
MPE averaging/collapse internals and other MIDI output batches remain separate
work, as do direct state writes bypassing arp revision invalidation.

MIDI all-notes-off now watches output/clip lifetime and validates song/panel,
channel/zone configuration and arp revision after each channel output. It includes
the master channel, bounds the zone and preserves replacement events on nested
callbacks. Six sanitizer regressions cover both zones, mono/clipless behavior,
deletion, event/configuration replacement and retired/invalid entry. All 36 native
suites and `./dbt build relwithdebinfo` pass. A cancelled sweep can have already
sent a prefix; downstream transport/hardware delivery is not rolled back.

Non-audio polyphonic expression routing now validates output/clip lifetime,
song/panel, channel, arp revision, note count and the current note address after
each chord output. It rejects invalid dimensions/characteristics and preserves
exact-note versus channel matching. Seven production-body sanitizer regressions
cover live matching, owner deletion, freed/replaced notes, channel changes and
invalid input. All 36 suites and `./dbt build relwithdebinfo` pass. Same-address
raw note replacement and callback internals remain separate lifetime work; an
already written expression or emitted event is not rolled back.

MIDI post-arp expression output now rejects invalid dimensions, note indices and
member channels before array access, and retired/reassigned owners before routing.
Shared-channel negative pitch conversion uses defined multiplication. Five
production-body sanitizer regressions cover those guards, negative averaging,
unchanged-value suppression, all three output dimensions, final-callback deletion
and mono/internal behavior. All 36 suites and `./dbt build relwithdebinfo` pass.
Borrowed note acquisition and direct same-address replacement remain unproven;
this entry validation does not make upstream raw pointers safe to acquire.

Clip MIDI bank/sub-bank/program output now watches clip/output lifetime, validates
MIDI output type and rechecks routing, song/panel and selected program fields
between sends. Six production-body sanitizer regressions cover ordering, all
unset-field combinations, deletion at every boundary, routing/program changes,
retired/missing/wrong-type outputs and sending from an inactive clip. All 36 suites
and `./dbt build relwithdebinfo` pass. The sequence may emit a prefix on cancellation;
clip activation callers and transport delivery remain separate boundaries.

MIDI clip activation now watches target/output lifetime and revalidates clip,
model-stack, song/panel and channel after base activation and program output.
Deactivation uses a cancellation-reporting note sweep and stops expression resets
after owner/context loss or a replacement arp event. Nine production-body sanitizer
regressions cover live activation/deactivation, program suppression, base/program/
expression deletion, stack retargeting, cancelled sweeps and invalid targets.
All 36 suites and `./dbt build relwithdebinfo` pass. The return value still describes
the base clip change, not transactional success; completed changes are retained.
Base/helper internals, bend-range arithmetic and other activation routes remain
separate review work.

CV clip activation now validates target/output lifetime and clip/model-stack,
song/panel, channel and CV modes after base activation before reading expression
parameters. Six production-body sanitizer regressions cover caching, deactivation,
missing parameters, base/final callback deletion, eight retargeting cases and
invalid targets. All 36 suites and `./dbt build relwithdebinfo` pass. Current voltage
remains unchanged during activation; the base change result is preserved on
cancellation. Base activation and pitch-helper internals remain separate work.

CV mono/poly expression handlers and final modulation output now validate the
expression dimension and output/clip ownership before indexing or updating state.
Six production-body sanitizer regressions cover invalid dimensions, retired or
reassigned owners, inactive notes, pitch/modulation/pressure behavior, saturation,
mode filtering and clipless final-callback deletion. All 36 suites and
`./dbt build relwithdebinfo` pass. Pitch/voltage helper internals and acquisition of
borrowed clip pointers are still separate boundaries.

MIDI mono-expression/collapse entry points now reject invalid dimensions and
retired/reassigned owners. Pitch collapse uses defined negative conversion,
saturates before float-to-integer conversion and treats a zero main bend range
as zero polyphonic contribution. Nine production-body sanitizer regressions cover
those cases, averaging/max selection, unchanged output, clipless/arp behavior,
final-callback deletion and zero-range activation. All 36 suites and
`./dbt build relwithdebinfo` pass; the added activation regression also passes the
targeted lifetime suite. Parameter acquisition and transport internals remain
separate work; these checks do not establish full sound-render safety.

Sound-instrument output rendering now watches output/clip lifetime and validates
song/panel, recorder, model-stack, clip collection layout and row count before
continuing after rendering. Clip and row sample ticks receive owner validation;
row ticks also check row address/identity before advancing. Nine production-body
sanitizer regressions cover live/skipped rendering, deletion after render and at
each tick, routing/layout changes, row removal/replacement, playback gating,
interpolation flags and invalid entry. All 36 suites and
`./dbt build relwithdebinfo` pass. The actual sound renderer, voice traversal and
same-address collection replacement remain separate inner boundaries.

Sound render arp generation/dispatch now runs through a guarded helper, checking
caller ownership after generation and arp/settings identity plus instruction
revision after each note callback. Synth and kit render callers supply lifetime,
model-stack and parameter-layout validation; kit validation also re-resolves its
row/backup manager. Seven production-body sanitizer regressions exercise live and
pending modes, generation/output deletion, freed replacement instructions, settings
changes, rejected owners and voice-budget deferral. Two kit regressions cover
collection and model-stack replacement; a source contract verifies render uses the
guarded helper. All 36 suites and `./dbt build relwithdebinfo` pass. Sound LFO/
patching, delay, voice and effects internals remain separate audit work, along with
same-address raw replacement and hardware timing/stack validation.

Sound render's effects tail now validates caller ownership after bitcrushing,
effects, stutter, reverb/volume processing, compression, recorder feed and render
reassessment before later work. Seven production-body sanitizer regressions cover
all eight callback stages, cancelled output/state publication, rejected entry,
recorder state and compressor/delay branches. A source contract verifies renderer
wiring. All 36 suites and `./dbt build relwithdebinfo` pass. Earlier effect/reverb/
recorder work is not rolled back; initialization may retain its prefix. Voice
traversal, pre-arp patching/delay stages, helper internals and nested use of shared
render buffers remain open.

Sound voice rendering now checks caller lifetime plus voice-vector storage, count
and current pointer after each render/release callback. Finished voices are detached
before destruction, with validation before continuing cleanup. Eleven production-
body sanitizer regressions cover normal cleanup, empty/retired entry, deletion,
erase/replacement/reallocation, context changes, null slots and destructor-driven
owner/container changes. All 36 suites passed; the final null-slot regression and
render wiring contract pass targeted suites, and `./dbt build relwithdebinfo` passes.
Same-address voice reconstruction, equal-size mutations that preserve the current
slot, voice/destructor internals and other voice-list consumers remain open. This
change does not establish hardware audio equivalence or timing.

Sound render modulation and delay setup now validate ownership between global
LFO, DX, sidechain, patching and delay callbacks. LFO/sidechain results publish only
after validation; DX patch/type replacement cancels the sequence. The no-voice
render reassessment also checks ownership before continuing. Eight production-body
sanitizer regressions cover live/unchanged/unpatched paths, each modulation deletion
boundary, rejected result publication, DX replacement, rejected entry and delay
feedback/finalization. Wiring contracts cover renderer dispatch. All 36 suites and
`./dbt build relwithdebinfo` pass. DSP callback internals, shared scratch buffers,
voice identity reuse and hardware behavior remain unproven.

Voices now expose a non-owning lifetime watch and retire it before destructor
callbacks. The render loop checks this watch, so reconstruction at the same address
cannot impersonate the previous voice. Four added sanitizer regressions execute
production destructor/watch bodies with cleanup doubles and cover early retirement,
render/release reconstruction, fresh replacement watches and retired entry. All 36
suites and `./dbt build relwithdebinfo` pass. Each voice now stores a lifetime source;
aggregate heap/stack/timing impact still needs hardware measurement. Other voice
consumers and same-size changes elsewhere in the voice list remain open.

Synth/drum expression entry points now reject invalid dimensions/characteristics
and retired owners. MIDI aftertouch uses a guarded shared helper that snapshots
chord notes, requires an exact synth-note match and checks owner/routing/arp revision
between sends; drums also validate kit lifetime/membership. Nine production-body
sanitizer regressions cover matching, smoothing/channel output, owner/kit deletion,
freed arp storage, routing changes, invalid entry and disabled MIDI. All 36 suites
and `./dbt build relwithdebinfo` pass. Voice expression setters are scalar-only and
were not given synthetic callback boundaries. Direct arp writes bypassing revision,
borrowed-pointer acquisition and downstream MIDI internals remain open.

Sound-drum note-on now snapshots MPE values before kit choking and revalidates
drum/kit/clip lifetime, membership, row identity, model-stack, song/panel, mode and
arp revision before starting the note. Six production-body sanitizer regressions
cover stable copied MPE, deletion of each owner, ten context/row changes, normal
polyphonic/standalone paths, null MPE/final-callback deletion and rejected entry.
All 36 suites and `./dbt build relwithdebinfo` pass. Sound note-on internals and
borrowed parameter-manager acquisition remain separate boundaries; choking already
performed on other drums is not rolled back.

Direct sound note-on now snapshots input MPE before arp generation, publishes note
status before voice creation and validates caller ownership, parameter/settings
association and arp revision before continuing. Synth and drum callers supply
clip/row/model-stack validation. Ten added production-body sanitizer regressions
cover copied input, deletion at rewind/generation/voice boundaries, replacement
instructions/parameters, voice-budget deferral and sender ownership/row changes.
All 36 suites and `./dbt build relwithdebinfo` pass. Voice-start and generator
internals, direct note-off and raw parameter acquisition remain separate work;
completed note starts and status publication are retained on cancellation.

Direct sound note-off now validates caller ownership, model-stack/settings and arp
revision after generation and each glide/regular release. Synth note-off forwards
its owner guard; drum note-on/off share context validation while note-off preserves
its clipless/null-manager path and does not choke. Eight added production-body
sanitizer regressions cover live release, deletion/replacement/retargeting and both
callers' validator propagation. All 36 suites and `./dbt build relwithdebinfo` pass.
Voice release internals, all-notes-off/voice-clear cleanup and borrowed acquisition
remain separate work; released notes are not restored on cancellation.

Sound voice clearing now requires caller validation, watches each released voice
and detaches voices before destructor callbacks. Synth/drum overrides validate
ownership and arp revision; drum reset only follows successful clearing, preserving
replacement arp events on cancellation. Ten added production-body sanitizer
regressions cover release/destructor deletion, vector and same-address replacement,
rejected entry, caller propagation and reset suppression. All 36 suites and
`./dbt build relwithdebinfo` pass. Destructor/voice-release internals and all-notes-off
menu callers still require separate checks; completed releases are retained.

Arp mode/preset menus now share a guarded application path. Clip/output/drum
watches and editor/session/parameter context checks cancel settings writes after
note-stop or render-reassessment callbacks. Sound all-notes-off reports cancellation
and avoids resetting replacement arp instructions. Twenty-two added production-body
sanitizer regressions cover successful synth/kit edits, inactive clips, deletion,
retargeting, rejected entry and reset suppression; routing contracts follow the
shared implementation and check both menu callers. All 36 suites and
`./dbt build relwithdebinfo` pass. Inner MIDI/voice-release callbacks and propagation
of partial cancellation from void kit/drum/MIDI stop APIs remain open; completed
note releases are not rolled back. Independent mode remains disabled.

Sound post-arp MIDI note-off now checks caller lifetime, MIDI routing and arp revision
between sends, publishes cleared slots before callbacks and preserves replacement
events on cancellation. All-notes-off can resume across holes left by a completed
prefix. Cancellation propagates through direct note-off, render, synth/kit tick and
all-notes-off reset callers. Fifteen added production-body sanitizer regressions
exercise MIDI boundaries, drum mapping, sparse retry and caller cancellation;
a routing contract checks MIDI validation precedes voice access. The full 36-suite
run passed except a new kit fixture count corrected to include its three preceding
kit events; both affected suites pass on rerun. `./dbt build relwithdebinfo` passes.
Voice-release traversal/internals and raw arp mutations bypassing revision remain
open; MIDI already sent is not rolled back. Independent mode remains disabled.

Post-arp voice release now watches each voice and checks owner/arp/settings context
and vector storage after release and legato callbacks. Mono/legato fallback copies
MPE input before callbacks; mono permits its expected voice-list replacement while
still validating ownership. Fourteen added production-body sanitizer regressions
cover matching/released voices, deletion, vector changes, same-address reuse,
settings changes, MIDI cancellation and mono/legato/one-shot behavior. All 36 suites
and `./dbt build relwithdebinfo` pass. Voice/sample release and voice-start internals
remain separate work; completed releases are retained, and unrelated equal-size
voice-list edits are not comprehensively detected. Independent mode stays disabled.

Learned pitch-bend fanout now passes each sound drum's note-row index, matching CC
routing and preventing parameter lookup from falling back to the clip-level
manager. Four added production-body tests verify row indices, skipped drums,
message-used aggregation and whole-kit clone retargeting. All 36 suites and
`./dbt build relwithdebinfo` pass. This standalone routing correction also applies
to ordinary single-device use; callback lifetime protection for the fanout and
learned-parameter handler internals remains open.

Kit learned CC/pitch-bend fanout now shares lifetime/context validation for kit,
source/current clips, current drum and row identity. It reacquires rows after
legitimate arrangement-clone retargets and preserves the message-used prefix on
cancellation. Sixteen added production-body sanitizer regressions cover both
routes, whole/row deletion, same-address drum reuse, detachment, row/session/song
changes, clone retargeting and clipless routing. All 36 suites and
`./dbt build relwithdebinfo` pass; the four final boundary tests also pass in the
affected suite. Learned-parameter handler internals remain open, including the
contract that a retargeted model stack returns a live clip; unrelated equal-size
row edits and partial parameter-write recovery are not comprehensively protected.
Independent mode remains disabled.

Clip learned-CC/pitch-bend handlers now validate caller ownership, source clip/output,
knob storage/binding and returned clip/row/model-stack context around cloning and
parameter lookup. Kit forwards its whole-kit and drum-row predicates into these
handlers. CC display metadata is copied before parameter notifications; cancellation
stops display work and later knobs. Eighteen added production-body sanitizer tests
cover deletion, rebinding, clone failure/retargeting, retired targets and notification
boundaries; existing parameter-input tests retain their value/step-edit assertions.
All 36 suites passed; after moving retired-target validation ahead of row lookup,
all three affected suites and `./dbt build relwithdebinfo` pass. Borrowed target
acquisition, parameter lookup/write internals, clipless owner protection and song-level
learned handlers remain open. Applied parameter writes are not rolled back.

Song now exposes allocation-free lifetime watches and retires them at the start of
its destructor, before audio servicing and clip/output cleanup. Learned kit/clip
MIDI routes use song watches so retirement and same-address song reconstruction
cancel work even if `currentSong` retains the same address. Seven added sanitizer
regressions exercise the production destructor/accessor and route cancellation;
existing song teardown tests still cover cleanup behavior. All 36 suites and
`./dbt build relwithdebinfo` pass. Other song consumers have not yet adopted these
watches; this neither pins songs nor closes raw acquisition, callback internals or
undo ownership/recovery blockers. Independent mode remains disabled.

Song-level learned CC now validates the owning song watch, global-effectable/model-
stack identity, session and knob storage/binding around lookup, notification and
display callbacks. Display metadata is captured before parameter writes. Ten added
production-body sanitizer regressions cover automation/performance display, owner
and parameter deletion, same-address song replacement, rebinding and rejected
contexts. All 36 suites and `./dbt build relwithdebinfo` pass. The enclosing playback
MIDI dispatch still requires cancellation checks; lower parameter lookup/write
internals and partial-write recovery remain open. Independent mode stays disabled.

Learned MIDI routes now check song clip registration before acquiring lifetime
watches on replacement model-stack targets. Initially registered sources must
remain registered; replacement clones must remain registered through lookup/write
validation. Unchanged detached sources retain their existing watch-based path.
Six added sanitizer regressions cover freed replacement pointers and registration
removal, while live arrangement-clone retarget tests still pass. All 36 suites and
`./dbt build relwithdebinfo` pass. This relies on the owning registry removing clips
before destruction; other acquisition paths and the enclosing playback dispatch
remain open. Independent mode stays disabled.

Playback CC/pitch-bend/aftertouch now share guarded fanout across MIDI follow,
song learned parameters and output delivery. Song/current-next output watches,
main-list membership, clip ownership and session checks cancel stale continuation;
replacement clips are registered before watch acquisition. Input bounds precede
cable-array access, and nested/cancelled pitch dispatch restores its previous flag.
Twenty-two added production-body sanitizer regressions cover routing behavior,
MPE defaults, learning/consumption, deletion/reuse/detachment, clone retargets and
registration/active-clip changes. All 36 suites and `./dbt build relwithdebinfo` pass.
Note-on/off and learned-command internals still require checks; cable lifetime and
hardware timing remain unverified. Completed deliveries are retained; independent
mode stays disabled.

Playback note-on/off now uses the shared guarded MIDI dispatcher, with song/session
validation around MIDI-learn and learned-command callbacks. Eight added production-
body sanitizer regressions preserve muted note-on filtering, always-delivered
note-offs, recording flags and MIDI-learn fallback, and cover deletion and malformed
input. All 36 suites and `./dbt build relwithdebinfo` pass. Learned-command traversal
and individual command/output internals remain open; completed note delivery is not
rolled back and cable lifetime/hardware acceptance remain unverified.

Playback learned-command traversal now validates song lifetime and session ownership
after global commands, section switches, clip toggles and rendering callbacks. Clip
watches and current-slot checks protect switching, while deliberate clip deletion
and index adjustment by toggleClipStatus remain supported. Program-change learning
also validates before fallback. Twenty-one production-body sanitizer regressions
cover live command ordering, deletion/reuse, traversal changes, learning and deferred
undo scheduling. All 36 suites and `./dbt build relwithdebinfo` pass. Individual
command internals, equal-size list reordering, recovery and hardware acceptance
remain open; completed commands are retained and independent mode stays disabled.

MIDI follow's four top-level event batches now watch song lifetime before selected
and per-track dispatch. Three regression cases exercise all four event types and
reject retired entry, retirement during track delivery and same-address song
replacement during selected delivery. All 36 suites and the RelWithDebInfo firmware
build pass. Inner selected/track routes still need their own song-lifetime checks;
this outer guard only cancels continuation after they return. G1 remains open.

Selected and per-track MIDI-follow note/CC routes now watch song lifetime within
their own callbacks and loops. Six regressions cover same-address replacement,
retirement after parameter/note delivery and retired entry, preventing remaining
note releases or CC activation/delivery. All 36 suites and the RelWithDebInfo build
pass. Parameter/feedback internals, expression activation and other acquisition
paths remain to audit; completed deliveries are retained. Independent mode remains
disabled.

MIDI-follow parameter input and feedback sweeps now include song lifetime in their
existing callback predicates. Six regressions cover retirement/reuse during clone,
lookup, write and feedback delivery plus retired entry. All 36 suites and the
RelWithDebInfo build pass. Replacement-clip acquisition still needs registry
validation before acquiring a watch; lower parameter internals and partial-write
recovery remain open. Independent mode remains disabled.

MIDI-follow active-clip acquisition now rejects missing/retired songs and validates
song lifetime after activation. Selected pitch-bend/aftertouch routes validate song
and session after activation, then song/clip/output lifetime after instrument
delivery before returning a target. Seven regressions cover retirement, same-address
replacement, session changes and deletion during delivery. All 36 suites and the
RelWithDebInfo build pass. Specific-track expression acquisition and lower activation
services remain open; these checks do not undo an already delivered expression.

Specific-track pitch-bend/aftertouch now reject retired songs and require main
output-list membership before acquiring the target output watch. Three regressions
cover detached targets, freed unregistered pointers and retired song entry; the
live kit fixture now explicitly registers its output. All 36 suites and the
RelWithDebInfo build pass. Registry unlink-before-destruction remains a prerequisite;
this does not close general raw-pointer acquisition or lower expression internals.

MIDI-follow CC arrangement-clone retargets now require song registration before
watch acquisition and during subsequent parameter/display checks. Initially
registered source clips must remain registered; unchanged detached sources keep
their existing watch-based path. Four regressions cover freed replacement pointers
and registration removal during cloning, lookup and writing; existing live clone
tests pass with explicit registration. All 36 suites and the RelWithDebInfo build
pass. This depends on registry unlink-before-destruction and does not close other
acquisition paths or partial-write recovery. Independent mode remains disabled.

MIDI-follow note delivery now rejects retired songs at entry and validates song,
session, clip and output before returning the cached routed output after instrument
callbacks. Four regressions cover song reuse, clip deletion, output reuse and retired
entry. All 36 suites and the RelWithDebInfo build pass. Note-retention cleanup still
relies on the existing deletion notifications; completed note events are retained.
Nested routing cancellation propagation and lower instrument internals remain open.

Shared arpeggiator mode/preset edits now require a live song and include its
lifetime in the existing stop/reassessment validation. Three regressions cover
same-address song replacement during stop, retirement during reassessment and
missing/retired entry. All 36 suites and the RelWithDebInfo build pass. Other menu
callbacks and internal note-stop services remain open; independent mode stays
disabled.

The MIDI-follow parameter-lookup wrapper now validates optional model-stack song,
clip/output lifetime, output association, stack song identity and UI owner before
inspecting a lookup result or displaying an error, and again after error display.
Nine callback/entry scenarios run against the compiled production lookup code in
both diagnostics configurations, covering retirement, deletion, address reuse,
retargeting and error-display deletion. All 36 suites and the RelWithDebInfo build
pass. Parameter storage/layout invalidation inside a still-live owner and deeper
lookup services remain open; caller ownership is still required at entry.

Independent mirror transmission, input dispatch, Remote UI service/root startup and
snapshot preparation now watch song lifetime across their callbacks. Remote readiness
rejects retiring songs; visible mirroring preserves its existing song-change behavior.
Seven runtime regressions cover packet/input/encoder callbacks, timers/rendering, root
construction/installation/opening and retired entry, including same-address reuse.
All 36 suites and the RelWithDebInfo build pass. Deferred client-start requests still
need lifetime tracking across service turns; hardware acceptance, persistent session
cancellation and the broader blockers remain open. Independent mode stays disabled.

Deferred client startup now retains an allocation-free song lifetime watch across
service turns and discovery waits, rejecting same-address replacement before
requeue or takeover. Cancellation/completion clears the watch. Stable-address
watches can now reset/rebind without becoming movable; Song exposes observation
into such a watch. Nine regressions cover intrusive-list rebinding, song teardown,
queued/discovery-wait replacement, discovery/cleanup callbacks and retired entry.
All 36 suites and the RelWithDebInfo build pass. This closes the deferred-start
song-address gap, not active independent-session lifetime or hardware acceptance.

Host sessions now retain their starting song watch. Independent session liveness
and Remote readiness reject song replacement/retirement between service turns;
visible mirroring keeps its existing song-change behavior. Independent teardown
clears held controls without dispatching them into a replacement song and stops a
release batch if a callback invalidates its song/owner. Six runtime regressions
cover inter-turn transmission/input, timer/render suppression and teardown; the
missing-song test now requires permanent session cancellation. All 36 suites and
the RelWithDebInfo build pass. This closes inter-turn independent song binding,
not per-object lifetime/recovery or hardware acceptance. Independent mode stays
disabled; cleanup of the retired song's voices remains owned by song teardown.

Modulation selection, encoder/button handling, mode lookup, individual/batched knob
indicators and automation menu refresh now watch optional song lifetime across
callbacks. Eleven regressions cover same-address reuse and retired entry, including
the first indicator publication and direct automation-timer render path. The native
parameter lifecycle target links the extracted production Song accessor used by
the real indicator renderer. All 36 suites and the RelWithDebInfo build pass.
Live-song child-object/parameter ownership and whole timer-batch cancellation remain
open; these checks preserve existing no-song contexts. Independent mode stays disabled.

Timer dispatch now watches its initiating song for the whole batch, stopping after
replacement/retirement (including same-address reuse) while retaining later timer
deadlines. UI/back-menu retries and graphics rearming reject invalidated song
contexts; graphics also rejects client takeover. Four regressions exercise both
panels, retired entry, preserved pending work and valid no-song service. All 36
suites and the RelWithDebInfo build pass. This closes the whole timer-batch song
cancellation gap; live-song child ownership, recovery and hardware acceptance
remain open. Independent mode stays disabled.

Timer callback continuation also rejects client takeover before rearming UI/back
menu retries. Automation feedback only publishes its timestamp/reset while the
initiating song and panel remain valid and client takeover has not started; a
cancelled callback cannot overwrite replacement feedback state. Three regressions
cover retry takeover on both panels, Local-owned feedback cancellation during
playback/stopped service, and unchanged-context success. The new failure cases
reproduced before the fix. All 36 suites and the RelWithDebInfo build pass. These
checks cover timer continuation, not nested callback cancellation or cable/object
lifetime within MIDI transmission. Independent mode remains disabled.

The shared encoder dispatcher now retains its song/panel/UI context across each
handler, rejects retired/missing contexts before consuming ticks, and stops after
song reuse, navigation replacement, owner changes or client takeover. Invalidated
handlers cannot restore card-routine retries or overwrite mod input state; later
ticks remain pending. Seven new EncoderDispatch regressions compile the production
dispatcher with real encoder counters/banks and lifetime watches, covering both
panels, vertical/horizontal retry, mod continuation, no-song menus and SD deferral.
Three cases failed before the fix. All 37 suites and the RelWithDebInfo build pass.
This covers the shared dispatch boundary, not handler-internal object ownership,
same-UI navigation revisions or nested cancellation propagation. G1/L1 remain open;
independent mode remains disabled.

Button dispatch now retains song/panel/UI context, rejects invalidated UI results
before playback fallback or card retries, and validates playback/recording callbacks
before consuming record holds. Popup cancellation cannot redirect the original
button into a replacement UI. Missing/retired/client contexts still record release
edges without dispatching actions. Nine ButtonDispatch regressions compile the real
function with callback doubles; four reproduced failures before the fix. Both
panels, normal playback/recording, no-song menus and retry behavior are covered.
All 38 suites and the RelWithDebInfo build pass. Handler internals, same-UI retargeting
and nested cancellation remain outside this boundary fix. G1/L1 remain open and
independent mode stays disabled.

Seven navigation entry points (open/close, level/sideways/root changes and low-level
root setup/swap) now watch optional song lifetime before mutation and through
resolver, greyout, open/focus and rendering callbacks. Same-address replacement
cannot publish a resolved old target or restore/focus the old UI after a rejected
open. Seven UIOpen regressions cover both panels, retired entry and no-song menus;
five failed before the fix. All 38 suites and the RelWithDebInfo build pass.
Already-published navigation is preserved on cancellation rather than rolled back
against a changed song; reconciling that partial transition remains R4. UI-object
lifetime and rendering entry points are still separate audit work. Independent
mode remains disabled.

Greyout queries, redraw requests and grid/OLED rendering now watch optional song
lifetime through callbacks. Cancelled passes reject stale masks and stop further
layer queries/publication; grid/OLED requests remain pending. Direct rendering and
continuations also reject client takeover. Ten UIOpen regressions cover greyout,
redraw visibility, image clear/scroll/render/send boundaries, shared refresh,
retired entry and no-song success. Six failed before the fix. All 38 suites and the
RelWithDebInfo build pass. Already-transmitted pixels cannot be rolled back; these
checks do not pin UI/model targets inside callbacks or close recovery/hardware
gates. Independent mode stays disabled.

Pad dispatch now validates its song/UI/panel before invoking a handler or returning
an SD retry, restores the caller's panel, and still records releases for invalid
contexts. Four regressions extend ButtonDispatch with extracted production pad
handling/pressed-state lookup, covering same-address reuse, navigation/owner/client
changes, retired entry, no-song retry, bounds and stem-export behavior. Two failed
before the fix. All 38 suites and the RelWithDebInfo build pass. Multi-control
release sweeps and the outer physical-input continuation still need auditing;
this is single-event cancellation, not complete held-input recovery. Independent
mode stays disabled.

Loaded-song UI setup now validates the current song's lifetime and initiating
panel/UI around lazy view access, root installation and opening. Cancelled root
installation cannot open the previous UI, and invalidated opening cannot queue a
redraw. Six regressions use the real setup/navigation methods with fixtures for
all six root choices, same-address reuse, failed resolution, owner/UI/client
changes and null/retired/noncurrent entry. Three failed before the fix. All 38
suites and the RelWithDebInfo build pass. The caller's broader load/clear transaction
and already-published root recovery remain separate concerns. Independent mode
stays disabled.

The physical PIC input reader now retains song/panel/UI context through mirror
routing, pad/button dispatch and the pad-to-button release-sweep boundary. Stale
continuations cannot replay an edge or update Shift feedback. Shift use is marked
before a pad callback so normal navigation consumes it without overwriting modifier
state established by that callback. Seven regressions exercise the real reader with
UART/transport doubles, including valid storage retry, mirror consumption, no-song
protocol handling and restoration of a suspended Remote caller. Three failures
reproduced before the fix. All 38 suites and the RelWithDebInfo build pass. Internal
multi-control release sweeps and recovery of remaining holds are still open; no
hardware PIC/USB acceptance is implied. Independent mode stays disabled.

Song sample-loading scans now watch source/current-song, output and clip lifetime
across storage/audio callbacks, check output registration, and reject changed clip
counts or positions before continuing. Both full and crucial loading avoid cached
range ends across list mutation. Fifteen sanitizer-enabled regressions cover actual
output/clip destruction, retirement, same-address song reuse, detached loading,
owner changes, resized/same-size replaced clip lists and existing audio cadence/
active filtering. Four failed before the fix. All 38 suites (including ASan/UBSan
ClipLifetimeTests) and the RelWithDebInfo build pass. Cancellation leaves already
loaded samples intact; this does not make loading transactional or protect inside
individual holder/source/drum loading callbacks. Those inner paths remain L1/R4
audit work. Independent mode stays disabled.

AudioFileHolder loading now retains reference-counted original/working filename
strings across storage callbacks (no extra path-buffer allocation), validates owner
cancellation when supplied, and rejects concurrent path/type/file replacement before
publication. AudioClip supplies clip/song/output/panel validation and shares the
resolved name only while that context remains valid. Thirteen sanitizer-enabled
regressions exercise the real holder/clip methods with reference-counted string and
storage doubles: destroyed clip/output, lookup/assignment cancellation, concurrent
selection, owner changes, normalization, normal errors and cached/empty paths. Two
failures reproduced before the fix. All 38 suites and the RelWithDebInfo build pass.
This does not yet propagate validation inside SampleHolder::setAudioFile,
SampleHolderForClip::setAudioFile, claimClusterReasons/claimClusterReasonsForMarker,
or Source/Drum loading. Legacy callers without a validator still require their
own lifetime contract. Cached files remain owned by AudioFileManager; cancelled
loads do not acquire a holder reason. Independent mode remains disabled.

Sample assignment now propagates cancellation through clip/voice holder setup and
marker cluster acquisition. Acquisition pins the source Sample and releases temporary
cluster reasons before releasing that pin when a callback cancels, destroys the
holder, or replaces its selection. Voice loop markers are validated across start
and loop acquisition. Thirteen additional sanitizer regressions cover destruction,
nested replacement, marker edits, successful ownership transfer, partial loading,
and cancellation propagation into clip loading; the loop-marker regression failed
before its fix. All 38 suites pass, the final added propagation test passes in
ClipLifetimeTests, and the RelWithDebInfo build passes. Already-published sample/start
clusters remain holder-owned on later cancellation; this is not transaction rollback.
No sample buffers are duplicated. Source/Drum/Kit validator propagation and legacy
callers without explicit lifetime contracts remain open. Independent mode stays disabled.

Kit full/crucial sample-loading traversal now validates Kit, current Song, panel,
Drum membership/lifetime and active Clip context after callbacks. Crucial loading
also checks row count, row position and drum assignment before continuing. Thirteen
sanitizer regressions cover actual Kit/Drum/Clip destruction, detached drums, changed
rows, context retirement, filtering and existing directory/error behavior. The
detached-drum regression failed against the previous implementation. All 38 native
suites and RelWithDebInfo pass. Same-address row replacement, nested alternate-path
ownership, and cancellation inside Source/Drum loading remain separate audit work;
these traversal checks do not establish those inner contracts. Independent mode
stays disabled.
