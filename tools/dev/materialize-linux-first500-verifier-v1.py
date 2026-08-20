#!/usr/bin/env python3
"""Materialize the next unresolved first500 semantic slice."""
import runpy

runpy.run_path("tools/dev/materialize-linux-first500-union-selection-v2.py", run_name="__main__")
runpy.run_path("tools/dev/materialize-linux-first500-parameter-array-v1.py", run_name="__main__")
