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
