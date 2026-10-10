# Runtime-setting menu routing

`RuntimeMenuTests` compiles the production developer SysEx menu implementation
and header, with the production UI session and shared-value cache. The Selection
base, settings store, random source and localization are lightweight fixtures.

Tests cover per-panel pending codes and retained option strings, shared commit
and disable behavior, and label refresh when options are queried before values.
The fixture's commit method models the base menu's cache invalidation; these are
not end-to-end input, rendering or SysEx authorization tests.

The battery group includes the production menu header and real timer ownership.
It covers independent sampling windows, re-entry, charging status, voltage bounds
and owner-specific OLED redraw requests. Battery ADC acquisition and actual OLED
pixel transport are not part of these fixtures.
