#!/usr/bin/env python3
"""Materialize the next unresolved first500 semantic slice."""
import runpy

runpy.run_path("tools/dev/materialize-linux-first500-zero-length-record-slots-v1.py", run_name="__main__")
