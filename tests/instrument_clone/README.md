# Instrument clone regressions

Extracts the production InstrumentClip::clone method into an isolated native
executable. Allocation/deallocation are real host allocations; row and parameter
cloning are controlled collaborators. ASan/UBSan follow UNDO_TEST_SANITIZERS.

Tests verify the first row error is preserved, all rows are processed before failed
clone destruction, the original model-stack target is restored, and successful
clones retain fresh row identities. A borrowed-storage flag checks cleanup ordering;
this does not execute production row or parameter teardown, or callback lifetimes.

Entry and early-failure tests reject missing or mismatched stack targets before
allocation and inject clip allocation, parameter cloning, and row-array copying
failures. They verify error propagation, preservation of the original stack, and
balanced clone allocation/cleanup without processing borrowed rows.

Nonpositive clip lengths are rejected before allocation, with and without reverse
flattening. Retry coverage injects failure at allocation, parameter cloning,
row-array copying and each of three row positions, then successfully clones the
same source and checks balanced cleanup. Row storage remains a controlled double.
