# Mirror and indicator runtime regressions

`runtime_tests.cpp` compiles the entire production `hid/mirror.cpp`. Private state
is reset between tests because the firmware owns one process-lifetime singleton;
stimuli go through its public entry points. `indicator_tests.cpp` similarly
compiles the production indicator-LED implementation. The physical device,
clock, PIC, display DMA queue, encoder dispatcher and downstream UI are bounded
doubles in `mocks/mirror_environment.h`. Protocol packing/decoding, mirror state
transitions, retry/ACK handling and indicator output logic are production code.
OLED congestion tests exercise retry without a new host frame, isolation from
uncommitted deltas, latest-complete-frame replacement, and reconnect cleanup.

`usb_queue_tests.cpp` compiles four production queue methods extracted verbatim
from `midi_device_manager.cpp`. Extraction is necessary to avoid linking unrelated
device-discovery/storage code. CMake depends on the source and regenerates the
include on change. Extraction fails on missing/ambiguous definitions and preserves
source locations using `#line`. The extractor has its own tests. The USB queue
storage and driver are doubled; the filtering and queue method bodies are not.
Fixture transfer/ring constants are checked against the production header.

The single production portability adjustment changes the OLED DMA pointer
comparison from `uint32_t` to `uintptr_t`. Both are 32 bits on the Deluge; the
latter also lets a 64-bit sanitizer build compare test buffer addresses correctly.

Build `UnitTests` to build this dependency; run CTest to execute it. Running just
`build/tests/unit/UnitTests` does not execute this separate suite. Enable
`UNDO_TEST_SANITIZERS=ON` for address/undefined-behavior checking.

See [the squash audit](../contracts/README.md) for covered areas and integration
gaps. These tests do not establish physical USB/PIC/DMA correctness or safe
independent-mode activation.

`UnitTests` also runs `oled_snapshot_tests.cpp`: it extracts the production
`OLED::copy_remote_frame` accessor and uses real OLED frame and session storage.
The surrounding hardware UI class is represented by its method declaration.
These cases verify completed Remote-frame capture without changing UI ownership,
invalid-size/unpublished rejection, revision wrap, and stable transport copies.

Session-mode request tests exercise the production receiver with explicit visible
host requests, disabled independent/unknown modes, malformed payloads, incompatible
displays and an existing active session. Legacy requests remain covered by the
existing runtime suite. Independent rendering/input routing is not enabled.

Capability discovery tests verify response payload/framing, no session or MIDI queue
side effects, malformed/congested query rejection, active-session isolation and
reentrant send protection. Only visible-host support may be advertised.

Unsupported-mode rejection tests check token/reason framing without peer reservation,
matching versus stale/malformed rejection during handshake, and immunity of an
already accepted client session. Independent-mode requests remain unsupported.

Client discovery API tests cover idle query emission without timer pause, one-shot
result consumption, response shape/token/sequence validation, timeout, observed
disconnect and session-start invalidation. Discovery is advisory; same-cable late
responses across retries are not correlated by the current zero-token format.

Discovery correlation tests now require an echoed nonzero query ID, rejecting old
replies after retries and immediate re-query. Host echo preserves session counters.
IDs use the existing bounded generator and repeat after 16383 queries; zero-ID
legacy queries remain answerable but cannot satisfy the new client API.

Discovery lifecycle tests cover immediate re-query after observed disconnect,
service-loop invalidation before reconnect, wrong-cable replies with matching IDs,
and synchronous replies/reentrant query rejection during transmission. USB
generations are not modeled for disconnects entirely between observations.

Startup integration tests delay capability replies and verify that timers remain
active until compatibility succeeds. Unsupported capabilities/timeouts cannot
start a session, and playback is rechecked after response delivery. Existing session
tests automatically simulate a compatible discovery reply and retain their prior
session-packet assertions; dedicated tests inspect discovery traffic directly.

Discovery cancellation tests press Back before sending, while waiting, and during
transmission. Local input remains available, late replies cannot trigger takeover,
and a subsequent start succeeds with a fresh ID. Existing hold-Back session tests
continue covering established-session exit.

Abandoned-startup tests change song, UI owner, playback or USB peer count while
discovery waits. They require immediate query/cache cleanup, rejection of late
replies and no UI freeze. A cached response is also discarded when startup aborts.

OLED round-trip coverage captures production host baseline/delta packets, replays
their payloads through the client receiver in a new test session, and compares all
768 bytes of each rendered frame. It checks six initial blocks plus commit and
one changed block plus commit; USB and physical display remain mocked.

Discovery ownership tests prevent the public advisory getter from consuming
startup-owned replies, both asynchronously and during transmission. Startup must
also obtain fresh capabilities rather than reuse a cached advisory result.

USB generation tests extract the production setup method to verify monotonically
advancing setup generations and transfer resets. Runtime discovery tests simulate
same-pointer reconnection, cached replies across reconnect and slot movement. The
full USB driver lifecycle and established-session generation handling are not
covered by this discovery-only change.

Established-session generation tests reconnect the same cable pointer and require
host input rejection, queued-input discard, removal of the old musical-MIDI filter,
and client teardown without sending to the replacement connection. USB generation
changes are simulated; in-flight driver traffic is not modeled.

Send-return generation regressions reconnect during ACK, client Input and Accept
transmission. They require failure without sequence advancement, later-input
dispatch or initial frame publication. The first packet is already handed to the
mock transport; tests do not claim it can be recalled from USB hardware.

Generation-boundary tests reconnect after discovery response delivery, during
client editor cleanup and during host input dispatch. Startup must retain its
discovered generation; host input must not create an ACK after replacement.

Deferred-input generation tests reconnect during button retry and completed or
deferred encoder dispatch. They call the production input processor directly to
check immediate failure before transport service, then verify teardown prevents
replay and clears pending encoder state. Encoder callbacks are controlled doubles.

Protocol mode-support coverage exhausts all byte values and verifies that the
shared advertised/accepted support definition includes only visible-host mode.
Runtime capability and rejection tests continue checking the wire behavior.
