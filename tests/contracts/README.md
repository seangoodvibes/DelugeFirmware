# Squash regression audit

Audit baseline: `c43f185157` (475 changed files). The full, checked-in inventory is
[squash_coverage.json](squash_coverage.json). Every file is assigned an area and test
evidence; each area states the limits of that evidence. The test run does not
require the baseline commit to remain in git history.

## Coverage layers

| Area | Executable coverage | Limits |
| --- | --- | --- |
| Mirror runtime | Production `mirror.cpp`: negotiation, both display types, peer/session rejection, ordered ACKs, input retry, queue overflow, disconnect/release, client suppression, OLED DMA-buffer selection, timeout and SD transport servicing | Hardware and downstream UI are doubled |
| USB output | Production queue methods: enqueue/dequeue SysEx filtering, pre-negotiation queued MIDI, both USB roles, transfer limits, unsigned index wrap, in-flight buffer preservation | USB driver and cable I/O are doubled |
| Indicators | Production LED implementation: same LED on both panels, blink/timer ownership, all knob levels, software-frame vs PIC output, meter holdoff | PIC writes and timer scheduling are captured, not performed on hardware |
| Session UI/menu state | Existing native tests for independent instances, stacks, caches, same-setting edits, menu order, timer state, navigation, gesture state and model refresh | Most individual screens/menu implementations are not executed |
| Scheduler | Existing native scheduler tests for owner preservation across nested and conditional execution | Not the hardware scheduler/audio integration |
| Shared model | Production Song membership/output-reference methods and audio-shift preflight, plus existing song/clip/favourites state tests | Collections, storage and playback collaborators are doubled |
| Undo | Production consequence tests, real membership methods, reversible-prefix failure injection and history-state tests | Full action-logger queue recovery, address reuse and every allocation failure remain integration gaps |
| Rendering/containers | Existing OLED/7seg/pad frame, reserved insertion, retained-list and arithmetic tests | Full rendering pipelines and the allocator are not executed |
| Cross-cutting conversions | Source-routing contracts on 388 of the 475 changed files, plus explicit USB installation, OLED routing, shift ordering, array-boundary and localization contracts | These are architecture tests, not behavioral or line coverage |

The remaining inventory entries include files exercised by behavior tests (such
as protocol and state headers), generated localization, test infrastructure, and
documentation. Inventory membership by itself is **not** coverage. No claim of
100% line/branch coverage or full end-to-end safety is made.

## Source contract design

Contracts require the introduced session accessors and ownership wrappers and
reject reintroduced direct singleton accesses. Comments/literals are stripped so
documentation cannot satisfy them. A mutation test deliberately reverts individual
accessor calls and requires the checker to detect the regressions. Additional
checks guard hardware-output routing, the two USB filter installation points,
preflight/history-before-mutation ordering, and mirror menu/display labels.

These checks are intentionally tied to the architecture. A legitimate redesign
may require a reviewed manifest update alongside replacement behavior tests. Do
not remove an assertion merely to get a build green.

## Run all regressions

```sh
cmake -S tests -B build/tests -DUNDO_TEST_SANITIZERS=ON
cmake --build build/tests
ctest --test-dir build/tests --output-on-failure
./dbt build relwithdebinfo
```

`./dbt test` also builds and runs the CTest suites. Sanitizers instrument the undo
and mirror runtime executables on supported Clang/GCC hosts. Physical two-Deluge
validation remains necessary for USB roles/timing, display DMA, PIC output,
playback/storage contention and reconnect behavior. Independent mode remains
disabled; tests are not a substitute for finishing the implementation.

The Harden-auto-param integration moved several audited implementations into new
translation units. `SOURCE_RELOCATIONS` in `session_contracts.py` applies each
original routing contract to both the original file and its extracted implementation;
mutation checks use the same combined sources. Parameter lookup tests additionally
exercise distinct Local/Remote menu owners and fallback selections in both diagnostic
configurations.
