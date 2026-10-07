#!/usr/bin/env python3
import sys,time
start=time.monotonic_ns()
for line in sys.stdin:
    now=time.monotonic_ns()
    ms=(now-start)/1_000_000.0
    sys.stdout.write(f"{ms:12.3f} ms | {line}")
    sys.stdout.flush()
