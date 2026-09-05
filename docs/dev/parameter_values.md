# Parameter Values and Automation

## Current Preparatory Stage

`ParamSet::current_values_` owns the current values of patched, unpatched, and
expression parameters. `getValue()` and `setCurrentValueBasicForSetup()` access
that storage directly; neither needs an `AutoParam` to read or update the scalar.
The setup setter deliberately does not record undo or notify sound/UI consumers.

The existing preallocated `AutoParam` arrays remain unchanged. Each entry binds
to its owner's scalar through `bind_current_value()`. Automation processing,
interpolation, legacy loading, expression updates, and undo therefore read and
write the same value, rather than maintaining a synchronized copy. Notifications
no longer copy a value back into the set.

`ParamManager` clones collections by copying their memory. Each derived parameter
set first restores its array pointers, then `ParamSet::beenCloned()` rebinds every
automation entry to the cloned set's scalar before cloning automation nodes.
Moving or copying a bound `AutoParam` to another owner must similarly rebind it.
`AutoParam::cloneFrom()` instead preserves the destination binding and copies the
source value into it.

MIDI CCs, patch cables, and temporary loading objects retain standalone scalar
storage in `param_value_binding`. A binding uses either this storage or the external
owner's scalar, never both. Standalone bindings have no pointer into their own
objects, preserving the containers' existing memory-copy/move behavior.

## Persistence

`param_value_serializer.h` writes scalars and `param_value_deserializer.h` reads
them, without either depending on `AutoParam`.
`ParamSet::readParam()` reads directly into its scalar slot; `read_automation()`
handles only the following node records. A true result from `read_current_value()`
means a complete hexadecimal scalar was consumed, not that nodes necessarily
follow. Decimal legacy values have no automation payload.

`ParamSet::writeParamAsAttribute()` always writes the scalar itself and appends
`write_automation()` only when needed. Standalone callers retain the combined
`AutoParam::readFromFile()` and `writeToFile()` compatibility wrappers.

Automation records are streamed in 16-character units. Do not use
`readTagOrAttributeValue()` for an automation payload: values spanning file-buffer
boundaries can be truncated by the deserializer's filename-sized string buffer.
The stream reader retains interpolation flags, skips out-of-order records, and
converts legacy nodes exactly at the loop endpoint to position zero.

## Deferred Allocation Work

This stage does not allocate automation on demand and does not reduce memory use.
It adds a value-binding pointer while retaining standalone storage for unmigrated
owners. A later change can replace the preallocated arrays with optional storage,
leaving scalar persistence and setup access intact.

That change must also make ordinary editing, undo, and notifications work without
an automation object, handle first-node allocation failure, and release empty
automation only when no active caller retains its pointer. Those lifecycle changes
are intentionally not part of this preparatory refactor.

## Regression Tests

Native tests separate scalar/automation serialization from value-binding behavior.
`NativeParameterPersistenceTests` also runs production XML/JSON parsing and cluster
refills against in-memory file bytes, including decimal terminators and long node
streams. It also covers production XML/JSON writer round-trips and shared reload
coordination with a node-storage test adapter. Disabled automation loads consume
the unused payload so the next JSON attribute remains readable. The
[parameter integration suite](../../tests/integration/parameters/README.md)
documents how to run the CMake/CTest targets and their coverage limits. Production
ownership, cloning, consequence undo/redo, and playback still need native coverage
before changing automation allocation.
