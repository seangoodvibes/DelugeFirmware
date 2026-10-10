# Recording-source menu routing tests

`SourceMenuTests` compiles the production `SpecificSourceOutputSelector` header.
Only its surrounding song/output, display and base-menu dependencies are fixtures;
UI ownership and peer-refresh state use the production implementations.

The cases exercise independent panel targets, shared-track values, list reorder
and growth, both display paths, deferred peer refresh, invalid sources, filtering,
extreme encoder offsets and missing editor contexts. They do not validate audio
monitoring claims, output reclamation or hardware rendering.

Build with `cmake --build build/tests --target SourceMenuTests` after configuring
`tests`, then run `ctest --test-dir build/tests -R '^SourceMenuTests$' --output-on-failure`.
