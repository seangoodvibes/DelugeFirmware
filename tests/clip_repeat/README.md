# Clip repeat regression tests

This isolated native executable extracts and compiles both production
`InstrumentClip` repeat entry points. Note rows, parameter generation, playback,
and model stacks are controlled doubles; this is not a full firmware UI test.
ASan/UBSan follow `UNDO_TEST_SANITIZERS`.

Coverage includes invalid clip lengths, an invalid later independent row,
overflow in doubled/rounded independent targets, large repeat-count arithmetic,
successful rounding, and row failures stopping later rows and clip-level commits.
It does not simulate object destruction inside callbacks, repair edits already
applied to earlier rows, or cover arrangement clone ownership/cleanup.

The suite also extracts production `Song::doubleClipLength`, exercising its call
into the production increase method. Failure and overflow must suppress output
length notifications, tick-scale updates and playback resync; success must retain
those effects. Existing increase cases assert success/failure return values.

Multiply-redo tests extract the production consequence and execute its chain into
production song doubling and clip increase. Failure must return `Error::BUG`;
success must double and notify. Halving is doubled and is not covered by these tests.

Multiply-context tests exercise production consequence entry checks for absent or
wrong-type targets and halving lengths that would become invalid. They verify
rejection before the doubled parent/row setters and acceptance of valid lengths;
they do not test real halving or callback lifetimes.

The halving helper is now extracted production code. A controlled row setter error
must reach the multiply consequence and stop later rows. Successful undo checks
actual helper dispatch. Row setters and parent length changes remain doubled;
this is not a transactional rollback test.

The row-length setter is now production code as well. The remaining trim and
playback collaborators are doubled. Direct setter tests cover nonpositive input,
failed trimming before playback-state changes, wrapped position on shortening,
restoration of parent-inherited length and suppression of playback resume.

Production setter context tests reject absent stacks/songs/clips, mismatched rows,
and invalid source lengths without trimming or playback-state changes. The model
stack is doubled; production uses its null-tolerant accessors for these guards.

Halving callback tests now invalidate clip ownership during resume or structurally
invalidate and delete the entire heap clip. Production halving and row setter code
must reject the result before the next loop access; ASan/UBSan check the freed-clip
case. Ownership and playback remain controlled collaborators.

Increase-with-repeats callback tests cover ownership loss, parameter callbacks
changing parent length, and deletion of the heap clip during either row or parameter
repetition. A success case preserves support for unpublished arrangement clones.
The production outer method runs with controlled row/parameter collaborators.

Repeat-or-chop callback coverage checks ownership loss and deletes the heap clip
during row generation, trimming, parameter repetition and base lengthChanged.
The production outer method must stop before the next target access. Callback
collaborators remain doubled; arrangement clone cleanup is not covered.

Repeat-or-chop result assertions now include clip deletion during final playback
resume (a fifth callback boundary). The arrangement caller's guarded cleanup and
failure-before-installation ordering have source-contract coverage only.

Multiply-undo parent-setter callback tests cover active-song/model-stack changes,
ownership loss, selection changes, unexpected length/output changes and remote
structural refresh. A separate test frees the target clip inside the doubled
setter, exercising rejection before the consequence dereferences it again. Entry
checks reject unowned targets for undo and redo. These tests do not exercise the
production parent setter's internal callbacks or provide transaction rollback.

Halving preflight tests check invalid later rows before earlier rows are changed,
and invalid parent lengths even with an empty row array. Multiply-undo tests inject
a later invalid row length and row insertion/removal during the doubled parent
setter, requiring rejection before row halving. Parent rollback and same-count row
replacement are not covered by these guards.

Multiply-undo row snapshots reject same-count identity replacement, reordering,
array relocation and unexpected length changes across the parent setter. Tests
verify allocation failure before mutation, balanced snapshot cleanup and acceptance
of the setter's normal matching-length conversion to inherited. Allocation and
parent setting remain controlled doubles.

Snapshot-allocation return tests invalidate song/stack, ownership, parent length,
row count, later row length and local structural revision. Heap-target deletion is
covered for successful and failed allocation. Tests require no parent mutation and
balanced temporary storage; allocator callbacks remain controlled doubles.

Halving result tests mutate the effective length or row-stack song/clip/row during
playback resume and require failure before later rows are touched. A success case
checks that the requested half-length may be represented by inheriting the parent
length. These tests exercise the production setter with doubled playback callbacks.

Row-setter trim-return tests change song, ownership, identity, source lengths or
row ID and verify no playback-state writes. Separate cases destroy the row or
heap clip during the controlled trim callback. The production setter must reject
before accessing the removed target; full production trim internals remain doubled.

Direct setter resume tests require error propagation for changed length, row-stack
redirection, song/identity changes and row/clip destruction. They exercise the
production setter without relying on the halving caller to detect invalidation.
Playback and trim collaborators remain doubled; no rollback is asserted.

Setter publication coverage publishes an initially unpublished target during trim
and frees it during resume, requiring ownership-based rejection before dereference.
Positive cases retain unpublished edits and successful publication. The song's
ownership query and trim/playback callbacks are controlled doubles.

Repeat publication matrices cover increase, exact growth and chopping. A first-row
callback publishes the target; a later callback deletes it, requiring rejection
before further access. Positive cases complete after publication. Row callbacks
and ownership are controlled doubles; these checks do not provide rollback.
