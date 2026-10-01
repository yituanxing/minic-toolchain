#!/usr/bin/env python3
import runpy
from pathlib import Path

here = Path(__file__).resolve().parent
runpy.run_path(str(here / "apply-riscv64-core-object-slot-reuse-legacy-v0.py"), run_name="__main__")
runpy.run_path(str(here / "apply-riscv64-core-object-slot-reuse-cfg-hotfix-v0.py"), run_name="__main__")
