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
