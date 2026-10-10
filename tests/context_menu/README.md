# Context-menu shared-value routing

`ContextMenuTests` compiles the real launch-style menu implementation and header,
plus the production base encoder and seven-segment draw methods. Session ownership
and peer-refresh state are production code; Clip, display and localization are
lightweight fixtures.

Coverage includes same-clip edits before deferred refresh, both display types,
different-clip navigation, unchanged boundary values and absent targets. This does
not prove clip lifetime across callbacks or real hardware rendering.

The audio-input group also compiles the production AudioInputSelector. Tests cover
live channel reads, pad/encoder peer notifications, Track-source refresh, source
repair, mode locks and rejected inputs. Audio monitoring claims are mocked and
not validated by this target.

Storage-lock coverage verifies audio-source pad presses defer without changing
model or UI state on either panel, releases remain handled, and retry resolves a
changed pad target after unlock. This does not prove retained output lifetime.

Target-membership cases cover departure and reattachment on both panels, missing
songs, wrong output types and absent roots during greyout. Edited audio outputs
are registered in the fixture song list. These checks do not establish lifetime
across rendering/monitoring callbacks or detect same-address object replacement.

Launch-style target checks compile the production song-membership body against
fixture clip lists. They cover departed/reattached clips, arrangement-only clips,
and missing/replaced songs, without claiming callback-spanning lifetime protection.

Injected seven-segment callbacks exercise launch-style removal during refresh or
encoder feedback, menu retargeting, and song/owner changes. Pending edits cancel
before subsequent model access; these tests do not prove allocator reachability
or protection against same-address reuse.
