#!/usr/bin/env python3
"""Reset the ESP32-C3 on PORT and print the benchmark's output: the lines
between BENCH_BEGIN and BENCH_END. Exit 1 if BENCH_END doesn't come within
TIMEOUT seconds.

  python3 scripts/read_serial.py PORT TIMEOUT
"""
import sys
import time

import serial  # pyserial, in the ESP-IDF environment

port, timeout = sys.argv[1], float(sys.argv[2])

s = serial.Serial(port, 115200, timeout=0.2)
s.dtr = False
s.rts = True            # hold the chip in reset
time.sleep(0.1)
s.reset_input_buffer()
s.rts = False           # release it: the firmware starts

started = False
pending = b""
deadline = time.monotonic() + timeout
while time.monotonic() < deadline:
    pending += s.read(4096)
    *lines, pending = pending.split(b"\n")
    for raw in lines:
        line = raw.decode("utf-8", "replace").strip()
        if line == "BENCH_BEGIN":
            started = True
        elif line == "BENCH_END" and started:
            sys.exit(0)
        elif started and line:
            print(line, flush=True)
sys.exit(1)
