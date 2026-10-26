#!/usr/bin/env python3
"""Compile the real OLED/CV driver against register mocks and exercise its IRQ paths."""

import argparse
import pathlib
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cc", default="cc")
args = parser.parse_args()
here = pathlib.Path(__file__).resolve().parent
root = here.parents[2]
driver = root / "src/RZA1/oled/oled_low_level.c"
with tempfile.TemporaryDirectory(prefix="oled-cv-test-") as directory:
    temporary = pathlib.Path(directory)
    # Shadow hardware-only includes, not the production driver's implementation.
    for include in re.findall(r'^#include "([^"]+)"', driver.read_text(), re.MULTILINE):
        path = temporary / include
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("/* Hardware declarations supplied by mock_hardware.h. */\n")
    binary = temporary / "oled_cv_tests"
    subprocess.run(
        [
            args.cc,
            "-std=c11",
            "-Wno-pointer-to-int-cast",
            "-I",
            str(temporary),
            "-I",
            str(root),
            "-include",
            str(here / "mock_hardware.h"),
            str(here / "driver_tests.c"),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary)], check=True)
