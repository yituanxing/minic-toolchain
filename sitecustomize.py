import runpy
import sys

if sys.argv and sys.argv[0].endswith("tools/dev/materialize-codegen-span-trace.py"):
    runpy.run_path("tools/dev/materialize-call-trace.py", run_name="__main__")
