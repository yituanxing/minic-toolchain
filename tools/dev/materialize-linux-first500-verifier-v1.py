#!/usr/bin/env python3
"""Materialize the next unresolved first500 semantic slice."""
import runpy

runpy.run_path("tools/dev/materialize-linux-first500-interleaved-return-pointer-attribute-v1.py", run_name="__main__")
