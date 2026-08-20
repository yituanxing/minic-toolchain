#!/usr/bin/env python3
"""Materialize the final first500 aggregate-array union relocation replay slice."""
import runpy

runpy.run_path("tools/dev/materialize-static-aggregate-array-union-before-relocation-v1.py", run_name="__main__")
