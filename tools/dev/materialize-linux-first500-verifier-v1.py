#!/usr/bin/env python3
"""Materialize the final first500 aggregate-array relocation ownership slice."""
import runpy

runpy.run_path("tools/dev/materialize-static-aggregate-array-relocation-owner-v1.py", run_name="__main__")
