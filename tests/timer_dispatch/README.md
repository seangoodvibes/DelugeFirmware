# Remote timer dispatch boundaries

`TimerDispatchTests` compiles the production timer manager header, timer storage,
and extracted dispatch/scheduling methods. UI, display, playback and hardware
callbacks are lightweight fixtures.

The cases cover absent Remote navigation, navigation closed during a callback,
preservation of pending timers, replacement screens, Local hardware timers and
the client OLED hardware handshake. These tests do not establish lifetime safety
inside individual callbacks or whole-session teardown/reclamation.

Retry cases cover both UI-specific and exit timers on both panels: a departed UI
cannot implicitly rearm its old event, normal retries remain active, and a new UI's
explicitly scheduled deadline is preserved. Pointer comparison is not a lifetime
pin and does not detect destruction followed by same-address replacement.

Local navigation-loss cases cover UI-specific, exit, root-note flash and graphics
callbacks. Hardware input servicing continues, expired one-shot callbacks are
consumed, and periodic graphics service resumes when navigation returns.

Wraparound cases service both empty banks at the simulated starting time, then
check exact-deadline behavior, distinct panel deadlines and Remote deferral across
UINT32 wrap. They do not simulate an unserviced bank idle for more than half the
32-bit clock range; signed deadline comparisons require a separate audit there.

The idle-bank regression additionally schedules without first servicing the bank
and verifies delivery after wrap on both panels. The timer-state suite checks
expired-cache recomputation preserves genuinely overdue events. Active events
left unserviced for more than half the clock range remain outside this coverage.

Owner-change fault injection checks dispatch stops at the callback boundary,
restores the caller, rejects retries despite matching UI pointers, and preserves a
peer graphics deadline. It establishes containment of an unbalanced owner change,
not that ordinary scoped callbacks leak ownership or that their internals are safe.

A takeover-during-input case verifies later timers remain pending, the next client
pass services only the OLED handshake, and ordinary timers resume after client
mode ends. The transition is injected; USB negotiation remains in MirrorRuntimeTests.

Automation display cases compile both the timer's direct automation-view path and
`View::displayAutomation`'s fallback. They check the follow-up menu read against
owner, UI, root, menu and song changes, client takeover, missing menus and normal
reads. Both panel owners are exercised. Additional cases call the fallback directly
to verify owner restoration and remove a menu during indicator updates. A menu-read
callback that changes owners leaves later timers pending in the original bank.

The real view method uses fixture indicator updates, and the automation-view
renderer itself remains a fixture. These tests do not retain or destroy real menu
objects and do not establish safety inside the callbacks.
