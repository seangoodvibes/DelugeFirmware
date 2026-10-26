# OLED/CV driver regression tests

Run `python3 tests/hardware/oled_cv/run.py` from the repository root, or run the
`OledCVDriverTests` CTest target. A host C compiler is required (`--cc` selects it).

The harness compiles the production driver with mocked registers and peripheral
functions. Tests explicitly deliver completion interrupts, allowing a transaction
to remain in flight while another channel queues a voltage update. No firmware
source is rewritten. These tests cover software ordering, not electrical timing.
