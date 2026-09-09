# Native parameter lifecycle tests

`NativeParameterLifecycleTests` runs on the host through CTest and exercises the
firmware's nullable parameter arrays and shared `auto_param_pool`.

From the repository root:

```sh
cmake -S tests -B build/tests
cmake --build build/tests --config Debug --target NativeParameterLifecycleTests
ctest --test-dir build/tests -C Debug -R '^NativeParameterLifecycleTests$' --output-on-failure
```

To check object lifetimes with AddressSanitizer on a supported Clang/GCC host:

```sh
cmake -S tests -B build/tests -DPARAMETER_LIFECYCLE_ASAN=ON
cmake --build build/tests --config Debug --target NativeParameterLifecycleTests
ctest --test-dir build/tests -C Debug -R '^NativeParameterLifecycleTests$' --output-on-failure
```

Set `PARAMETER_LIFECYCLE_ASAN=OFF` to return to a normal build.

## Production code covered

The target compiles the real `ParamSet`, `AutoParam`, `auto_param_pool`, `ParamNodeVector`, resizable
node containers, patch cables and destination sorting, manager setup/transfer/cleanup, collection notifications, lookup,
and `ConsequenceParamChange`. It also compiles the production editor region-edit
helper, knob-indicator method, and parameter classification functions. These
implementations are not replaced with test adapters.
The merged `tests/param_manager` targets remain useful for manager layout, lookup,
and routing contracts; their parameter/automation doubles do not exercise the
ownership covered here.

The cases cover:

- Initial scalar values, neutral-value queries, and neighboring parameter isolation
  for patched, unpatched, and expression sets.
- Scalar edits through the production setter and user-input path, including
  change notifications and capturing the pre-edit value for undo.
- Repeated scalar, first-node creation, and last-node deletion undo/redo through
  production consequences; scalar preservation and automation/interpolation flags.
- Ownership transfer into a steal-data undo snapshot and back to the parameter,
  including destruction of the consequence after restoring the nodes.
- Discarding a steal-data undo snapshot without restoring it, releasing its nodes
  while preserving neighboring automation and allowing new automation on the owner.
- Append through both collection and manager APIs for every scalar-only/automated
  source and destination combination, forward and reversed, with source destruction
  and mutation proving independent node ownership; reversed step segments.
- Forward and ping-pong repeats, partial final repeats, and interpolation across
  a newly inserted loop-boundary node.
- Sparse collection traversal through seeking, ticking, stopping, time insertion,
  cloning, and clearing, preserving every unautomated scalar and the clone's nodes.
- Stealing and reinserting wrapped node regions, including the last nodes,
  destination replacement, truncation, disposal of temporary storage, and undo/redo.
- Expression region moves across the loop boundary in both directions, with
  untouched dimensions and undo/redo preserving values and nodes.
- Multiple outstanding undo snapshots for the same parameter, repeated undo/redo,
  and discarding older snapshots while another snapshot or the parameter owns nodes.
- A fixed 36-step sequence interleaving scalar/node edits, deletion, cloning,
  undo/redo, snapshot disposal, and owner destruction/recreation. An independent
  expected-state model checks every parameter and live snapshot after each step,
  including flags and unique ownership of node addresses. Failures identify the
  sequence step; final cleanup must release every tracked firmware allocation.
- Allocation-failure sweeps for forward and ping-pong append/repeat: every allocation
  position reached by these fixtures is failed before retrying without the fault.
  Surviving nodes remain ordered, flags match automation, and retry matches the
  successful operation's output.
- Actual manager cloning of unpatched and expression sets, with and without
  automation, editing and destroying the source before inspecting the clone.
- Reversed manager cloning with independent node storage.
- Cloning over a populated manager, releasing its old nodes while retaining its
  own expression collection.
- The patched collection's actual `beenCloned()` hook rebinding its scalar storage
  and cloning its nodes, after a raw copy matching the manager's cloning contract.
- `AutoParam::cloneFrom()` preserving the destination patched set's scalar binding.
- Forward and reverse seeking, tick interpolation, and stopping playback, with
  values observed through `ParamSet::getValue()`.
- Clearing all automation and creating it again; deleting time that removes the
  last nodes while another parameter retains automation.
- Summary-bit boundaries at parameter IDs 31/32 and the final valid parameter.
- Real XML/JSON `ParamSet` save/reload through production serializers and readers,
  at every simulated cluster alignment, with scalar or automated destinations,
  scalar-only saving, disabled automation loading, replacement nodes, a following
  sentinel attribute, and preservation of the source and neighboring parameter.
- Failed first-node allocation followed by retry; failed collection allocation
  during cloning; failed/partially successful node cloning; failed node allocation
  during reload; failed node capture without modifying the source; and partial
  insertion failures retaining their temporary records for retry.
- Failed undo snapshot allocation refusing both undo and redo without changing the
  edited owner, neighbors, flags, or notifications, even after memory recovers.
  Scalar-only and steal-data snapshots still work when allocations are disabled.
- Trimming away automation, deleting a region's last nodes, wrapping region
  deletion, full replacement paste, and undo/redo of those changes where applicable.
- Failed replacement paste clearing stale automation/interpolation flags.
- Patched consumer-threshold callbacks observing the final scalar and automation
  state, and expression edits reaching the monophonic notification callback.
  Full-region deletion must notify each consumer only once.

Every case checks that no active pooled objects remain, drains the idle cache,
and verifies that all host-tracked firmware allocations are released. Invalid
or duplicate frees abort the test. The tests caught stale clone flags both when
automation copying was disabled and when individual node allocations failed. The manager initializes destination
flags before cloning; `ParamSet::beenCloned()` clears flags for missing automation.
They also caught a wrapping-region deletion index error, stale flags after a failed
paste, and duplicate full-region deletion notifications. Append coverage caught
missing destination automation flags and manager traversal skipping previously
empty destination collections. Transfer and failure-path coverage also caught
stale flags after stealing the last nodes, wrapped rightward moves applying the
leftward adjustment too, and duplicate ping-pong boundaries on append retry.
Those paths are corrected.

## Platform boundary and limits

The platform fixture replaces allocation with tracked host allocations and an
explicit failure budget returning `nullptr`, as the firmware allocator does.
In-place extension/shortening is declined, allowing the production containers to use their
normal allocation/copy paths. This does not test the firmware allocator or its
physical memory regions.

The timeline is deterministic, with a configurable loop length, position, and
direction. A concrete test timeline exercises short-loop recording at a length
below and equal to the recording clear-ahead window, with an unpressed shift
button and no new action. It verifies scalar update, one notification, cleared
flags, and release without heap allocation. UI notifications are counted. The
action-logger hook creates a real `ConsequenceParamChange` for a scalar edit;
tests replay consequences directly.
This does not exercise the UI action queues, action grouping, live recording,
actual Clip/NoteRow scheduling, or audio rendering. Unsupported action and sound callbacks throw rather than silently succeeding.
Patch-cable fixtures explicitly enable a sound boundary that accepts destinations
and permits value-change notifications; destination setup and sorting remain real. Region-deletion tests
explicitly allow the action logger to decline a new action and use independently
captured production consequences. File services reuse the native persistence
fixture: buffered reads are production code; file bytes and writer output are in
host memory, without SD access. A type-only reverb header avoids an unrelated SIMD
dependency from `Song`'s headers; no Song or Reverb instance is created.

Patched/expression construction, scalar storage, and notification dispatch are
real. The patched observer deliberately rejects the value-change threshold, so
sound LPF/rendering remain outside coverage. Expression
coverage exercises monophonic dispatch, not actual MIDI output or polyphonic
NoteRow dispatch. Patch-cable cases exercise the full patched manager layout and its cloning path.
The separate native persistence target retains its broader parser fault matrix.

### Existing failure semantics

These tests do not promise transactional rollback. If node cloning fails after
collection allocation succeeds, the manager currently reports success and keeps
the scalar with any nodes it could clone. Its summary flags must match those nodes.
If reload or replacement paste runs out of memory, old automation may already have
been removed; a loaded scalar and completed replacement nodes may remain. Parser
recovery after a failed insertion is not guaranteed by the lifecycle suite.

`ConsequenceParamChange` remembers whether its snapshot-node clone succeeded.
An incomplete snapshot returns `Error::INSUFFICIENT_RAM` on undo or redo without
swapping values or nodes. A later return of available memory cannot repair that
missing history; a fresh snapshot is needed. The existing action logger displays
reversion errors and clears the logs. Edits are not rejected at snapshot creation,
and a grouped action may have reverted earlier consequences before encountering
an error; this suite does not promise transactional undo for an entire action.

Node capture returns an error and retains all source nodes if its temporary
allocation fails. Reinsertion remains nontransactional: it can remove old nodes
and insert only part of the replacement, but reports the failure, updates flags,
and leaves the caller-owned temporary record intact for retry. The note-editing
callers display transfer errors. Entire note moves across multiple expression
dimensions or rows are not rolled back as a transaction.

## Pooled automation contract

Patched, unpatched, and expression sets start with null automation slots and keep
scalar values separately. Non-creating lookup and scalar access do not acquire an
object. `getParam(id, true)` and creating model-stack lookups reserve an object and
can return a null `autoParam` on allocation failure. Legacy editing callers can
reserve an object before adding nodes; scalar notifications release that reservation.
Use `getValue`, `setCurrentValueBasicForSetup`, or `set_current_value` for scalar-only
operations that must not allocate. An explicit reservation without a notification
remains owned by the set until released or the set is destroyed.

One firmware-wide pool grows on demand and caches at most 32 released objects.
Release destroys the object and its nodes; reuse constructs fresh state and binds
to the new owner's scalar. The pool is not a fixed limit on automated parameters.
AddressSanitizer builds poison cached object storage (apart from the free-list
link), so stale accesses can fail even before the allocator frees a cached block.

Pool-specific cases check allocation-free scalar notifications, reuse across all
three set types, scalar binding isolation, reset playback/override state, heap
exhaustion and retry, use of cached storage while heap allocation fails, cache
bounds, and destruction. Deleting the last node must clear the slot. Undo may need
to acquire a new object; failure leaves the owner and snapshot untouched so a
retry remains possible. Both forward and reversed node-clone failures return the
new object without releasing the source. Whole-loop scalar replacement and failed
first-region edits exercise release after notification, with undo or retry.

The editor interpolation preference is a small platform hook. Native region-edit
tests supply the non-editor preference; they do not instantiate the full firmware UI.

The actual editor region helper is tested through whole-loop deletion, reuse of
the returned block by a neighboring parameter, and repeated edits using the same
model stack. The actual `View::setKnobIndicatorLevel` method is tested with a
non-creating lookup and captured LED output for pan and volume at minimum, zero,
and maximum values. Parameter classification and value conversion remain real;
no LED hardware is accessed. Quantized stutter and missing patch-cable display
branches are outside these cases.

Separate XML and JSON cases allow temporary node parsing to succeed, then fail
the AutoParam allocation. They check the failed allocation size, scalar value,
flags, cleanup of temporary nodes, the following sentinel attribute, and a
successful reload after memory recovers. A pool unit case fills the idle cache to
exactly 32 entries, drains it twice while another object owns automation, and
verifies that the active object's value/nodes survive further acquisitions.


## Patch-cable pooling

Each cable keeps its scalar strength and polarity independently of its nullable
pooled AutoParam. Audio patching and cable-list displays read the scalar directly.
Creating lookups can fail; non-creating lookups return null for scalar-only cables.
Each set stores a packed array of nullable cable pointers. Reordering swaps
pointers, so cable addresses and their scalar bindings remain stable until deletion.

Host cases cover all 32 scalar-only slots, grouping/reordering and compaction,
slot reuse, final-node deletion, scalar and automation undo, pool exhaustion and
retry, cloning with and without nodes, allocation-failure sweeps, inactive
range-adjusting cables, first-automation append, trim/nudge cleanup, removal of
all cables to a destination, and shared pool reuse with ordinary parameter sets.
XML/JSON round trips cover scalar strengths, separate parent/range polarity,
node inclusion/exclusion, replacement cleanup, and the following sentinel field.
The real sound acceptance policy, audio rendering, and hardware timing are outside
these host tests.

Undo reacquires a pooled object for an existing cable whose automation was
removed. As before, an automation consequence cannot recreate an entirely deleted
route: it has no snapshot of route metadata such as polarity, and returns an
error for a missing cable. Cable, cable-automation, and destination-map cloning
failures now propagate through the manager: a distinct destination is retained,
and a failed shallow manager clone is detached from the source. Existing
ParamSet/MIDI clone fallback behavior is unchanged. Loading remains
nontransactional: an unallocatable route is skipped, and any pending range cables
are released if their parent cannot be allocated.


The shared `patch_cable_pool` grows on demand and retains at most 32 unused cable
blocks. Releasing a cable destroys its AutoParam ownership before caching the raw
storage. A fresh set allocates no cables, and the per-set limit remains 32 routes.
Preset defaults use `setup_cable`, which reports allocation failure without
incrementing the count or leaving a hole; default construction can retain the
routes that fit. Audio traversal only dereferences already allocated cables.

Additional pool cases check stable addresses through grouping and compaction,
reuse by another route, inactive-route cleanup, bounded caching and cache draining,
full-set rejection, releasing an AutoParam reservation when cable allocation fails,
clone-failure rollback with a populated destination, shallow-manager clone failures,
and XML/JSON parent-allocation failure after a range cable was allocated.
AddressSanitizer poisons unused cached cable storage except its free-list link.
