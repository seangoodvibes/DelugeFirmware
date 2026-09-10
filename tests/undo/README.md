# Undo consequence regression tests

This executable compiles the production `.cpp` files for clip length/shift,
note-array swaps, individual-note existence, and note-row length/shift/mute consequences. It uses the actual
consequence headers, error types, and reversible-shift range helper.

Model collaborators are replaced: `Song`, clips, rows, model stacks, note
storage and playback. The doubles expose selected versus registered clips, row
lookup, mutation counters, clone failure and rejected shifts. They deliberately
do not implement the consequence validation under test.

## Run

From the repository root:

```sh
cmake -S tests -B build/tests -DUNDO_TEST_SANITIZERS=ON
cmake --build build/tests --target UnitTests
ctest --test-dir build/tests --output-on-failure -R '^(UnitTests|UndoConsequenceTests)$'
```

Building `UnitTests` also builds `UndoConsequenceTests`. Running the `UnitTests`
executable alone does not run this separate suite. CTest runs both. The optional
sanitizer setting instruments the new target with address and undefined-behavior
sanitizers on Clang/GCC. Use `-DUNDO_TEST_SANITIZERS=OFF` on unsupported platforms.

## Coverage

| Finding | Direct regression coverage |
| --- | --- |
| Undo follows current selection | Distinct edited and selected clips; clip/row shift, length, mute and note swaps |
| Dereference before membership/type validation | Missing song, unregistered target, deleted target under ASan, wrong clip type |
| Missing row | Absent row and wrong row ID fail without touching the other selection |
| Shift rejection reported as success | Model rejection propagates to consequence error |
| Shift merge uses wrong flags or overflows | Target/flag mismatches, positive/negative accumulation bounds |
| Shift inverse overflow | `INT32_MIN` rejected before dispatch |
| Invalid row lengths | Invalid parent/independent/saved lengths rejected before mutation |
| Retained sample-marker pointer | Start/end resolution, wrong type, invalid marker ID, marker swap before length change |
| Invalid clip-length snapshot | Saved/current nonpositive lengths rejected before marker or length mutation |
| Failed note snapshot applied | Injected clone failure rejected; successful clone and ownership transfer round-trip |
| Successful edits break redo | Before/after round trips preserve targets and saved state |

The initial run demonstrated two failures: invalid saved clip lengths were
accepted, and failed note snapshots could replace live notes with empty storage.
Both now have production guards and permanent tests.

The existing `UnitTests` suite additionally covers protocol framing, MIDI
filtering, acknowledgements, Local/Remote state banks, injected encoder retry and
re-entry, checked shift arithmetic, reserved ring insertion and reversible-prefix
rollback. See `tests/unit/mirror_protocol_tests.cpp` and
`tests/unit/ui_session_tests.cpp` for those cases.

## Explicit gaps

These tests do **not** exercise the full action logger's failed-undo queue cleanup,
real allocation pressure, full Song lifecycle, address reuse, row-ID reuse,
UI shift preflight/history reservation, concurrent callbacks, audio playback,
independent frame scheduling/transport, or USB hardware. The note-vector double
models clone failure; it does not test the real allocator or storage container.
A passing consequence test does not prove that the caller handles its error.

Keep independent mode disabled. Add a failing direct test with each further
fix where the production path can be isolated; retain explicit integration/device
coverage requirements for paths that these doubles cannot establish.


The squash audit additionally compiles Song membership/output-reference methods
and audio-shift preflight verbatim from production sources in `model_tests.cpp`.
The existing consequences now call the production membership method against small
collection doubles. This improves the earlier fake-catalog coverage, but still
does not execute full Song construction, destruction or storage operations.
See [the broader audit](../contracts/README.md).

Row lifetime tests now exercise all four row consequences against a replacement
constructed at the same address/ID, relocation preserving identity, and creation
after a missing-row capture. Production identity allocation is shared by the model
double. Source contracts guard real NoteRow initialization and clip-clone renewal;
these do not execute the real relocating array or clone operation.

`action_snapshot_tests.cpp` extracts the production Action snapshot lookup method.
It verifies row-identity matching, valid-snapshot requirements and linked-list
ordering against real consequence implementations. Only Action/model storage is
doubled; full action reversion, allocator behavior and logging remain integration
gaps.

`note_existence_tests.cpp` runs the production individual-note consequence, Note
fields/accessors and extracted constructor. The extended vector double provides
ordered note storage and deterministic insertion failure. Six cases cover target
validation, stale row identity, note-attribute round trips, allocation failure and
retry, and existing missing-note deletion behavior. Real allocator callbacks and
concurrent changes during insertion remain integration gaps.

Note recreation additionally exercises reservation callbacks that change the song,
remove the clip, replace/relocate its row, introduce a conflicting note or alter
history metadata. Commit failure is independently injectable. These tests check
post-reservation revalidation and mutation ordering, not allocator-internal
lifetime safety; vector storage and allocation remain doubled.

The extracted Action recording method is tested with injected clip-deletion and
allocation results. Five cases check cleanup and preservation of history on
failure, allocation failure, successful deletion and creation-only recording.
The clip-deletion consequence itself is a double in these tests: they verify
caller error handling, not real clip ownership changes or partial-deletion recovery.

Clip-removal lookup tests execute the production Song method against collection
doubles: exact-array membership, foreign/null/freed targets and index changes.
A source contract guards preflight placement in clip-existence reversion. It does
not execute the full deletion path or validate rollback after yielding callbacks.

Six direct `Action::revert` cases now cover missing context, ordinary failure/list
ordering, direction round trips, arrangement-clear failure, old-consequence
cleanup after failure, and skipped parameter replay. Consequences, Action storage,
action-kind symbols and arrangement clearing are controlled doubles; the traversal
and failure-handling method is extracted production code. No full song rollback
or callback-time lifetime protection is implied.

Clip-restoration preflight is extracted from the production consequence and tested
against controlled ownership fields and real Song membership lookup. Four cases
cover detached ownership, duplicate membership and ownership reconciliation,
insertion bounds, and invalid destination arrays. The associated source contract
checks invocation before reservation/reattachment. Full clip restoration and
callback-time ownership changes remain integration gaps.

Six clip-restoration reservation tests execute the extracted production method
with real session revision state and injected array callbacks. They cover normal
reservation/allocation failure, invalid initial ownership, changed songs, returned
ownership despite allocation failure, both panels' structural revisions and changed
indexes. Array allocation and lifetime are still doubled; callback-internal
safety and post-reattachment rollback remain untested integration work.

Four restoration-commit tests cover pointer insertion/ownership transfer, injected
insertion errors, duplicate ownership, and changed song/index. The production
commit method runs against doubled array storage; reservation counters also check
that commit does not reserve. A source contract verifies errors precede peer
notification. Real ring-array behavior and rollback of parameter reattachment are
outside these cases.

Three exact-parameter-backup cases execute the production Song lookup/transfer
method with controlled backup-array and parameter-collection storage. They cover
refusal to consume another clip's/generic backup, lookup without transfer and exact
transfer including expression state. Source contracts require exact lookup in undo
reattachment and error propagation rather than the associated freeze paths. Real
collection transfer, partial-kit rollback and callbacks remain integration gaps.

`kit_restore_tests.cpp` executes production kit-row restoration and exact backup
lookup with controlled rows, collection transfer and trimming. Five cases verify
preflight leaves earlier backups intact when a later one is missing, duplicate
backup-key rejection, successful main/expression restoration, non-sound rows and
missing context. Three additional cases inject song changes, either panel's
structural invalidation, and clip deletion during trimming, verifying restoration
stops before accessing another row or consuming its backup. The kit-level caller
is checked by a source contract. Real collection-transfer callbacks, lifetime
protection inside callbacks and rollback remain integration gaps.

Four reattachment-boundary cases execute the production clip-existence helper
against injected reattachment callbacks. They cover error preservation, ownership
returned to the song despite failure, either panel's structural invalidation and
song/index changes. These validate the boundary before insertion, not real output
reattachment internals or rollback.

Three additional reattachment cases reject missing/foreign stack timelines before
calling the injected restore operation, and reject callback changes to the stack's
song or timeline even when global song and consequence target remain unchanged.

The kit restoration fixture also executes production instrument-clip reattachment.
Three MIDI cases cover invalid-parameter error returns, successful validation,
song/either-panel invalidation, and clip deletion during an injected MIDI restore.
Parameter-type checks and MIDI restoration are controlled doubles, so these tests
validate the caller boundary rather than actual MIDI collection transfer/trimming.

Base `Clip::undoDetachmentFromOutput` now runs verbatim in the undo suite. Three
cases cover context validation before transfer, main/expression restoration,
post-trim song/panel invalidation and deletion during trimming. The clip-existence
boundary tests use a derived clip to inject reattachment independently. Parameter
storage and trim remain doubles; callback-internal safety and rollback are not
established by these tests.

Two exact-backup regressions reject transfer into either the matched backup or a
neighboring backup entry, and reject a null output while preserving generic-backup lookup.
They check main/expression values and array membership remain intact. Transfer
internals remain doubled; these are production API-boundary tests.

Three action-replay cases inject song/stack changes during ordinary consequence
replay, arrangement clearing and arrangement replay. They verify later dispatch
stops, history remains reachable, and arrangement cleanup is deferred at the
invalid boundary. The actual Action method runs with controlled consequences;
outer logger cleanup and callback-time Action destruction remain integration gaps.

Three arrangement-cleanup regressions inject song/stack changes during consequence
cleanup, checking that pending dispatch stops, generated history is retained and
final cleanup invalidation reports failure. Cleanup itself remains a controlled
callback; outer logger cleanup is not covered by these tests.

Two clip-existence cleanup tests execute its production destruction-preparation
method. They preserve clips returned to either song array and verify detached
cleanup removes only that clip's backups and frees it once. Backup removal uses
controlled storage; song lifetime and destruction callbacks remain untested here.

Three note-snapshot recording cases inject allocation callbacks into the production
recording method. They verify song/panel invalidation and row replacement reject
the snapshot before note stealing, release unused storage, and preserve the normal
success path. Clone-internal callbacks and Action destruction remain outside these
cases.

Four snapshot-publication regressions inject callbacks after controlled note
cloning and test clone failure. They verify revision/identity changes and target
deletion reject the unlinked snapshot, while allocation failure preserves existing
history. The real vector allocator and callback-internal source lifetime remain
integration gaps.

Three single-note recording regressions execute the production recorder and
consequence constructor with allocation callbacks. They preserve copied values
after source deletion and reject structural/row-identity invalidation. They do not
validate caller mutation after recording returns or Action lifetime.

Three deletion regressions execute production `NoteRow::deleteNoteByIndex` with
the real recorder: allocation failure and row invalidation stop deletion, while
success records the original note before removal. A source contract checks all
three single-note edit callers return on recording failure. Creation callers are
not executed by this contract; rollback of their already-inserted notes and
failure propagation beyond the deletion wrapper remain gaps.

Deletion tests now assert `INSUFFICIENT_RAM`, `BUG`, and `NONE` for allocation
failure, invalidation, and success respectively. Source contracts verify that
position deletion and both UI callers propagate or stop on the error. These
contracts complement the native index-deletion tests; they do not establish
transactional rollback or execute the complete scrolling/transposition UI.

Three single-note failed-allocation cases verify panel invalidation, row replacement,
and clip destruction after song invalidation return BUG instead of INSUFFICIENT_RAM.
They execute the production recorder with controlled allocation; the deleted-clip
case exercises the boundary under the suite's sanitizers. Plain allocation failure
and successful deletion remain covered separately. Recovery of previously inserted
notes and unannounced object destruction remain outside these tests.

Three creation-recovery tests exercise the production recorder on failed allocation:
remove only the inserted note, locate it after vector relocation/index changes, and
preserve a modified note. These use real Note values with controlled vector storage;
they do not cover rollback of MPE edits preceding insertion or identical-value
replacement without structural invalidation.

Creation-pointer regressions execute successful allocation with vector relocation
and preceding insertion, checking the returned pointer against the current vector.
A modified target rejects publication, clears the output and frees consequence
storage without deleting the note. Source contracts require both creation callers
to receive the refreshed pointer. Full UI execution and row relocation remain gaps.

Row-relocation regressions preserve undo identity while moving the row and freeing
its original storage inside the allocation callback. Creation tests both allocation
outcomes; deletion executes the production delete wrapper to verify it returns
without accessing the released row. Replacement notes remain intact and unused
consequence storage is freed. These sanitizer cases cover announced address changes,
not replacement at the same address or arbitrary clip/Action destruction.

Two production-deletion regressions cover a shifted index after allocation-time
insertion and a note modified during allocation. They verify the correct note is
recorded/deleted or that mutation and publication stop, respectively.

Three array-snapshot regressions cover row relocation with released source storage
before cloning/stealing, relocation during controlled cloning, and failed allocation
with panel invalidation. They execute the production recorder and check rejection,
retained replacement values and unused snapshot cleanup. Real clone-internal
callbacks and comprehensive object lifetime remain outside these tests.

Bulk edit coverage now extracts production addCorrespondingNotes, clearArea,
editNoteRepeatAcrossAllScreens, nudgeNotesAcrossAllScreens,
changeNotesAcrossAllScreens, trimNoteDataToNewClipLength, trimToLength,
complexSetNoteLength, recordNoteOff and clear. Their Action snapshot methods and
consequences are real; vector storage, working allocation, parameter callbacks and
playback scheduling are controlled collaborators.

The failure matrix runs eight edit operations against allocation failure and
structural invalidation, asserting unchanged notes, no playback scheduling, no
published history and balanced working allocations. Success cases check resulting
note counts/values and original undo snapshots. Additional cases cover destructive
trim fallback, missing/empty corresponding notes, clearing before automation,
trim-wrapper propagation, note-off length/lift updates, wrapped length failure,
and source-vector relocation or growth at each recording allocation boundary.
All Note test inputs in the recorder fixture now initialize their payload because
the production Note constructor deliberately leaves fields uninitialized.

SessionRoutingContracts additionally checks all 13 NoteRow array-recording sites,
repeat preflight ordering, outer trim returns and pointer reacquisition after
wrapped length editing. These are source contracts, not complete UI tests.

Working-allocation tests now execute six production bulk-edit paths and their
NoteRowEditContext checks. The matrix releases the original row at each applicable
search-array or replacement-vector allocation (ten boundaries), with both allocation
outcomes, checking BUG, unchanged replacement notes, no history/event publication
and balanced working allocations. Other cases cover source growth/relocation,
clip or independent-row length changes, and song replacement with clip destruction.
The allocator/vector callbacks are controlled doubles; callback-internal allocator
safety and same-storage content changes are not covered by these checks.

Parameter-boundary tests now release rows during clearing/trimming and note-off,
replace the active collection, and destroy a clip after song replacement. Clearing
must not reach later collections or note deletion, and trimming must not schedule
playback after invalidation. The production stopCurrentlyPlayingNote method is now
extracted too: reentry, callback-started playback and silent/already-stopped cases
verify state publication before dispatch. Actual audio dispatch and parameter
operations remain doubles; the tests do not establish their internal safety or
rollback already-completed mutations.

Collection-ownership regressions replace a later collection while the active one
remains unchanged, change the expression collection offset, and replace the clip
output or parameter collection during trimming. They verify context rejection
before clearing replacement data or scheduling playback. The shared production
context captures the entire summary pointer array; callbacks remain controlled
doubles and same-address replacement is not detected.

Repeat-generation coverage now extracts the full production NoteRow method. Six
cases cover parameter callback invalidation/release, vector-repeat failure, normal
and pingpong success, zero lengths and pingpong-resize invalidation. The vector
repeat helper and parameter callbacks are doubles; other native suites cover the
real array allocator separately. A source contract checks both clip callers stop
on false. These tests do not establish rollback of earlier parameter changes or
safety inside callbacks that destroy their own allocation source.

Row repeat tests now cover complete and partial removal of source notes outside the
old loop. They verify no empty-vector tail access and correct retained-source
iteration bounds. NativeParameterLifecycleTests separately runs the real ordered
array repetition and allocator over 1,312 wrapped layouts/end positions plus invalid
inputs, overflow, allocation failure and empty-source cases, checking full payloads
against an independent repetition reference.

Iteration-repeat regressions cover FIRST/LAST preservation (forward, pingpong and
periodic-flattening modes), captured source length after deleting an original note,
a loop/divisor product beyond 32-bit range, and refusal to mutate a neighboring
note when the expected copy is missing. The production repeat method runs with
controlled vector generation; these are transformation/boundary tests rather than
complete playback or transaction-rollback tests.

Repeat capacity regression executes production row preflight with out-of-range
lengths, negative repeat counts, and pingpong capacities exceeding signed element
or byte limits. It checks rejection before allocation/history, parameter callbacks,
direction changes, note changes, or event scheduling.

Pingpong direction regressions run production repeat processing with a failing
note allocation and successful empty/nonempty rows under both parent directions.
They verify that failure retains the original notes/direction and success commits
the expected flattened direction.

Loop-length note regressions cover periodic and FIRST/LAST conditions through
production repeat processing in forward and pingpong modes, explicit periodic
flattening, and successful unconditional drone stretching. They catch the drone
shortcut bypassing iteration processing for a conditional note.

Pingpong source-boundary tests exercise negative, boundary and beyond-loop source
onsets through production row processing and check rejection before side effects.
A successful wrapped-tail test distinguishes valid tails from invalid onsets.

Row-length consequence failure coverage checks propagation of BUG/allocation
errors without exchanging the saved length, followed by successful undo and redo
retries. It executes production consequence code with a controlled setter error.

Direct row-length performChange tests cover missing/mismatched model stacks,
replaced row identities, detached clips and successful retained-target edits.
They execute production consequence validation with the existing setter double.

Row-length callback regression cases invalidate row address, clip membership,
row identity or remote structural revision during the controlled setter. Production
consequence code must reject the result without exchanging its saved length.

Resize-result tests reject callback changes to the parent clip length/output/type,
stack row ID and effective row length before saved-length exchange. A success case
covers the zero independent-length representation of an inherited requested length.

Resize callback lifetime tests actually delete the original heap row or detached
heap clip before the production consequence resumes. ASan/UBSan exercise the
post-call rejection path. The setter and teardown collaborators remain doubles;
these tests do not delete the consequence or owning Action.

Trim event-boundary regressions redirect the model stack during parameter trimming
and delete a row or clip during expectEvent. The extracted production trim method
must reject redirected targets before notification and detect invalidation before
returning success. Parameter/event callbacks are controlled doubles; deletion of
the clip also invalidates the active song context.

Shared row-edit-context ownership tests remove or free a registered clip without
changing the song or structural revisions. Validation must reject before reading
released storage. A production trim regression deletes its registered clip during
the doubled parameter callback. Unpublished-clip publication remains accepted.

Row-edit-context constructor tests reject missing song/clip/row inputs, mismatched
row lookups and wrong clip types. Repairing those inputs later must not make an
invalid snapshot usable. These tests do not establish safety for arbitrary stale
addresses at constructor entry.

Row-edit-context publication tests validate an initially unpublished clip after
registration, then delete it and require safe rejection. Ownership loss remains
invalid after reinsertion. Moving between session and arrangement arrays while
remaining song-owned is accepted. Transitions entirely between validations are
not covered by this cooperative ownership tracking.

Permanent target-invalidation tests restore song, row, identity, length and type
after a failed check and require continued rejection. An invalidated context must
also avoid accessing a subsequently freed unpublished clip. Intentional note-array
resizing still permits target-only validation after full snapshot validation fails.

Metadata invalidation coverage includes parent length, output, expression offset,
each parameter-collection slot and both local/remote structural refreshes. Tests
verify old contexts stay invalid after restoration or refresh consumption while
fresh contexts remain usable.
