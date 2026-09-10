# USB Deluge mirroring

This implementation lets one Deluge act as the panel and controls for another.
Install the same firmware on both units and use matching display types (OLED to
OLED, or seven-segment to seven-segment). Connect them directly over USB, with one
unit operating as USB host. The USB host role and the musical host role are
independent: the unit on which you select mirroring becomes the mirror client.

## Operation

1. Connect the two powered Deluges over USB. Use a USB host adapter on one end.
   Only one USB MIDI device should be connected to the client.
2. Stop playback and recording on the client.
3. On the client, select **Song > Actions > Mirror Connected Deluge**.
4. The client leaves the menu and requests a mirroring session. The other unit
   continues running its song and UI normally.
5. Use either unit's pads, buttons and six encoders to control the musical host.
   The client reproduces pad colours, panel animations/flashes, button LEDs,
   gold-knob indicators, the sync LED and the display.
6. Hold **Back for two seconds on the client** to exit. Back is also forwarded to
   the host, so its normal back/navigation action still occurs. Disconnecting USB
   or losing the peer's heartbeat also ends mirroring. The client returns to its
   own UI; its song is not replaced or automatically started.

Client mode suspends synthesis/effects, sequencing, sample loading/recording and
ordinary UI timers/rendering. Incoming musical MIDI and unrelated SysEx are
ignored. USB/MIDI transport, control scanning, the scheduler, the audio DMA clock
(with silent output) and the OLED/panel drivers remain running. There is no audio
streaming or song synchronization. Analogue controls, including the physical
volume control, are not remotely controllable. Hardware power/battery status is
not part of the mirrored panel protocol.

## Protocol version 2

All traffic uses the official manufacturer header, on USB MIDI cable/port zero:

```
F0 00 21 7B 01 06 02 OP SESSION_LO SESSION_HI SEQ_LO SEQ_HI PAYLOAD F7
```

Session and sequence numbers are 14-bit, least-significant seven bits first.
Each direction has its own sequence, starting at zero and wrapping at 16384.
The client chooses a nonzero session ID. The host binds to the requesting cable
and session; traffic from other cables/sessions cannot operate its controls.
Missing or out-of-order packets terminate the session. This protocol requires a
direct, ordered USB connection; it does not retransmit lost packets.

Payloads use groups of up to seven data bytes, each prefixed by a byte carrying
their high bits (first data byte in bit zero). A decoded payload is at most 196
bytes. Packet framing, version, operation, bounds and command completeness are
validated before dispatch.

| OP | Direction | Decoded payload |
| --- | --- | --- |
| 0 Request | Client to host | Display type: 0 seven-segment, 1 OLED; optional mode: 0 visible host, 1 independent (currently rejected) |
| 1 Accept | Host to client | Empty |
| 2 Stop | Either | Empty |
| 3 Heartbeat | Either | Empty |
| 4 Panel | Host to client | One or more complete, allowed PIC output commands |
| 5 OLED | Host to client | Block 0–5 followed by 128 bytes, or block 6 alone to commit |
| 6 Input | Client to host | Kind, key/encoder index, signed 32-bit little-endian value |
| 7 SyncLED | Host to client | 0 off, 1 on |
| 8 InputAck | Host to client | Input packet sequence, unsigned 16-bit little-endian |
| 9 capability_query | Either, while idle | Empty; session zero, sequence carries query ID |
| 10 capabilities | Query response | Supported mode bitmask (bit 0 visible host, bit 1 independent), display type; echoes query ID |
| 11 request_rejected | Host to requester | Reason 1: unsupported mode; request session token, sequence zero |

Input kind zero uses PIC key indices 0–179 and values 0/1 for release/press.
Kind one uses encoder indices 0–5: vertical, horizontal, tempo, select, lower
gold, upper gold. Deltas are -127 through 127, excluding zero; larger physical
movements are split into ordered packets. Gold-knob acceleration and button
modifiers are interpreted on the host using the normal input handlers.

Version 2 adds a dispatch acknowledgement. Both devices must run compatible
version-2 firmware; version-1 packets are rejected. The client sends one input at
a time and waits for its acknowledgement before sending the next. A host whose
ACK cannot yet be buffered retries the ACK without dispatching that input again.
Wrong, duplicate or unsolicited ACKs end the session. The ACK payload is packed
like other payloads, rather than using the header's seven-bit integer format.
An ACK means the input handler has consumed the input, including any encoder
retry. Other asynchronous work may still be pending. It does not acknowledge a completed frame
or confirm that an edit succeeded. Independent Remote dispatch/frame transport
is still absent.


The initial snapshot includes pad colours and persistent indicator state.
Subsequent panel commands preserve hardware scrolling and flashing, rather than
sampling only the firmware's pad image. Only display-related PIC commands are
allowed; UART configuration, button scanning and OLED SPI handshakes are never
replayed from the peer.

OLED snapshots are sampled at up to 10 Hz. Only changed 128-byte blocks are sent,
followed by a commit. Separate buffers keep queued and active DMA images stable.
Panel traffic and OLED updates are bounded by available MIDI buffer space. A
full event queue or invalid session packet ends the session rather than dropping
control releases or replaying a partial panel command. Heartbeats are sent every
250 ms; the timeout is three seconds. A detected USB disconnect ends it sooner.

Physical and remote held keys are combined on the host. Releasing one source
does not release a key still held by the other. On termination, remote holds are
released without releasing physical host holds. The client suppresses keys held
across entry/exit until their physical release and restores paused UI timers and
its own panel settings on exit.

## Validation

Automated coverage in `tests/unit/mirror_protocol_tests.cpp` exercises every
payload length and byte value, packet framing/version rejection, truncated PIC
commands, forbidden hardware commands, decoder bounds and input index/delta
limits. The firmware must also pass the release build.

Hardware acceptance remains required; a successful build does not establish USB
throughput, visual timing, audio silence or electrical compatibility. Test:

- Both assignments of the USB-host role; OLED pairs and seven-segment pairs.
- Entry while stopped, rejection during playback/recording, absent peer,
  mismatched displays and simultaneous attempts to enter client mode.
- All pads/sidebar pads, every button, all six encoders, encoder presses,
  Shift combinations, fast turns and chords.
- A shared key held on both units: release each source in both possible orders.
- Host playback, pad scroll/zoom/flash, blinking buttons, meters and OLED popups.
- USB unplug and local exit while holding audition pads and modifiers: verify
  no stuck notes, no lost host holds, and normal client operation afterward.
- Repeated entry/exit, sustained dense animations, host SD operations, no client
  audio output, and no client playback from incoming MIDI/analogue clock.

Panel traffic may end a session if sustained output exceeds transport capacity.
The practical refresh rate and congestion limits must be measured on hardware.

## Current completion gates

The enabled feature is mirroring the host's visible UI. Independent native UI is
still disabled; the isolation work below must not be treated as a working second
screen. The detailed entries later in this document record incremental changes,
not separate remaining-task lists.

| Area | Current state | Required before enabling independent mode |
| --- | --- | --- |
| USB routing | Negotiated mirror connection filters non-SysEx packets, including queued packets | Verify both USB roles on hardware; an already in-flight packet cannot be recalled |
| Navigation and rendering | Separate native UI instances, navigation/input/timer state and software display/pad/LED banks | Audit remaining mutable singleton fields and wire remote rendering to transport |
| Song and clip view state | Per-panel selections, scroll, zoom, keyboard, automation selection, wrap editing, affect-entire and session layout | Audit shared playback decisions that currently depend on layout and remaining view fields |
| Shared model lifetime | Identity checks and structural refresh notifications at selected mutation boundaries | Protect retained targets during deletion, replacement, row relocation and callbacks that yield |
| Undo and import | Session-aware history, full-action failure discard, reversible note-array prefixes and song-owned detached outputs | Finish arrangement-recording recovery, early output reclamation and interleaved pattern-preview transactions |
| Independent protocol | Not implemented; existing protocol controls the visible host UI | Negotiate independent sessions; ordered input/acknowledgements; Remote frame transfer; stale-session rejection, releases and resynchronization |
| Device integration | Host build and native unit tests only | Two-device concurrent editing, playback/storage, reconnect, both displays, throughput and memory validation |

### Panel edit scope and session layout

Cross-screen editing enablement and interval, instrument/song affect-entire mode,
and the song's row/grid layout now use the initiating panel's state. Editor,
menu, MIDI-follow, save/load and undo accesses use session accessors. Existing
file attribute names are unchanged: saving persists the saving panel's choices;
loading applies them to the loading panel. A new song initializes both layout
banks from the flash preference. Replacing a clip's shared output resets
both panels' affect-entire mode to the appropriate instrument default, while
ordinary UI toggles and undo navigation change only the active panel.

These fields can influence musical editing and section progression, so state
isolation alone is not sufficient for simultaneous operation. In particular,
playback and overdub code still consult layout; the independent dispatcher must
preserve the shared engine's owner and define layout-dependent progression.
The new unit coverage exercises independent choices, per-panel cloning and
shared output-reset behavior. It does not simulate two native editor stacks.

## Independent native UI: implementation status

Independent navigation is not enabled. The existing mirror still controls the
host's visible UI. The intended second-screen implementation uses the full native
UI on the host, with a separate navigation session for the remote panel and one
shared song, playback engine and set of settings.

The initial isolation layer provides:

- Independently constructed Local and Remote state banks and scoped ownership.
- Separate navigation stacks, UI modes and render invalidation flags.
- Separate held-button, modifier, held-pad and menu-value state.
- Separate UI timer banks; hardware timers retain Local ownership.
- Scheduler callbacks and conditions that retain their originating UI owner,
  including across nested execution.
- Session accessors for SoundEditor, View, SessionView, ArrangerView,
  InstrumentClipView, AudioClipView, AutomationView and PerformanceView. The
  original Local instances retain boot-time initialization; Remote instances
  have reserved SDRAM storage and are constructed on first use under Remote
  ownership. Their owning members are never copied from the Local editor.
- Separate submenu cursors, initial-selection tracking, horizontal-menu paging,
  parent links, grouped-menu selection and encoder acceleration. Mod-encoder
  popup suppression and display ownership are also tracked per session.
- Session accessors for the sample/DX browsers, song/preset/pattern/MIDI-definition
  loading screens, saving screens and rename screens. Browser file lists, paths,
  selection/search state, QWERTY text and cursor state have independently
  constructed storage. Screen constructors no longer reset shared browser state.
- Separate keyboard screen instances and layout objects, including a layout
  pointer table referencing the objects in its own session. Clip-owned keyboard
  configuration and column-control state still need isolation as described below.
- Separate context menu, sample marker editor, slicer, waveform navigator and
  waveform renderer instances. Context menu selections and target pointers belong
  to the panel that opened them.
- Separate favourites category/bank/item selection. Both panels viewing the same
  bank share its in-memory contents and pending colour edits. Two fixed bank
  slots bound memory use; closing one panel does not erase the other's bank.
- Automation editor layout helpers are stateless and resolve editor state
  through session accessors; these helpers can remain shared.

- Separate recorder UI instances with a shared engine reservation acquired before
  allocation can yield. Completion work returns to the initiating session.
- Separate encoder timing, card deferral and acceleration histories for each
  panel; gold encoders no longer share direction/speed history. Physical IRQ
  counters continue to be drained under Local ownership.

- Separate submenu entry lists and cursors, including EQ dynamic ordering.
  Each panel gets its own pointer list at construction; menu objects and shared
  musical data are not cloned. Horizontal menus and groups also own their
  visible-page buffers, so saved paging spans cannot be overwritten by another
  menu or panel.
  Patch-cable selection, label storage and blink state, active-scale selection,
  DX engine selection/target pointers and EQ/envelope rendering scratch state
  also belong to the initiating panel. Shared musical objects are not copied.

DX global/operator cursors, parameter targets, blink state and title buffers now
have per-panel storage. Modulation source choices and formatted menu titles are
also session-owned. The DX engine selector uses the standard value cache and
resolves its patch through the active source when writing, rather than retaining
a separate cached engine-mode value and patch pointer. DX parameter target
lifetime and cross-panel refresh of its custom display still need integration.

Standard `Value` menu commits, both toggle types and quantized stutter edits
now invalidate the peer
cache for that menu. Its next value read reloads from its own model target before
applying an encoder delta, and a menu refresh is queued. This does not yet cover
custom write paths and aliases outside patched/unpatched parameter menus,
external model changes outside the tracked write path or operations that yield
during a model write. These must be audited before enabling simultaneous edits.

Production input does not enter a Remote scope yet. Native editor instance
separation alone is insufficient while other state and hardware rendering remain
shared. Before exposing a second-screen action, finish all of:

1. Protect recording targets against deletion/replacement by the other panel
   and service remote transport during synchronous recording. Finish auditing
   remaining mutable menu fields, encoder gestures and other
   file-static state. Do not byte-copy owning containers or UI objects.
2. Separate rendering canvases, panel caches, animations and display layers.
   Remote rendering must produce transport data without writing host hardware.
3. Separate UI selection, scroll and zoom fields currently stored in Song and
   Clip. Keep musical content shared. Audit undo navigation and invalidation when
   clips, instruments or songs are removed or replaced.
4. Keep audio, MIDI, storage and physical input services in the Local context
   when a remote operation yields, including direct service calls outside the
   scheduler. Service both UI timer banks.
5. Add a negotiated independent-session protocol with ordered input processing,
   acknowledgements, rendering updates and session teardown. Release remote
   holds on disconnect and reject input belonging to an obsolete session.
6. Validate simultaneous native clip/settings editing, host playback, nested
   storage operations, project replacement, disconnect and both display types
   on two devices. The host must retain its navigation while shared edits become
   visible on both panels.

`ui_session_tests.cpp` and `scheduler_tests.cpp` cover state ownership, nested
callbacks, native instance identity and owning storage, timer routing and clock
wraparound. These tests do not establish
full editor isolation or hardware second-screen operation.

`favourites_session_tests.cpp` covers distinct bank navigation, shared unsaved
edits, panel closure and bank-slot reuse. SD serialization still requires hardware
validation alongside the other native UI paths.

Patched and unpatched numeric parameter menus also observe a shared revision
advanced when `AutoParam::setCurrentValueInResponseToUserInput` returns. This
covers different menu objects and physical controls using that model write path,
without invalidating unrelated menu drafts. Revision checks reload on the next
value read. Each panel
also observes the revision in its render pass and refreshes stale value menus
on OLED or seven-segment displays. Menu commits enqueue a peer refresh.
Refreshes wait until storage activity finishes, coalesce repeated changes and
preserve new notifications raised inside a refresh callback. Submenu refreshes
redraw without moving the cursor or reopening the menu. Custom menus must opt
into this callback; other screens still require model-change integration.
Playback interpolation, other mutation APIs, target deletion and
serialization of edits across yields still require integration.

OLED main, popup and console canvases, dirty/current-image state, popup geometry,
loading animation, console records, blink area and owning side-scroller strings
now have separate panel storage. Remote composition copies into a completed-frame
buffer and advances a frame revision without enqueueing hardware output or the
legacy display SysEx stream. Screensaver composition and fatal diagnostics remain
Local. Legacy mirror/display capture explicitly reads the Local image. A remote
snapshot stays stable while working canvases change, until the next publication;
transport must copy or pin a revision while packetizing it. Independent-session
USB transmission is not connected to this buffer yet. Seven-segment layers, pad
and indicator caches, display-object state and hardware acceptance remain open.

Seven-segment layer stacks, popup objects, transition direction, lowercase mode
and published segment bytes now belong to each panel. Remote renders publish a
frame revision without calling the physical PIC output or legacy SysEx stream;
OLED emulation uses its session canvas. Published bytes include fixed dots.
Empty stacks are handled during rendering, timer callbacks and popup dismissal,
and display destruction frees both panels' stacks and cancels their timers.
Remote text blinking follows its own panel's display and indicator timers without
touching the host's LED blink state. Display-type changes are still global, and the new numeric
frames still need independent-session transport and hardware validation.

Indicator LED values, blink jobs/phases, knob level caches, bipolar blink state
and metering selection now have separate panel storage. Rendering records LED
states and final knob brightness bytes in a revisioned snapshot; only Local
updates call the PIC hardware routines. Remote seven-segment blinking can now
follow the Remote LED blink timer and phase. Snapshot transport, shared playback
status propagation to both panels and hardware validation remain
unfinished. A transport sender must copy the snapshot before packetization.

Pad working images, occupancy masks, transition stores, scroll/zoom/collapse
state, waveform transition data, greyout and tick state now have independent
panel storage. Remote column output captures colours after greyout/tick processing
without writing the PIC. Remote hardware-assisted scrolls instead publish full
software pad updates, and fast flashes retain per-pad request revisions for future
transport. Remote render scheduling no longer depends on host PIC buffer space.
Brightness/refresh settings remain shared host hardware settings. Flash requests
are coalesced per pad, not a lossless event queue; timing and transport delivery
still need implementation. Pad/clip model lifetimes, shared playback updates,
remaining direct hardware service paths and two-device validation remain open.

Song current/previous clip selection now belongs to each panel. Native callers of
`getCurrentClip()`, `setCurrentClip()` and current-clip model-stack construction
resolve the active panel. Clip replacement updates matching references in both
panels; removal, undo removal and song-owned clip destruction clear matching
current and previous selections. A new Remote selection starts empty; future
session initialization must explicitly choose its starting clip. This does not
invalidate pointers retained inside editors, recorder targets or undo actions,
nor does it move either panel to a safe screen after deletion. Those lifetime
and navigation transitions, shared scroll/zoom fields and transport remain
required before Remote input can be enabled.

Song horizontal clip/arranger scroll and zoom, including the saved coordinates
used when returning from the arranger, now have separate panel storage. Both
panels receive the song's default zoom at construction. Native navigation,
rendering, position-dependent editing and undo view restoration resolve these
coordinates through the active session. Existing file attribute names are
unchanged: save/load accesses the initiating session's coordinates; the other
panel retains its own defaults or view. Remote session startup still needs an
explicit initial-view policy. Vertical row scrolling, clip-owned view fields,
undo ownership and playback-driven updates to both panels remain unfinished.

Song/session and arranger vertical offsets now use the same panel-owned storage.
Manual navigation and undo view restoration affect the initiating panel. Adding
an output at the front adjusts both arranger offsets; removing an output applies
the existing visible-row balancing rule separately to each panel. Save/load
attribute names are unchanged and continue to use the initiating panel. Session
clip insertion/removal, output conversion compensation, row reordering and undo
structural changes still need a complete peer-view adjustment audit. Clip-owned
pitch/drum scrolling and keyboard state remain shared. This change does not enable
Remote input or establish safe simultaneous editing.

Normal session-clip removal now applies the existing visible-row balancing rule
to both panels separately. A forced vertical movement applies only to the panel
performing the removal. Pending-overdub insertion evaluates the existing scroll
anchor rule for each panel. These changes preserve the initiating panel's normal
behavior; they do not cover every direct clip-array mutation. General insertion,
cloning, reordering, undo/redo and output-conversion compensation still need
peer-view handling, along with redraw and held-input invalidation after changes.

General session insertion, row cloning, successful grid creation, arranger-to-session
moves and clip-existence undo/redo now adjust the other panel's vertical offset
when indices shift before its bottom visible clip. Adjacent clip swaps follow
that anchor if it is one of the swapped clips. The initiating panel retains its
existing navigation behavior. Grid creation notifies the peer only after its
rollback path has passed. Removing the anchor itself retains the row for its
successor; this is not a selection-lifetime guarantee. Peer redraw, held-pad and
editor-target invalidation, empty-view clamping, output conversion and broader
undo ownership still require work before Remote input is enabled.

The structural-change notification paths now also queue a coalesced peer overview
refresh. The peer consumes it during its own render pass only with a lone session
or arranger root, no active UI mode and no storage access. Consumption marks pads
and OLED dirty; editors, menus and active gestures leave the notification pending.
This supplies overview redraw scheduling without prematurely re-reading retained
editor targets. It does not cancel stale held inputs, repair editor/model pointers,
clamp emptied views, refresh all numeric display content or service the Remote
render loop; those remain prerequisites for independent mode.

Structural notifications now retain a revision after redraw consumption. Ordinary
session row holds capture it when pressed; a later peer structural notification
cancels the stale hold before session button, pad, encoder or timer handling.
Cancellation suppresses the release action, clears the selected row and UI timer,
and replaces the modulation target with the song without MIDI feedback. It does
not dereference the former clip. Grid holds, combined modes such as stuttering,
section holds, nested editors and service callbacks outside these handlers still
need their own lifetime and cancellation handling. Independent mode remains off.


While a mirror session is in Host state, output to the client's physical USB
connection is SysEx-only across all virtual ports. Filtering at the USB queue
covers normal MIDI routing, MIDI thru and direct cable messages, including clock,
start/stop/continue, notes, CCs, program changes, pitch bend and system common.
SysEx packet fragments and terminators remain allowed. The queue is checked again
when preparing a transfer so pre-session non-SysEx messages still in the ring are
discarded. A transfer already handed to USB hardware before acceptance can finish;
it is not modified in flight. Other USB connections and DIN MIDI are unaffected.
Normal routing resumes after session teardown. Merely attaching USB without
negotiating a mirror session does not identify the device as a mirror client.


Stale session-hold cancellation now also covers section-pad holds in row view,
grid edit mode and grid launch mode with green selection enabled. Each entry
captures the structural revision. A peer structural change suppresses section
release actions and repeat editing at the next guarded input/timer callback,
clears the hold timer and removes its OLED popup. Ordinary grid clip gestures,
combined modes and editor-target lifetimes remain unfinished; this does not
make independent mode available.


Ordinary grid clip holds now participate in stale-session-hold cancellation.
Grid first-pad capture records the structural revision before operations that may
yield. After a peer edit, guarded session input/timer handling cancels exact clip
hold mode and idle grid gestures with a first pad, clears both pad coordinates,
stops selected-clip pulsing, drops its cached clip pointer and clears the popup.
This also covers first-pad copy gestures; it does not undo model edits already
performed. Combined modes, nested UI callbacks and operations resuming inside a
yielding handler still require lifetime guards. No two-device validation has
been performed, and independent mode remains disabled.


Grid edit and launch handlers now checkpoint the song pointer and peer structural
revision before creating an empty-pad clip. When creation returns, a changed song
or revision ends the handler before it dereferences the returned clip for playback,
selection or display. The checkpoint is local to the call, so a nested gesture
cannot overwrite it. This does not roll back creation or protect its internal
accesses while it yields. Song-pointer comparison is not a model-generation or
lifetime guarantee; replacement at a reused address and unnotified mutations still
require explicit model lifetime management before independent mode is enabled.


Grid X/Y scroll, the last entered arrangement-instance position and arranger
auto-scroll now belong to each panel's SongNavigation state. Native session,
arranger, performance, automation and playback-start callers use the active
panel's values. Defaults and navigation reset initialize both banks; saved song
attribute names remain unchanged, with save/load using the initiating panel.
Grid track insertion/removal anchoring is still unaudited. Session layout,
triplet display settings, clip-owned pitch/drum scrolling and keyboard state,
model lifetimes, service ownership and independent transport remain blockers.


Clip screen-choice flags (keyboard and automation view) now have per-panel
storage. Native navigation, rendering, undo view restoration and clip clone
helpers access the initiating panel's flags; the other panel keeps its own
choice. Existing saved attribute names are unchanged. Newly constructed clips
start in standard view in both banks. This separates the remembered screen type,
not the remaining clip UI state: pitch/drum scroll, keyboard layout state,
automation selection, model lifetimes and independent transport still need work.


Instrument-clip pitch/drum vertical scroll now has per-panel storage. Native
editors, keyboard transitions, undo snapshots and cloning access the active
panel's offset. Construction gives both panels the root-note-aware initial
viewport. Shared scale-size remapping adjusts each panel independently to retain
its octave and degree. The saved yScroll attribute is unchanged and uses the
initiating panel. Other shared musical transformations (root-note changes,
transpose, instrument conversion and row insertion/removal) still need a complete
peer-scroll and clamp audit. Keyboard and automation state, model lifetimes,
service ownership and independent transport remain blockers.


Chromatic transpose, successful vertical note nudging and bottom drum-row insertion
now shift both panels' clip scroll offsets by the shared row movement. Their
relative viewport distance is preserved, regardless of which panel initiated the
edit. Musical notes and colour changes still occur once. Instrument conversion,
root-note changes, row removal and view clamping remain unaudited; this is not a
complete model-change invalidation solution and independent mode remains off.


Each instrument clip now constructs separate keyboard state for Local and Remote:
layout choice, layout scroll/interval settings, chord layout state and sidebar
control instances. Native keyboard access resolves the active panel. Cloning
copies only that panel's state and rebinds left/right control pointers to the
clone's own controls, avoiding pointers into the source clip. Saved attribute
names are unchanged and save/load accesses the initiating panel. This reserves
additional per-clip RAM; large-project memory and hardware acceptance remain
untested. Shared song chord data, note ownership, keyboard gesture cancellation,
automation selection, model lifetimes and independent transport still need review.


Clip and arranger automation selection now has per-panel storage: parameter ID,
kind, shortcut coordinates and array position, plus clip output type and patch
source. Native editor, rename, save/load and selection-reset callers resolve the
active panel. Defaults initialize both panels with no selected parameter. Saved
attribute names are unchanged, and automation values remain shared. Target
replacement/deletion still requires invalidation of these selections and editor
pointers; this does not enable independent transport or resolve model lifetimes.


Triplet-grid enablement and resolution now use each panel's song navigation
state. Timeline rendering, input-to-position conversion and undo restoration use
the initiating panel's grid. Notes and playback timing remain shared, and the
saved tripletsLevel attribute retains its existing format. Both banks start with
triplets disabled. Undo action ownership and model-target lifetime protection
are still required; independent mode remains disabled.


Undo actions now capture an immutable navigation owner when constructed. Action
coalescing requires the same owner as well as the existing type/view/time checks,
so matching edits from separate panels cannot merge their UI snapshots. Updating
an action's after-state reads navigation under the captured owner and restores the
caller context afterward. The history remains shared. Cross-panel undo navigation,
consequence callbacks, deletion of retained targets and musical-vs-UI snapshot
restoration still need an audit before independent mode can be enabled.


Undo restoration now skips saved UI navigation and immediate visual updates when
an action belongs to the other panel. Shared musical scale restoration still
runs when requested, while same-panel ordering and non-navigating resize behavior
are preserved. Foreign-action completion queues overview refreshes for both
panels rather than dereferencing retained editor targets for immediate redraw.
Consequence callbacks can still touch UI state or invalidate targets; this guard
is not sufficient to enable cross-panel undo safely in production. Model lifetime
protection and consequence ownership remain required before independent mode.


Performance-pad undo consequences now capture their originating panel and restore
that panel's FX press snapshot under a scoped owner, without switching the caller's
navigation. Linear-recording undo checks both panels before auto-deactivating a
clip being viewed in a clip editor; an uninitialized panel is ignored safely.
These changes address direct UI ownership in those callbacks, not recorder target
lifetimes, nested model callbacks or hardware refresh after cross-panel undo.
Independent mode remains disabled pending those audits and transport integration.


The recorder UI now captures its song/sound/source/range identities and peer
structural revision at entry. Completion attaches the sample only if that
checkpoint still matches, avoiding attachment to a replacement selection after a
peer edit. The synchronous process loop also services mirror transport in Local
context. This is not a lifetime lock: unnotified deletion, address reuse, target
accesses during allocation/renaming and parent-UI recovery remain unsafe for
independent operation. Skipping attachment does not roll back the recording.


Audio routine/slow routine/cluster-loading service and playback/MIDI service entry
points now explicitly enter Local ownership, covering direct callers as well as
scheduler dispatch. FileReader's periodic timer/OLED/PIC servicing also runs under
Local ownership and restores the suspended caller afterward. This prevents those
service paths from using Remote navigation or hardware caches during a yielding
operation. Remote timer scheduling, remaining direct service paths and model
lifetime protection still need audit before independent input is enabled.

Direct PIC input servicing now enters Local ownership before reading pad/button
edges, all-released recovery or OLED acknowledgements. Deferred SD-card events and
shift LED updates therefore also use the physical panel's state, and early returns
restore the suspended caller's ownership. Encoder IRQ draining already has the
equivalent guard. Generic button/pad dispatch remains session-aware for eventual
Remote replay. Independent mode remains disabled pending lifetime protection,
remaining service/UI audits, transport integration and hardware validation.

Sample post-processing now scopes its periodic audio/timer/PIC service block to
Local ownership. The timer call can no longer inherit Remote ownership after the
audio routine restores its caller's context. Sample conversion resumes under its
original owner after each service block. This does not add Remote timer scheduling
or protect recording targets against shared-model deletion; independent mode
remains disabled.

### Reject changed audio marker targets across callbacks

Audio marker helpers now retain output, sample-holder file and playback direction
alongside song/UI context. They reject changes after allocation or resize before
continuing with the original marker operation. An extracted-method regression
covers each field at both callback boundaries for both marker helpers, checking
failure without consequence updates or action closure. This is identity comparison,
not sample/clip lifetime pinning, and preceding marker edits are not rolled back.
Independent mode remains disabled.

### Reject overflowing marker sizes and clamp positions explicitly

Audio start-marker tick subtraction now uses a wide intermediate and rejects
lengths beyond the signed tick range. Both helpers reject overflow in rounded
sample-length multiplication before changing markers. Marker positions use
explicit subtraction/addition bounds rather than unsigned wraparound followed by
signed conversion; file-marker distance is computed unsigned. Regressions cover
tick overflow, sample multiplication overflow, and position clamping in both
marker operations. Overflowing intermediate calculations are rejected even when
their mathematical quotient might fit. Lifetime/rollback and independent-routing
work remain outstanding; independent mode stays disabled.

### Report clip-length mutation failures to undo

Song::setClipLength now returns a boolean result, including validation after the
final resumePlayback callback. Clip-length undo propagates a reported failure
without exchanging saved redo values, even if the visible length already matches
the request. The extracted-method deletion test now covers all six callback stages
and asserts failure; success and invalid-input tests assert their return values.
An undo regression covers explicit failure with an apparently correct resulting
length. Other callers still ignore the new result and require follow-up; this is
failure propagation, not rollback. Independent mode remains disabled.

### Reject redirected model stacks during clip-length callbacks

Song::setClipLength now validates its callback model stack's song and clip along
with target context after trimming, output notification and playback callbacks.
This prevents continued playback work using a redirected stack even when the
original clip still has the requested length. An extracted-method regression
redirects each stack field during trim and resync and checks immediate failure with
no later callback. Full rollback and lifetime pinning remain unresolved;
independent mode remains disabled.

### Separate generic clip resize success from undo-action availability

lengthenClip and shortenClip now return success separately from their optional
Action pointer, clear the output pointer on reported resize failure, and propagate
failed implicit undo. changeClipLength returns zero on failure before scrolling or
rendering. Its encoder callers stop on that result; audio loop adjustment and
pattern paste also stop when their resize helper fails. Extracted-method tests
cover both resize directions with failure and successful edits without an undo
action. Callback lifetime and rollback gaps remain; independent mode is disabled.

### Preserve the caller's resync policy across implicit undo

Lengthening via implicit undo now restores the previous resync flag instead of
unconditionally enabling it. The subsequent manual resync also respects that
policy. A regression covers both initial flag values and both undo outcomes,
checking suppression during undo, restoration afterward and exactly the permitted
manual resync. This does not isolate the global flag into per-session state or
resolve remaining lifetime/rollback and independent-routing work.

### Isolate clip-length resync suppression by UI session

The former global resync flag is now a per-UI setting, enabled by default for both
Local and Remote. Implicit undo retains a reference to its initiating bank when
temporarily suppressing resync, so restoration does not follow a changed active
owner. Song resizing consults the active bank. An extracted-helper regression runs
a Local edit inside a Remote undo callback and verifies Local resync remains
enabled while Remote suppression stays active until undo returns. Shared playback,
rollback and complete object lifetime protection remain unresolved; independent
mode remains disabled.

### Reject clip-length callbacks that leave UI ownership changed

Song::setClipLength now retains its initiating UI owner and includes it in callback
context validation. A changed owner cannot select another session's resync policy
or continue playback work under that session. Extracted-method tests switch owner
at all six callback stages from both Local and Remote, and verify properly scoped
nested service still completes successfully after restoring ownership. This does
not add rollback or lifetime pinning; independent mode remains disabled.

### Validate computed generic resize lengths before editing

changeClipLength now uses a wide intermediate for extension and shortening and
rejects results outside the positive sequence-length range before resizing or
navigation. It clears the output action before calculation, so failure cannot
return a stale pointer. Extracted-method regressions cover signed extension
overflow, excessive extension, zero/underflow shortening and both valid limits.
Invalid lengths now return the failure sentinel instead of reaching subsequent
length displays or audio-marker edits. Independent mode remains disabled.

### Keep generic resize bound to its initiating clip

Generic lengthen/shorten helpers capture song, clip, output, UI owner and both
structural revisions before undo or action allocation. They reject changed context
after those callbacks and before returning success, clearing the output action on
failure. Extracted-method tests redirect selection during allocation in both
directions and during successful implicit undo, verifying neither clip is resized
after redirection and no navigation follows. Retained action lifetime and rollback
are still not fully protected. Independent mode remains disabled.

### Reject removed resize actions at callback boundaries

Generic resize helpers now require a non-null returned action to remain the
current undo entry after allocation and before returning success. This check
precedes inspecting an allocated action's clip. Extracted-method regressions
delete/remove actions during allocation and resize in both directions, ensuring
failure clears the output pointer and skips navigation/rendering. A success case
checks that a retained current action is still returned. These are cooperative
history checks, not lifetime pinning; same-address reuse and destruction inside
other mutation callbacks remain unresolved. Independent mode remains disabled.

### Reject removed actions in audio marker editing

Audio start/end marker helpers check that non-null actions remain the current undo
entry after allocation and after successful resize, before using their
consequences. An extracted-method regression deletes actions at either callback
boundary for both marker operations and both playback directions, checking failure,
no consequence updates or action closure, and no resize after allocation removal.
Existing successful edits remain covered. These checks do not pin action lifetime
inside callbacks or roll back preceding marker changes. Independent mode remains
disabled.

### Keep audio marker resizing bound to its original song and clip

Marker helpers now validate song ownership and basic inputs before sample-length
arithmetic, and check the original song, UI owner, both structural revisions and
clip membership after action allocation and before successful completion. Tests
change songs or delete/unregister the clip during allocation for both helpers,
checking that neither song receives a resize; invalid original lengths leave
markers untouched. This does not cover every arithmetic edge case, same-address
reuse or rollback of markers already changed before allocation. Independent mode
remains disabled.

### Stop recording and note-entry callers after resize failure

Instant recording-stop now returns if its clip resize fails, before arming or
rendering the clip. Instrument note-entry likewise stops before continuing note
creation after a failed parent resize. Source-contract regressions require both
guards before the following work; these do not execute the complete handlers.
Earlier scheduling changes or edits are not rolled back. Generic clip-view resize
helpers still need unambiguous failure propagation. Independent mode stays disabled.

### Stop multiply undo when the parent length change fails

Multiply undo now propagates a false setClipLength result before inspecting or
halving note rows. A regression returns failure after setting the expected parent
length and verifies unchanged independent row lengths, no row trims and release
of the temporary row snapshot. Existing successful multiply undo remains covered.
The already-applied parent edit is not rolled back. Remaining callers, transactional
recovery, lifetime protection and independent routing still require work;
independent mode remains disabled.

### Stop audio start-marker undo updates after resize failure

Both forward and reversed branches of changeUnderlyingSampleStart now return when
setClipLength reports failure, before reading the action's consequence or closing
the action. Extracted-method regressions cover failure with clip deletion and
successful marker backup in both directions. Song, action and clip collaborators
are doubles. The marker edit made before resizing is not rolled back, and the
outer UI callers and other length-edit paths still need failure propagation.
Independent mode remains disabled.

### Stop audio end-marker undo updates after resize failure

changeUnderlyingSampleLength now returns when setClipLength reports failure,
before inspecting undo consequences or closing the action. Extracted-method tests
cover failure with clip deletion in both playback directions and successful
end-marker edits with the expected original-marker backup. This matches the
start-marker path. Earlier marker changes are not rolled back, and outer UI
failure propagation remains unfinished. Independent mode stays disabled.

### Propagate marker resize failure to pad and encoder handlers

Both audio marker helpers now return success/failure. Their two pad call sites
and the encoder length-edit call site return immediately on failure, preventing
subsequent rendering requests, length display updates and undo scroll writes.
Extracted helper tests assert both outcomes in both playback directions, and a
source contract checks all three UI guards. The contract does not execute the full
UI handlers. Preceding mutations and earlier helper calls still need broader
lifetime/rollback protection; independent mode remains disabled.

### Clip-target validation for undo

Horizontal-shift consequences now retain the edited clip explicitly instead of
resolving the currently selected clip when undo runs. Horizontal-shift, clip-length,
and note-array consequences compare their target against the song's registered
session and arrangement-only clips before dereferencing it. Note-array undo also
requires an instrument clip. Missing targets return an error through existing undo
recovery, before marker changes, length changes, shifts, or note swaps occur.

Detached clips must be restored by their existence consequence before subsequent
consequences can edit them. These checks establish membership at entry only; they
do not prevent address reuse, protect pointers across yielding operations, or make
multi-consequence undo transactional. Independent mode remains disabled. Native
unit tests are regression checks and do not directly exercise these firmware
consequences with destroyed targets.

Deferred undo/redo commands now capture their initiating panel when queued, and
the slow service routine executes them under that owner before restoring its
caller. Commands are consumed before undo/redo can yield, avoiding recursive
execution or clearing a newly queued command afterward. The existing single-slot,
latest-command-wins behavior is unchanged; this is not an independent input queue.
Shared undo target lifetimes and independent transport remain blockers.

Deferred command servicing now rejects recursive execution while an undo/redo
callback is active. A command queued during a yield remains pending for the next
service after completion, with its own panel owner. Regression tests cover nested
servicing, owner restoration, single execution and latest-command replacement.
This protects deferred dispatch only; other undo entry points and stale model
lifetimes (including song changes) still require review. Independent mode remains
disabled.

Clearing the complete undo history now cancels pending deferred undo/redo before
history destruction, including the song loader's existing history reset. Pending
requests therefore cannot survive that reset and act on subsequently created
history. Cancellation leaves an executing callback's recursion guard intact;
regression tests cover cancellation, subsequent requests and nested servicing.
This does not stop an already executing undo, block new input during history
teardown, or provide model lifetime protection. Independent mode remains disabled.

Full undo-history destruction now suspends deferred command submission and
servicing throughout both queue cleanups, which can yield between consequences.
Entering suspension cancels prior requests; requests received during cleanup are
discarded. Nested cleanup cannot resume dispatch early, and cleanup inside an
active callback preserves its execution guard. Two regression tests cover these
cases. Direct undo calls, edits during destruction, and active model lifetimes
remain outside this guard; independent mode remains disabled.

Whole-action reversion now holds a cooperative re-entry guard from before empty
history cleanup through reinsertion onto the opposite history list. Nested direct
revert/redo fails, undo returns before its recording side effects, and deferred
eligibility rejects commands while reversion is active. Partial note-row undo
checks that history exists and guards its consequence callbacks as well. Guard
regression tests cover nested rejection and early-return cleanup. This does not
block deletion or new edits during reversion, nor cover undo's recording prelude;
model lifetime protection and independent-mode integration remain outstanding.

History destruction now holds the reversion guard for full resets, individual
queue deletion and last-action deletion. Direct undo/redo and partial reversion
are rejected while consequence destruction yields. Nested cleanup preserves an
outer guard, including across both queues of a full reset; a regression test
covers that ownership sequence. Cleanup is still permitted inside reversion for
existing call paths. This does not prevent unrelated edits or deletion of a live
reversion target, so independent mode remains disabled.

Partial note-row undo now rejects a non-note-array head before reading its row ID.
Its deletion loop stops at the exact repeated-row consequence found by the initial
scan, avoiding casts of intervening consequences based on a different pointer's
type. A failed whole-action fallback returns failure without deleting redo history.
These are traversal/return-value fixes, not lifetime protection if another operation
invalidates the action or boundary while callbacks yield. Independent mode remains
disabled; hardware and mixed-history integration validation are still required.

Quantize, note-repeat and Euclidean edit shortcuts now require the latest action
to belong to the active panel before performing an implicit partial undo. If the
peer owns it, the caller follows its normal edit path instead of silently skipping
the edit after rejection. Partial undo also enforces ownership at its entry point.
Explicit shared-history undo remains available through its separate path. This
isolates implicit gesture replacement only; same-panel target changes, retained
note-row lifetimes and interleaved gesture values still require review before
independent mode can be enabled.

Note-row horizontal shift and length edits now require matching panel ownership
before directly reusing the latest action. Resize also handles a missing previous
consequence by taking the new-action path, and stops if its implicit reversion
fails rather than claiming chopped notes were restored. This does not protect
retained clip/note-row pointers across successful reversion or allocation yields.
Clip-length and pattern-preview implicit undo still need review; independent mode
remains disabled.

Clip lengthening now requires matching panel ownership before implicitly undoing
a shortening, and aborts the resize when that reversion fails. Both length paths
check for an existing, open, same-panel, same-clip pattern-paste action before
joining its preview, avoiding empty-history dereferences and peer-preview reuse.
Clip horizontal shifts also require owner agreement before modifying a previous
action directly. Pattern browser rollback and retained model pointers across
successful reversion still need review. Independent mode remains disabled.

Pattern browser rollback on file change, Back and final load now checks that the
latest paste action belongs to the active panel and selected clip before closing
or reverting it. A peer's paste can no longer be mistaken for this panel's preview
solely by action type. This is an eligibility guard, not transactional preview
rollback: interleaved history can still leave an earlier preview applied, and
failed reversion plus target lifetimes require further work. Independent mode
remains disabled.

Pattern rollback now checks reversion success before file-change replacement,
Back exit or final-load continuation. Confirmation keeps the browser open on load
failure; Play preview also stops on failure and handles an empty file selection.
Rejected rollback reports the existing generic error because reversion exposes
only a boolean result. This does not undo a browser selection already changed,
recover a preview buried under interleaved history or roll back partial file-load
mutations. Independent mode remains disabled pending those and lifetime/transport
work.

Pattern-file import now preserves parsing/import errors and reports a close failure
as an SD-card error only when import otherwise succeeded. The previous reversed
FRESULT check could mask both outcomes. Initial preview setup now displays the
returned error, and performLoad no longer displays an unrelated cached selection
status in place of the real error. Confirmation/Play callers retain their existing
failure handling. Import mutations and no-scaling clears are not transactional;
independent mode remains disabled pending lifetime and transport work.

No-scaling pattern clear now requires an undo action before clearing notes and
returns an error when action creation fails. Initial setup, encoder navigation
and file-change handling stop and display that error instead of continuing the
clear/load sequence. This prevents clearing with a null action only: allocation
of individual consequences inside clear can still fail, and getNewAction can
reject requests for reasons besides memory pressure. Complete snapshot reservation
and transactional preview recovery remain unresolved; independent mode is disabled.

Correction to the preceding pattern-clear notes: patternClear currently passes
false for both clearAutomation and clearSequenceAndMPE, so the underlying clear
implementation does not delete notes or automation. Its null-action check is not
a guarantee of undoable note clearing, and changing its intended behavior remains
separate work.

Note-array snapshot construction now reports clone allocation failure to Action.
A failed snapshot is destroyed without adding it to history, and the recording
method returns INSUFFICIENT_RAM rather than success. Stealing an existing array
retains its existing behavior. Callers that ignore snapshot errors still need
review; this does not reserve all snapshots before edits or protect model lifetimes.
Independent mode remains disabled.

### Guard successive song clip-length callbacks

Song::setClipLength now validates length inputs and rechecks context after scale,
undo-recording, trim, output and playback-resync callbacks before using the clip
again. Registered-target removal is checked before dereferencing the target;
unpublished clips remain supported and observed publication is retained. An
extracted-production-method test covers deletion at each of five callback stages,
changed resulting length, invalid lengths and successful unpublished clips. The
collaborators are controlled doubles, not full audio/storage integration. This
void API still cannot report every internal failure, and unpublished lifetime,
owner destruction and partial-edit rollback remain unresolved. Independent mode
remains disabled.

Note-row paste now stops before inserting notes if its required snapshot fails.
The enclosing paste operation returns its existing allocation failure instead of
only displaying it, and file import propagates that result to the browser so it
cannot report successful loading after paste failure. Other callers still receive
the existing popup. Earlier row edits, overwrite clearing and no-scaling resize
are not rolled back; this is failure propagation, not an atomic import. Independent
mode remains disabled.

Paste now rejects a null undo action before overwrite clearing or row insertion.
Single-drum paste also validates the selected drum's row index before accessing
the note-row array, returning an error if the selection no longer resolves.
No-scaling resize happens earlier and is not rolled back by these checks; an action
allocation is not a reservation of every subsequent snapshot. Target lifetime and
complete import rollback remain unresolved, and independent mode remains disabled.

Single-drum paste now resolves the selected drum through the current clip's
getNoteRowForDrum lookup, using the returned row index for the model stack. It no
longer assumes kit-list order matches clip-row order or applies whole-kit viewport
bounds before resolving the target. Missing selections/rows fail before row access.
The shared selectedDrum field and target validity across yielded callbacks remain
unresolved; independent mode remains disabled pending those and transport work.

Kit selected-drum navigation is now banked by panel, with callers using the active
session accessor across editors, menus, clip operations, save/load and undo state.
Each new kit starts with null selections; file loading restores the initiating
panel's saved selection. Kit::drumRemoved clears matching pointers in both banks.
The kit, drums and parameter values remain shared. Other retained drum pointers,
replacement/removal paths that bypass drumRemoved, held notes and peer refresh
still need lifetime review. Independent mode remains disabled.

Silent-row cleanup now checks both panels' active modulation targets before
removing a sound drum's row, preserving the existing target protection across
sessions. Scoped checks restore the caller's panel before cleanup continues.
This protects that automatic cleanup path only: selected drums, editor-held
pointers, explicit removal and kit replacement still need broader lifetime
protection. Independent mode remains disabled.

RemoteInstance now supports a const lookup that does not initialize its Remote
object. Silent-row cleanup uses this lookup through View and checks only existing
panel targets, avoiding Remote UI construction during ordinary Local cleanup.
A regression test covers uninitialized Remote lookup, subsequent construction and
Local identity after scope restoration. Independent mode remains disabled.

Automatic silent-row cleanup also preserves a row whose drum is selected on either
panel, even when that drum has not become the panel's modulation target. Selection
protection covers sound, MIDI and gate drums; modulation checks continue to inspect
only existing View instances. This can retain an otherwise silent selected row.
Explicit deletion, kit replacement and other retained editor pointers still need
lifetime protection before independent mode can be enabled.

Drum removal now advances the peer's structural revision and queues its safe
overview refresh, including when the removed drum was not selected. If removal
clears the initiating panel's selection, that panel also receives a structural
refresh. Null notifications are ignored. Existing revision checkpoints can now
detect this path, but retained editor pointers are not made safe merely by a
revision change, and immediate editor redraw remains deferred. Independent mode
remains disabled.

Drum insertion now advances the peer structural revision after attaching the drum
to its kit. Kit destruction clears both selected-drum banks and requests refresh
for affected panels before its first audio-service yield. These notifications may
also occur for kits being prepared or discarded during loading; they are conservative
invalidation, not a live-kit lifetime lock. Other pointers to the kit/drums and
callbacks during destruction remain unsafe for independent operation, which stays
disabled.

Action-closing helpers now require the newest action's navigation owner to match
the active panel before ending its additions. A peer finishing a same-type gesture
can no longer close that action merely by matching its type. These helpers still
operate only on the newest action; gesture closure across interleaved history and
non-UI callers remain audit items before independent mode is enabled.

Recording boundaries use an explicit shared-history close operation. Record
arming, playback setup and the session transition between recording clips close
the latest RECORD action regardless of panel owner, preserving separation between
recording runs. Ordinary gesture closeAction remains owner-restricted. This is
limited to the latest action and does not solve history interleaving or recorder
lifetimes; independent mode remains disabled.

Gesture-closing helpers now skip peer history entries to locate the active panel's
newest action before applying their type/time checks. They do not skip a newer
same-panel action of another type or reorder shared history. This closes gestures
that ended after a peer edit without changing the peer's action. A regression test
covers interleaving, missing owners and empty history. Lookup cost grows with the
number of intervening peer entries; undo/redo-list gesture lifetimes remain audit
items, and independent mode remains disabled.

Reversion that requests navigation now closes the action for further additions
before moving it between history lists. A redone action therefore cannot resume
an old gesture's coalescing simply because openForAdditions survived undo. Rejected
UI-mode checks leave the action unchanged, and non-navigating implicit resize
reversion retains its existing grouping behavior. This does not make undo target
lifetimes safe; independent mode remains disabled.

Consequence reversion errors now propagate from revertAction to revert's boolean
result. Explicit undo/redo no longer emits its success message on that failure,
and implicit resize/preview callers that check the result stop accordingly. This
preserves the existing error cleanup and history placement: the partially reverted
action is still reinserted after other history is cleared. Its mixed consequence
ownership and safe disposal/replay remain a serious unresolved lifetime issue;
independent mode remains disabled.

Partial note-row undo now distinguishes failure, partial success and whole-action
success. A consequence error stops traversal before unlinking or deleting the
failed consequence, and quantize/repeat/Euclidean callers stop on failure instead
of treating it as a successful partial edit. Empty/invalid history is failure.
Previously completed consequences and UI gesture values are not rolled back, and
mixed-state action recovery remains unresolved. Independent mode remains disabled.

Partial-undo entry now requires the caller's current song, active instrument clip
and action clip to agree. Before changing any consequence, it checks every
note-array consequence against that clip and verifies its row exists. Retained
consequence clip pointers are compared before dereferencing the selected clip.
This rejects mismatched clips and missing rows at entry, but does not validate
other consequence types, prove the selected clip is alive, or prevent row identity
reuse and target changes during yielded callbacks. Independent mode remains disabled.

Partial-undo callbacks now checkpoint the song, history head, selected clip and
panel structural revision. After reversion and after detached-consequence cleanup,
a changed checkpoint stops traversal before dereferencing the retained action or
following its next consequence. The identity checks short-circuit before reading
the selected clip from a changed song. This does not protect accesses inside the
callbacks, unnotified deletion/address reuse or recover completed changes; mixed
consequence lifetime protection remains required and independent mode stays disabled.

Consequence cleanup now detaches the action's list before its first audio-service
yield and destroys the detached list. The action no longer exposes a head pointing
at already freed nodes during later callbacks, and nested list cleanup cannot
reclaim the same chain through that head. This does not prevent destruction of the
action itself, additions during destruction or unsafe retained model references;
independent mode remains disabled pending lifetime and transport work.

Action destruction now detaches its clip-state snapshot storage, clears the
snapshot count and closes additions before consequence cleanup can yield. The
cleanup frees its detached storage afterward, leaving no stale snapshot pointer
on the action. This prevents a nested preparation from claiming that same snapshot
allocation through the action, but does not stop action-object deletion, retained
external pointers or new consequences being added during cleanup. Independent mode
remains disabled.

Per-clip undo navigation snapshots now retain the captured clip identity. Updating
and restoring positional entries requires identity agreement in addition to the
existing count checks, so reordered/replaced clips do not receive another clip's
scroll, wrap or drum-selection state. Mismatches are skipped rather than remapped;
the retained identity is compared only, not dereferenced. Address reuse and changes
inside later callbacks remain lifetime risks. Independent mode remains disabled.

Undo clip snapshots now retain selected-drum identity instead of kit-list index.
Restoration compares that identity against live kit members and clears selection
when it is absent, avoiding selecting a different or fallback first drum after
reordering/removal. The saved pointer is not dereferenced during membership lookup.
This does not protect address reuse or pointers retained in other editor state;
independent mode remains disabled pending lifetime and transport work.

ActionClipState is now a non-polymorphic value record with initialized fields.
The allocated snapshot array explicitly constructs each element before capture;
its trivial destruction matches bulk deallocation. This removes the unused virtual
interface and avoids calling methods on unconstructed polymorphic snapshot objects.
A regression test covers defaults and the trivial-destruction contract. Allocation
yields and changing clip collections during capture remain unresolved; independent
mode remains disabled.

New-action allocation now checkpoints song identity, undo head and panel structural
revision around both allocations, and checks clip count again before constructing
the snapshot array. A changed context releases the uncommitted allocations and
returns no action, avoiding filling an array sized for an older clip collection.
This does not detect unnotified same-count replacement/address reuse or make
callers that continue without an action safe. Independent mode remains disabled.

Action creation now captures its initiating song, panel, UI, selected clip and
structural revision before redo-history cleanup. It rechecks that context after
redo/empty-action cleanup and during subsequent allocation checks, so cleanup
callbacks cannot silently redirect the request into a different editing context.
Checks compare retained identities rather than dereferencing them. They do not
restore already discarded redo history or detect unnotified address reuse; callers
that edit after a null action still require review. Independent mode remains disabled.

General action coalescing now also requires the captured clip to match the clip
selected when the new edit was requested. Reusing the same editor and action type
after switching clips therefore starts a new action rather than extending a stale
clip snapshot. This is conservative for song-level actions whose selected clip
changes and does not distinguish different targets inside one clip. Independent
mode remains disabled pending lifetime, remaining UI and transport work.

Clip navigation snapshots now capture output identity as well as clip identity.
Update/restoration skips entries when an instrument/output was replaced on the
same clip, avoiding applying old navigation/drum state to the new output. Identity
matching does not dereference stored pointers and has a regression test for clip
and output replacement. Same-address reuse and in-place output changes remain
outside this check; independent mode remains disabled.

Action requests now checkpoint the selected clip's output through cleanup and
allocation yields. New actions also capture that output identity, and coalescing
requires it to match, so replacing an instrument on the same clip cannot silently
extend the old action. Output identities are compared only; the clip is read only
after song/selection checks pass. This does not detect in-place replacement or
address reuse and is not model lifetime protection. Independent mode stays disabled.

Direct edit-action reuse now checks captured output identity across quantize,
repeat, Euclidean edits, row/clip shifting and resizing, and pattern-preview
rollback. Partial undo enforces the same check at entry. These paths can no longer
bypass general coalescing's output check after an instrument replacement; mismatched
shortcuts use their existing normal-edit or no-rollback paths. This does not make
interleaved previews transactional or protect in-place mutations/address reuse.
Independent mode remains disabled.

Partial-undo callback checkpoints now also require the original panel owner, UI
and selected clip's output identity. A callback that changes editor context or
replaces the instrument on the same clip stops subsequent traversal even when
clip identity remains unchanged. These are post-callback checks, not protection
for accesses inside the callback or in-place mutations. Independent mode remains
disabled pending lifetime, recovery and transport work.

Actions now capture their owning song identity before entering history. Coalescing,
navigation snapshot updates, whole reversion and partial undo require the current
song to match; snapshot updates also reject null action/song inputs. This prevents
an old-song action from being interpreted against a different loaded project when
the action object still exists. It does not validate the action object's own
lifetime or distinguish a replacement song at the same address. Independent mode
remains disabled.

Direct clip/row edit shortcuts and pattern rollback now require action song
identity as well as their existing panel/clip/output checks. Gesture-closing and
shared recording-boundary helpers also refuse actions from another song. This
closes bypasses around general action coalescing; stale action objects themselves,
address reuse and history cleanup ownership remain unresolved. Independent mode
remains disabled.

Empty-recording-action cleanup now requires the action's song to match the current
song before destruction. Reversion rejects a missing song and checks its initiating
song/panel/UI/structural revision after that cleanup yields. Undo also rejects a
missing song before recording-related side effects. This prevents cleanup from
silently redirecting a pending revert, but whole-history destruction and action
lifetimes still need protection. Independent mode remains disabled.

Undo's recording prelude now holds the reversion guard while ending/resuming
recording, so nested undo/redo and partial reversion cannot enter through those
callbacks before normal reversion starts. The scoped guard releases before the
subsequent revert call and on the tempoless early-success path. Song/panel/UI
checkpoints prevent continuation in a changed context. Recording side effects
are not rolled back, and callback-internal lifetimes remain unresolved; independent
mode remains disabled.

Action creation now skips snapshot-array allocation when the song has no clips,
allowing song-level actions without depending on zero-byte allocator behavior.
Context validation still rejects a clip-count change before capture, and cleanup
handles the absent array. Snapshot discard during updates now clears the action's
pointer/count before deallocation, matching destruction's ownership order.
Independent mode remains disabled pending lifetime/UI and transport work.

Generic silent-clip cleanup now preserves clips selected or targeted for modulation
by either panel, retaining the existing playback/rendering/sync-scaling exclusions.
Remote View inspection uses the non-constructing lookup and scoped checks restore
the caller context on all returns. This can retain otherwise silent selected clips;
InstrumentClip's override and explicit deletion paths remain separate lifetime
audit items. Independent mode remains disabled.

Instrument-clip automatic cleanup now exits before MIDI-backup or row removal
when either panel selects the clip or has it as its modulation timeline target.
This covers the kit override as well as melodic instruments and avoids deleting
rows retained by an open editor even when no drum is selected. It conservatively
retains the whole clip's cleanup candidates; explicit deletion and stale retained
references remain separate lifetime problems. Independent mode remains disabled.

NoteRowVector deletion now advances the peer structural revision before destroying
rows, including full-vector destruction before its audio-service yields. This
covers row removal without drum removal and bulk clip-row cleanup through this
entry point. It conservatively invalidates peer checkpoints for temporary vectors
too; it does not make retained rows safe inside callbacks or cover raw array edits
that bypass the wrapper. Independent mode remains disabled.

Note-row insertion now advances peer structural revision before allocation and
after successful row construction. This covers relocation of existing rows and
checkpoints captured during an allocation yield; failed insertion still causes
the conservative initial invalidation. It does not serialize access while the
array changes or protect raw row pointers inside callbacks. Independent mode
remains disabled pending lifetime/UI and transport work.

Row insertion, deletion and vector destruction now use scoped peer structural
notifications at entry and exit. The guard captures the peer at entry, so callback
context switches cannot redirect its completion notification. Empty deletion skips
notification; failed insertion conservatively invalidates at both boundaries.
Tests cover completion under another panel and empty scopes. This invalidates
checkpoints rather than locking rows during mutation; independent mode stays disabled.

Explicit instrument row deletion now brackets its full operation with the same
peer structural scope, including note-stop, arpeggiator and drum-detachment work
before vector deletion. Nested vector notifications remain conservative; callbacks
inside these operations still require lifetime protection before independent mode.

Kit-row creation now brackets insertion, drum assignment and viewport adjustment
with a peer structural-change scope. It resolves the available unassigned drum
after insertion rather than retaining that pointer across allocation. This avoids
that specific pre-allocation stale drum reference; clip/output changes and row
relocation during later callbacks still require lifetime protection. Independent
mode remains disabled.


### Failed undo: discard unusable history

Clip-existence consequences now track ownership when the clip is removed from or
inserted into its array. Destruction no longer guesses ownership from the action's
undo/redo queue. This matters when a reversion succeeds for some consequences and
fails for a later one: those consequences can describe different applied states.

A failed full-action reversion is destroyed rather than inserted into the opposite
history. The remaining history is cleared, pending success animations are skipped,
and the affected panel returns to session view with its modulation target set to
the song. The original error is displayed. Changes made before the failure remain;
this is recovery by discarding unusable history, not transactional rollback.

This does not complete failed-edit recovery. Partial gesture undo and arrangement
recording can still make changes before failing, output-existence consequences
have a separate existing ownership TODO, and nested model deletion during yielding
callbacks still needs lifetime protection. The native unit suite does not execute
the firmware's complete allocation-failure and UI recovery paths. Independent
transport and independent input dispatch remain unimplemented and disabled.


### Partial note-array undo: complete swaps before destruction

Partial gesture undo now requires a prefix consisting entirely of validated
note-array consequences. A mixed prefix is rejected before mutation. All swaps
complete without allocation or yielding; only then is the prefix detached from
history and its snapshots destroyed. This prevents a destruction callback from
seeing a half-consumed gesture or deleting a later snapshot that the operation
still intends to use.

The reversible-prefix helper restores successful swaps in reverse order if a
later swap fails, rebuilding the original history links. Its contract requires
non-yielding operations, failure without mutation, and infallible rollback;
validated note-array swaps satisfy that contract. It is not suitable for arbitrary
consequences that allocate, yield or partially mutate on error. Unit tests inject
failure at each position against overlapping state, and check both state/link
restoration and successful retirement with an untouched history suffix.

This closes the snapshot-destruction interruption within that restricted partial
undo path. It does not establish comprehensive editor/model lifetime safety or
implement the independent-session transport.


### Undo-detached output ownership

Outputs removed by an output-existence consequence are now retained in a separate
song-owned intrusive list. Recreating the output releases it from that list before
reinsertion. A failed removal returns an error rather than claiming success or
preparing an output that was not removed. Discarding history cannot orphan a
detached output: song teardown destroys retained outputs after its clips and
backed-up parameter managers. Recording-source invalidation also visits retained
outputs so later restoration cannot revive a freed source pointer.

This intentionally retains detached output memory until restoration or song
teardown; it does not establish a safe point for early reclamation while arbitrary
editor/history references still exist. It is not comprehensive lifetime protection
for clips, note rows, instruments or editor targets. Independent mode remains
disabled.


### Arrangement clearing: preserve history on deletion-snapshot failure

Arrangement clearing now returns an error when it cannot allocate a clip-instance
deletion snapshot. It stops before removing that instance. If allocation of the
subsequent arrangement-only clip deletion snapshot fails, the clearing path leaves
the clip owned by the song and returns the error instead of clearing the action
logs and permanently destroying the clip.

During arrangement-record undo, that error prevents applying the old consequence
chain to a partly cleared arrangement. The old and newly recorded chains remain
attached to the failed action for the existing discard/recovery path. Both Session
and Performance recording entry points stop before placing new instances or
starting playback when clearing fails.

Earlier successful changes are not rolled back. Automation trimming and shortening
a crossing instance still have snapshot paths without error propagation, and the
ordinary arranger deletion path retains its previous low-memory fallback. The
native unit suite does not execute these firmware allocation-failure paths; this
change needs hardware fault-injection coverage as well as the firmware build.
Independent dispatch, frame delivery and comprehensive target lifetime protection
remain unfinished, with independent mode disabled.


### Arrangement clearing: strict automation and shortening snapshots

Arrangement clearing now temporarily requires complete snapshots from its action.
The requirement is restored on every return, while an allocation error is retained
for the caller's recovery path. Automation trimming stops rather than silently
falling back to an unrecorded trim when its temporary node array or snapshot cannot
be allocated. Later parameters are left alone once an error is recorded. Failure
to insert a replacement node at position zero is also reported; earlier trimming
may already have occurred and is not rolled back by this change.

A crossing clip instance is left unchanged if its shortening snapshot cannot be
allocated under this requirement. Both errors propagate through the existing
arrangement-clear error handling. Other operations retain their previous optional
snapshot behavior; complete error propagation across all edit paths is unfinished.

Parameter snapshot construction now checks automation-array cloning. A failed
clone is destroyed rather than inserted into undo history, and snapshot helpers
report success/failure. Callers outside the strict clearing path can still ignore
that result. Existing native tests do not inject failures into these firmware paths;
allocation-failure and two-device integration validation remain outstanding.
Independent mode remains disabled.


### Undo arrangement-instance identity checks

Clip-instance change and deletion consequences now require an exact position and
clip-pointer match after their ordered-array lookup. A missing original instance
can no longer cause a greater-or-equal search to overwrite or delete its next
neighbour. Recreating an instance rejects an already occupied start position.
Length is not used as identity because an instance recorded during playback can
legitimately grow after its existence snapshot is captured.

These consequences validate output ownership by comparing the retained pointer
against the song's active and undo-detached output lists before dereferencing it.
Output-existence deletion requires active-list membership, avoiding a second
removal of a detached output. Mismatches report an error through undo recovery.

This is validation at entry, not a lifetime lock: allocator callbacks during
insertion, reuse of the same address, and replacement by an indistinguishable
instance remain unresolved. Existing unit tests and the firmware build do not
establish concurrent two-panel safety. Independent mode remains disabled.


### Arrangement-instance recreation: reserve, revalidate, then insert

Undo now reserves space before its final position lookup and rechecks the song,
output ownership and both panels' structural revisions after reservation. It
copies the consequence's position, length and clip identity before reservation
can service callbacks. A changed context or newly occupied position is rejected.

The final insertion uses existing circular storage only. The ordinary insertion
path can attempt expansion at a wrap even when capacity is available, so it is
not sufficient to reserve and then call that path. The new reserved insertion
shifts the suffix without allocation or callbacks; undo initializes the new
instance before returning to code that can yield. Insufficient reserved capacity
returns an error rather than falling back to an allocating insertion.

Native tests exhaustively exercise capacities 1 through 12, all starting offsets,
lengths, insertion positions and fitting gap sizes, with outside-memory sentinels.
They also verify rejected operations leave storage and size unchanged. These
exercise the production circular-storage helper, not the complete firmware undo
stack. The owner can still be destroyed inside the reservation itself; address
reuse, untracked mutations and retained clip lifetime remain unresolved. This
change provides an allocation-free commit, not comprehensive lifetime protection.
Independent mode remains disabled.


### Undo clip-reference validation and reservation audit

Arrangement-instance change and recreation now verify a non-null destination clip
against the song's session and arrangement-only clip arrays before dereferencing
it, then require its output to match the instance's output. An empty instance is
still valid. Recreation repeats this validation after reservation and before the
allocation-free insertion, so an output alone is no longer sufficient proof that
its saved clip reference may be installed. A clip retained only in undo history
must be restored to the song before an instance can reference it.

The earlier reservation notes were deliberately conservative. Inspection of the
current allocator/cache-manager entry points and Stealable implementations found
cache/audio-data reclamation (AudioFile, Cluster, WaveTableBandData and GrainBuffer),
not a path dispatching native UI or deleting song/output objects. This narrows the
specific reservation concern; it is not a demonstrated song/output deletion path
in the current allocator. Cache callbacks can recurse into allocation and mutate
cache ownership. Broader operations that explicitly service audio/UI/storage,
address reuse, and editor pointers across model changes still require lifetime
protection. Keep the post-reservation checks and allocation-free commit; do not
infer that all yielding firmware operations are safe from this audit.

The existing native tests and firmware build are regression checks, not direct
failure-injection tests for these new model-reference guards. Independent dispatch
and Remote frame delivery remain unfinished; independent mode stays disabled.


### Session-owned injected encoder dispatch

The native encoder interpreter now has separate physical and injected-input entry
points sharing the same handler logic. The physical entry still forces Local
ownership and drains IRQ counters. The injected entry retains the caller's UI
session and reads an independently constructed encoder bank for that panel.
Counters and acceleration storage are separate from the physical encoders.

The enabled mirror now queues encoder input in the Local injected bank rather
than adding it to physical IRQ counters. It queues each event once, services any
card-routine retry, and acknowledges only after the bank is drained. A deferred
movement cannot be duplicated or overtaken by the next client pad/button event.
Session start/stop clears the mirror's injected bank without discarding physical
host ticks. Tests cover per-panel ownership, independent physical ticks, clearing,
retry ordering, invalid input rejection and all valid encoder indices/deltas.

This creates a session-preserving encoder dispatch entry point; it does not
activate a Remote UI session. Remote panel negotiation, initialization, timers,
frame delivery, complete teardown and shared-model lifetime protection remain
unfinished. Independent mode remains disabled. Native bank tests do not execute
the complete firmware UI/card-deferral path, which still needs device validation.

### Injected encoder dispatch across yielding handlers

An injected encoder bank stays occupied for the whole interpreter call, including
after the handler takes its ticks. Re-entry into the same bank is rejected, and
later input cannot be queued or considered complete while that handler is still
running. Local and Remote banks have independent dispatch guards. This supplements
the enabled mirror's existing outer busy guard; it does not enable Remote dispatch.

Clearing a bank during dispatch defers cleanup until the interpreter returns.
That cleanup also discards any ticks restored by a card-routine retry, preventing
an old movement from surviving teardown. The bank remains unavailable for new
input until cleanup completes. Native tests exercise re-entry, peer-bank dispatch,
retry ordering, and cleanup requested during a handler. They do not execute the
full yielding UI path or establish shared-model lifetime safety. Independent mode
remains disabled.


### Audio shift failure handling

Audio-clip shifts now reject recording, missing-sample, and out-of-range sample
positions before shifting any automation or expression. These rejection paths
leave the clip unchanged. Automation-only shifts report success, allowing their
callers to record undo history. Successful sample shifts resume the edited clip
instead of resolving the currently selected clip.

Horizontal-shift undo propagates a rejected shift through existing failed-undo
recovery instead of reporting success and moving the action to the opposite
history queue. Earlier successful consequences are still not rolled back. Native
unit tests and the firmware build are regression checks, not direct tests of
audio playback or these sample-shift rejection paths. Independent mode remains
disabled; broader lifetime and transaction protection are still required.


### Clip-length sample-marker ownership

Clip-length consequences retain a start/end marker identifier instead of a raw
pointer into an AudioClip's sample holder. Undo first checks song membership, then
requires an audio clip before resolving and swapping the selected marker. Invalid
marker identifiers fail before changing marker or clip length. All three audio
length-edit paths record the marker identifier, preserving forward/reversed edits.

This removes a retained interior pointer; it does not establish clip generations,
protect sample replacement, or make setClipLength transactional. The native suite
and firmware build are regression checks, not direct marker-undo or two-device
tests. Independent mode remains disabled.


### Horizontal-shift undo grouping

The view's direct horizontal-shift history shortcut now requires the same UI,
retained clip, and automation/sequence/MPE flags before accumulating another
movement. It retains the existing panel, song, output and open-action checks.
A changed view or shift policy starts a separate action rather than applying
the first movement's undo flags to later edits. Accumulation uses a wide range
check so both the stored amount and its inverse remain representable. Reversion
also rejects an unrepresentable inverse before mutation.

These checks do not reserve history before the edit, recover an allocation failure
after a shift, or protect context across every yielding callback. Native tests and
the firmware build are regression checks, not direct tests of this UI/history
shortcut. Independent mode remains disabled.


### Checked encoder-to-shift conversion

Horizontal clip shifts now calculate grid width and encoder multiplication in
64 bits, then reject movements whose forward or inverse value cannot fit int32_t.
Invalid grids and zero movements do not edit the clip or create history. The same
range predicate is used for history accumulation and reversion. This closes the
overflow before the previously added history checks; it does not reserve history
before mutation or fix post-edit allocation failures.

Native tests exercise the production conversion across ordinary encoder deltas,
positive and negative limits, extreme grid endpoints, invalid widths and undo
negation. Full UI/history failure injection and shared-model lifetime work remain
unfinished. Independent mode stays disabled.


### Reserve horizontal-shift history before mutation

The clip-view shift path now obtains its action and reserves a new consequence
before editing. Failure to obtain an action skips the edit; consequence allocation
failure reports insufficient RAM and skips it. A new consequence remains privately
owned until a successful shift, and is freed if the operation is rejected. Existing
compatible consequences accumulate only after success. Context and history-head
checks avoid dereferencing a retained action after detected replacement.

Creating an action still follows the logger's existing policy of clearing redo and
closing previous actions, even if the later shift is rejected. Empty actions may
remain after an unsuccessful attempt. This is not a transaction or lifetime lock:
history changes during the shift, address reuse, and in-place mutations during
yielding callbacks remain unresolved. The post-shift check can abandon recording
a completed edit when its original context is gone. Independent mode remains
disabled. Native tests and the firmware build are regression checks, not direct
allocation-failure injection for this UI path.


### Revalidate in-place shift-history changes

Shift history checks now verify action ownership, song/output, type, open state,
clip and view after reservation and after the shift. An existing consequence must
also retain its original amount and remain compatible with the requested clip,
flags and reversible range. Pointer identity alone no longer authorizes the final
amount accumulation. Checks establish context and history-head identity before
dereferencing retained history.

This detects additional in-place changes but is not a lock, generation check, or
rollback mechanism. A conflict detected after a completed shift can still leave
that edit unrecorded. Direct reentrant-history tests remain outstanding; the native
suite and firmware build are regression checks. Independent mode stays disabled.


### Preserve redo for already-invalid audio shifts

The clip-view shift path performs a read-only preflight before preparing history.
Audio clips reject sample shifts while recording, without a sample, or beyond the
allowed sample-start bounds. Already-rejected attempts therefore no longer clear
redo or close the previous action. Automation-only shifts remain eligible.
Audio shift execution repeats the same validation before mutation.

A failure or context change after history preparation can still leave redo cleared;
this is not transactional history allocation. Native tests and the firmware build
are regression checks, not direct audio/redo-path tests. Independent mode remains
disabled pending the remaining transport, lifetime and recovery work.


### Checked sample-shift conversion

Audio-shift preflight and execution now use a checked sample-position conversion.
It rejects nonpositive clip lengths, reversed marker ranges, overflowing products,
out-of-bounds starts and overflowing resulting end markers before mutation.
Ordinary conversion preserves truncation toward zero; end markers beyond the
sample file remain permitted. Unrepresentable intermediate products are rejected
conservatively, even if division could bring the final distance back into range.

Native tests cover production conversion boundaries, extreme signed tick values,
unsigned overflow and ordinary positive/negative shifts. Other ticks-to-samples
callers still use the existing conversion. This does not establish shared-object
lifetime protection or transactional history. Independent mode remains disabled.


### Safe note/automation shift normalization

The shared ordered-array horizontal shift now returns without mutation for
nonpositive loop lengths and uses signed remainder to normalize movement. It no
longer negates INT32_MIN or performs modulo by zero. For positive loop lengths,
the normalized amount and its inverse fit int32_t before cutoff calculation.
Normal valid-input wrapping and array rotation are unchanged.

This low-level void API does not report invalid lengths to undo callers. Native
tests and the firmware build are regression checks, not direct execution of this
array implementation at boundary values. Independent transport, object lifetime
protection and full undo recovery remain unfinished; independent mode is disabled.


### Propagate invalid clip and row lengths

Clip shift preflight now requires a positive parent loop length, including
automation-only audio shifts. Instrument clips validate every note row before
moving clip automation: zero independent length inherits the parent, while a
negative independent length rejects the operation. Instrument shift execution
repeats preflight, so invalid lengths return failure to undo recovery instead
of silently skipping low-level arrays and reporting success.

These checks cover whole-clip shifts, not every individual note-row edit or
automation API. Native tests and firmware compilation are regression checks;
direct malformed-length undo tests remain outstanding. Independent mode remains
disabled pending transport, lifetime and transactional recovery work.


### Bind note-row undo to its edited clip

Note-row length and horizontal-shift consequences now retain the edited instrument
clip instead of resolving the current selection during reversion. Length, shift
and mute undo validate song membership and instrument type before looking up the
row. Missing rows fail through undo recovery. Length and shift also reject invalid
effective lengths; shift rejects an unrepresentable inverse instead of overflowing.
The shift missing-row path returns an error rather than freezing debug firmware.

Row lookup still uses the existing row ID and does not detect deletion followed by
replacement at the same ID, nor clip address reuse. This is entry validation, not
a lifetime lock or transactional rollback. Native tests and firmware compilation
are regression checks; direct deleted/replaced-row undo tests remain outstanding.
Independent mode stays disabled.


### Direct undo regression suite

`tests/undo` now compiles seven production consequence implementations against
bounded model doubles, with optional address/undefined-behavior sanitizers. It
replaces the earlier regression-only caveat for the tested consequence validation,
mutation ordering and round-trip behavior. It does not replace integration testing
of the real action logger, allocator, song ownership, UI or playback.

The suite first reproduced acceptance of a nonpositive clip-length snapshot and
a failed note-array snapshot. Reversion now rejects both before changing markers,
length or notes, and rejects an invalid current clip length before creating an
invalid redo snapshot. Existing action creation already filters failed note
snapshots; the new guard makes the consequence itself reject one if invoked.

See [the coverage table and run instructions](../../tests/undo/README.md).
Independent mode remains disabled.


### Retry committed OLED frames under DMA congestion

The client now retains the latest complete OLED frame when all mirror DMA buffers
are queued or in flight. Transport servicing retries publication after a buffer
becomes available, even when the host sends no further display changes. Incoming
uncommitted deltas cannot alter this retained image. A newer commit replaces the
pending image, and disconnect discards it. The mutually exclusive host/client
roles reuse the existing snapshot storage, adding no framebuffer allocation.

Three production-runtime tests cover congestion retry and partial-frame isolation,
latest-complete-frame replacement, and disconnect/reconnect cleanup. The existing
test continues to check queued and active DMA buffer protection. This closes a
display-delivery gap in the shared mirror transport; it does not negotiate an
independent session or enable Remote UI dispatch. Physical DMA validation remains
required.

### Copy completed Remote OLED frames for transport

`OLED::copy_remote_frame` copies the last completed Remote image and returns its
revision without entering Remote UI scope or exposing a mutable canvas pointer.
Transport can retain this copy across packet sends while subsequent rendering
continues. No image is returned before publication, invalid destination sizes
leave the destination untouched, and revision zero remains valid after wrap.
The copy must run in cooperative code without yielding; it is not an interrupt
or multithread synchronization mechanism.

Four native tests compile the production accessor body against real frame and
session storage. They cover ownership selection/restoration, unpublished and
invalid-size rejection, working-canvas isolation, subsequent publication, and
revision wrap. This provides the OLED source API for independent transport;
negotiation and Remote dispatch remain unimplemented and independent mode stays
disabled.


### Bind row undo to row lifetime

Note-array, row-length, row-shift and row-mute consequences now capture a row
identity at construction. They reject a different identity at reversion before
mutating the replacement row, even if its row ID and address match the deleted
row. New rows receive boot-lifetime monotonic 64-bit identities; exhaustion
returns zero and disables binding instead of reusing an identity. Array relocation
preserves identities, while clip cloning renews copied row identities. The
identity adds eight bytes of data per row and per affected consequence, plus
possible alignment padding. It is not serialized.

Three production-consequence tests cover same-address replacement, relocation,
and a row created after capture. Source contracts verify production initialization,
ordering-key placement and clone renewal. The model storage in these runtime tests
is doubled; real array relocation and clip cloning still need integration coverage.
This is stale-target rejection, not a lifetime lock during yielding callbacks or
transactional undo recovery. Independent mode remains disabled.

### Match snapshot reuse to the live row identity

Action note-array snapshot lookup now requires the captured row identity to match
its live edit target and the captured note vector to be valid. Recreating a row
at the same ID therefore cannot suppress a fresh snapshot. Rejected entries are
left in place; a matching live snapshot can still be moved to the front without
losing other consequences. Callers must supply a live clip, as elsewhere in action
recording; this lookup does not establish clip lifetime protection.

Four native tests execute the production lookup method with real consequences
and doubled Action/model storage, covering replacement, list ordering, invalid
or missing targets, and failed snapshots. This does not make a mixed action with
stale consequences transactional. Independent mode remains disabled.

### Validate individual-note undo targets

Individual-note existence consequences now capture the edited row identity and
validate song membership, instrument type and row identity before resolving or
mutating notes. A freed clip is rejected before dereference, and replacing a row
at the same ID/address cannot redirect old note insertion or deletion history.
Missing-note deletion retains its existing no-op behavior for iteration-dependent
clip-multiply redo.

Six runtime tests compile the production consequence and Note implementation
against bounded model/vector storage. They cover deleted clips, invalid contexts,
replacement rows, create/delete round trips preserving every note attribute,
allocation failure followed by retry, and missing-note deletion. Allocation-time
callbacks and note replacement at the same position within a surviving row remain
outside this guard. This does not provide transactional undo or enable independent
mode.

### Reserve and revalidate individual-note recreation

Individual-note undo now copies restoration metadata before reserving vector
space, then checks the active song, clip membership/type and row identity again.
It re-resolves relocated row storage, searches again for conflicts, and inserts
without allocation before initializing the note. A note already at the restored
position causes failure rather than creating a duplicate. Reservation and commit
failures propagate without initializing a note.

Seven additional production-consequence tests simulate song changes, clip
removal, row replacement/relocation, a conflicting note, commit failure and changed
history metadata during reservation. These checks prevent stale accesses after
reservation returns; they do not protect accesses *inside* the allocator/vector
operation if its owner is destroyed or relocated by a callback. Full lifetime
protection and transactional recovery are still required; independent mode stays
disabled.

### Propagate failure when recording clip deletion

`Action::recordClipExistenceChange` now checks the deletion consequence result
before adding history or resetting clip scroll snapshots. Failure destroys and
frees the unused consequence and returns false, leaving existing history in place.
Five tests compile the production Action method with injected allocation and
clip-deletion collaborators, checking failure cleanup, existing history,
allocation failure, successful deletion and creation-only recording.

The current clip-deletion consequence normally succeeds or freezes rather than
returning an error. These tests cover the Action caller's failure contract, not
end-to-end clip deletion or rollback. Any future recoverable deletion failure must
leave the clip song-owned before returning; partial deletion still requires a
separate recovery design. Independent mode remains disabled.

### Preflight clip deletion against its owning array

Clip-existence reversion now rejects missing context, null clips and foreign clip
arrays before constructing a timeline stack. Deletion additionally requires the
clip to be present in the exact song-owned array before selection, playback or
recording mutations. Song lookup compares retained pointers without dereferencing
them, and resolves the current index instead of trusting a captured index. The
later missing-index path returns an error instead of freezing at E244.

Three native tests execute production Song lookup for exact-array membership,
foreign/null/freed targets and changing indexes. A source contract checks that
consequence preflight precedes target access and mutation, and that missing-index
failure returns an error. The full deletion consequence is not executed by these
new tests. Mutations or destruction inside callbacks still require lifetime
protection; errors after playback/recording changes are not rolled back. Independent
mode remains disabled.

### Direct action-reversion failure coverage

Action reversion now rejects missing model-stack/song context before detaching
history or clearing arrangement content. Six tests execute the production
`Action::revert` method with controlled consequence and arrangement-clear
collaborators. They cover ordinary error propagation and stopped dispatch,
list reversal/round trips, failed arrangement clearing with retained old/new
chains, cleanup after an arrangement consequence fails, and skipped parameter
replay with cleanup.

This verifies the current recovery machinery and its chain ownership. It does not
make partially reverted actions atomic, protect callbacks from deleting the
executing action, or resolve clip-restoration ownership failures. Independent mode
remains disabled.

### Preflight detached-clip restoration ownership

Clip recreation now requires actual detached ownership, a song-owned destination
array, a non-null clip and an insertion index within the current array bounds.
A clip already in either song registry is rejected; detached ownership is cleared
in that case so failure cleanup cannot destroy a song-owned clip. These checks
run before array reservation or parameter reattachment.

Four tests execute the production preflight with controlled consequence storage,
covering ownership, duplicate membership and ownership reconciliation, insertion
bounds, and foreign/freed arrays. A source contract verifies the call precedes
reservation and reattachment. The existing full consequence is not exercised by
these tests. Changes during reservation/reattachment and insertion failure after
reattachment still need coordinated recovery; independent mode remains disabled.

### Revalidate clip restoration after reservation

Clip restoration now uses a checked reservation step before parameter reattachment.
It verifies active-song identity, detached ownership, target metadata and both
panels' structural revisions after reservation returns. A changed but still valid
insertion index is rejected. Ownership reconciliation runs even when allocation
failed, so a clip returned to the song during the callback is not destroyed by
failure cleanup.

Six tests execute the production reservation method with callback injection for
allocation failure, invalid initial ownership, song changes, duplicate ownership,
either panel's structural changes and changed indexes. The source contract keeps
reservation before parameter reattachment. This guards the boundary after
reservation; it does not protect the executing consequence or array inside a
callback, nor recover failures after parameter reattachment. Independent mode
remains disabled.

### Check clip restoration insertion before ownership transfer

Clip restoration now commits through `insert_at_index_without_allocation` rather than
ignoring an allocating insertion result. Commit rechecks the active song,
destination/index and detached ownership, inserts the clip pointer, and only then
transfers ownership to the song. Failure returns before peer insertion notification
or further activation work, retaining detached ownership unless the song already
owns the clip.

Four native tests execute the production commit method against controlled array
storage, checking success, insertion errors, duplicate ownership and changed
song/index. A source contract checks failure propagation before notification.
These tests do not execute the underlying ring array or parameter reattachment.
Returning failure still does not roll back partially restored parameters, and
callback-internal lifetime protection remains required. Independent mode stays
disabled.

### Require exact parameter ownership during undo reattachment

Clip and sound-drum-row undo reattachment now require parameter backups belonging
to the exact clip instead of falling back to a different clip or generic backup.
Missing backups return an error through the consequence instead of freezing at
E245, E229 or E046. This preserves other clips' backups when history is incomplete;
it deliberately fails restoration rather than substituting another clip's state.

Three native tests execute the production exact-lookup/transfer method with backup
storage and parameter-collection doubles. They cover missing exact matches,
non-mutating lookup, and exact transfer including expression parameters. Source
contracts guard the undo callers and removed freeze paths. Pointer-key conversion
now explicitly passes through uintptr_t before narrowing to the existing 32-bit
firmware key, allowing native compilation without changing the on-device key.
This does not roll back rows already restored before a later missing backup, or
provide callback-internal lifetime protection. Independent mode remains disabled.

### Integration with Harden-auto-param

The parameter-pool branch through `4bda218b72` is integrated. Moved menu, MIDI-follow,
kit, automation-selection and knob-rendering implementations retain this branch's
session accessors. Scalar/current-value separation and pooled automation coexist
with the undo validation and mirroring transport changes; independent mode remains
disabled.

The combined native build includes parameter layout, transfer, cleanup, backup,
lookup, persistence and lifecycle suites alongside the mirror/undo suites. Lookup
regressions cover different Local/Remote selections and menu parameter owners with
diagnostics enabled and disabled. The original source-routing audit follows moved
implementations rather than dropping their requirements. This is native/build
validation; two-device hardware and the remaining independent-mode blockers still
require work.

### Preflight complete kit parameter restoration

Kit undo now checks its kit-level backup before beginning row restoration, and
row restoration checks every required exact drum/clip backup before transferring
any collections. Duplicate rows referencing the same sound-drum backup key fail
preflight rather than consuming that backup once and failing on the second row.
MIDI and unassigned rows require no sound-parameter backup.

Five tests execute production row restoration and exact Song lookup against
controlled row/backup storage. They cover a missing later backup, duplicate keys,
successful main/expression restoration, non-sound rows and missing context. A source
contract verifies kit-level preflight precedes row transfers. This prevents
partial restoration caused by initially missing or duplicate backups; it does not
roll back changes introduced by callbacks during subsequent transfer/trimming.
Independent mode remains disabled pending lifetime, recovery and transport work.

### Stop kit restoration after callback invalidation

Row restoration now checks the current song and both panels' structural revisions
after parameter transfer and trimming, before accessing the clip or another row.
Three native regressions exercise song replacement, either panel's invalidation,
and deletion of the restored clip during trimming. These execute the production
restoration method with controlled storage and callbacks under the undo suite's
sanitizers. They protect subsequent accesses, not accesses inside a callback or
rollback of parameters already transferred. Independent mode remains disabled.

### Revalidate clip reattachment before recreation continues

The clip-existence consequence now rechecks song, destination, index, ownership
and both panels' structural revisions after output reattachment, including error
returns. If a callback returned the clip to the song, detached ownership is cleared
before failure cleanup. Invalidated restoration stops before diagnostic clip access
or insertion. Four native cases execute the production helper with injected
reattachment, covering ordinary errors, returned ownership on failure, structural
invalidation and changed song/index. Callback-internal lifetime protection and
rollback of partially reattached parameters remain unresolved; independent mode
remains disabled.

Reattachment also requires the model stack's timeline to match the restored clip,
using the null-safe accessor before any parameter mutation. After callbacks it
rechecks the stack's song and timeline independently of the global song and
consequence fields. Three native regressions cover missing/foreign timelines and
callbacks changing either stack field. This validates context at the boundary;
it does not extend object lifetime or enable independent mode.

### MIDI undo restoration failure boundary

Instrument-clip MIDI reattachment returns `Error::BUG` for invalid restored
parameters instead of freezing at PM22 in diagnostic builds. After MIDI restoration
it checks the current song and both panels' structural revisions before reading
output or parameter state again. Native cases execute the production instrument
reattachment method with controlled MIDI restoration and parameter-type checks,
covering invalid/valid parameters, song and panel invalidation, and clip deletion.
The MIDI transfer/trimming internals remain doubled; callback-internal lifetime
protection, rollback and independent transport are still pending. Independent mode
remains disabled.

### Shared clip parameter restoration boundaries

Base clip reattachment validates song, output and timeline before consuming the
exact parameter backup. It checks song and panel structural revisions after
transfer and after trimming, returning an error if callbacks invalidated the
context. Three native regressions execute this production method with controlled
parameter storage and trimming: invalid timeline without backup consumption,
successful main/expression transfer, song/either-panel invalidation and deletion
during trim. These checks do not protect accesses inside transfer/trim callbacks
or roll back consumed backups. Independent mode remains disabled.

### Reject backup-array transfer destinations

Exact-clip backup transfer now rejects destinations inside the backup array before
mutation. This covers both self-transfer (which can destroy the source) and another
backup entry (which may move when the source entry is removed). Null output pointers
are also rejected; null clip keys still support generic-backup lookup. Two native regressions execute
the production lookup/transfer method and verify all stored and destination values
remain unchanged on rejection. Existing callers supply live clip/row managers.
This guards invalid transfer requests; it does not establish callback lifetime
protection or rollback. Independent mode remains disabled.

### Stop action replay when its song context changes

Action replay now validates its initial song and detects changes to either the
current song or the supplied stack's song after arrangement clearing and each
consequence. It returns an error before dispatching more consequences or destroying
pending arrangement history with the changed context. Processed and pending chains
remain reachable for failure handling. Three native cases cover a song change
midway through ordinary replay, a stack change during arrangement clearing, and
a song change during arrangement replay. This does not protect an action deleted
inside a callback, roll back processed consequences, or establish safe outer
cleanup after song destruction. Independent mode remains disabled.

Action replay also checks context before every consequence dispatch and before
returning success. This catches song/stack changes during arrangement consequence
cleanup, including cleanup of the final consequence. Three native tests verify
pending dispatch and cleanup stop, generated history remains reachable, and final
cleanup invalidation returns an error. This guards subsequent dispatch; it does
not protect cleanup internals or fix the outer logger's stale-song cleanup path.

### Reconcile clip ownership at destruction

Clip-existence cleanup now checks song membership before destroying a clip marked
detached. A clip returned to either song array is retained and the cached detached
ownership flag is cleared. Two native cases execute the production cleanup method:
returned ownership in either array, and normal detached destruction with exact
backup removal and repeated-cleanup protection. Backup deletion is doubled. This
requires a live song argument; it does not solve the outer logger's stale-song
pointer or callbacks inside destruction. Independent mode remains disabled.

### Avoid retaining backup entries across explicit audio callbacks

Clip-backup cleanup now services its explicit audio callback after processing an
entry, then restarts the scan. It no longer holds a backup-entry pointer across
that callback, and callback-driven insertion/removal cannot invalidate its next
scan index. Three regression scenarios run in both SongBackupTests configurations:
null-clip cleanup preserving generic backups without restarting indefinitely, removing the owner's backups from the callback and inserting another matching
backup that the restarted scan must process. The tests use production backup and
parameter-management code with controlled table storage. Song destruction inside
callbacks and callbacks inside collection operations remain lifetime gaps. Scan
restarts add work proportional to previously visited entries; independent mode
remains disabled.

### Preserve generic backups without replacement allocation

When removing a clip backup that needs a new generic entry, cleanup now rebuilds
that entry in its existing slot and repositions it into sorted order. It no longer
deletes the slot and allocates a replacement, so insertion failure cannot discard
its main parameters. Removed clip expression parameters are still destroyed. The
former reinsertion-failure regression now requires successful generic conversion
with insertion disabled, preserved neighboring clip state and balanced collection
ownership. Both diagnostic configurations exercise it alongside randomized backup
lifecycle cases. The table is doubled in native tests; real array relocation and
callback-internal lifetime protection remain integration concerns. Independent
mode remains disabled.

### Keep deleted clip expression out of generic backups

Converting the first backup for an output into a generic backup now discards the
deleted clip's expression collection while preserving its main collection. This
matches conversion of later entries and prevents a subsequent restore from
inheriting the deleted clip's expression state. Native regressions cover both
expression-present and expression-absent managers, main collection identity,
conversion with insertion disabled, subsequent restoration and balanced allocation
ownership in both diagnostic configurations. Table storage remains doubled and
callback-internal lifetime protection remains unresolved. Independent mode remains
disabled.

### Report partial-undo rollback failure

The reversible-prefix helper now distinguishes successful application, successful
rollback, and failed rollback. If recovery fails, it stops invoking rollback but
reconnects every history node. Partial undo propagates the rollback error and
discards the unusable history instead of allowing it to be retried. Unit tests
inject failures at both rollback positions and verify all links remain reachable;
existing tests cover ordinary apply failure and successful replay. A source
contract guards the logger's failure cleanup. This does not restore the model after
failed rollback, protect callback-time object lifetimes, or validate the full
logger on hardware. Independent mode remains disabled.

### Revalidate note snapshot targets after allocation

Note-array snapshot recording validates its target and captures row identity before
allocating consequence storage. After allocation it checks the current song, both
panels' structural revisions and row identity before constructing the snapshot or
stealing notes. Invalidated allocation is freed and the caller receives an error.
Three native cases execute the production recording method with allocation
callbacks: song change without note stealing, either panel's invalidation with
storage release, and row replacement followed by stable-target success. This does
not protect callbacks inside snapshot cloning or Action lifetime. Independent mode
remains disabled.

### Validate cloned snapshots before publishing history

Note snapshot recording now revalidates song, panel revisions and row identity
after consequence construction, which can allocate while cloning notes. An
invalidated snapshot is destroyed before being linked into history. Four native
cases cover either panel's clone-time invalidation, row replacement, clip deletion
and clone allocation failure preserving existing history. These execute production
recording/consequence code with controlled vector cloning; they do not establish
safety inside the real clone operation or protect Action lifetime. Independent
mode remains disabled.

### Real ring-array reposition regression

The native parameter lifecycle suite now exercises production ResizeableArray
repositioning with full multiword entries. It covers every source/destination pair,
all eight physical start offsets, and lengths one through eight, including full
and wrapped rings, while firmware allocation is disabled. Logical entry order,
payload and count are checked against an independent rotation reference. This
closes the primitive relocation-test gap for generic-backup slot reuse; the full
backup algorithm still uses controlled table storage in its dedicated suite.

Readiness remains governed by the completion-gates table above. The accumulated
boundary checks and native tests do not establish comprehensive callback lifetime
protection or a working independent protocol. Independent session negotiation,
Remote input dispatch/frame delivery, full failure recovery and two-device
validation still require implementation and integration work before enablement.

### Terminal protocol failure before deferred teardown

A failed mirror session now ignores all subsequent incoming packets until the
scheduler completes teardown. This prevents panel replay or input acknowledgement
after a protocol error or Stop while storage delays disconnect. Two runtime tests
exercise the production receive path: sequence failure followed by panel traffic
under the storage lock, and Stop followed by an otherwise valid input ACK. Existing
teardown still releases inputs and restores local UI state. This strengthens the
current mirror transport; independent-session negotiation and Remote dispatch/frame
transport remain unimplemented and independent mode remains disabled.

### Release input ownership before teardown callbacks

Mirror teardown clears each remote-held key before dispatching its release,
so a local press observed during that callback is not suppressed as remotely held.
Starting a session is also rejected while mirror processing or transport is busy,
including teardown callbacks. Two production-runtime tests cover local input during
release and a restart rejected during cleanup but accepted afterward. Independent
mode remains disabled; this addresses current transport reentrancy, not Remote
session negotiation or shared-model lifetime.

Teardown now snapshots and clears the complete remote-held key set before the
first release callback. Per-key local ownership is checked at dispatch time, so a
callback-delivered local press of a different formerly remote-held key is neither
suppressed nor immediately released. Two runtime regressions cover that cross-key
case and exactly-once release of multiple remote keys. This completes the known
multi-key teardown ownership gap; it does not enable independent sessions.

### Distinct reconnect tokens within a boot

Client connection attempts now use a clock-seeded counter that persists across
teardown, rather than deriving every token independently from the clock. A repeated
clock sample therefore cannot immediately reuse an old session token. Runtime
coverage reconnects without advancing time, rejects an old Accept, and accepts the
new session's acknowledgement. A protocol test covers all 16383 nonzero tokens and
wraparound. The existing 14-bit wire format still permits reuse after a full cycle
or reboot; this is not an unbounded replay guarantee. Independent-session protocol
integration and enablement remain pending.

### Reset OLED delta state on client reconnect

Beginning a client connection clears its incoming OLED buffer and pending commit
state. A new session's partial frame or early commit therefore cannot inherit
pixels from a previous host. Two runtime regressions cover a committed old block
followed by a different partial frame, and an uncommitted old block followed by an
early commit. Unreceived blocks start blank; the host still sends a complete
initial snapshot. Independent Remote frame transport remains pending.

### Revalidate client startup after callbacks

Client startup now checks the captured song, panel owner, USB connection and
playback/recording state between menu exit, input cleanup, audition/note cleanup,
voice cleanup, MIDI flushing and display preparation. Invalidated startup returns
before further song access or timer suspension. Three runtime regressions inject
song removal/replacement, USB disconnect and playback start during those steps.
This protects subsequent startup operations; it does not roll back cleanup already
performed or protect callback-internal object lifetimes. Independent mode remains
disabled.

### Bind deferred startup to its initiating context

A pending Mirror request now records its initiating song and panel owner. Startup
consumes and cancels that request if either changed before the scheduled task runs,
instead of suspending a different context. Duplicate pending requests are rejected.
Three runtime tests cover changed song with an explicit retry, changed panel with
no delayed restart, and a duplicate request producing only one handshake. Pointer
identity is not a lifetime pin or generation token; same-address song replacement
remains part of the broader lifetime work. Independent mode remains disabled.

### Authorize host input only after acceptance

A host reserves its peer on Request but now rejects Input until Accept has been
successfully queued. Congestion cannot leave an unaccepted connection able to
operate the host. Requests also require a current song; host playback remains
allowed. Three runtime regressions cover pre-accept input, acceptance congestion,
and missing-song rejection followed by successful input while playback is active.
Independent negotiation and Remote dispatch remain unimplemented.

### Stop outbound frames after transport failure

The send path now rejects every operation except Stop once a session has failed.
If a send callback reports protocol failure, later panel/OLED sends in that same
transport pass cannot continue publishing frames. A runtime regression injects
that failure during sync-LED transmission and verifies frame suppression plus
normal Stop delivery. Another verifies a congested handshake retries Accept before
sending frames. Independent transport integration remains pending.

### Publish pending input before sending

The client now registers its in-flight input sequence before sending, so an ACK
received from a transmission callback matches the pending event. If transmission
is blocked, the pending ACK is cleared and the input remains queued for retry.
Two runtime regressions cover callback-time acknowledgement without duplicate
transmission, and congestion retry without leaving a phantom pending ACK. This
hardens existing mirror input ordering; independent Remote dispatch remains pending.

### Serialize packet transmission across callbacks

A send-in-progress guard now prevents nested sends and defers main/transport
routine re-entry until the packet sequence has advanced. Startup is also rejected
while transmission is active. Two runtime tests cover an input-send callback
attempting heartbeat transmission and a Stop received during a heartbeat whose
callback attempts immediate teardown. Subsequent packets retain distinct ordered
sequences, and teardown runs after transmission returns. Independent-session
transport and shared-model lifetime integration remain pending.

### Queue input arriving during Accept transmission

The host now recognizes the interval inside an actual Accept transmission, after
send-capacity checks pass. Input received during that callback is queued for normal
dispatch after transmission and initial snapshot preparation complete. Input before
transmission or while acceptance is congested remains rejected. Two runtime tests
cover exactly-once dispatch/ordered ACK after callback-time input, and Stop during
Accept suppressing subsequent input. Independent mode remains disabled.

### Stop dispatch after ACK-time session failure

Host input processing now checks failure immediately after ACK transmission, before
reading another queued command in that iteration. A Stop received during the send
therefore cannot permit one extra edit. Two runtime cases verify pending input is
not dispatched after Stop, while valid input arriving during ACK transmission is
dispatched and acknowledged exactly once. Independent protocol integration remains
pending.

### Preserve SysEx-only routing through host teardown

The closing host peer remains classified as a mirror-client connection while
remote-key release callbacks run, even after the session state becomes Idle.
Musical MIDI emitted during those callbacks therefore remains subject to the
existing enqueue/dequeue filter. A runtime regression checks that note and clock
packets are rejected, SysEx is allowed, other peers are unaffected, and ordinary
routing resumes after teardown. This covers the cleanup classification boundary;
it is not two-device USB validation. Independent mode remains disabled.

### Remove pre-negotiation musical packets from the send queue

Host acceptance now compacts the negotiated device's queued USB packets under an
interrupt guard, retaining only SysEx in original order. Packets queued before
negotiation therefore cannot survive until after teardown and bypass a filter that
has returned to ordinary routing. The in-flight USB buffer is untouched. Native
coverage exercises real queue compaction with counter/ring wrap, mixed traffic,
empty cleanup and in-flight-buffer preservation; a runtime case verifies only the
accepted peer is selected. The interrupt guard and hardware driver are doubled in
native tests. Two-device validation and independent mode remain pending.

### Preserve a full USB send ring

USB enqueue now rejects packets when the ring is exactly full, rather than only
when its count already exceeds capacity. This prevents overwriting the oldest
packet and corrupting queued SysEx during congestion. A production-queue regression
fills a ring across counter wrap, attempts overflow, verifies every original
packet remains ordered, and confirms enqueue works again after draining. Another
covers queue purging across all 16 USB cable numbers, preserving SysEx and removing
notes, CC and clock. Independent mode remains disabled.

### Clear stale transfer length on empty dequeue

An empty USB dequeue now clears `numBytesSendingNow`. The host's multi-device
flush loop calls dequeue without using its return value, so retaining an old
length could expose previous transfer data as pending output. Two production-queue
regressions cover empty dequeue in both USB roles, repeated empty calls, unchanged
transfer-buffer contents, purge after an earlier transfer, and subsequent enqueue.
This validates queue metadata; hardware USB completion remains untested here.
Independent mode remains disabled.

### Skip empty prepared USB host transfers

After preparing the circular host device range, the flush path now selects the
first connected device with nonzero prepared bytes. If filtering emptied every
queue, it exits without setting the sending flag or starting a zero-length
transfer. Native tests exhaust all four-device circular ranges and readiness sets,
and exercise selection after real queue filtering. A source contract verifies the
host performs this selection before starting transmission. The full USB driver and
interrupt completion path still require hardware validation. Independent mode
remains disabled.

### Capture single-note values before consequence allocation

Single-note history recording now copies note values before allocating consequence
storage and revalidates song, panel revisions and row identity afterward. It no
longer reads the original Note pointer after allocation callbacks. Three native
regressions cover source-note deletion, either panel's invalidation with storage
release, and row replacement. The production recording method and consequence
constructor run with controlled allocation. Recording still has a void API:
propagating failure to edit callers and protecting their subsequent mutations
remain required work, as does Action lifetime protection. Independent mode remains
disabled.

### Propagate single-note recording errors to immediate edit callers

Single-note recording now returns an Error, and all three immediate callers stop
on failure. Deletion leaves the note intact if consequence allocation fails and
stops after target invalidation. Native regressions execute the production deletion
method for both failures and normal success; a source contract checks all three
call sites. Creation inserts its note before recording, so its error return does
not roll back that insertion. The void deletion wrapper also does not propagate
failure to its own callers. Those recovery paths, comprehensive object lifetime
protection and independent transport remain outstanding; independent mode remains
disabled.

### Carry deletion failure through position lookup and UI edits

Both note-deletion wrappers now return Error. Position-based deletion propagates
recording failure before requesting a playback event, and both UI callers stop
before further expression edits or destination insertion. Missing notes remain
successful no-ops. The index-based caller without an Action cannot encounter a
history-allocation failure and retains its existing behavior. Native deletion
tests now assert the exact error; source contracts protect propagation through the
position wrapper and UI callers. Earlier edits in a multi-note operation are not
rolled back, and source contracts do not execute the complete UI workflow.
Independent mode remains disabled; transport, lifetime and recovery gates remain.

The drag-scroll grab helper also reports failure to its caller, which stops before
row movement and reinsertion. A separate source contract guards this outer return.

### Revalidate even when single-note allocation fails

Single-note recording now checks song, both panel revisions and row identity after
failed allocation as well as successful allocation. An invalid target reports BUG
instead of a recoverable memory error, without reading a clip after a detected
song/revision change. Three native regressions inject failed allocation alongside
either panel's invalidation, row replacement, and song replacement with clip
destruction. These boundary checks do not establish comprehensive lifetime safety
or roll back already-inserted notes. Independent mode remains disabled.

### Recover inserted notes on history allocation failure

CREATE recording now removes the already-inserted note on allocation failure after
revalidating song, panel revisions and row identity. It searches the refreshed row
by position and checks every note value before removal, avoiding cached pointers
and indices across allocation. Changed or missing notes report BUG without deletion.
Both creation callers already stop on error. Three native regressions cover
neighbor preservation, vector relocation with an earlier insertion, and preserving
a modified note. Existing invalidation tests cover rejection before recovery.
This is not a complete edit transaction: earlier MPE clearing, partial multi-note
edits, and unannounced replacement by an identical note are not resolved.
Independent mode remains disabled, with transport and lifetime gates outstanding.

### Refresh creation pointers after history allocation

Successful CREATE recording now reacquires and validates the note by position and
values after allocation, then returns its current pointer to both creation callers.
A changed or missing target rejects publication and frees unused consequence
storage. A pre-existing row snapshot returns the original pointer without allocating.
Three native regressions cover vector relocation/index changes on success,
modified-target rejection and the existing-snapshot fast path; the caller source contract requires the refreshed output.
This closes a stale note-vector pointer boundary, not row/Action lifetime or MPE
transaction recovery. Independent mode remains disabled.

### Stop single-note edits when the row moves during allocation

Single-note recording now checks row address as well as undo identity after
allocation. Row relocation preserves identity, but immediate callers still hold
the old NoteRow pointer; returning success would let them resume against freed
storage. Relocation therefore returns BUG before publication or recovery. Native
tests release the original row and preserve its identity in replacement storage:
creation covers both successful and failed allocation, and deletion executes the
production caller. They assert no replacement-row mutation or history publication.
This is a stop-on-relocation boundary, not a rollback of the edit, protection of
Action/clip lifetimes, or support for transparent relocation. Independent mode
remains disabled.

Deletion also requests the refreshed note pointer and searches its current index
after recording. This prevents allocation-time insertion before the target from
causing deletion of a neighbor. Native tests cover vector relocation plus preceding
insertion, and preservation of a target modified during allocation. Failed DELETE
recording never performs the CREATE rollback. Earlier edits remain unrolled back.

### Reject relocated rows during note-array recording

Note-array recording now checks row address as well as identity after consequence
allocation and after cloning. It validates context even when allocation fails,
returning BUG for an invalidated target instead of a recoverable memory error.
Three native regressions cover released source storage during relocation for both
allocation outcomes, relocation after cloning, and either panel's invalidation on
failed allocation. Stable allocation failure still returns INSUFFICIENT_RAM.
These checks prevent use/publication across detected relocation boundaries; they
do not protect the internals of vector cloning, Action lifetime, or roll back
previous mutations. Independent mode remains disabled.

### Bulk note-edit failure propagation and source-vector checks

All 13 note-array recording call sites in NoteRow now stop on snapshot failure.
Bulk additions, area clearing, repeat edits and nudges keep their prepared vectors
private until recording succeeds. Corresponding-note changes free working memory
on failure and skip missing targets; empty rows return before allocating.

Trimming returns errors instead of falling back to an unrecorded destructive trim
when temporary allocation fails. The trim wrapper stops before parameter trimming
and event scheduling, and its immediate row/clip callers stop further work on
failure. Length editing propagates clear-area failures; square selection reacquires
its note after a wrapped edit replaces the vector. Note-off recording stops before
changing note length/lift when history fails.

Sequence clearing snapshots notes before automation/MPE deletion or playback
changes. Repeat generation likewise secures note history before repeating
parameters or flattening direction. Snapshot recording rejects changes to source
count or starting address across allocation/cloning, protecting callers that retain
note pointers or indices at those boundaries.

The native undo suite now executes ten additional production NoteRow methods with
the real Action recorders and consequences. Tests cover eight edit operations with
both allocation failure and structural invalidation, successful edits and retained
snapshots, temporary-allocation failure, clearing order, missing targets, note-off
failure, trim propagation, wrapped length failure, and vector relocation/growth.
Source contracts cover every NoteRow snapshot caller and outer trim/length callers;
they do not execute full UI navigation or repeat-generation playback behavior.

Independent mode remains disabled. Remaining work includes callback safety during
working-vector allocation and inside cloning/parameter operations, content changes
that preserve vector shape/storage, Action/clip lifetime, rollback of already-applied
multi-row/clip/MPE changes, outer undo failure recovery, independent protocol and
remote rendering, and two-device hardware validation. A returned error stops the
covered caller; it does not make a larger edit transaction atomic.

### Validate bulk-edit sources after working allocations

Six bulk-edit paths now capture a NoteRowEditContext before preparing working
memory. They revalidate after search-array allocation and replacement-vector
allocation, including allocation failure, before reading source notes. The context
checks song, both panel revisions, row address/identity, vector count/start address,
and clip/independent-row lengths. Invalidated edits release working allocations and
return BUG; stable memory failures retain their memory-error result.

Native tests exercise released-row relocation at ten allocation boundaries, with
both allocation outcomes, across bulk addition, clearing, repeating, nudging,
corresponding-note changes and trimming. Additional tests change loop lengths,
vector storage/count, or replace the song and destroy the clip. These execute the
production methods and context checks with controlled allocation callbacks.

The context requires live clip/row arguments at construction and is not an object
pin. It does not detect arbitrary destruction without notifications/address change,
same-storage note-content edits, or protect Action lifetime or allocation internals.
Independent mode and its transport remain disabled/unimplemented respectively;
MPE and multi-edit rollback, outer undo recovery and hardware validation remain.

### Stop clearing and trimming across invalidating parameter callbacks

Clearing now validates the row context after its snapshot, each parameter-collection
clear, and note-off dispatch. Replacement of the active collection also stops the
loop before another collection is cleared. Trimming validates the post-note-trim
context after parameter trimming before scheduling playback. Native regressions
release rows during parameter clearing, trimming and note-off, replace a collection,
and replace the song while destroying the clip.

The production stopCurrentlyPlayingNote method now publishes sequenced=false before
note-off dispatch, then returns without writing the row again. Its native tests
cover callback-time row release, reentrant stop suppression, preserving playback
started by a callback, silent stopping and already-stopped rows. Sound dispatch and
parameter-operation internals remain controlled collaborators. Earlier automation
clears or note trimming are not rolled back when a later callback invalidates the
operation. Independent mode remains disabled; comprehensive lifetime, transaction
recovery, independent transport and hardware validation remain outstanding.

### Preserve parameter ownership across bulk-edit callbacks

NoteRowEditContext now captures every parameter-collection pointer, the expression
collection offset, and the clip output. A callback that replaces a later collection,
changes its MPE classification, or switches the output invalidates the operation
before it continues. This extends the existing clearing/trimming and working-memory
checks beyond the active collection. Three native regressions cover later-collection
replacement, expression-index changes, and output/collection replacement during
trimming, with no subsequent playback event or replacement-collection clearing.

Pointer equality does not establish object lifetime after same-address reuse, and
already-applied edits are not rolled back. Callback-internal parameter safety,
Action/clip lifetime, transaction recovery, independent transport and device testing
remain outstanding. Independent mode remains disabled.

### Stop repeat generation at invalidated parameter and resize boundaries

Repeat generation now validates its edit context after snapshot recording and
parameter repetition, before flattening direction or reading notes. Intentional
note-vector growth uses ownership/length/collection checks without requiring the
old vector count/address. Ordinary vector repetition failures now propagate instead
of continuing into tail/iteration processing. Both clip-level callers stop on a
failed row repeat, and zero loop lengths fail before parameter work.

Six native regressions execute production row repeat generation: row release during
parameter repetition, direction preservation on invalidation, failed vector repeats,
successful forward/pingpong repeats, zero lengths, and invalidation during pingpong
resize. Note-vector repeat allocation and parameter operations are controlled
doubles; clip callers have source-contract coverage. Prior parameter/direction or
resize mutations are not rolled back when a later step fails, and outer clip/UI
transactions still need recovery. Independent mode remains disabled.

### Bound array repetition and handle discarded source notes

The real ordered-array repeat helper now rejects nonpositive wrap points, negative
end positions and counts whose byte size exceeds signed 32-bit arithmetic. Empty
arrays and arrays with no eligible source entries return without walking the repeat
range. Shrinking does not reserve memory. Three native real-array tests cover
1,312 wrapped-storage/end-position combinations, complete payload preservation,
invalid/overflow requests, allocation failure preserving existing entries, and huge
repeat ranges with no source notes.

The row caller handles an empty successful repeat result before accessing the final
note and recalculates the retained source count before processing iteration flags.
Two production-row regressions cover complete and partial source removal. Array
storage/allocation are real in the lower-level tests; the row-level repeat helper
remains doubled. These changes do not resolve callback-internal object lifetime or
rollback prior parameter edits. Independent mode remains disabled.

### Harden iteration-dependent repeat processing

FIRST/LAST conditions (zero divisor) now remain attached to copied notes rather
than entering periodic-iteration arithmetic, including when periodic flattening is
requested. The loop/divisor product uses 64-bit arithmetic. Pingpong iteration
processing captures source length before deleting any original note, so shifted
vector entries cannot change the computed reverse-copy position. A missing exact
copy now fails instead of editing the next note returned by a lower-bound search.

Four native regressions cover FIRST/LAST in forward/pingpong and flattening modes,
periodic source deletion among notes of different lengths, loop/divisor overflow,
and a missing copy next to another note. They execute production row processing
with controlled vector repetition. Already-applied copies/parameter changes are
not rolled back on failure; independent mode remains disabled and broader lifetime,
undo recovery, independent transport and hardware validation remain outstanding.

### Preflight pingpong repeat capacity

Row repetition rejects lengths outside signed note-position range and negative
rounded repeat counts. Pingpong repetition calculates rounded capacity in 64 bits
and checks the allocator's signed byte limit before recording undo history or
changing automation/direction. A native regression exercises invalid inputs and
both element-count and byte-count overflow, checking that history, notes,
automation callbacks and event scheduling remain untouched. Existing small-repeat
success tests still exercise accepted requests. This is an input/capacity guard,
not transactional rollback; independent mode remains disabled.

### Commit repeated-row direction only on success

Row repetition now defers flattening its explicit pingpong direction until a
successful return. Failed note allocation or later iteration processing therefore
cannot publish the success-only direction change. Native tests cover allocation
failure preserving notes/direction and successful empty/nonempty repetition under
forward and reverse parents. Earlier parameter or note edits are still not rolled
back on later failure; independent mode remains disabled.

### Preserve iteration semantics for loop-length notes

The single droning-note shortcut now requires default iterance. A loop-length
conditional note proceeds through normal copying/iteration processing instead of
being stretched across iterations where it should be silent. Two native tests
cover forward/pingpong periodic conditions (including explicit flattening),
FIRST/LAST copies, and the unchanged single-note stretch for unconditional drones.
Sound/vector dependencies are doubled; the row repeat implementation is production
code. This does not complete transactional rollback or independent mode, which
remains disabled.

### Reject out-of-loop pingpong source onsets

Pingpong row repetition checks the first and last sorted source positions before
undo allocation or automation edits. Negative onsets and onsets at/beyond the old
loop boundary fail rather than entering reverse-position and wrapped-tail math.
Native regressions verify unchanged notes, direction, history and callbacks on
rejection, and retain support for a valid in-loop onset whose tail wraps. Forward
repetition retains its existing source filtering behavior. Outer clip repeat APIs
still return void: propagating failures through song doubling and arrangement
clone installation, with safe clone cleanup, remains outstanding. Independent mode
remains disabled.

### Preflight clip repeat lengths across all rows

Both InstrumentClip repeat entry points reject nonpositive clip/target lengths
and invalid independent lengths before processing rows. Increase-with-repeats
also calculates every independent target in 64 bits and rejects doubled/rounded
targets outside signed range before an earlier row can change. Rounded repeat
counts use wide addition in both entry points.

A separate native ClipRepeatTests executable compiles both production methods
against controlled row/parameter/playback doubles, with six regressions covering
invalid input, later-row validation, target overflow, large-count arithmetic,
successful rounding and existing row-failure short-circuiting. This adds behavioral
coverage beyond source contracts. The APIs still return void: upstream failure
propagation, safe clone cleanup, callback lifetime and transactional rollback
remain unresolved. Independent mode remains disabled.

### Propagate repeat failure through song doubling

`increaseLengthWithRepeats` now returns success/failure, and `Song::doubleClipLength`
stops before scale notifications, output length notifications and playback resync
when repetition fails. Song doubling rejects nonpositive or overflowing source
lengths before multiplication. The base AudioClip no-op remains successful.

ClipRepeatTests now also compiles the production song-doubling caller. Three new
native regressions cover row failure suppressing notifications, invalid/overflow
lengths, and successful notification/resync; existing increase tests assert its
returned status. The arrangement-recording clone caller still does not handle this
status, and `repeatOrChopToExactLength` still returns void. Safe clone cleanup and
rollback of earlier rows/parameters remain required, as do broader callback lifetime
and independent transport work. Independent mode remains disabled.

### Propagate multiply failure to redo and UI

Song doubling now returns its success status. The multiply consequence returns
`Error::BUG` when redo cannot double the clip, allowing the existing action revert
failure path to stop rather than treating a partial/failed edit as successful.
The clip UI also stops before zoom/history-after-state updates and success feedback.
Two native regressions execute the production consequence, song caller and clip
increase together for failed and successful redo. A source contract checks the UI
failure branch; it is not a full behavioral UI test.

This does not roll back earlier row edits, repair a partially recorded multiply
action, or harden the undo (halving) branch. Arrangement clone cleanup, broader
lifetime protection and independent transport remain outstanding. Independent mode
remains disabled.

### Validate multiply consequence targets before mutation

Multiply undo/redo rejects missing model stacks/songs/current clips and current
clips of the wrong type before casting. Undo validates the parent length and every
explicit independent-row length before calling the parent length setter: halving
must retain a positive length, while zero continues to mean an inherited row.
Three native regressions cover missing/wrong context, invalid parent or later-row
lengths leaving setters untouched, and accepted inherited/positive lengths.
The consequence implementation is production code; undo length setters are doubled.

These are entry checks, not target identity binding or lifetime protection during
callbacks. The consequence still resolves the current clip at execution time;
callback invalidation, failures inside halving and transactional recovery remain
outstanding. Independent mode remains disabled.

### Report independent-row halving failures

`NoteRow::setLength` returns trim errors and rejects nonpositive lengths before
modulo or mutation. The independent-row halving helper returns the first row error
and stops before later rows. Multiply undo now returns that error to the action
revert machinery instead of reporting success.

The native clip suite now executes the production halving helper rather than a
helper double, covering row failure stopping later rows and successful halving
calls. The row setter remains doubled in this suite; source contracts cover its
trim-error propagation and nonpositive-length guard. The parent clip may already
have changed before a row fails, and earlier successful rows are not rolled back.
Other row-length callers still need error handling. Independent mode remains
disabled; broader lifetime, arrangement cleanup and transport work remain.

### Preserve row-length undo state on resize failure

The row-length consequence now returns setter errors from both performChange and
revert, and only exchanges its saved length after success. Both UI resize paths
check the result, display the returned error and stop before scrolling/feedback.
A native consequence regression covers BUG and allocation failures, successful
retry, failed redo preserving its saved length, and successful redo retry. UI
branches have source-contract coverage; the native row setter is a controlled
error double.

This preserves the consequence's length bookkeeping, not a complete transaction:
previous parameter/note edits and newly attached UI action records still require
rollback/recovery. Callback lifetime and arrangement cleanup remain outstanding,
as does independent transport. Independent mode remains disabled.

### Execute the real row-length setter in native halving tests

ClipRepeatTests now extracts production NoteRow::setLength, so multiply undo tests
exercise the consequence, independent-row halving helper and row setter together.
Only trimming and playback collaborators remain controlled doubles. Three direct
setter regressions cover nonpositive input before callbacks, trim failure retaining
row/playback state, and successful position wrapping, inherited-length restoration
and resume suppression. This closes the setter-double coverage gap described above;
it does not add transaction rollback or callback lifetime guarantees. Independent
mode remains disabled.

### Validate row-length setter context before resizing

The row-length setter rejects missing stacks, songs, clips, mismatched stack rows,
nonpositive parent lengths and negative independent lengths before trimming or
changing playback state. Null-tolerant model-stack accessors make these checks
return errors in diagnostic builds as well. Native production-setter tests cover
missing/mismatched context and invalid source lengths without trim/resume calls.
These entry checks do not pin objects or validate ownership after callbacks.
Independent mode remains disabled; rollback, lifetime, arrangement cleanup and
independent transport remain outstanding.

### Validate direct row-length consequence edits

The UI-facing performChange entry point now validates the retained clip's song
membership, type, stack identity, row ID/address/undo identity and valid lengths
before accessing effective length. Previously these checks existed only in revert,
which the UI does not call for a new edit. Three native regressions cover mismatched
or missing context, a replaced row or detached clip, and successful retained-target
editing, with no saved-length exchange on rejection. This is entry validation;
post-callback lifetime and rollback remain unresolved. Independent mode remains
disabled.

### Recheck row-length targets after setter callbacks

Before exchanging its saved length, the row-length consequence now checks that
active song, stack song, both panel structural revisions, clip membership, stack
clip/row and row identity still match the pre-call snapshot. Native callback cases
cover row replacement, clip detachment, row identity replacement and remote panel
invalidation, preserving the saved length on rejection. Target checks use captured
values and precede post-call row dereferences.

This is cooperative boundary validation, not a pin on the consequence or Action.
Unannounced destruction/same-address reuse and rollback of setter changes remain
unresolved. Independent mode remains disabled; transport and arrangement cleanup
remain outstanding.

### Validate instrument clone entry and early failures

InstrumentClip::clone rejects a missing model stack or a target different from the
source clip before allocating. Four native regressions cover these invalid inputs
and injected clip allocation, parameter cloning and row-array copying failures.
They check error propagation, unchanged source stack and balanced cleanup. These
tests execute the production clone method with controlled collaborators; they do
not establish safety of the full parameter teardown or callback lifetime chain.
All 29 native suites and the RelWithDebInfo firmware build pass. Independent mode
remains disabled; transport, comprehensive lifetime protection and transactional
undo recovery remain outstanding. New identifiers use snake_case.

### Verify the completed row resize before undo bookkeeping

The row-length consequence now checks unchanged parent length/output/type and
stack row ID after setter callbacks, then verifies the effective resulting row
length equals the requested length. A same-identity row changed by a nested edit
therefore cannot silently commit the outer resize's saved-length exchange.
Native regressions cover five callback changes (parent length, output, row ID,
result length and clip type) and successful parent-inherited length. Earlier edits
are not rolled back by rejection. Independent mode remains disabled; broader
lifetime protection, rollback, arrangement cleanup and transport remain unfinished.

### Exercise released row/clip storage after resize callbacks

Two native consequence regressions now free the original heap row or detach and
free the original heap clip during the controlled setter callback. They verify the
post-call checks reject the edit without dereferencing released target storage or
exchanging the saved length. These run with the undo suite's ASan/UBSan option,
using real object deallocation rather than only pointer replacement. Setter
internals and production teardown are still doubled, and the consequence itself
remains alive. This adds coverage; it does not establish complete lifetime safety.
Independent mode remains disabled.

### Guard halving-loop continuation after playback callbacks

Independent-row halving now checks stack/clip ownership at entry and snapshots
song, panel structural revisions, parent length/output and row count. After each
successful setter call it verifies external state before clip access, then clip
ownership, row address and identity before continuing. A resize that releases or
invalidates the clip therefore cannot simply advance the loop.

Two native tests execute the production halving helper and row setter: one removes
clip ownership during resume and checks later rows remain untouched; another frees
the entire heap clip after a structural invalidation. The latter runs under ASan/
UBSan. Playback and membership are controlled doubles. These are cooperative
boundary checks, not rollback or comprehensive lifetime protection. Independent
mode remains disabled.

### Guard clip repeat continuation after row/parameter callbacks

Increase-with-repeats snapshots song, both panel revisions, parent length/output,
row count and initial clip membership. It checks these after row generation and
clip parameter repetition before further clip access or length/direction commit.
After row generation it also checks row address, identity and independent length.
Unpublished arrangement clones remain supported; their lifetime depends on
structural invalidation because they are not yet in the song's clip arrays.

Four native regressions cover ownership invalidation stopping later rows, parent
mutation during parameter repetition, actual clip deletion during row/parameter
callbacks, and successful unpublished-clone repetition. The deleted-clip cases run
under ASan/UBSan with controlled callback collaborators. Prior row/parameter edits
are not rolled back. Independent mode remains disabled; comprehensive lifetime,
rollback, arrangement cleanup and independent transport remain outstanding.

### Guard repeat-or-chop callback continuation

Repeat-or-chop now applies the same cooperative song/revision/ownership and
parent/row-context checks after row repetition or trimming and clip parameter
repetition. It also checks the expected committed parent length after the base
lengthChanged callback before attempting playback resume. Native regressions
cover ownership loss preventing later-row processing and actual heap-clip deletion
at each of four callback boundaries. ASan/UBSan exercise these outer-method paths;
row/parameter/base callback collaborators are controlled doubles.

The method still returns void, so the arrangement caller cannot yet act on these
failures. Cleanup of unpublished clones and rollback of prior edits remain open.
Independent mode remains disabled; broader lifetime protection and independent
transport remain unfinished.

### Reject failed arrangement repeat-or-chop clones

Repeat-or-chop now returns success/failure. The unique arrangement clone caller
stops before installation on failure and deletes its unpublished clone only while
song, stack and structural revisions still match. Invalidating callbacks may have
already released or transferred the clone, so changed contexts are not dereferenced
for cleanup. This leaves unresolved cleanup when context changes but a clone stays
alive; it is not full transactional recovery.

Native repeat-or-chop tests assert failure/success status, including deletion at
callback boundaries. A source contract checks arrangement failure precedes
installation and cleanup is guarded. Production destruction/ownership transfer is
not behaviorally tested by that contract. Independent mode remains disabled.

The final success result also rechecks context after playback resume; a fifth
native deletion case covers that callback. Cleanup additionally requires the clone
to remain unpublished, avoiding deletion if a callback registered it with the song.

### Behavioral coverage for arrangement clone failure cleanup

ArrangementCloneTests now compiles the production unique-clone caller. Five native
regressions cover failed repeat cleanup before installation, callback deletion
without double cleanup, callback publication without deletion/duplicate installation,
successful installation/notifications, and capacity/clone errors. Actual heap
release in the callback runs under ASan/UBSan. Clone/repeat and Song teardown remain
controlled collaborators, so this improves on the source contract without claiming
full production destruction coverage. Independent mode remains disabled; changed-
context cleanup of live clones, rollback, broader lifetime and transport remain open.

### Check arrangement context after reservation and cloning

Unique arrangement cloning now validates entry pointers and snapshots the source
song, instance clip/position/length and both structural revisions before capacity
reservation. It checks this snapshot after reservation and cloning, before source
instance reuse or first access to the returned clone. Native regressions release
the instance during reservation or the clone during cloning after structural
invalidation, and change instance length during reservation without invalidation.
The released-object cases run under ASan/UBSan.

These are caller-boundary checks; clone internals and production teardown remain
doubled. A context change with a still-live unpublished clone still needs ownership-
aware cleanup. Independent mode remains disabled, with rollback, broader lifetime
protection and transport remaining.

### Revalidate arrangement source after successful repetition

A successful repeat result no longer bypasses source-instance validation. Before
installation the caller checks the source snapshot, stack song/clip and whether a
callback already published the clone. Changed instance clip/position/length is
preserved, with stable-context unpublished clones cleaned up. Already published
clones are not deleted or inserted again.

Three native regressions cover a matrix of source-instance mutations, callback
publication despite a successful return, and structural invalidation with actual
instance deletion. The last case verifies rejection without touching released
instance storage; cleanup of its still-live clone is skipped and remains unresolved.
Independent mode remains disabled; rollback, broader lifetime protection and
transport remain outstanding.

### Validate arrangement clone ranges before allocation

Unique cloning rejects nonpositive requested lengths except the keep-length -1
sentinel, negative source positions, nonpositive source lengths, and source/target
end positions beyond signed range. End calculations use 64-bit arithmetic before
reservation or cloning. Four native regressions cover rejected requests, source
and target overflow, and accepted keep-length/requested-length boundary cases.
New identifiers and test names use snake_case. Independent mode remains disabled;
context-invalidated live-clone cleanup, rollback, broader lifetime and transport
remain outstanding.

### Recheck arrangement installation notifications

The unique-clone caller validates source context and clone membership after song
insertion and the removal notification before replacing the source instance. After
the final playback notification it checks external revisions first, then verifies
the installed instance still references the clone at the requested position/length.
Native regressions preserve a callback-changed source length and actually delete
the instance at either notification boundary under ASan/UBSan. New identifiers use
snake_case. The clone is already song-owned at these boundaries; failed partial
installation is not rolled back. Independent mode remains disabled.

### Handle arrangement clone insertion errors

The unique-clone caller now checks and returns the clip-array insertion error.
It cleans an unpublished clone only while song/stack/revisions still match, and
stops before notifications or source replacement. Three native regressions cover
insertion failure cleanup, failure after callback deletion without double cleanup,
and successful insertion whose callback frees the source instance. The latter
leaves the clone song-owned and reports failure; partial-publication rollback
remains unresolved. Callback deletion cases run under ASan/UBSan. New identifiers
use snake_case. Independent mode remains disabled.

### Discard unused clones after stable-context publication failures

When source validation fails after publication or the first notification, the
caller can now remove and destroy its unused clone if song/stack/revisions remain
stable and neither a session clip nor an arrangement instance has adopted it. The
clone is removed from arrangementOnlyClips before destruction. Callback changes
to the original instance are preserved.

Native tests cover cleanup after insertion changes the source and after the first
notification, plus preservation when callbacks adopt the clone in an arrangement
or session. Ownership checks/teardown remain controlled test collaborators. Changed
structural contexts still skip cleanup; playback side effects and full transaction
rollback remain unresolved. Independent mode remains disabled.

A direct source-instance reference to the clone also prevents cleanup, independently
of output instance lookup; a native regression covers that adoption path.

### Preserve unpublished clones adopted by instance references

Cleanup after repeat or insertion failure now checks the source instance and
output arrangement references as well as song membership before deleting the
unpublished clone. Four native regressions cover source-instance and output adoption
at both failure boundaries. Existing unreferenced-clone cleanup tests retain the
successful cleanup behavior. New tests use snake_case. Instance lookup and teardown
are controlled collaborators; broader ownership transfer, structural-invalidation
cleanup and transaction rollback remain open. Independent mode remains disabled.

### Reject clone output replacement across arrangement callbacks

The caller captures the cloned output and rejects a changed output before
installation or subsequent notification processing. Failure cleanup also requires
that output identity to remain unchanged, avoiding reliance on a replacement
output's instance list when the original output may still reference the clone.
Three native regressions cover output replacement during repetition, insertion
failure and the first notification. Changed-output cleanup is intentionally skipped
and remains unresolved; this is not complete ownership recovery. New identifiers
use snake_case. Independent mode remains disabled.

### Keep arrangement notifications tied to the validated source output

Source-instance validation now also checks the original clip output. Notifications
use the captured source output after validation rather than rereading a potentially
changed output. Three native regressions replace that output during reservation,
repetition or the first notification, verifying processing stops and preserves the
callback's change. Stable-context unused-clone cleanup remains exercised. This is
pointer identity validation, not a lifetime pin on outputs. Independent mode remains
disabled; broader lifetime, invalidated-context cleanup, rollback and transport
remain unfinished. New identifiers use snake_case.

### Validate successful clone results before mutation

The arrangement caller uses the null-tolerant timeline accessor and rejects a null
result, the original source clip, or an already song-owned clip before changing
section/activation fields. Unexpected results are not destroyed because ownership
is unknown. Three native regressions cover these cases, preserving the source or
published object's fields and avoiding cleanup/duplicate insertion. New tests use
snake_case. This does not validate clone internals or pin returned objects;
independent mode remains disabled with ownership recovery, rollback, broader
lifetime and transport work outstanding.

### Propagate instrument row-cloning failures

InstrumentClip::clone now retains the first beenCloned error while finishing every
copied row, then restores the original stack target and destroys the failed clone.
It no longer returns success with partially cloned row content. Processing all rows
before destruction preserves the existing requirement to detach borrowed source
storage. Three native tests execute the production clone method, checking first-
error retention, late failure, cleanup ordering/stack restoration and success with
fresh row identities. Row/parameter cloning and teardown internals remain controlled
doubles; callback lifetime is not covered by this change. New identifiers use
snake_case. Independent mode remains disabled, with broader lifetime, ownership
recovery, rollback and transport work outstanding.

### Verify real array detachment after shallow-clone failure

Native parameter lifecycle tests now exercise the production array beenCloned path
with actual allocator-backed ring storage at all 32 starting offsets. Allocation
failure must leave an empty copy that can be reused and destroyed without changing
the source; successful cloning must preserve wrapped payloads in independent
storage. Two tests cover 64 ring-layout scenarios. This replaces a borrowed-storage
assumption with lower-level behavioral coverage, but does not execute the entire
row/parameter clone chain. No production behavior changed in this pass. Independent
mode remains disabled; broader lifetime, ownership recovery, rollback and transport
remain outstanding.

### Reject invalid clone lengths and verify retries

InstrumentClip::clone now rejects nonpositive source loop lengths before allocation
or reverse-flattening calculations. Native tests cover zero, negative and minimum
32-bit lengths with flattening enabled and disabled. A retry matrix injects failure
at clip allocation, parameter cloning, row-array copying and each of three rows,
then successfully clones the same source and verifies balanced cleanup. This runs
the production clone method with controlled row/parameter collaborators, not the
full production clone and teardown chain. Independent mode remains disabled;
transport, comprehensive lifetime protection and transactional undo recovery are
still outstanding. New identifiers use snake_case.

### Revalidate multiply undo after parent length changes

Multiply undo now checks song ownership before accessing the selected clip, and
revalidates active song, model-stack song, both structural revisions, ownership,
selection, clip type, expected length and output after the parent length setter.
Invalidation stops independent-row halving. Native regressions inject seven context
changes, free a heap clip during the setter callback, and reject unowned entry
targets for undo and redo. The setter is a controlled collaborator: this protects
the consequence's return boundary, not the internals of Song::setClipLength. Parent
changes are not rolled back and the consequence itself is not lifetime-pinned.
Independent transport, broader lifetime protection and undo recovery remain
outstanding; independent mode remains disabled. New identifiers use snake_case.

### Preflight independent-row halving after parent callbacks

The row-halving helper validates a positive parent length and every independent
row length before changing any row. Multiply undo also rejects row-count changes
during the parent setter. Four native regressions cover invalid later rows, invalid
parents with no rows, a callback introducing an invalid later length, and callback
row insertion/removal. This prevents earlier row edits in these cases but does not
roll back the already changed parent. Same-count row replacement and mutations
during subsequent row callbacks still require broader lifetime/transaction work.
Independent mode remains disabled. New identifiers use snake_case.

### Check row identity across multiply undo parent updates

Multiply undo snapshots row addresses, identities and independent lengths before
changing the parent. It rejects same-count replacement, reordering, relocation and
unexpected length changes before halving rows. The normal conversion of a row
matching the new parent length to inherited remains accepted. Snapshot allocation
failure returns INSUFFICIENT_RAM before parent mutation; temporary storage is freed
on every exit. Native tests cover these mutations, allocation failure, cleanup and
the permitted inherited-length transition using a controlled parent setter. These
are boundary checks, not lifetime pins or rollback; production setter internals
and later callbacks still need protection. Independent mode remains disabled.

### Revalidate multiply targets after snapshot allocation

Multiply undo now validates song/stack context, both structural revisions, clip
ownership and selection, parent length/output and row count after allocating the
row snapshot and before reading rows. It rechecks independent lengths while taking
the snapshot. Native tests inject seven allocation-return mutations and free the
target with both successful and failed allocation, checking rejection before parent
mutation and balanced snapshot cleanup. Allocation is a controlled collaborator;
these tests exercise the caller boundary rather than production allocator callbacks.
Same-address reuse without identity/revision notification, consequence lifetime and
full rollback remain unresolved. Independent mode remains disabled; independent
transport and comprehensive lifetime protection remain outstanding.

### Verify halved row results after playback callbacks

Independent-row halving now validates the row stack's song, clip and row target
after the setter returns, then checks that the surviving row's effective length
equals the requested half-length. A conflicting playback callback returns BUG
before later rows are edited. Three native regressions cover unexpected lengths,
row-stack redirection and successful conversion to a parent-inherited target. The
production helper and setter run with controlled playback callbacks. Earlier edits
are not rolled back, and later-row replacement still needs broader transaction
protection. Independent mode remains disabled; lifetime, rollback and independent
transport work remain outstanding. New identifiers use snake_case.

### Revalidate row-length targets after trimming

NoteRow::setLength now verifies the instrument-row lookup at entry and revalidates
song/stack context, structural revisions, registered clip ownership, row identity,
parent/row lengths and output after successful trimming before writing length or
playback state. Unpublished clip support is retained through conditional ownership
validation. Native regressions inject six context changes and destroy a row or
heap clip during the doubled trim callback. They execute the production setter,
not the full trim/parameter chain. Trimmed notes are not rolled back, unpublished
target lifetime still depends on structural notification, and independent mode
remains disabled with broader lifetime, rollback and transport work outstanding.

### Preserve trimming targets through event notification

Row trimming validates its entry stack, retains the original target, and checks
target continuity after note trimming and parameter trimming. It notifies the
original clip and validates context again after expectEvent before returning
success. Native tests redirect the stack during parameter trimming and destroy
a row or clip during event notification. Production trim code runs against
controlled parameter/event collaborators; clip destruction is accompanied by
context invalidation. These checks do not pin objects or restore trimmed data.
Independent mode remains disabled; comprehensive lifetime, rollback and independent
transport work remain outstanding. New identifiers use snake_case.

### Check registered clip ownership in shared row-edit contexts

NoteRowEditContext records whether its clip was song-owned at entry and rejects
ownership loss before dereferencing clip or row storage. Four native regressions
cover removal without structural refresh, actual clip deletion, unpublished-clone
publication, and deletion during parameter trimming without changing the active
song. Unpublished contexts remain supported; they still depend on structural
notifications for destruction, and same-address reuse is not lifetime-pinned.
The shared guard strengthens existing bulk-edit and trim boundaries but does not
provide rollback or complete lifetime protection. Independent mode remains disabled.

### Validate row-edit context inputs before taking snapshots

The shared row-edit context checks the song, clip, row, instrument type and row
lookup before reading row snapshot fields. Invalid construction remains invalid
even if later state changes repair the lookup or restore the song. Three native
regressions cover missing inputs, mismatched rows and wrong clip type, including
subsequent repair attempts. This guards invalid inputs, not arbitrary dangling
pointers; comprehensive lifetime protection, rollback and independent transport
remain outstanding. Independent mode remains disabled. New identifiers use snake_case.

### Remember observed publication in row-edit contexts

Shared row-edit validation now remembers when an initially unpublished clip becomes
song-owned. Subsequent ownership loss invalidates the snapshot before any clip
access, and later reinsertion cannot revive it. Three native regressions cover
publication observed through either validation API followed by deletion, reinsertion
after observed removal, and continuous ownership while moving between song arrays.
Publication and deletion occurring entirely between validations remain unobserved;
this is cooperative validation, not a lifetime pin or concurrency primitive.
Independent mode remains disabled; broader lifetime, rollback and independent
transport work remain outstanding. New identifiers use snake_case.

### Keep detected row-target invalidation permanent

Shared row-edit contexts now permanently reject detected song, structural, row,
identity, length, output and parameter-target changes, including wrong clip type.
Restoring matching values cannot revive a stale target snapshot. Three native
regressions exercise five change/restore cases, access after an invalidated
unpublished clip is freed, and continued target-only validation after intentional
note-array resizing. A note-array mismatch alone does not invalidate target-only
checks. This does not detect changes occurring entirely between checks or pin
objects. Independent mode remains disabled; lifetime, rollback and transport work
remain outstanding. New identifiers use snake_case.

### Complete metadata invalidation regression coverage

Three additional shared-context tests cover parent length, output replacement,
expression classification, every parameter-collection slot and both panels'
structural refresh revisions. Restoring metadata or consuming a refresh must not
revive an invalidated context; a fresh context must accept the resulting stable
state. This pass changes tests only. Model collaborators remain controlled doubles,
and independent mode remains disabled with lifetime, rollback and transport work
outstanding. New identifiers use snake_case.

### Report row-length resume conflicts to direct callers

NoteRow::setLength now reuses its target validation after playback resume and
requires the requested resulting independent length before returning success.
Direct-caller regressions cover changed length, row-stack redirection, song change,
identity change and destruction of a row or registered clip during resume. Existing
positive tests retain normal and inherited-length behavior. The production setter
runs against controlled trim/playback collaborators. Earlier note/playback edits
are not rolled back, and unpublished-target destruction still requires cooperative
invalidation. Independent mode remains disabled; broader lifetime, rollback and
independent transport remain unfinished. New identifiers use snake_case.

### Track publication across row-length callbacks

The row-length setter now remembers song ownership observed after trimming, so
later removal during playback resume is rejected before target access even when
the clip was unpublished at entry. A native regression publishes during trim and
frees the clip during resume without changing song or revisions. A second test
preserves successful unpublished and newly published targets. This uses controlled
trim/playback collaborators; publication and destruction entirely between checks
remain unobserved. Independent mode remains disabled with comprehensive lifetime,
rollback and transport work outstanding. New identifiers use snake_case.

### Track publication throughout clip repeat operations

Both instrument repeat entry points now remember ownership observed after a row
callback rather than relying only on entry ownership. Later removal is rejected
before clip access. Native matrices publish an unpublished clip in the first row
callback, then either free it in the next callback or complete successfully. They
cover increase-with-repeats, exact-length growth and chopping using controlled row
collaborators. Earlier edits are not rolled back, and publication/removal entirely
between checks remains unobserved. Independent mode remains disabled; broader
lifetime, rollback and independent transport remain outstanding.

### Explicit session-mode request parsing

Version-2 Request accepts the legacy one-byte display payload or a two-byte
display/mode payload. Omitted mode and mode 0 select visible-host mirroring. Mode 1
reserves independent UI semantics but remains rejected while independent mode is
disabled; unknown modes, invalid displays and extra bytes are rejected. Existing
clients continue sending legacy requests and Accept remains unchanged. Older hosts
reject extended requests by length; there is no automatic fallback. Four runtime
regressions cover explicit mirroring, unsupported modes, malformed/display-mismatched
requests and preservation of an active session. This is a negotiation foundation,
not an independent transport implementation: mode acceptance, remote input/render
routing, resynchronization and hardware validation remain outstanding.

### Discover supported modes without creating a session

An idle device answers opcode 9 with opcode 10, session/sequence zero, advertising
mode mask 1 (visible host only) and its display type. Discovery never reserves a
peer, consumes session sequence numbers or purges musical MIDI. Busy, pending,
active, malformed and congested queries are ignored. Sending is guarded against
reentrant discovery and session starts; unsolicited responses cannot affect an
active session. Four runtime regressions cover response framing/state, invalid
queries/congestion, session isolation and send reentry. Existing clients do not
yet initiate discovery. Independent mode remains disabled and unadvertised; remote
input/render routing, resynchronization and hardware validation remain outstanding.

### Explicit unsupported-mode handshake rejection

An idle host now responds to a valid independent-mode request with rejection
reason 1, echoing the request token without reserving a peer or changing MIDI
routing. Congestion drops the response rather than starting a session. A matching
rejection ends a waiting client handshake; malformed, stale and post-acceptance
rejections cannot alter sequencing or an accepted session. Three runtime tests
cover framing/state and waiting/accepted-client handling. Independent mode remains
disabled. Client discovery initiation and independent input/render transport remain
unimplemented; this change provides an explicit failure response, not enablement.

### Client capability discovery API

request_capabilities sends an idle-only query without freezing local UI tasks.
take_capabilities consumes a validated response once; queries/results expire after
three seconds and are discarded on observed disconnection or session startup.
Only a response from the queried cable with zero session/sequence, two payload
bytes and a valid display type is accepted. Mode bits are reported, never used to
enable independent UI. Three runtime regressions cover success, single consumption,
malformed/expired replies, disconnect and startup invalidation. Existing startup
does not yet invoke this API. The zero-token discovery format cannot distinguish
a late response to an earlier query on the same cable; it is advisory capability
information, not session authorization. Startup integration, correlated discovery,
independent routing and hardware validation remain unfinished.

### Correlate discovery replies with their query

New client discovery queries use a nonzero 14-bit ID in the sequence field and
accept only replies echoing that ID from the selected cable. Hosts echo the query
ID without changing live session sequence counters. Zero-ID queries remain
answerable for earlier callers, but their replies cannot satisfy a new client's
query. Tests cover maximal-ID echo, late replies after timeout/retry and fresh IDs
without clock advancement. IDs repeat after 16383 queries and are not persisted
across reboot; this is bounded correlation, not authentication. Earlier zero-only
discovery hosts will time out. Startup integration and independent input/render
transport remain unfinished, and independent mode remains disabled.

### Service discovery disconnection and response reentry

Discovery expiration now checks USB connectivity and runs in the normal service
loop, allowing an observed disconnect to discard pending/cached discovery before
reconnection. A new peer can be queried immediately without first consuming the old
result or waiting for timeout. Four runtime tests cover peer replacement, cached
result invalidation, matching IDs on the wrong connected cable, and synchronous
responses during guarded transmission. A disconnect/reconnect entirely between
observations still needs USB connection-generation tracking. Startup integration
and independent input/render transport remain unfinished; independent mode stays
disabled. New identifiers use snake_case.

### Discover compatibility before client takeover

Mirror startup now sends correlated capability discovery before stopping local
voices/notes or pausing UI timers. It waits up to three seconds with local tasks
active, then requires visible-host support and matching display type before the
existing session request. Unknown/older peers that cannot answer correlated
discovery time out; there is no automatic legacy fallback. Startup rechecks song,
UI owner, connectivity and playback/recording after discovery before takeover.
New runtime tests cover asynchronous success, unsupported modes, timeout and
playback starting during a response. Existing session tests simulate a compatible
discovery peer; discovery packets remain visible in dedicated tests. Independent
mode stays disabled; independent rendering/input transport and device validation
remain unfinished.

### Cancel discovery startup with Back

A Back press while startup is pending cancels before client takeover, clears the
discovery result and allows the local UI to process the button normally. Cancellation
also works during discovery transmission; late responses cannot resume the attempt.
A new start uses a fresh query ID. Three runtime regressions cover cancellation
before transmission, during the asynchronous wait and during transmission, plus
successful retry. Established sessions retain hold-Back exit behavior. Independent
mode remains disabled; independent routing and broader lifetime/undo work remain.

### Clear discovery on every abandoned startup

Startup cleanup now retains discovery only when explicitly requeuing the wait.
Early exits caused by song/UI-owner changes, playback or ambiguous USB connections
clear the peer and cached capabilities immediately. Native tests cover these four
contexts, late replies, cached replies and immediate re-query without waiting for
timeout. Local UI timers remain active and no session request is sent. This closes
startup cleanup paths; independent mode remains disabled with independent routing,
comprehensive lifetime protection, rollback and hardware validation outstanding.

### Exercise OLED sender-to-receiver reconstruction

A native runtime regression now captures the production host sender's six-block
baseline and commit, then a single changed block and commit. It replays the decoded
payloads through the production client receiver under a new session and compares
all 768 bytes of both rendered frames. This connects sender delta selection to
receiver reconstruction, while transport/hardware remain controlled doubles and
packet session/sequence fields are adapted for the receiving test session. No
production behavior changed. Independent mode remains disabled; independent
routing, broader lifetime/rollback and two-device validation remain outstanding.

### Reserve startup discovery replies for startup

The public advisory capability getter no longer consumes a reply owned by pending
startup. Runtime regressions attempt consumption after asynchronous receipt and
during transmission, verifying startup still completes. A third test confirms
startup creates a fresh correlated query rather than reusing cached advisory
capabilities. Independent mode remains disabled; independent routing, comprehensive
lifetime protection, rollback and hardware validation remain outstanding.

### Bind discovery to USB slot generations

USB device setup now advances a 32-bit connection generation in the shared C/C++
device structure. Discovery captures the slot and generation and rejects a changed
connection even when its cable pointer and connected flags look unchanged. Native
regressions cover same-pointer reconnect, cached-result rejection and slot movement;
a production setup-method test checks generation advancement and transfer resets.
Both host and peripheral setup paths call this method, but hardware detach/reconnect
validation remains outstanding. Generation wraps after 2^32 setups and does not yet
guard established mirror sessions. Independent mode remains disabled; independent
routing, broader lifetime/rollback and device validation remain unfinished.

### Bind established sessions to USB generations

Host and client sessions capture their USB slot/generation at negotiation. Input
reception, queued-input dispatch, transport service and packet sending reject a
changed connection. Teardown does not send the old session's Stop packet to a new
generation, and musical MIDI filtering applies only to the negotiated generation,
including release callbacks. Three runtime regressions cover host input rejection,
queued-input discard and client transport teardown after same-pointer reconnect.
Driver in-flight traffic cannot be recalled; hardware reconnect validation remains
required. Independent mode remains disabled with independent routing, broader
lifetime/rollback and device validation outstanding.

### Recheck USB generation after session sends

Session packet sending now rechecks the negotiated connection after sendSysex
returns. A reconnect during its callback marks failure without advancing the old
sequence or reporting success to the caller. Runtime tests cover reconnect during
input acknowledgement (stopping the next queued input), client input transmission
and host acceptance (preventing frame publication). The packet already handed to
the driver cannot be recalled. Independent mode remains disabled; remaining
callback audits, independent routing, lifetime/rollback and hardware validation
remain outstanding. New identifiers use snake_case.

### Preserve the discovered connection through takeover and input callbacks

Startup now checks the discovered slot/generation before consuming capabilities
and throughout local UI cleanup, preventing takeover of a replacement connection
without fresh discovery. Host input processing also checks the generation after
dispatch before creating an acknowledgement. Three runtime regressions reconnect
after a discovery response, during editor cleanup and during host input, checking
no takeover or subsequent ACK/input dispatch. Earlier local cleanup or dispatched
input is not rolled back. Independent mode remains disabled with independent
routing, lifetime/rollback and hardware validation outstanding.

### Detect reconnects before retaining deferred input

Deferred button dispatch now marks a changed connection failed before returning
for retry. Encoder dispatch checks generation before querying or retaining pending
encoder work. Runtime regressions reconnect inside a deferred button callback and
inside completed/deferred encoder callbacks, verifying immediate failure, no ACK
and no replay after teardown clears encoder state. Callbacks are controlled doubles;
hardware validation remains required. Independent mode remains disabled with
independent routing and broader lifetime/rollback work outstanding.

### Use one local session-mode support definition

Capability advertisement and host request acceptance now share the protocol's
supported-session-mode definition. A unit regression checks all 256 mode values,
requiring visible-host support and rejecting independent/unknown modes. Existing
runtime negotiation tests still verify the actual advertised mask and rejection
response. No independent dispatcher is enabled by this refactor: wiring remote
input/rendering remains required before extending support. Independent mode remains
disabled; broader lifetime/rollback and hardware validation also remain outstanding.

### Cover reconnection during host teardown

Two runtime regressions reconnect the USB cable during Stop transmission and
during held-button release. They verify that the old connection's MIDI filter
does not mute the replacement, all owned buttons are released, reentrant session
requests cannot interrupt cleanup, and a fresh session can connect afterward.
The Stop-send case also checks that a generation change cannot advance the old
transmit sequence. These tests exercise existing production teardown with mocked
USB and input callbacks; no production behavior changed. Independent routing,
broader lifetime/rollback protection and hardware validation remain outstanding.

### Bind visible-host teardown to its UI owner

Mirror teardown now enters Local UI ownership before releasing controls and
restoring client UI state. Previously a failure serviced from a yielded Remote
operation could dispatch releases into Remote although the original presses were
dispatched into Local. A runtime regression disconnects with a held control while
Remote is active, checks the release belongs to Local and verifies Remote ownership
is restored on return. Independent mode remains disabled; this fixes an existing
cleanup boundary, not independent input/render routing or shared-model lifetimes.

### Bind initial pad snapshots to the visible host

Acceptance now generates the initial pad snapshot under Local UI ownership. Pad
renderers select their bank from the active UI, so servicing acceptance from a
yielded Remote operation previously selected the wrong bank. Two runtime tests
record main/sidebar renderer ownership for immediate acceptance and a retry after
USB congestion, checking Local selection, caller restoration and one snapshot per
acceptance. Renderers and USB are mocked; these tests do not validate physical pad
output. Independent input/render routing and the remaining readiness gates above
are still outstanding, and independent mode remains disabled.

### Restrict physical mirror takeover to Local UI requests

The mirror-start API now rejects Remote UI ownership before changing startup
state. Becoming a client takes over physical controls, display and timers; it is
a device-local operation rather than an edit to the shared song or host settings.
Runtime tests verify rejection without discovery or takeover, successful Local
startup afterward, and preservation of an already pending Local discovery query.
Independent mode remains disabled, with independent routing, comprehensive model
lifetime protection, undo recovery and hardware validation still outstanding.

### Skip initial rendering after acceptance is cancelled

Host acceptance now rechecks terminal failure after sending Accept, before
generating the initial pad snapshot. Incoming Stop or malformed input can be
serviced during transmission; previously the host still invoked pad renderers
after that failure. Regression coverage checks both cases, including normal Stop
sequencing after the already-sent Accept. The malformed-input test failed against
the previous implementation before the fix. USB and renderer callbacks are mocked;
independent routing, broader lifetime/rollback protection and hardware validation
remain outstanding, and independent mode remains disabled.

### Enforce session expiry before refreshing receive liveness

The receive path now checks the existing three-second session deadline before
accepting a matching packet or updating its receive timestamp. This prevents late
input or heartbeats from reviving an expired session while storage delays the main
routine. Runtime regressions cover host input during storage, client heartbeats
after expiry and the exact deadline boundary (still accepted, matching routine).
The host regression failed before the fix. Teardown remains deferred until storage
permits it; this change does not introduce rollback or independent routing.

### Enforce liveness at transport and input callback boundaries

Receive, main-loop, transport and input processing now share the same connection
and deadline check. Expired transport stops during storage without prematurely
releasing the SysEx filter or restoring client timers. Input callbacks cannot
retain deferred work or start another queued input after expiry. Sends check the
deadline before transmission and mark expiry after yielding, preserving the
sequence of an already-sent packet so teardown can still send Stop correctly.
Four runtime regressions cover host/client transport expiry, input callback expiry
and expiry during ACK transmission. Already applied edits are not rolled back.
Independent mode remains disabled and the broader readiness gates remain open.

### Cover live long-running callbacks and expired deferred input

Three runtime regressions exercise the timeout boundary around callbacks: an
eight-second input callback stays live with periodic valid heartbeats without
reentrant input dispatch; an expired deferred button is not retried; and deferred
encoder work is cleared before a new session. These use controlled callbacks and
time, not real storage latency or USB hardware. No production behavior changed in
this coverage pass. Independent routing, comprehensive lifetime protection, undo
recovery and hardware validation remain outstanding; independent mode is disabled.

### Validate clip-length undo after the length callback

Clip-length consequences now check song context, both structural revisions,
target membership/type/output and resulting length after setClipLength returns.
Saved length and sample-marker redo values are exchanged only after those checks
pass. Regressions cover a callback that fails to retain the requested length,
target removal/deletion and redirected stack ownership. Existing marker round-trip
tests continue to cover successful changes. This detects failure without rolling
back already-applied length or marker edits; consequence lifetime during callbacks
and comprehensive transactional recovery remain open. Independent mode is disabled.

### Validate sample-marker results and clip-length context changes

Clip-length undo now checks the requested sample marker after validating the
surviving clip and before exchanging saved redo values. Tests cover callbacks
changing either marker, active song, output, clip type and either UI structural
revision. The deletion regression now also retains a marker pointer, checking that
target membership is validated before accessing it. Existing successful round trips
remain covered. These checks detect invalid results; partial edits and callback
destruction of the consequence still require broader lifetime/rollback work.
Independent mode remains disabled.

### Reject changed sample targets during marker undo

Clip-length consequences with sample markers now retain the sample-holder file
and playback direction across resize callbacks and reject changes before exchanging
redo values. Tests cover sample replacement and direction changes for both start
and end markers; successful round trips remain covered. These checks do not pin
sample lifetime or undo partial mutations. Independent mode remains disabled.

### Validate both audio marker positions across callbacks

Audio marker helpers retain the expected start/end pair after applying their own
marker change. Context checks now reject callback changes to either position,
including the opposite marker, before resizing or updating undo metadata. An
extracted-method regression covers both helpers, both playback directions, both
callback stages and both marker positions. Existing successful edits remain
covered. Prior marker mutations are not rolled back; full lifetime protection and
independent routing remain unfinished, and independent mode stays disabled.

### Validate both marker positions during undo

Marker-bearing clip-length consequences now validate the expected start/end pair
after resize, rather than checking only the restored marker. A regression moves
the opposite marker during the callback for both marker types and undo/redo
directions, verifying failure without exchanging redo values. Existing target
marker and successful round-trip coverage remains active. Earlier mutations are
not rolled back; comprehensive lifetime protection and independent routing remain
unfinished, and independent mode stays disabled.

### Verify final generic resize length after manual resync

Generic lengthen/shorten helpers now require the requested final length as well as
valid target/history context before returning success. This catches manual resync
changing the length after implicit undo. Extracted-method regressions cover that
callback and a resize collaborator reporting success with a different resulting
length in both directions, checking that no navigation or rendering follows.
This does not roll back the callback's mutation. Independent mode remains disabled.

### Reject clip-type changes during generic resize

Generic resize helpers now include clip type in retained-target validation.
An extracted-method regression covers type, output and either UI structural
revision changing during allocation or resize, in both resize directions. It
checks that allocation changes prevent resizing and all detected changes prevent
navigation/rendering. This does not detect all same-address object reuse or roll
back applied mutations. Independent mode remains disabled.

### Validate retained resize-action ownership

Generic resize helpers now verify that a non-null action still belongs to the
initiating UI, song, output and clip, after checking its history membership.
Validation runs before resize and before returning the action. Extracted tests
cover a wrong-song allocation before mutation and ownership-field changes during
resize in both directions. They do not protect action destruction inside the
mutation itself or same-address reuse. Independent mode remains disabled.

### Validate audio marker action ownership

Audio clip start/end marker edits now validate the retained action's UI owner,
song, output, and clip after allocation and resizing, checking history membership
before dereferencing it. An ownership mismatch stops the edit before resizing or
updating and closing marker undo history, depending on when it is detected.
Regression tests cover all four fields, both callback stages, both marker helpers,
and forward/reverse playback. These checks do not roll back marker or length
changes already applied, provide lifetime pins, or enable independent mode.

### Preserve audio marker resize length context

Audio start/end edits now retain the entry clip length while allocating undo
history and require the requested length after resizing. A callback that changes
that length aborts the helper before further resizing or marker-history updates.
Tests exercise both callback stages, both playback directions, both helpers,
and operation with or without an allocated undo action. Marker or length changes
already applied are not rolled back; independent mode remains disabled.

### Validate audio marker sample inputs

Audio marker helpers now reject a sample that does not match the clip's current
sample holder before reading sample metadata or changing markers. End-marker
snapping ignores file markers beyond the sample length, preserving the existing
sample-end clamp. Regression tests cover empty and mismatched holders in both
helpers and playback directions, plus valid and out-of-range file markers.
This adds input validation, not sample lifetime pinning or partial-edit rollback;
independent mode remains disabled.

### Defer marker publication until history validation

Audio start/end edits now allocate and validate undo history before writing the
new sample marker. A failed allocation-context check therefore leaves no marker
change from the attempted edit and preserves callback-owned marker changes.
The marker is still published before setClipLength so playback observes it.
Tests cover both helpers and directions, unchanged markers on invalidated
history, preservation of callback edits, and marker visibility during resizing.
Failures after resizing begins still need transactional recovery; this does not
provide lifetime pinning or enable independent mode.

### Preflight missing outputs before marker undo

Clip-length undo now rejects a missing output before changing a sample marker,
matching the resize operation's existing precondition. Tests cover both undo
and redo with no marker, a start marker, or an end marker, and verify unchanged
length, marker values, and retained undo data without calling the resize path.
A refused consequence can be retried and round-tripped after restoring its output.
This avoids one partial-mutation failure; failures after resizing begins still
need transactional recovery, and independent mode remains disabled.

### Preflight audio marker resize targets

Audio marker helpers now reject missing outputs and nonpositive current clip
lengths before undo allocation or marker writes, matching the resize operation's
existing prerequisites. Regression tests cover zero and negative current lengths,
missing outputs, both helpers and directions, and successful retry after restoring
the prerequisites. The test fixtures now supply valid outputs for normal edits.
This prevents these early partial edits; recovery after resize callbacks and
comprehensive lifetime protection remain unfinished. Independent mode is disabled.

### Preflight generic resize entry before undo work

Generic clip lengthening and shortening now reject missing songs and out-of-range
requested lengths before reading selection. They reject missing clips, missing
outputs, and nonpositive current clip lengths before allocating or reverting
undo history. Regression tests verify both helpers preserve history and resync
state without dispatching callbacks on these failures, and count selection reads
to cover the missing-song ordering. Normal test fixtures now supply valid outputs.
Independent routing, comprehensive lifetime protection, and recovery from partial
resize callbacks remain unfinished; independent mode remains disabled.

### Preserve clip length across undo allocation

Generic clip resizing now checks that undo allocation left the target length
unchanged before applying a resize. Lengthening captures this value after any
successful implicit undo, preserving that workflow. Regressions cover ordinary,
fallback, and pattern-paste allocation in both directions, verifying callback
length edits survive and no resize, navigation, or rendering follows the failure.
This does not roll back allocation side effects or resize callbacks; independent
mode remains disabled pending routing, lifetime, and recovery work.

### Validate the length restored by implicit undo

Lengthening now validates the current length after a successful implicit undo,
before allocating new history or issuing a follow-up resize. Regressions cover
zero, negative, and over-limit restored lengths with resync allowed or suppressed,
and verify a valid restored length can still be resized to the requested length.
Invalid results are reported without further mutation; this does not roll back
the completed implicit undo. Independent mode remains disabled pending routing,
lifetime protection, and transactional recovery work.

### Validate audio clip type across marker callbacks

Audio marker helpers now require audio clip type at entry and during context
revalidation, before accessing sample-specific state. Regressions cover type
changes at entry, during undo allocation, and during resizing for both helpers
and playback directions. Early rejection leaves markers unchanged; later
rejection avoids updating marker undo history. The tests change type metadata
on a controlled audio object, not a destroyed/reused allocation. Lifetime pinning,
partial-resize recovery, and independent routing remain outstanding; independent
mode remains disabled.

### Verify marker undo consequence targets

Audio marker helpers now verify that a clip-length consequence belongs to the
edited clip before writing its marker and backup value. An action's correct
ownership alone is insufficient when callbacks change its consequence target.
Regressions cover null and different-clip targets for both helpers and directions,
verifying no marker-history update or action close follows the mismatch. Existing
success tests use a resize collaborator that records the consequence target.
This does not roll back the resize or pin consequence lifetime; independent mode
remains disabled pending the broader routing, lifetime, and recovery work.

### Find marker history beyond the first consequence

Audio marker edits now search the action's consequence list for the edited clip's
length consequence. Resizing records length before trimming automation, so later
consequences can precede it. Tests prepend parameter history and optionally another
clip's length history, then verify only the target receives the original marker
and the action list remains intact. An action containing length consequences but
none for the target is still rejected. Existing behavior for an action with no
length consequence is unchanged; allocation-failure recovery and lifetime pinning
remain outstanding. Independent mode remains disabled.

### Prepare marker length history before mutation

When an undo action exists, marker edits now prepare and verify its clip-length
consequence before publishing a marker or resizing. Preparation failure or callback
invalidation aborts before mutation. The resize operation reuses the prepared
length history. Missing target history after resize is now an error as well,
superseding the earlier behavior for actions with no length consequence.
Regression tests cover preparation failure and retry, preparation callbacks that
change length or remove history, and disappearance of prepared history during
resize. Existing no-action edits remain supported. These tests use a controlled
history collaborator; full allocation/lifetime protection and rollback of resize
side effects remain unfinished. Independent mode is still disabled.

### Revalidate length-history allocation callbacks

Action::recordClipLengthChange now validates song, UI owner, both structural
revisions, clip output/type/length, registered clip membership, and retention of
an action that began at the head of history after allocation. Invalidated
allocations are freed without attaching history. A second consequence lookup
prevents duplicates when a nested callback records the same target.
The native suite extracts the production recorder and tests allocation failure
and retry, callback invalidation, registered clip deletion, retained action
deletion, nested recording, and stable unpublished/unlisted callers. These are
bounded protections: initially unlisted actions, unpublished clip destruction,
and same-address replacement still need lifetime identities or pins. The void
recording API also leaves some callers unable to distinguish failure. Independent
mode and broader transactional recovery remain unfinished.

### Propagate clip-length history recording failure

Action::recordClipLengthChange now returns success for existing or newly attached
history and failure for invalid input, allocation failure, or callback invalidation.
Song::setClipLength consumes failure before trimming, output notification, and
playback work. Audio marker preflight consumes it before marker publication.
Tests assert recorder results, including nested recording and allocation retry,
and verify resize stops at the history stage for shrinking/lengthening and scaling
clips. Length and scaling changes preceding recording are not rolled back. Linear
recording callers still ignore the result and need separate recovery work; full
lifetime protection and independent routing remain unfinished. Independent mode
remains disabled.

### Stop tempoless finalization after history failure

Tempoless recording finalization now handles a missing undo action and checks
length-history recording failure before updating originalLength, copying pending
overdub metadata, or changing transport and record LEDs. Native tests extract the
full handler with controlled song, clip, allocator, and playback collaborators.
They cover no-action completion, explicit history failure, clip deletion during
a failed recording callback, successful stop, and unchanged-length completion.
The handler still may have finished recording, changed tempo, or assigned length
before failure; it does not roll those back or guarantee full session recovery.
Automatic recording extension and other callback lifetimes need further work.
Independent mode remains disabled.

### Revalidate tempoless finalization callbacks

Tempoless finalization now captures song/UI ownership and structural revisions,
then checks registered clip identity, output/type, and retained action after
recording, action allocation, tempo setup, and length-history recording. Pending
overdub lookup occurs after these callbacks; metadata-copy callbacks are also
revalidated. Tests cover clip deletion at four successful callback stages, action
deletion at three stages, changed song/output/type/structure, missing song, and
pending-overdub replacement. Collaborators model callback boundaries rather than
full hardware recording. Earlier recording/tempo changes still lack rollback;
playback restart allocation and initially unprotected callbacks need more work.
Independent mode remains disabled.

### Validate tempoless playback-restart allocation

Tempoless finalization now revalidates session and retained history after stopping
playback, updating LEDs, and allocating playback-begin history. Invalidated storage
is freed without attaching it or restarting playback. If an action exists but its
playback-begin consequence cannot be allocated, restart is refused; the existing
no-action path is preserved. Regressions cover successful attachment, allocation
failure, action deletion with successful/failed allocation, song/structure changes,
and stop/LED callback invalidation. Recording may already have been finalized and
record mode exited before a refused restart; full rollback remains outstanding.
Independent mode and comprehensive lifetime protection remain unfinished.

### Validate retained tempoless history ownership

Tempoless finalization now requires a retained action's UI owner and captured
song to match the initiating session, checking history membership first. Tests
change either field while keeping the same action at the head of history during
action allocation, tempo setup, length recording, and restart allocation. Invalid
restart storage is freed, and matching Remote-owned history has a successful
restart regression. This is a controlled session-context test, not enabled remote
transport. Full rollback, lifetime identities/pins, and independent routing remain
unfinished; independent mode stays disabled.

### Validate tempoless final length results

Tempoless finalization rejects zero or over-limit lengths returned by tempo setup
before assigning clip length. It checks the requested length after history
recording, and both current/original lengths after pending-overdub metadata copy.
Regressions cover invalid computed lengths, valid minimum/maximum lengths, and
callback edits to either length that must stop further finalization. Callback
edits are preserved rather than overwritten. This does not undo tempo setup or
recording/metadata changes already applied. Independent routing, full lifetime
protection, and transactional recovery remain unfinished; independent mode stays
disabled.

### Preserve the source of tempoless tempo calculation

Tempoless finalization captures current clip length, sample identity, and marker
range after recording finishes. It revalidates them after undo allocation and
tempo setup before assigning the computed length. Tests change each source field
at both callback stages and verify the callback's edit remains intact, with no
length-history recording, metadata copy, or playback restart. A tempo change made
inside setup is not rolled back when its source later fails validation; sample
lifetime identities and full transaction recovery remain outstanding. Independent
mode remains disabled pending those protections and independent routing.

### Bound tempoless tempo calculation

Tempo derivation now rejects missing songs, zero sample duration, zero current
tick duration, and a required tick count beyond the sequence limit before tempo
mutation. The doubling loop checks its bound before shifting, avoiding wraparound
and a nonterminating search. Finalization validates the signed sample duration
before conversion to unsigned. A new extracted-helper suite covers invalid input,
ordinary tick selection, the highest supported doubling, and rejection of the
next doubling; finalization tests cover zero and negative sample durations.
Tempo-allocation callback lifetime and full recovery remain outstanding, and
independent mode remains disabled.

### Prepare tempo history before tempo mutation

Tempo derivation now allocates undo storage before changing tempo. It validates
song/UI ownership, both structural revisions, retained action ownership, and the
original tempo after allocation, then revalidates after tempo and display callbacks.
Invalid storage is freed and failure returns zero. Tests cover allocation failure
without tempo mutation, correct before/after history, changed source/context,
and action/song deletion during allocation or tempo callbacks. The no-action path
remains supported. Tempo callback side effects are not rolled back, and same-address
replacement still requires lifetime identities/pins. Independent routing and full
transactional recovery remain unfinished; independent mode is disabled.

### Validate tempo after display callbacks

Tempo derivation now compares the final tempo with the value captured for undo
before returning its tick count. A display callback that changes tempo returns
failure rather than letting finalization apply a stale tick result. Tests cover
changed tempo with and without history, song/action deletion during display,
either panel's structural invalidation, and a nested UI scope that restores its
owner normally. Callback changes and already attached history are not rolled back;
full transactional recovery, lifetime identities, and independent routing remain
unfinished. Independent mode stays disabled.

### Identify reconstructed undo actions

Actions now receive a nonzero construction identity that never wraps within a
boot; copying actions is disabled so identities cannot be duplicated by copying.
Length-history recording and tempo derivation retain and revalidate that identity
across callbacks, in addition to history membership and ownership checks. Tests
reconstruct an action at the same address with matching owner/song metadata and
verify rejection during history allocation, tempo allocation, tempo mutation,
and display. Stable history tests continue to exercise successful operations.
This protection is limited to those paths: other action consumers and clip/song
lifetimes still need identity checks or pins. Zero identity signals exhaustion
and these guarded operations reject it. Independent routing and full recovery
remain unfinished; independent mode is disabled.

### Clip doubling callback boundaries

Song-level clip doubling now rejects missing targets/outputs before repeat work
and validates song/UI ownership, both structural revisions, observed membership,
model-stack ownership, output/type, and the resulting length after repetition,
scaling notifications, output notifications, and playback resynchronization.
Extracted production-method regression tests delete registered clips during
scaling, output, and resync callbacks and cover changed song/output/type/length,
both panels' structural invalidation, unpublished targets, and restored nested UI
scopes. These checks stop subsequent dispatch; they do not roll back completed
repetition or pin unpublished objects against deletion or same-address reuse.
Action-specific work remains deferred. Independent routing, comprehensive lifetime
protection, and transactional recovery remain unfinished; independent mode stays
disabled.

### Repeat/chop direction and UI ownership

Instrument clip repetition and exact repeat/chop now reject missing outputs and
non-instrument targets before editing rows. Their callback checks also retain the
initiating UI owner, clip type, and sequence direction, so callback changes stop
later processing and are not overwritten by ping-pong flattening. Exact repetition
updates its expected direction after its own flattening step before checking trim
and playback callbacks. Regression tests cover row, parameter, length-change, and
resume boundaries, changed direction/type/owner, invalid entry targets, and nested
UI scopes that restore the initiating owner. Completed row and parameter edits
are not rolled back; object pinning and independent routing remain unfinished.
Action-specific work remains deferred and independent mode stays disabled.

### Retain the note-row edit's UI owner

The shared note-row edit context now captures its initiating UI owner and rejects
validation under the other panel before reading retained clip or row storage.
Observed owner mismatch permanently invalidates the context, including after the
original owner returns. Nested UI work that restores its owner before validation
remains supported. Regression tests exercise both owners directly and through
production row trimming at parameter and event callbacks. This stops subsequent
processing but does not undo note or parameter mutations already completed.
Comprehensive lifetime protection, transactional recovery, and independent routing
remain unfinished; action-specific work remains deferred and independent mode is
disabled.

### Repeat/chop row-stack ownership

After row repeat or trim callbacks, both instrument repeat helpers now validate
the row-specific model stack's song, clip, row pointer, and row ID before changing
independent lengths or processing later rows. Tests redirect each field separately
for doubling, exact repetition, and chopping, and check that later rows, clip
length, parameters, and playback are untouched. A callback that restores its row
stack before returning remains supported. These guards do not roll back changes
inside the row callback or provide object lifetime pins. Action-specific work
remains deferred; independent routing and transactional recovery remain unfinished,
and independent mode stays disabled.

### Empty-row wrap-edit insertion

Corresponding-note insertion now handles an empty source row without reading a
nonexistent first note. Its initial note lengths use the distance back to the
first insertion position within the effective row length; the existing final
clamp handles the last inserted note. Extracted production-method tests cover
empty rows with a shortened final wrap interval, independent row lengths, and
allocation failure followed by a successful retry. This fixes an empty-source
edit failure; it does not complete the remaining input-arithmetic, lifetime,
transactional recovery, or independent routing work. Action-specific work remains
deferred and independent mode stays disabled.

### Wrap-edit input and arithmetic bounds

Corresponding-note insertion now checks its model stack, target position, note
length, effective row length, and wrap interval before allocation or division.
Insertion positions outside a shorter row return success without editing it.
The insertion count avoids addition overflow; allocation sizes are checked and
search positions use wide intermediates without advancing past the final screen.
Wrap-length calculations also use wide intermediates. Regression tests cover
invalid inputs, a short-row no-op, and insertion at the signed position limit.
This does not validate every existing note's contents or finish cooperative
lifetime protection, transactional recovery, or independent routing. Action work
remains deferred and independent mode remains disabled.

### Wrap-edit allocation source checks

Corresponding-note insertion now rejects a foreign song at entry and rechecks
its model stack's song, clip, row, row ID, and captured wrap interval after both
working-memory and temporary-vector allocation. Regression tests change each
field at each allocation boundary and verify the source notes and event dispatch
remain untouched, while preserving a callback's changed wrap setting. Existing
successful insertion tests exercise stable sources. This does not guard every
later callback or provide lifetime pins or full rollback. Action-specific work
remains deferred; independent routing and recovery remain unfinished, and
independent mode stays disabled.

### Wrap-edit final event validation

Corresponding-note insertion now validates its target and newly published note
vector after event notification. A callback that removes the clip or row, redirects
the model stack, changes the wrap interval, or resizes the published notes causes
an error return. Regression tests exercise these cases, including deletion under
sanitizers. Existing insertion tests cover successful publication. Notes already
published and callback changes are not rolled back; same-address replacement and
in-place note mutations remain outside these checks. Action-specific work remains
deferred, and independent routing and full recovery remain unfinished. Independent
mode stays disabled.

### Latch observed note-source invalidation

The shared note-row edit guard now permanently rejects its captured source after
observing a changed note count or buffer address. Restoring the old count/address
cannot revive that source snapshot. Its separate target validation still supports
operations that intentionally resize notes. Regression tests cover empty and
nonempty count restoration, buffer replacement/restoration, fresh snapshots, and
source revalidation after an unpublished target is freed. The latter short-circuits
without reading the target; target-only checks still require lifetime protection.
Unobserved replacement and in-place note mutations remain outside this guard.
Action work remains deferred; comprehensive lifetime protection, recovery, and
independent routing remain unfinished. Independent mode stays disabled.

### Wrap-edit source-note validation

Corresponding-note insertion now validates strictly increasing, nonnegative note
positions within the effective row length and positive note lengths before
allocation and after both allocation callbacks. Tests cover malformed positions,
ordering, lengths, and in-place callback corruption, while retaining support for
valid tails that wrap past the row boundary. Invalid source data is preserved and
rejected before search/copy/event work. This adds linear source scans at the
allocation boundaries; device performance remains unverified. Valid-looking
in-place edits, later action callbacks, lifetime pins, transactional recovery, and
independent routing remain unfinished. Action-specific work remains deferred and
independent mode stays disabled.

### Validate published wrap-edit note contents

The final wrap-edit event check now validates note ordering, position bounds,
and positive lengths across the entire published vector. It no longer relies
only on count/address stability to detect malformed callback edits. Tests corrupt
notes in place, including newly inserted notes beyond the original source count,
and verify an error return with callback changes preserved. Successful insertion
coverage remains in place. This adds a final linear scan; it does not detect all
valid-looking concurrent edits or restore already published data. Lifetime pins,
transactional recovery, independent routing, and hardware validation remain
unfinished. Action-specific work remains deferred and independent mode is disabled.

### Retain note-edit playback directions

The shared note-row edit guard now captures both clip and row sequence directions
and permanently invalidates an observed change. Regression tests cover restoring
the old direction after invalidation, creating fresh contexts, and parameter
callbacks that change direction during repeat generation. The latter return
failure before note repetition and preserve the callback's direction. Existing
successful repetition tests cover intentional final direction flattening. This
is cooperative validation, not rollback or object pinning. Comprehensive lifetime
protection, recovery, independent routing, and hardware validation remain
unfinished; action work remains deferred and independent mode stays disabled.

### Wrap-edit firmware byte-count limits

Wrap-edit capacity checks now use the allocator's 32-bit byte limit for working
memory and the array operations' signed byte limit for note data, rather than the
build host's SIZE_MAX. Regression tests reject excessive capacity before allocation,
include existing notes in the capacity calculation, and exercise the accepted
boundary with injected allocation failure instead of reserving a huge buffer.
These checks do not establish usable hardware memory capacity or finish generic
array allocation hardening. Lifetime protection, recovery, independent routing,
and hardware validation remain unfinished. Action work remains deferred and
independent mode stays disabled.

### Array insertion capacity preflight

Production ResizeableArray insertion now rejects invalid ranges/counts and checks
requested elements plus spare allocation capacity against signed element and byte
limits before locking or allocation. Native tests using the actual array code
cover invalid input, padded-capacity overflow, allocator failure at the accepted
boundary, and preserving existing elements followed by successful retry. This
covers insertAtIndex preflight; other array allocation methods and cooperative
lifetime/reentrancy paths still require review. Full recovery, independent routing,
and hardware validation remain unfinished. Action-specific work remains deferred
and independent mode stays disabled.

### Array reservation capacity preflight

ensureEnoughSpaceAllocated now rejects negative requests and oversized padded
capacities before locking or adjusting storage, using signed element/byte limits.
Zero-element requests succeed without allocation. Native tests cover invalid and
excessive requests, existing-element accounting, preserved values, successful
retry, zero reservation on empty/populated arrays, and injected allocation failure
at the accepted boundary. This does not complete other array allocation paths or
cooperative lifetime/reentrancy protection. Recovery, independent routing, and
hardware validation remain unfinished; action work remains deferred and independent
mode stays disabled.

### Array clone source preflight

cloneFrom now rejects null/incompatible sources, malformed source counts/layouts,
and source or destination-copy capacities beyond signed byte limits before
changing the destination or allocating. Self-cloning succeeds without allocation.
Native tests use the real array implementation and verify rejection preserves
existing destination data. This preflight does not make ordinary clone allocation
failure transactional, pin the source across allocation, or harden beenCloned.
Lifetime protection, recovery, independent routing, and hardware validation remain
unfinished; action work remains deferred and independent mode stays disabled.

### Array clone allocation recovery

cloneFrom now restores the destination's prior storage, count, size, and start on
copy-allocation failure. Successful replacement releases independently owned old
storage, including when the source is empty; an allocation shared with the source
is not freed. Static destinations reject replacement and retain self-clone support.
Native tests cover preserved data/address on failure, successful retry without
leaking old storage, independent copies, empty-source cleanup, source aliases, and
static destination preservation. This does not pin either array across callbacks
or make borrowed aliases safe to destroy after failed cloning. Other lifetime,
recovery, independent routing, and hardware gates remain unfinished. Action work
remains deferred and independent mode stays disabled.

### Failed borrowed copies preserve the original

cloneFrom now detaches a destination that shares the source allocation before
validation or allocation can fail. Such a borrowed copy becomes empty on failure,
so destroying it cannot free the original. Independently owned destinations still
retain their previous contents on failure. Native tests explicitly destroy failed
and rejected borrowed copies, verify the original address/value/allocation survive,
and verify retry produces independent storage. This resolves the borrowed-alias
failure limitation noted above for cloneFrom with an identified source. It does
not provide general shared ownership or pin arrays across callbacks. Independent
mode remains disabled; remaining lifetime, recovery, routing, and hardware work is
unfinished, with action-specific work deferred.

### Shallow-array clone repair safety

beenCloned now detaches borrowed storage before validating its source layout and
copy capacity. Invalid input returns BUG with an empty destination; allocation
failure likewise leaves a safely destructible empty copy. Static-storage ownership
is not inherited by the new allocation. Native tests destroy failed/rejected
repairs and verify the original survives, and check independent copying of wrapped
array elements. This entry point still requires a genuine shallow copy and does
not pin the source across allocation callbacks. Remaining lifetime, recovery,
independent routing, and hardware work is unfinished; action work remains deferred
and independent mode stays disabled.

### Revalidate clone sources after allocation

cloneFrom now rechecks the live source's buffer, capacity, start, element count,
and element size after allocation and before copying bytes. A mismatch frees the
unused new buffer and preserves independently owned destination storage. Native
allocation-callback tests release the source buffer or change source metadata,
check destination preservation and cleanup, and exercise retry. This requires
the source array object itself to remain alive: it does not provide object pins,
detect all same-address reuse or in-place content changes, or validate beenCloned's
unidentified original. Remaining recovery, routing, lifetime, and hardware gates
are unfinished. Action work stays deferred and independent mode stays disabled.

### Stage clone copies before destination publication

cloneFrom now builds its replacement in a temporary array and publishes only if
the destination's captured storage/layout is unchanged. Allocation failure leaves
the destination alone; callback-driven clearing or replacement is preserved and
the unused staged copy is released. Native tests cover clearing during successful
and failed allocation, nested replacement, cleanup, and retry, alongside existing
original-preservation tests. This requires both array objects to remain alive and
does not detect all in-place edits or same-address reuse. Broader lifetime,
recovery, independent routing, and hardware validation remain unfinished. Action
work stays deferred and independent mode stays disabled.

### Static array storage ownership

Static-backed array reservation now succeeds only within supplied capacity and
rejects growth before heap queries or storage adjustment. Array destruction no
longer frees externally supplied static storage. Native tests use stack-backed
buffers to verify reservation limits, unchanged contents after rejection and
destruction, and independently allocated shallow-copy repair cleanup. The existing
static-destination clone test now also uses external storage. General storage swaps,
source-object lifetime protection, remaining recovery, independent routing, and
hardware validation still require review. Action work remains deferred and
independent mode stays disabled.

### Preserve storage ownership during array swaps

swapStateWith now moves the static-storage byte count along with the buffer and
layout. Native tests exchange heap and external storage, verify that reservation
uses the transferred capacity, and check that emptying/destruction free only heap
storage. Tests also cover exchanging differently sized external buffers and a
self-swap. This fixes storage ownership transfer; it does not add object lifetime
pins or validate incompatible element types. Remaining recovery, independent
routing, and hardware validation are unfinished. Action work remains deferred and
independent mode stays disabled.

### Keep static insertion out of heap expansion

Regular insertAtIndex now routes static-backed arrays through the reserved-ring
insertion operation. Growth beyond supplied capacity fails without querying or
expanding heap storage. Native tests cover full-capacity insertion, rejected growth,
reusing wrapped capacity after deletion, and guard values surrounding external
storage. Dynamic insertion continues through its existing checked allocation path.
Lifetime protection, remaining recovery, independent routing, and hardware gates
remain unfinished; action work stays deferred and independent mode is disabled.

### Reject invalid array deletion ranges

Array deletion now validates index and count before subtraction, locking, or
storage mutation. Zero/negative counts and ranges extending beyond the array
leave it unchanged rather than overflowing arithmetic or emptying it. Native
tests cover heap and external buffers, extreme signed inputs, retained addresses
and values, surrounding guard values, and subsequent valid partial/full deletion.
This does not pin arrays across callbacks or finish broader recovery, independent
routing, and hardware validation. Action work remains deferred and independent
mode stays disabled.

### Array reordering index checks

Element swapping and repositioning now reject out-of-range indices and skip
same-index operations before locking or accessing memory. Native tests cover empty
arrays, signed extremes, heap/external storage preservation, and valid moves in
both directions plus swapping across a wrapped external buffer. Guard values
verify the wrapped operations stay within supplied storage. Lifetime protection,
remaining recovery, independent routing, and hardware validation remain unfinished;
action work stays deferred and independent mode stays disabled.

### Independent input routing behind the negotiation gate

Mirror input dispatch, deferred retries, encoder injection/reset, and disconnect
releases now select the negotiated session's UI owner. Independent sessions use
Remote and do not merge their held keys with the physical host's Local keys.
Visible mirroring retains Local dispatch and shared-key behavior. Native mirror
runtime tests select the internal independent state to exercise pad/encoder retries,
acknowledgements, same-key isolation, and Remote teardown; wire negotiation still
rejects independent sessions and advertises only visible mirroring. This is an
input-routing foundation, not an enabled second screen. Remote panel/OLED frame
transfer, UI startup, routing integration, and the other readiness gates remain
unfinished. Routing is the current priority; other hardening work is deferred.

### Route independent OLED transport to published Remote frames

Independent OLED transmission now snapshots the published Remote frame through
copy_remote_frame; visible mirroring continues to use the Local image. Without a
published Remote frame, independent transport waits instead of leaking Local
pixels. Runtime tests verify Remote pixel selection, stable block/commit transfer
while a newer frame is published, subsequent updates, and waiting without Local
fallback. Tests select independent mode internally; negotiation remains disabled.
Panel/indicator routing, Remote UI startup/render scheduling, and end-to-end session
integration remain unfinished and are the current priority before other hardening.

### Route panel streams and snapshots by UI owner

Panel parsing and persistent indicator caches now have separate Local and Remote
state. Remote bytes never reach the physical PIC; host transport queues only the
selected session owner's commands. Initial indicator and pad snapshots use that
same owner. Runtime tests cover independent stream selection, physical suppression,
interleaved partial commands, visible-session exclusion of Remote output, and
Remote initial snapshots. The Remote cache adds approximately 14 KiB of static
SDRAM. Independent negotiation remains disabled. Remote UI startup/render scheduling,
resynchronization, and end-to-end session integration remain routing priorities;
hardware validation and other readiness gates are still outstanding.

### Service initialized Remote UI work

Independent host sessions now service the Remote timer bank and pending UI
rendering after queued input. Rendering is limited to one pass per 10 ms; the
service waits for a nonempty Remote navigation stack and skips SD/audio locks.
Session liveness, song identity, and UI ownership are rechecked after callbacks
before further rendering/transmission. Runtime tests cover owner selection,
render cadence, missing initialization, SD deferral, and timer-triggered Stop.
Independent negotiation remains disabled. Remote UI initialization, graphics-loop
integration, reconnect/resynchronization, and end-to-end readiness remain routing
priorities; this service does not itself create the Remote UI.

### Remote graphics timer lifecycle

An initialized independent Remote UI now arms its graphics timer before timer
service, without resetting an already scheduled deadline. Disconnect clears the
Remote graphics timer while preserving Local timing. Runtime tests exercise both
timer banks, initial UI deferral, retained deadlines, and teardown. Independent
negotiation remains disabled. The graphics callback still has a physical-PIC-space
check to decouple, and Remote UI initialization and reconnect/resynchronization
remain routing priorities. Other readiness gates remain outstanding.

### Decouple Remote graphics from physical output capacity

The graphics timer and pending UI renderer now share an owner-aware readiness
check. Local rendering retains its existing PIC UART capacity thresholds; Remote
rendering bypasses the hardware queue query because its output uses mirror
transport. Unit tests cover Remote query suppression, Local backpressure, and
restoring Remote behavior after nested Local service. Independent negotiation
remains disabled. Remote UI initialization, reconnect/resynchronization, and
end-to-end session validation remain routing priorities.

### Discard incomplete Remote panel commands on disconnect

Independent teardown clears the Remote parser after injected-key release callbacks,
which can themselves emit panel bytes. A truncated command can no longer consume
new-session output. Completed indicator caches and the Local parser are preserved.
Runtime regressions cover reconnect framing, Local partial-command continuation,
and partial output emitted during release. Independent negotiation remains disabled;
Remote UI initialization and broader resynchronization remain outstanding.

### Gate Remote input on navigation readiness

Independent input remains queued until the Remote navigation stack has a current
UI, matching the render-service readiness requirement. Readiness is checked before
each dispatch, including after an earlier command or ACK callback. Completed input
can still be acknowledged while navigation is unavailable; unprocessed input is
neither dispatched nor acknowledged. Runtime tests cover startup deferral and
navigation removal between queued commands. Existing independent input tests now
explicitly establish Remote navigation. Negotiation remains disabled; this guard
does not initialize the Remote UI or complete reconnect resynchronization.

### Gate initial Remote snapshots on navigation readiness

Independent acceptance and initial pad/indicator snapshots now wait for Remote
navigation, using the same readiness helper as input and render service. Readiness
is rechecked after Accept transmission: if a callback removes navigation, the
session fails without snapshot output or a second Accept. Runtime tests exercise
startup deferral, resumed acceptance, owner restoration, and callback invalidation.
This establishes a navigation prerequisite, not proof of a freshly rendered frame.
Remote UI initialization and frame/cache resynchronization remain unfinished;
independent negotiation remains disabled.

### Invalidate completed Remote OLED frames at disconnect

Independent teardown invalidates the published Remote OLED frame after input
release callbacks and marks Remote OLED rendering dirty. Invalidation also clears
the current image pointer, preventing publication of the old completed image
without a new render. Local frame state is preserved. Reconnecting transport waits
for a new publication instead of sending the previous session's image. Tests cover
the production invalidation accessor and frame storage, Local isolation, fresh
publication, runtime reconnect, and output published by teardown callbacks.
Remote UI initialization and remaining panel/cache resynchronization are still
unfinished; independent negotiation remains disabled.

### Recheck rendering locks after Remote timer callbacks

Remote rendering now rechecks both audio and SD locks after timer service, before
consuming the render cadence slot. Runtime coverage exercises each lock being set
by a timer callback, preservation of pending OLED/pad redraws, owner restoration,
and rendering resuming after unlock. Panel caches cannot simply be cleared while
indicator producers may suppress unchanged values; complete producer refresh and
Remote startup/resynchronization remain unfinished. Negotiation remains disabled.

### Connect Remote indicator producers to panel transport

Remote LED and gold-knob brightness updates now feed the mirror panel parser as
well as their software frame. Previously they returned before producing transport
bytes, leaving the Remote command cache stale even while the software frame
changed. Physical PIC output remains Local-only. A runtime regression invokes the
production indicator setters and verifies Remote cache bytes, transmitted LED and
knob commands, and no physical writes. Existing exhaustive knob-level and blink
isolation tests remain in place. Remote UI startup and complete snapshot refresh
remain unfinished; independent negotiation stays disabled.

### Route Remote seven-segment rendering into mirror transport

Remote numeric rendering now emits the completed four-digit display, including
fixed dots, through the mirror panel parser (command 224). OLED emulation continues
through the Remote OLED canvas. Physical numeric output and legacy display SysEx
remain Local-only. Tests compile the production render body and cover popup dots,
blank output, Remote OLED emulation, and Local output preservation. Remote startup,
pad producer routing, and complete snapshot refresh remain unfinished. Independent
negotiation stays disabled.

### Connect Remote pad-column producers to transport

Prepared Remote pad columns now enter the mirror parser using PIC column commands
1 through 9 and column-major RGB payloads. Main-pad and sidebar redraws, including
animation paths that use sortLedsForCol, previously stopped at the software frame.
Physical output remains Local-only. Tests compile the production column function
and exercise every column pair, sidebar, odd-index normalization, prepared colours,
byte order, software-frame publication, and owner isolation. Colour preparation
itself is stubbed in these routing tests. Remaining animation/flash producers,
Remote startup, and full snapshot refresh are still unfinished; independent
negotiation remains disabled.

### Route Remote pad flashes

Remote flashMainPad now sends the optional colour-selection command followed by
the pad flash command through the mirror parser. Local hardware output is unchanged.
Production-function tests cover every main-pad address for default and explicit
colours, protocol command lengths, invalid coordinates, and Local/Remote isolation.
Horizontal and vertical scroll already fall back to complete Remote column redraws;
those now use the connected column path. Remaining output producers still require
an audit, and Remote startup and full snapshot refresh remain unfinished.
Independent negotiation remains disabled.

### Route shared panel settings alongside independent UI output

Refresh timing (19), flash duration (23), and dimmer interval (243) are shared host
settings whose producers explicitly run under Local ownership. Independent live
transport now includes these commands. Remote snapshots source them from the Local
host cache, while retaining Remote indicators and screen content. Runtime tests
cover live settings, exclusion of Local indicator output, and stale Remote setting
cache entries not overriding host values. Remote startup and complete snapshot
refresh remain unfinished; independent negotiation remains disabled.

### Rebuild Remote indicator snapshots from authoritative frames

Remote snapshots now serialize all 36 LED states and both knob brightness arrays
from IndicatorFrame. Cached LED/knob commands are excluded, so missing or stale
cache entries cannot override the software frame. Shared settings still use the
host cache; numeric display cache handling is unchanged. Runtime coverage verifies
complete output with conflicting/missing cache values and updates initial snapshot
tests to use production indicator setters. Remote UI initialization and remaining
snapshot readiness work are unfinished; independent negotiation remains disabled.

### Invalidate Remote numeric output across disconnect

Independent teardown now invalidates the Remote seven-segment command cache after
release callbacks. A numeric Remote snapshot without fresh rendered digits emits
an explicit blank instead of retaining the client's old display; new digits replace
the blank normally. Local numeric cache entries are preserved, and OLED snapshots
do not receive this fallback. Runtime tests cover callback-produced stale digits,
Local isolation, blank fallback, and fresh output. Remote startup and remaining
snapshot readiness work are unfinished; independent negotiation remains disabled.

### Recheck song identity after acceptance callbacks

Initial snapshot setup now captures the host song before transmitting Accept and
checks it again afterward. A callback replacing or clearing the song fails the
session before pad/indicator snapshots, queued input, or OLED transmission proceed.
Runtime coverage replaces the song while queuing input during Accept in both
visible and internally selected independent modes, then verifies teardown without
dispatch or snapshot output. This is an identity check, not object-lifetime pinning.
Remote startup and remaining readiness work are unfinished; independent negotiation
remains disabled.

### Validate Remote navigation after rendering

Remote render service now rechecks navigation readiness after rendering callbacks,
in addition to song identity, liveness, and owner checks. Losing the Remote UI ends
the session before queued panel or OLED output can be transmitted. Runtime tests
cover a callback publishing output then removing navigation, teardown invalidation,
and a valid navigation-depth change that continues transmitting. These tests use
mock rendering callbacks; full Remote UI initialization remains unfinished and
independent negotiation remains disabled.

### Render initialized Remote UI before initial acceptance

Independent snapshot preparation now marks Remote pads and OLED dirty and runs
pending UI rendering before Accept. It validates song identity, navigation, owner,
and session liveness afterward, and defers acceptance if audio/SD locks remain.
Runtime tests verify render-before-Accept ordering and invalidation preventing
acceptance/output. This refreshes an already initialized Remote UI; it does not
create the navigation root or prove that every renderer completed its work.
Remote initialization and remaining readiness validation are unfinished, and
independent negotiation remains disabled.

### Defer snapshot preparation while Remote rendering is active

Preparation now checks the Remote rendering guard before marking rows dirty or
calling the renderer, and rechecks it afterward. An in-progress render no longer
allows acceptance with a skipped snapshot render pass. Runtime tests verify dirty
row preservation, no acceptance during an existing render, post-callback deferral,
and resumption without failing the session. Remote root creation and complete
render-readiness validation remain unfinished; negotiation stays disabled.

### Wait for pending snapshot redraw work

Snapshot preparation now defers acceptance while main-pad rows, sidebar rows, or
OLED rendering remain dirty. This covers deliberate grid-render deferral during
horizontal scrolling/zooming and redraw requests left pending by callbacks. The
runtime renderer mock now models successful dirty-flag consumption with an explicit
deferral switch. Tests retain each output independently and verify no acceptance
until a subsequent successful pass. Dirty flags are a readiness prerequisite, not
proof of complete screen coverage. Remote root creation remains unfinished and
independent negotiation stays disabled.

### Service Remote transition timers before initial acceptance

Snapshot preparation now services the Remote timer bank before requesting a full
render. This lets existing transitions finish while normal Remote service is still
blocked by snapshot_pending. After callbacks, preparation rechecks liveness, song,
UI owner/navigation, locks, and rendering state before proceeding. Runtime tests
cover timer-driven deferral completion, Stop during preparation, and audio-lock
retry without early rendering or acceptance. Remote root creation and remaining
readiness validation are unfinished; independent negotiation stays disabled.
