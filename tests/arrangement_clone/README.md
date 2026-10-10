# Arrangement clone caller regressions

Compiles the production `Arrangement::doUniqueCloneOnClipInstance` method against
controlled clone/repeat, clip-array and cleanup collaborators. ASan/UBSan follow
`UNDO_TEST_SANITIZERS`.

Tests cover failed repetition deleting an unpublished clone without installing it,
a callback deleting the clone (no double cleanup), a callback publishing it (no
cleanup or duplicate installation), successful installation/notifications, and
capacity/clone-allocation failures. Object deletion is real in the freed-clip test.
Firmware clone internals and Song teardown are not executed here. Changed-context
cleanup of a still-live unpublished clone remains unresolved.

Reservation/clone boundary tests delete the source instance or returned clone after
structural invalidation and ensure no later access/installation. A separate test
changes instance length during reservation and checks cloning never begins.

Successful-repeat callback tests now mutate the source instance, publish the clone,
or structurally invalidate and delete the instance. Success from the repeat helper
must not bypass caller validation or cause duplicate installation. These also
expose the remaining live-clone cleanup gap after structural invalidation.

Range preflight regressions reject invalid lengths and overflowing notification
ranges before reservation, while accepting the keep-length sentinel and exact
signed-position boundary. New test names and local identifiers use snake_case.

Installation-notification tests mutate source length or delete the instance during
either notification. Production caller checks must stop before stale access or a
false success result. Notifications are controlled callbacks; partial installation
rollback remains outside this coverage.

Insertion tests propagate the allocator error with guarded cleanup, avoid double
cleanup after callback deletion, and reject source-instance deletion during a
successful insertion. Clip-array allocation and cleanup collaborators are doubled.

Stable-context publication rollback tests remove unused clones after source
mutation, preserving clones adopted by an arrangement instance or session. The
membership/instance-reference checks and Song teardown are controlled doubles.

Unpublished-clone cleanup tests preserve source-instance or output references
created during repeat/insertion failure, even without clip-array publication.

Output replacement tests reject installation or continuation and skip unsafe
cleanup after repeat, insertion failure or notification callbacks. Original-output
reference recovery and production teardown remain outside these controlled tests.

Source-output tests change the original clip output during reservation, repetition
and notification. The production caller must reject the changed source before
further notifications or installation, preserving the callback's edit.

Successful-clone result tests reject null, source-alias and callback-published
results before any field changes or cleanup. The clone collaborator is controlled;
these exercise the production caller's ownership assumptions.
