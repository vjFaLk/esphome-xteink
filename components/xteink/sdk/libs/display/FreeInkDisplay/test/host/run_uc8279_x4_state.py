#!/usr/bin/env python3
"""Compile and execute the hardware-independent UC8279 X4 state model."""
from pathlib import Path
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
LIB = HERE.parents[1]

with tempfile.TemporaryDirectory(prefix="freeink-uc8279-x4-state-") as directory:
    exe = Path(directory) / "state"
    subprocess.run(
        [
            os.environ.get("CXX", "c++"),
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I" + str(LIB),
            str(HERE / "test_uc8279_x4_state.cpp"),
            "-o",
            str(exe),
        ],
        check=True,
    )
    subprocess.run([str(exe)], check=True)
